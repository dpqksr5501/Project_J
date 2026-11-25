param(
    [Parameter(Mandatory)][string]$PostgresBin,
    [Parameter(Mandatory)][string]$NakamaExe,
    [string]$UnrealEditor,
    [switch]$VerifyGameLogin,
    [switch]$VerifyOnlineDungeon,
    [switch]$VerifyAuthoredSandbox,
    [ValidateRange(1026,65533)][int]$PostgresPort = 15432,
    [ValidateRange(1026,65533)][int]$NakamaPort = 17350
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
. "$root/Scripts/Audit/Continuation-ScratchPaths.ps1"
$context = Get-ContinuationScratchContext $root
$scratch = (New-ContinuationScratchRun $context ('NativeBackend-' + [guid]::NewGuid().ToString('N'))).Root
$db = Join-Path $scratch 'db'
$pgLog = Join-Path $scratch 'postgres.log'
$pgControl = Join-Path $PostgresBin 'pg_ctl.exe'
$psql = Join-Path $PostgresBin 'psql.exe'
$uri = "http://127.0.0.1:$NakamaPort"
$dbPassword = [guid]::NewGuid().ToString('N')
$nakamaKey = [guid]::NewGuid().ToString('N')
$nakamaSocketKey = [guid]::NewGuid().ToString('N')
$passwordFile = Join-Path $scratch 'initdb-password.txt'
$config = Join-Path $scratch 'nakama.yml'
$previousPassword = $env:PGPASSWORD
$process = $null
$postgresStarted = $false
if ($PostgresPort -in @(($NakamaPort - 1), $NakamaPort, ($NakamaPort + 1))) { throw 'The loopback ports overlap.' }

function Invoke-Postgres([string[]]$Options) {
    $result = & $psql -h 127.0.0.1 -p $PostgresPort -U project_j -d postgres -X -At -v ON_ERROR_STOP=1 @Options
    if ($LASTEXITCODE -ne 0) { throw 'PostgreSQL command failed.' }
    return $result
}

function Start-Postgres {
    # Waiting on the launcher process avoids waiting on standard streams inherited by the Windows daemon.
    $arguments = @('-D', ('"' + $db + '"'), '-l', ('"' + $pgLog + '"'), '-o', ('"-h 127.0.0.1 -p ' + $PostgresPort + '"'), '-w', 'start')
    $launcher = Start-Process -FilePath $pgControl -ArgumentList $arguments -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $scratch 'pg-ctl.log') -RedirectStandardError (Join-Path $scratch 'pg-ctl-error.log')
    $launcher.WaitForExit()
    if ($launcher.ExitCode -ne 0) { throw "PostgreSQL failed to start. See $pgLog" }
    $script:postgresStarted = $true
}

function Stop-Postgres {
    if (!$postgresStarted) { return }
    & $pgControl -D $db -m fast -w stop | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "PostgreSQL failed to stop. See $pgLog" }
    $script:postgresStarted = $false
}

function Start-Nakama {
    $nakamaArgs = @('--config', ('"' + $config + '"'))
    $script:process = Start-Process -FilePath $NakamaExe -ArgumentList $nakamaArgs -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $scratch 'nakama.log') -RedirectStandardError (Join-Path $scratch 'nakama-error.log')
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while ([DateTime]::UtcNow -lt $deadline -and !$process.HasExited) {
        try { if ((Invoke-RestMethod "$uri/healthcheck" -TimeoutSec 1).status -ne 'unhealthy') { return } } catch {}
        Start-Sleep -Milliseconds 200
    }
    throw "Nakama failed to start. See $scratch/nakama.log"
}

function Stop-Nakama {
    if ($process -and !$process.HasExited) { Stop-Process -InputObject $process; $process.WaitForExit() }
    $script:process = $null
}

function New-RpcBody($Intent, [string]$Key) {
    $json = ConvertTo-Json -InputObject $Intent -Depth 10 -Compress
    $payload = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($json))
    return @{ schema_version = 1; request_id = [guid]::NewGuid().ToString(); idempotency_key = $Key; payload_b64 = $payload } | ConvertTo-Json -Compress
}

function Invoke-CommandRpc([string]$Name, $Intent, [string]$Key) {
    $auth = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($nakamaKey + ':'))
    Invoke-RestMethod "$uri/v2/rpc/j_${Name}?unwrap" -Method Post -Headers @{ Authorization = "Basic $auth" } `
        -ContentType 'application/json' -Body (New-RpcBody $Intent $Key) -TimeoutSec 8
}

function Invoke-CompetingClaims($Intent) {
    $http = [Net.Http.HttpClient]::new()
    $http.Timeout = [TimeSpan]::FromSeconds(8)
    $auth = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($nakamaKey + ':'))
    $http.DefaultRequestHeaders.Authorization = [Net.Http.Headers.AuthenticationHeaderValue]::new('Basic', $auth)
    $bodies = @(1..2 | ForEach-Object {
        [Net.Http.StringContent]::new((New-RpcBody $Intent ([guid]::NewGuid().ToString())), [Text.Encoding]::UTF8, 'application/json')
    })
    try {
        $pending = @($bodies | ForEach-Object { $http.PostAsync("$uri/v2/rpc/j_mail_claim?unwrap", $_) })
        foreach ($task in $pending) {
            $response = $task.GetAwaiter().GetResult()
            try {
                $null = $response.EnsureSuccessStatusCode()
                ($response.Content.ReadAsStringAsync().GetAwaiter().GetResult() | ConvertFrom-Json).status
            } finally { $response.Dispose() }
        }
    } finally {
        $bodies | ForEach-Object { $_.Dispose() }
        $http.Dispose()
    }
}

function New-NakamaPlayer {
    $auth = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($nakamaSocketKey + ':'))
    $custom = @{ id = [guid]::NewGuid().ToString('N') } | ConvertTo-Json -Compress
    $session = Invoke-RestMethod "$uri/v2/account/authenticate/custom?create=true" -Method Post -Headers @{ Authorization = "Basic $auth" } `
        -ContentType 'application/json' -Body $custom -TimeoutSec 8
    $account = Invoke-RestMethod "$uri/v2/account" -Headers @{ Authorization = "Bearer $($session.token)" } -TimeoutSec 8
    return @{ token = $session.token; id = $account.user.id }
}

function Request-WorldLogin([string]$Token, [string]$Character, [int]$Channel, [string]$Zone = 'LocalWorld') {
    $json = @{ character_id = $Character; zone = $Zone; channel = $Channel } | ConvertTo-Json -Compress
    Invoke-RestMethod "$uri/v2/rpc/j_world_login_ticket?unwrap" -Method Post -Headers @{ Authorization = "Bearer $Token" } `
        -ContentType 'application/json' -Body $json -TimeoutSec 8
}

function Invoke-UnrealClaim([string]$Character, [string]$Mail, [string]$Key) {
    $environment = @{
        PROJECT_J_NAKAMA_HTTP_KEY = $nakamaKey
        PROJECT_J_TEST_MAIL_CHARACTER = $Character
        PROJECT_J_TEST_MAIL_ID = $Mail
        PROJECT_J_TEST_MAIL_KEY = $Key
    }
    $savedEnvironment = @{}
    foreach ($entry in $environment.GetEnumerator()) {
        $savedEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key)
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value)
    }
    try {
        $report = Join-Path $scratch 'UnrealReport'
        $arguments = @("$root/Project_J.uproject", '-unattended', '-nop4', '-nosound', '-NullRHI', '-NoLiveCoding', '-NoDDCCleanup',
            "-UserDir=$scratch/UnrealUser", "-ABSLOG=$scratch/Unreal.log", "-ReportExportPath=$report",
            "-ini:Game:[/Script/Project_J.Project_JGatewaySubsystem]:GatewayUrl=$uri",
            '-TestExit=Automation Test Queue Empty', '-ExecCmds=Automation RunTests ProjectJ.MMO.Integration.NakamaRealMailClaim; Quit')
        $editor = Start-Process -FilePath $UnrealEditor -ArgumentList ($arguments | ForEach-Object { '"' + $_ + '"' }) -PassThru -WindowStyle Hidden
        $editor.WaitForExit()
        $result = Get-Content -Raw -LiteralPath (Join-Path $report 'index.json') | ConvertFrom-Json
        if ($editor.ExitCode -ne 0 -or $result.succeeded -ne 1 -or $result.failed -ne 0) {
            throw "Unreal Nakama integration failed. See $scratch/Unreal.log"
        }
    } finally {
        foreach ($entry in $savedEnvironment.GetEnumerator()) { [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value) }
    }
}

function Invoke-GameLogin([string]$Character, [string]$AccessToken, [string]$ItemId) {
    if (!$UnrealEditor) { throw 'VerifyGameLogin requires an existing Unreal Editor executable.' }
    $port = 17989
    $map = '/Engine/Maps/Entry?game=/Script/Project_J.Project_JOnlineLoginFixture'
    $common = @("$root/Project_J.uproject", $map, '-unattended', '-nop4', '-nosound', '-NullRHI', '-NoLiveCoding', '-NoDDCCleanup',
        '-DisablePython', '-DisablePlugins=EditorToolset,AutomationTestToolset,ToolsetRegistry', '-multihome=127.0.0.1', '-ProjectJOnlineLoginFixture')
    $serverArgs = $common + @('-server', "-port=$port", "-UserDir=$scratch/LoginServer", "-ABSLOG=$scratch/LoginServer.log",
        "-ini:Game:[/Script/Project_J.Project_JGatewaySubsystem]:GatewayUrl=$uri",
        '-ini:Game:[/Script/Project_J.Project_JNakamaServicePortSubsystem]:RuntimeMode=LocalDockerNakama',
        "-ini:Game:[/Script/Project_J.Project_JGameMode]:OnlineAdvertisedEndpoint=127.0.0.1:$port")
    $server = Start-Process -FilePath $UnrealEditor -ArgumentList ($serverArgs | ForEach-Object { '"' + $_ + '"' }) -PassThru -WindowStyle Hidden `
        -Environment @{ PROJECT_J_NAKAMA_HTTP_KEY = $nakamaKey; PGPASSWORD = $null }
    $deadline = [DateTime]::UtcNow.AddSeconds(35)
    do {
        if ($server.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw "Online Unreal server failed to register. See $scratch/LoginServer.log" }
        Start-Sleep -Milliseconds 300
        $registry = Invoke-CommandRpc world_channel_list @{ zone = 'Entry' } ''
    } until ($registry.channels.Count -eq 1)
    $authProbe = Request-WorldLogin $AccessToken $Character 1 Entry
    if ($authProbe.status -ne 'OK') { throw "Online world credential preflight failed with status $($authProbe.status)." }
    $clientArgs = $common + @('-game', "-UserDir=$scratch/LoginClient", "-ABSLOG=$scratch/LoginClient.log",
        "-ini:Game:[/Script/Project_J.Project_JOnlineWorldSubsystem]:NakamaClientUrl=$uri")
    $client = Start-Process -FilePath $UnrealEditor -ArgumentList ($clientArgs | ForEach-Object { '"' + $_ + '"' }) -PassThru -WindowStyle Hidden `
        -Environment @{ PROJECT_J_NAKAMA_HTTP_KEY = $null; PGPASSWORD = $null; PROJECT_J_TEST_LOGIN_CHARACTER = $Character
            PROJECT_J_TEST_LOGIN_ACCESS = $AccessToken; PROJECT_J_TEST_LOGIN_ITEM = $ItemId }
    $server.WaitForExit()
    $client.WaitForExit()
    if ($server.ExitCode -ne 0 -or $client.ExitCode -ne 0 -or
        !(Select-String -LiteralPath "$scratch/LoginClient.log" -SimpleMatch 'ONLINE_FIXTURE_INVENTORY_REPLICATED' -Quiet)) {
        throw "Online game login or replicated inventory failed. See $scratch/LoginClient.log"
    }
}

function Invoke-OnlineDungeon([string]$Character, [string]$AccessToken) {
    if (!$UnrealEditor) { throw 'VerifyOnlineDungeon requires an existing Unreal Editor executable.' }
    $sourcePort = 17987
    $destinationPort = 17990
    $signingKey = [guid]::NewGuid().ToString('N') + [guid]::NewGuid().ToString('N')
    $fixtureMode = '?game=/Script/Project_J.Project_JOnlineLoginFixture'
    $sourceMap = if ($VerifyAuthoredSandbox) { "/Game/Maps/Test_MMO_Sandbox$fixtureMode" } else { "/Engine/Maps/Entry$fixtureMode" }
    $destinationMap = if ($VerifyAuthoredSandbox) { "/Game/Maps/Test_MMO_Dungeon$fixtureMode" } else { $sourceMap }
    $dungeonId = if ($VerifyAuthoredSandbox) { 'Dungeon_Starter' } else { 'LocalOnlineDungeon' }
    $common = @("$root/Project_J.uproject",
        '-unattended', '-nop4', '-nosound', '-NullRHI', '-NoLiveCoding', '-NoDDCCleanup', '-DisablePython',
        '-DisablePlugins=EditorToolset,AutomationTestToolset,ToolsetRegistry', '-multihome=127.0.0.1', '-ProjectJOnlineLoginFixture')
    if ($VerifyAuthoredSandbox) {
        $common += '-ProjectJOnlineAuthoredSandbox'
    }
    $serverSettings = @('-server', "-ini:Game:[/Script/Project_J.Project_JGatewaySubsystem]:GatewayUrl=$uri",
        '-ini:Game:[/Script/Project_J.Project_JNakamaServicePortSubsystem]:RuntimeMode=LocalDockerNakama')
    function Start-DungeonRole([string]$Role, [string[]]$Arguments, [hashtable]$Environment) {
        $all = $common + @("-UserDir=$scratch/Dungeon$Role", "-ABSLOG=$scratch/Dungeon$Role.log") + $Arguments
        $roleProcess = Start-Process -FilePath $UnrealEditor -ArgumentList ($all | ForEach-Object { '"' + $_ + '"' }) `
            -PassThru -WindowStyle Hidden -Environment $Environment
        $null = $roleProcess.Handle
        return $roleProcess
    }
    $serverEnv = @{ PROJECT_J_NAKAMA_HTTP_KEY = $nakamaKey; PROJECT_J_DUNGEON_TICKET_KEY = $signingKey; PGPASSWORD = $null }
    $source = Start-DungeonRole Source (@($sourceMap) + $serverSettings + @("-port=$sourcePort", '-ProjectJOnlineDungeonSource',
        "-ini:Game:[/Script/Project_J.Project_JGameMode]:OnlineAdvertisedEndpoint=127.0.0.1:$sourcePort",
        '-ini:Game:[/Script/Project_J.Project_JGameMode]:MaxTrustedSessions=48')) $serverEnv
    $destination = Start-DungeonRole Destination (@($destinationMap) + $serverSettings + @("-port=$destinationPort", '-ProjectJOnlineDungeonDestination',
        "-ini:Game:[/Script/Project_J.Project_JGameMode]:OnlineAdvertisedEndpoint=127.0.0.1:$destinationPort",
        '-ini:Game:[/Script/Project_J.Project_JGameMode]:OnlineChannelId=3',
        '-ini:Game:[/Script/Project_J.Project_JGameMode]:MaxTrustedSessions=256',
        "-ini:Game:[/Script/Project_J.Project_JGameMode]:OnlineDungeonId=$dungeonId")) $serverEnv
    $deadline = [DateTime]::UtcNow.AddSeconds(40)
    do {
        if ($source.HasExited -or $destination.HasExited -or [DateTime]::UtcNow -ge $deadline) {
            throw "The online dungeon servers did not register. See $scratch/DungeonSource.log and DungeonDestination.log"
        }
        Start-Sleep -Milliseconds 250
        $ready = Invoke-Postgres @('-c', "SELECT EXISTS (SELECT 1 FROM pj_dungeon_workers WHERE endpoint = '127.0.0.1:$destinationPort' AND leased_until > NOW()) AND EXISTS (SELECT 1 FROM pj_world_channels WHERE endpoint = '127.0.0.1:$sourcePort' AND leased_until > NOW());")
    } until ($ready -eq 't')
    $capacities = Invoke-Postgres @('-c', "SELECT string_agg(channel_id || ':' || capacity, ',' ORDER BY channel_id) FROM pj_world_channels WHERE endpoint IN ('127.0.0.1:$sourcePort', '127.0.0.1:$destinationPort') AND leased_until > NOW();")
    if ($capacities -ne '1:48,3:127') { throw "The two real channel leases did not advertise their configured and Iris-clamped capacities. Observed $capacities." }
    $beforeEpoch = [long](Invoke-Postgres @('-c', "SELECT session_epoch FROM pj_characters WHERE character_id = '$Character';"))
    $client = Start-DungeonRole Client @($sourceMap, '-game', "-ini:Game:[/Script/Project_J.Project_JOnlineWorldSubsystem]:NakamaClientUrl=$uri") `
        @{ PROJECT_J_NAKAMA_HTTP_KEY = $null; PROJECT_J_DUNGEON_TICKET_KEY = $null; PGPASSWORD = $null
            PROJECT_J_TEST_LOGIN_CHARACTER = $Character; PROJECT_J_TEST_LOGIN_ACCESS = $AccessToken; PROJECT_J_TEST_LOGIN_ITEM = $null }
    $client.WaitForExit()
    $source.WaitForExit()
    $destination.WaitForExit()
    $afterEpoch = [long](Invoke-Postgres @('-c', "SELECT session_epoch FROM pj_characters WHERE character_id = '$Character';"))
    $finished = Invoke-Postgres @('-c', "SELECT EXISTS (SELECT 1 FROM pj_dungeon_instances i JOIN pj_dungeon_members m USING (instance_id) WHERE dungeon_id = '$dungeonId' AND state = 'COMPLETE' AND cleared_at IS NOT NULL AND character_id = '$Character' AND returned_at IS NOT NULL);")
    if ($client.ExitCode -ne 0 -or $source.ExitCode -ne 0 -or $destination.ExitCode -ne 0 -or $afterEpoch -ne ($beforeEpoch + 3) -or
        $finished -ne 't' -or !(Select-String -LiteralPath "$scratch/DungeonSource.log" -SimpleMatch 'ONLINE_DUNGEON_RETURNED_AT_ANCHOR' -Quiet) -or
        !(Select-String -LiteralPath "$scratch/DungeonDestination.log" -SimpleMatch 'ONLINE_DUNGEON_DESTINATION_JOINED' -Quiet)) {
        throw "The PostgreSQL-backed dungeon round trip failed. See $scratch/DungeonSource.log, DungeonDestination.log and DungeonClient.log"
    }
    if ($VerifyAuthoredSandbox) {
        $craft = Invoke-Postgres @('-c', "SELECT EXISTS (SELECT 1 FROM pj_quest_states q CROSS JOIN LATERAL jsonb_array_elements(q.progress_json->'objectives') g WHERE q.character_id = '$Character' AND q.quest_id = 'prototype.gatherandcraft' AND g->>'recipe' IS NOT NULL AND (g->>'count')::int = 1) AND EXISTS (SELECT 1 FROM pj_inventory_items WHERE owner_character_id = '$Character' AND owner_scope = 'CHAR_INV' AND definition_id = 'ManaPotion' AND quantity = 1);")
        $gather = Invoke-Postgres @('-c', "SELECT count(*) FROM pj_idempotency_receipts WHERE operation = 'online_craft_commit' AND request_json->>'kind' = 'gather' AND request_json->>'character_id' = '$Character';")
        $reward = Invoke-Postgres @('-c', "SELECT count(*) FROM pj_boss_reward_receipts r JOIN pj_dungeon_instances i ON i.instance_id = r.defeat_id WHERE i.dungeon_id = '$dungeonId' AND i.cleared_at IS NOT NULL AND r.character_id = '$Character' AND jsonb_array_length(r.response_json->'mail_ids') > 0;")
        if ($craft -ne 't' -or $gather -ne '2' -or $reward -ne '1' -or
            !(Select-String -LiteralPath "$scratch/DungeonSource.log" -SimpleMatch 'ONLINE_SANDBOX_CRAFT_PROGRESS 1' -Quiet) -or
            !(Select-String -LiteralPath "$scratch/DungeonDestination.log" -SimpleMatch 'ONLINE_SANDBOX_BOSS_DEFEATED' -Quiet)) {
            throw "The authored sandbox did not prove craft progress and a single durable boss reward. See $scratch."
        }
    }
}

try {
    $dbPassword | Set-Content -LiteralPath $passwordFile -Encoding ascii
    & (Join-Path $PostgresBin 'initdb.exe') -D $db -U project_j --auth=scram-sha-256 --encoding=UTF8 --no-locale "--pwfile=$passwordFile" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'initdb failed.' }
    Remove-Item -LiteralPath $passwordFile
    $env:PGPASSWORD = $dbPassword
    Start-Postgres
    foreach ($migration in Get-ChildItem -LiteralPath "$root/Infra/migrations" -Filter '*.sql' -File | Sort-Object Name) {
        Invoke-Postgres @('-f', $migration.FullName) | Out-Null
    }
    $modules = (Join-Path $root 'Infra/nakama/modules').Replace([IO.Path]::DirectorySeparatorChar, [char]'/')
    @"
name: project_j_local
database:
  address:
    - project_j:${dbPassword}@127.0.0.1:${PostgresPort}/postgres?sslmode=disable
runtime:
  path: '$($modules.Replace("'","''"))'
  js_entrypoint: project_j_mmo.js
  http_key: $nakamaKey
session:
  token_expiry_sec: 600
socket:
  address: '127.0.0.1'
  port: $NakamaPort
  server_key: $nakamaSocketKey
console:
  address: '127.0.0.1'
  port: $($NakamaPort + 1)
"@ | Set-Content -LiteralPath $config -Encoding utf8
    & $NakamaExe migrate up --config $config *> (Join-Path $scratch 'nakama-migrate.log')
    if ($LASTEXITCODE -ne 0) { throw "Nakama migration failed. See $scratch/nakama-migrate.log" }
    Start-Nakama
    $account = [guid]::NewGuid().ToString()
    $character = [guid]::NewGuid().ToString()
    $defeat = [guid]::NewGuid().ToString()
    $itemKey = [guid]::NewGuid().ToString()
    $goldKey = [guid]::NewGuid().ToString()
    Invoke-Postgres @('-c', "INSERT INTO pj_accounts(account_id) VALUES ('$account'); INSERT INTO pj_characters(character_id, account_id, char_name, class_id) VALUES ('$character', '$account', 'Native test', 'Test');") | Out-Null
    $session = Invoke-CommandRpc profile_acquire_session @{ account_id = $account; character_id = $character } ''
    if ($session.status -ne 'OK') { throw 'Session acquisition failed.' }
    $reward = Invoke-CommandRpc reward_boss @{ defeat_id = $defeat; character_id = $character; gold = '70'; items = @(@{ definition_id = 'NativeTestItem'; quantity = 3 }) } ''
    if ($reward.status -ne 'COMMITTED' -or $reward.mail_ids.Count -ne 2) { throw 'Boss reward mail publication failed.' }
    $firstPage = Invoke-CommandRpc mail_inbox @{ character_id = $character; session_epoch = '1'; limit = 1 } ''
    $nextPage = Invoke-CommandRpc mail_inbox @{ character_id = $character; session_epoch = '1'; limit = 1; cursor = $firstPage.next_cursor } ''
    if ($firstPage.mail.Count -ne 1 -or !$firstPage.next_cursor -or $nextPage.mail.Count -ne 1 -or $nextPage.next_cursor -or
        $firstPage.mail[0].mail_id -eq $nextPage.mail[0].mail_id) { throw 'Mailbox pagination did not return two distinct deliveries.' }
    $itemIntent = @{ character_id = $character; mail_id = $reward.mail_ids[0]; session_epoch = $session.character.session_epoch }
    $goldIntent = @{ character_id = $character; mail_id = $reward.mail_ids[1]; session_epoch = $session.character.session_epoch }
    if ($UnrealEditor -and !$VerifyGameLogin -and !$VerifyOnlineDungeon -and !$VerifyAuthoredSandbox) {
        Invoke-UnrealClaim $character $itemIntent.mail_id $itemKey
        $initialReplay = Invoke-CommandRpc mail_claim $itemIntent $itemKey
        if ($initialReplay.status -ne 'REPLAYED') { throw 'Unreal did not commit the tracked command.' }
        $item = $initialReplay.receipt
    } else {
        $item = Invoke-CommandRpc mail_claim $itemIntent $itemKey
    }
    if ($item.status -ne 'COMMITTED' -or !$item.item_instance_id -or $item.quantity -ne 3) { throw 'The item attachment was not claimed.' }
    $principal = New-NakamaPlayer
    $unrelated = New-NakamaPlayer
    $onlineCharacter = [guid]::NewGuid().ToString()
    Invoke-Postgres @('-c', "UPDATE pj_accounts SET external_sub = 'nakama:$($principal.id)' WHERE account_id = '$account'; INSERT INTO pj_characters(character_id, account_id, char_name, class_id) VALUES ('$onlineCharacter', '$account', 'Online test', 'GreatSword');") | Out-Null
    $worldWorker = [guid]::NewGuid().ToString()
    $channel = @{ worker = $worldWorker; zone = 'LocalWorld'; channel = 2; endpoint = '127.0.0.1:17988'; capacity = 220; population = 0; draining = $false }
    if ((Invoke-CommandRpc world_channel_heartbeat $channel '').status -ne 'OK') { throw 'Channel lease registration failed.' }
    $wrong = Request-WorldLogin $unrelated.token $onlineCharacter 2
    if ($wrong.status -ne 'CONFLICT') { throw 'A different account obtained a login ticket.' }
    $ticket = Request-WorldLogin $principal.token $onlineCharacter 2
    $worldIntent = @{ ticket = $ticket.ticket; zone = 'LocalWorld'; channel = 2; worker = $worldWorker
        return_nonce = [guid]::NewGuid().ToString(); return_character = $onlineCharacter
        return_expires = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() + 55 }
    $redeemed = Invoke-CommandRpc world_redeem_login $worldIntent $ticket.ticket
    if ($redeemed.status -ne 'OK' -or $redeemed.character -ne $onlineCharacter -or $redeemed.epoch -ne '1') { throw 'Player login did not redeem.' }
    Stop-Nakama
    Stop-Postgres
    Start-Postgres
    Start-Nakama
    if ((Invoke-CommandRpc world_channel_heartbeat $channel '').status -ne 'OK') { throw 'Channel lease could not renew after restart.' }
    $sharedChannels = Invoke-CommandRpc world_channel_list @{ zone = 'LocalWorld' } ''
    if ($sharedChannels.channels.Count -ne 1 -or $sharedChannels.channels[0].channel_id -ne 2) { throw 'The shared channel was missing after restart.' }
    $next = Request-WorldLogin $principal.token $onlineCharacter 2
    $worldIntent.ticket = $next.ticket
    if ((Invoke-CommandRpc world_redeem_login $worldIntent $next.ticket).status -ne 'CONFLICT') { throw 'A consumed return nonce replayed after restart.' }
    $worldIntent.Remove('return_nonce')
    $worldIntent.Remove('return_expires')
    $worldIntent.Remove('return_character')
    if ((Invoke-CommandRpc world_redeem_login $worldIntent $next.ticket).epoch -ne '2') { throw 'A fresh ordinary login failed after replay rejection.' }
    if ($VerifyGameLogin) {
        $gameReward = Invoke-CommandRpc reward_boss @{ defeat_id = [guid]::NewGuid().ToString(); character_id = $onlineCharacter; gold = '0'; items = @(@{ definition_id = 'HealthPotion'; quantity = 3 }) } ''
        $gameItem = Invoke-Postgres @('-c', "SELECT attached_item_id FROM pj_mail_messages WHERE mail_id = '$($gameReward.mail_ids[0])';")
        try { Invoke-GameLogin $onlineCharacter $principal.token $gameItem }
        catch {
            $epochAtFailure = Invoke-Postgres @('-c', "SELECT session_epoch FROM pj_characters WHERE character_id = '$onlineCharacter';")
            Write-Output "Database session epoch after the game run: $epochAtFailure"
            throw
        }
        if ((Invoke-Postgres @('-c', "SELECT owner_scope || ':' || quantity FROM pj_inventory_items WHERE item_instance_id = '$gameItem';")) -ne 'CHAR_INV:2' -or
            (Invoke-Postgres @('-c', "SELECT sum(quantity) FROM pj_inventory_items WHERE owner_character_id = '$onlineCharacter' AND owner_scope = 'CHAR_INV';")) -ne '3') {
            throw 'The replicated game split did not match its PostgreSQL rows.'
        }
    }
    if ($VerifyOnlineDungeon -or $VerifyAuthoredSandbox) {
        if ($VerifyGameLogin -and !$VerifyAuthoredSandbox) {
            # These two world incarnations deliberately hold the same zone/channel key.
            do {
                $remaining = [double](Invoke-Postgres @('-c', "SELECT COALESCE(GREATEST(0, extract(epoch FROM max(leased_until) - clock_timestamp())), 0) FROM pj_world_channels WHERE zone_id = 'Entry' AND channel_id = 1;"))
                if ($remaining -gt 0) { Start-Sleep -Milliseconds ([int][Math]::Ceiling([Math]::Min($remaining, 5) * 1000)) }
            } while ($remaining -gt 0)
        }
        Invoke-OnlineDungeon $onlineCharacter $principal.token
    }
    $restoredInbox = Invoke-CommandRpc mail_inbox @{ character_id = $character; session_epoch = '1' } ''
    if ($restoredInbox.mail.Count -ne 1 -or $restoredInbox.mail[0].mail_id -ne $goldIntent.mail_id) { throw 'The pending mailbox did not survive the restart.' }
    $replay = Invoke-CommandRpc mail_claim $itemIntent $itemKey
    $duplicate = Invoke-CommandRpc mail_claim $itemIntent ([guid]::NewGuid().ToString())
    $gold = Invoke-CommandRpc mail_claim $goldIntent $goldKey
    $competingReward = Invoke-CommandRpc reward_boss @{ defeat_id = [guid]::NewGuid().ToString(); character_id = $character; gold = '0'; items = @(@{ definition_id = 'NativeCompetingItem'; quantity = 1 }) } ''
    $competingStatuses = @(Invoke-CompetingClaims @{ character_id = $character; mail_id = $competingReward.mail_ids[0]; session_epoch = $session.character.session_epoch } | Sort-Object)
    $ownership = Invoke-Postgres @('-c', "SELECT owner_scope || ':' || quantity FROM pj_inventory_items WHERE item_instance_id = '$($item.item_instance_id)';")
    $netCurrency = Invoke-Postgres @('-c', 'SELECT sum(delta_amount) FROM pj_ledger_entries;')
    if ($replay.status -ne 'REPLAYED' -or $replay.receipt.item_instance_id -ne $item.item_instance_id -or
        $duplicate.status -ne 'CONFLICT' -or $ownership -ne 'CHAR_INV:3' -or $gold.currency_balance -ne '70' -or
        $netCurrency -ne '0' -or ($competingStatuses -join ',') -ne 'COMMITTED,CONFLICT') {
        throw 'Restart, single-delivery or balanced ledger verification failed.'
    }
    $bagEdit = @{ character_id = $character; session_epoch = '1'; edit = 'split'; source = $item.item_instance_id
        source_count = 3; source_order = 0; target_count = 0; target_order = -1; amount = 1; max_stack = 99 }
    $bagKey = [guid]::NewGuid().ToString()
    $bagResult = Invoke-CommandRpc world_bag_edit $bagEdit $bagKey
    $bagReplay = Invoke-CommandRpc world_bag_edit $bagEdit $bagKey
    $original = $bagResult.items | Where-Object id -eq $item.item_instance_id
    if ($bagResult.status -ne 'COMMITTED' -or $bagReplay.status -ne 'REPLAYED' -or $original.count -ne 2 -or
        $bagResult.inventory_revision -ne $bagReplay.inventory_revision) { throw 'The online bag command was not durable and idempotent.' }
    $gearId = [guid]::NewGuid().ToString()
    Invoke-Postgres @('-c', "INSERT INTO pj_inventory_items(item_instance_id,owner_character_id,owner_scope,definition_id,slot_index) VALUES ('$gearId','$character','CHAR_INV','NativeOnlineGear',5);") | Out-Null
    $equipKey = [guid]::NewGuid().ToString()
    $gearIntent = @{ character_id = $character; session_epoch = '1'; item = $gearId; slot = 1; equip = $true
        definition = 'NativeOnlineGear'; version = 1 }
    $equipped = Invoke-CommandRpc world_equipment_edit $gearIntent $equipKey
    if ($equipped.status -ne 'COMMITTED' -or ($equipped.items | Where-Object id -eq $gearId).equipped_slot -ne 1 -or
        (Invoke-CommandRpc world_equipment_edit $gearIntent $equipKey).status -ne 'REPLAYED') { throw 'Equipment projection or replay failed.' }
    $gearIntent.equip = $false
    $unequipped = Invoke-CommandRpc world_equipment_edit $gearIntent ([guid]::NewGuid().ToString())
    if ($unequipped.status -ne 'COMMITTED' -or ($unequipped.items | Where-Object id -eq $gearId).equipped_slot -ne 0) { throw 'Equipment removal failed.' }
    $jobId = [guid]::NewGuid().ToString()
    $startedTicks = [DateTime]::UtcNow.Ticks
    $terms = @{ recipe = 'LocalTimedRecipe'; recipe_version = 1; batches = 1; rules = ('f' * 40)
        started_ticks = [string]$startedTicks; due_ticks = [string]($startedTicks + [TimeSpan]::TicksPerMinute)
        outputs = @(@{ definition = 'FixtureOutput'; version = 1; level = 1; count = 1; max_stack = 10 }) }
    $gearRow = @{ id = $gearId; definition = 'NativeOnlineGear'; version = 1; count = 1; level = 1; bag_order = 5; escrow = $true }
    $jobIntent = @{ character_id = $character; session_epoch = '1'; expected_revision = $unequipped.inventory_revision
        kind = 'start'; job_id = $jobId; recipe = $terms.recipe; recipe_version = 1; batches = 1; rules = $terms.rules
        job_terms = $terms; changes = @($gearRow) }
    $job = Invoke-CommandRpc world_craft_commit $jobIntent $jobId
    if ($job.status -ne 'COMMITTED' -or $job.crafting_jobs[0].escrow[0].id -ne $gearId -or
        ($job.items | Where-Object id -eq $gearId)) { throw 'Craft escrow was not isolated from the bag.' }
    $gearRow.escrow = $false
    $cancel = Invoke-CommandRpc world_craft_commit @{ character_id = $character; session_epoch = '1'
        expected_revision = $job.inventory_revision; kind = 'cancel'; job_id = $jobId; changes = @($gearRow) } ([guid]::NewGuid().ToString())
    if ($cancel.status -ne 'COMMITTED' -or $cancel.crafting_jobs.Count -ne 0 -or
        ($cancel.items | Where-Object id -eq $gearId).count -ne 1) { throw 'Craft cancel did not return the original item.' }
    [ordered]@{ passed = $true; unrealServicePort = [bool]$UnrealEditor; persistedItem = $item.item_instance_id; persistedQuantity = 3; pendingMailRestored = $true
        restartReplay = $replay.status; duplicate = $duplicate.status; competingClaims = $competingStatuses
        authenticatedCharacterLogin = $true; sharedChannelAfterRestart = $true; returnReplayRejectedAfterRestart = $true
        onlineBagSplitCommitted = $true
        onlineEquipmentCommitted = $true; onlineCraftEscrowRestored = $true
        gameLoginAndReplicatedInventory = [bool]$VerifyGameLogin
        postgresDungeonRoundTrip = ($VerifyOnlineDungeon -or $VerifyAuthoredSandbox)
        authoredSandboxCraftAndBoss = [bool]$VerifyAuthoredSandbox
        currencyBalance = $gold.currency_balance; ledgerNet = $netCurrency } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $scratch 'verification.json') -Encoding utf8
    Write-Output "Nakama and PostgreSQL loopback restart checks passed. $scratch"
} finally {
    try { Stop-Nakama; Stop-Postgres }
    finally {
        $env:PGPASSWORD = $previousPassword
        foreach ($file in $passwordFile, $config) { if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file } }
    }
}
