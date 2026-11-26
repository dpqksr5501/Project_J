param(
    [Parameter(Mandatory)][string]$PostgresBin,
    [Parameter(Mandatory)][string]$NakamaExe,
    [string]$UnrealEditor,
    [switch]$VerifyGameLogin,
    [switch]$VerifyGameChat,
    [switch]$RenderLoginClient,
    [switch]$VerifyServerDrain,
    [switch]$VerifyOnlineDungeon,
    [switch]$VerifyAuthoredSandbox,
    [switch]$VerifyPublicRegions,
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

function Invoke-ChannelRouting([string]$Account, [string]$Token) {
    $zone = 'LocalNativeRouting'
    $first = [guid]::NewGuid().ToString()
    $second = [guid]::NewGuid().ToString()
    $waiting = [guid]::NewGuid().ToString()
    Invoke-Postgres @('-c', "INSERT INTO pj_characters(character_id, account_id, char_name, class_id) VALUES ('$first', '$Account', 'Route primary', 'Test'), ('$second', '$Account', 'Route overflow', 'Test'), ('$waiting', '$Account', 'Route queue', 'Test');") | Out-Null
    foreach ($channel in 1..2) {
        $lease = @{ worker = [guid]::NewGuid().ToString(); zone = $zone; channel = $channel
            endpoint = "127.0.0.1:17993"; capacity = 1; population = 0; draining = $false }
        if ((Invoke-CommandRpc world_channel_heartbeat $lease '').status -ne 'OK') { throw 'Route fixture lease failed.' }
    }
    $route = @{ account_id = $Account; character_id = $first; zone = $zone; preferred_channel = 1 }
    $unauthorized = Invoke-WebRequest "$uri/v2/rpc/j_world_channel_route?unwrap" -Method Post -SkipHttpErrorCheck `
        -Headers @{ Authorization = "Bearer $Token" } -ContentType 'application/json' -Body (New-RpcBody $route '') -TimeoutSec 8
    if ($unauthorized.StatusCode -lt 400) { throw 'A player token reached the server-only routing RPC.' }
    $primary = Invoke-CommandRpc world_channel_route $route ''
    $route.character_id = $second
    $direct = Request-WorldLogin $Token $second 1 $zone
    $overflow = Invoke-CommandRpc world_channel_route $route ''
    $route.character_id = $waiting
    $queued = Invoke-CommandRpc world_channel_route $route ''
    if ($primary.status -ne 'ROUTED' -or $primary.channel -ne 1 -or !$primary.ticket -or
        $direct.status -ne 'UNAVAILABLE' -or $overflow.status -ne 'ROUTED' -or $overflow.channel -ne 2 -or
        !$overflow.ticket -or $queued.status -ne 'QUEUED' -or $queued.position -ne 1) {
        throw 'Nakama HTTP routing failed to share reservations with direct login.'
    }
    $playerRoute = @{ character_id = $waiting; zone = $zone; channel = 1 }
    function Send-PlayerRoute {
        Invoke-RestMethod "$uri/v2/rpc/j_world_player_route?unwrap" -Method Post -Headers @{ Authorization = "Bearer $Token" } `
            -ContentType 'application/json' -Body ($playerRoute | ConvertTo-Json -Compress) -TimeoutSec 8
    }
    $head = Send-PlayerRoute
    if ($head.status -ne 'QUEUED' -or $head.position -ne 1 -or !$head.slot) { throw 'Player route did not return a FIFO slot.' }
    $playerRoute.slot = $head.slot
    if ((Send-PlayerRoute).slot -ne $head.slot) { throw 'Polling changed the queue slot.' }
    $playerRoute.cancel = $true
    if ((Send-PlayerRoute).status -ne 'CANCELLED') { throw 'Queue cancellation failed.' }
    $playerRoute.Remove('cancel')
    if ((Send-PlayerRoute).status -ne 'EXPIRED') { throw 'Stale poll resurrected a cancelled queue slot.' }
    $playerRoute.Remove('slot')
    $nextSlot = (Send-PlayerRoute).slot
    Invoke-Postgres @('-c', "DELETE FROM pj_world_login_tickets WHERE zone_id = '$zone' AND channel_id = 2;") | Out-Null
    if ((Request-WorldLogin $Token $second 2 $zone).status -ne 'UNAVAILABLE') { throw 'Exact login bypassed the FIFO queue.' }
    $playerRoute.slot = $nextSlot
    $admitted = Send-PlayerRoute
    if ($admitted.status -ne 'ROUTED' -or $admitted.channel -ne 2 -or !$admitted.ticket) { throw 'Queued player did not receive an overflow ticket.' }

    # Public region A -> B -> A uses routed admission and a fresh fenced session at each destination.
    # These are disposable service leases. The authored dungeon check below also runs actual UE servers.
    $traveler = [guid]::NewGuid().ToString()
    Invoke-Postgres @('-c', "INSERT INTO pj_characters(character_id, account_id, char_name, class_id) VALUES ('$traveler','$Account','Region traveler','Test');") | Out-Null
    $regions = @(
        @{zone='FieldNorth';channel=1;worker=[guid]::NewGuid().ToString();endpoint='127.0.0.1:17994';capacity=4;population=0;draining=$false},
        @{zone='FieldSouth';channel=1;worker=[guid]::NewGuid().ToString();endpoint='127.0.0.1:17995';capacity=4;population=0;draining=$false}
    )
    foreach ($region in $regions) {
        if ((Invoke-CommandRpc world_channel_heartbeat $region '').status -ne 'OK') { throw 'Public region lease failed.' }
    }
    $epoch = 0
    foreach ($region in @($regions[0],$regions[1],$regions[0])) {
        $playerRoute = @{character_id=$traveler;zone=$region.zone;channel=$region.channel}
        $hop = Send-PlayerRoute
        if ($hop.status -ne 'ROUTED' -or $hop.endpoint -ne $region.endpoint -or !$hop.ticket) { throw 'Public region route returned the wrong destination.' }
        $intent = @{ticket=$hop.ticket;zone=$region.zone;channel=$region.channel;worker=$region.worker}
        $session = Invoke-CommandRpc world_redeem_login $intent $hop.ticket
        ++$epoch
        if ($session.status -ne 'OK' -or $session.character -ne $traveler -or [long]$session.epoch -ne $epoch) { throw 'Public region transfer did not advance the character session.' }
        [long]$session.epoch
    }
}

function Invoke-GameLogin([string]$Character, [string]$AccessToken, [string]$ItemId, [switch]$Drain) {
    if (!$UnrealEditor) { throw 'VerifyGameLogin requires an existing Unreal Editor executable.' }
    $port = if ($Drain) { 17991 } else { 17989 }
    $channel = if ($Drain) { 4 } else { 1 }
    $role = if ($Drain) { 'Drain' } else { 'Login' }
    $map = '/Engine/Maps/Entry?game=/Script/Project_J.Project_JOnlineLoginFixture'
    $common = @("$root/Project_J.uproject", $map, '-unattended', '-nop4', '-nosound', '-NullRHI', '-NoLiveCoding', '-NoDDCCleanup',
        '-DisablePython', '-DisablePlugins=EditorToolset,AutomationTestToolset,ToolsetRegistry', '-multihome=127.0.0.1', '-ProjectJOnlineLoginFixture')
    if ($Drain) { $common += '-ProjectJOnlineDrainFixture' }
    else { $common += '-ProjectJVerifyOnlineQuest' }
    $serverArgs = $common + @('-server', "-port=$port", "-UserDir=$scratch/${role}Server", "-ABSLOG=$scratch/${role}Server.log",
        "-ProjectJOnlineOutboxPath=$scratch/${role}Outbox.sqlite",
        "-ini:Game:[/Script/Project_J.Project_JGatewaySubsystem]:GatewayUrl=$uri",
        '-ini:Game:[/Script/Project_J.Project_JNakamaServicePortSubsystem]:RuntimeMode=LocalDockerNakama',
        "-ini:Game:[/Script/Project_J.Project_JGameMode]:OnlineAdvertisedEndpoint=127.0.0.1:$port",
        "-ini:Game:[/Script/Project_J.Project_JGameMode]:OnlineChannelId=$channel")
    $health = 'http://127.0.0.1:17992/healthz'
    if ($Drain) { $serverArgs += '-ProjectJMetricsPort=17992' }
    $server = Start-Process -FilePath $UnrealEditor -ArgumentList ($serverArgs | ForEach-Object { '"' + $_ + '"' }) -PassThru -WindowStyle Hidden `
        -Environment @{ PROJECT_J_NAKAMA_HTTP_KEY = $nakamaKey; PGPASSWORD = $null }
    $null = $server.Handle
    $deadline = [DateTime]::UtcNow.AddSeconds(45)
    do {
        if ($server.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw "Online Unreal server was not ready. See $scratch/${role}Server.log" }
        Start-Sleep -Milliseconds 300
        $registry = Invoke-CommandRpc world_channel_list @{ zone = 'Entry' } ''
        $channelListed = @($registry.channels | Where-Object channel_id -eq $channel).Count -eq 1
        $inService = !$Drain
        if ($Drain -and $channelListed) {
            # The channel lease can publish while the world is still loading and the metrics ticker is idle.
            try { $inService = (Invoke-WebRequest -Uri $health -TimeoutSec 1 -SkipHttpErrorCheck).StatusCode -eq 200 } catch {}
        }
    } until ($channelListed -and $inService)
    $authProbe = Request-WorldLogin $AccessToken $Character $channel Entry
    if ($authProbe.status -ne 'OK') { throw "Online world credential preflight failed with status $($authProbe.status)." }
    if (!$Drain) {
        # Seed just the queue head. The real UE login must receive a position and poll for its ticket.
        $queueHead = [guid]::NewGuid().ToString()
        Invoke-Postgres @('-c', "INSERT INTO pj_characters(character_id,account_id,char_name,class_id) SELECT '$queueHead',account_id,'Queue head probe','Test' FROM pj_characters WHERE character_id='$Character'; INSERT INTO pj_world_login_queue(account_id,character_id,zone_id) SELECT account_id,character_id,'Entry' FROM pj_characters WHERE character_id='$queueHead';") | Out-Null
    }
    $clientArgs = $common + @('-game', "-UserDir=$scratch/${role}Client", "-ABSLOG=$scratch/${role}Client.log",
        "-ini:Game:[/Script/Project_J.Project_JOnlineWorldSubsystem]:NakamaClientUrl=$uri")
    if ($RenderLoginClient -and !$Drain) {
        $clientArgs = @($clientArgs | Where-Object { $_ -ne '-NullRHI' }) +
            @('-d3d12','-RenderOffscreen','-windowed','-ResX=1280','-ResY=720',"-ProjectJQueueScreenshot=$scratch/LoginQueue.png")
    }
    $client = Start-Process -FilePath $UnrealEditor -ArgumentList ($clientArgs | ForEach-Object { '"' + $_ + '"' }) -PassThru -WindowStyle Hidden `
        -Environment @{ PROJECT_J_NAKAMA_HTTP_KEY = $null; PGPASSWORD = $null; PROJECT_J_TEST_LOGIN_CHARACTER = $Character
            PROJECT_J_TEST_LOGIN_ACCESS = $AccessToken; PROJECT_J_TEST_LOGIN_ITEM = $ItemId; PROJECT_J_TEST_CHAT_TARGET = $queueHead }
    $null = $client.Handle
    $drainingObserved = $false
    if (!$Drain) {
        $deadline = [DateTime]::UtcNow.AddSeconds(40)
        $clientLogPath = "$scratch/${role}Client.log"
        do {
            Start-Sleep -Milliseconds 200
            $queued = (Test-Path -LiteralPath $clientLogPath) -and (Select-String -LiteralPath $clientLogPath -SimpleMatch 'ONLINE_FIXTURE_QUEUE_POSITION 2' -Quiet)
        } until ($queued -or $client.HasExited -or [DateTime]::UtcNow -ge $deadline)
        Invoke-Postgres @('-c', "DELETE FROM pj_world_login_queue WHERE character_id='$queueHead' AND zone_id='Entry';") | Out-Null
        if (!$queued) { throw "UE did not observe its FIFO position. See $clientLogPath" }
    }
    if ($Drain) {
        $deadline = [DateTime]::UtcNow.AddSeconds(55)
        while (!$server.HasExited -and [DateTime]::UtcNow -lt $deadline) {
            try {
                $response = Invoke-WebRequest -Uri $health -TimeoutSec 1 -SkipHttpErrorCheck
                if ($response.StatusCode -eq 503 -and (ConvertFrom-Json $response.Content).draining) {
                    $drainingObserved = $true
                    break
                }
            } catch {}
            Start-Sleep -Milliseconds 100
        }
    }
    $server.WaitForExit()
    $client.WaitForExit()
    if ($RenderLoginClient -and !$Drain -and !(Test-Path -LiteralPath "$scratch/LoginQueue.png")) {
        throw "Rendered queue frame missing. See $scratch/LoginClient.log"
    }
    if ($server.ExitCode -ne 0 -or $client.ExitCode -ne 0) {
        throw "Online fixture process failed. See $scratch/${role}Server.log and ${role}Client.log"
    }
    if ($Drain) {
        $drainLog = Get-Content -LiteralPath "$scratch/DrainServer.log" -Raw
        $clientLog = Get-Content -LiteralPath "$scratch/DrainClient.log" -Raw
        if (!$drainingObserved -or $drainLog -notmatch 'Graceful Server Drain started.*players=1' -or
            !$drainLog.Contains('ONLINE_DRAIN_AUTHORIZED_LOGOUT') -or
            !$drainLog.Contains('Graceful Server Drain completed (evacuated=1 outboxSettled=1)') -or
            !$clientLog.Contains('ONLINE_DRAIN_CLIENT_POSSESSED') -or !$clientLog.Contains('ONLINE_DRAIN_CLIENT_DISCONNECTED')) {
            throw "Connected-player drain failed. See $scratch/DrainServer.log and DrainClient.log"
        }
    } elseif (!(Select-String -LiteralPath "$scratch/LoginClient.log" -SimpleMatch 'ONLINE_FIXTURE_INVENTORY_REPLICATED' -Quiet)) {
        throw "Replicated inventory failed. See $scratch/LoginClient.log"
    } elseif ((Invoke-Postgres @('-c', "SELECT EXISTS(SELECT 1 FROM pj_social_relations WHERE owner_char_id='$Character' AND target_char_id='$queueHead' AND relation_type='BLOCKED');")) -ne 't' -or
        !(Select-String -LiteralPath "$scratch/LoginClient.log" -SimpleMatch 'ONLINE_FIXTURE_CHAT_BLOCK_ROUND_TRIP' -Quiet)) {
        throw 'The client did not complete block, unblock and re-block through the authoritative server.'
    }
}

function Invoke-GameChat([string]$Sender, [string]$SenderToken, [string]$Receiver, [string]$ReceiverToken) {
    if (!$UnrealEditor) { throw 'VerifyGameChat requires an existing Unreal Editor executable.' }
    $channel = 5
    $port = 17993
    $map = '/Engine/Maps/Entry?game=/Script/Project_J.Project_JOnlineLoginFixture'
    $common = @("$root/Project_J.uproject", $map, '-unattended', '-nop4', '-nosound', '-NullRHI', '-NoLiveCoding', '-NoDDCCleanup',
        '-DisablePython', '-DisablePlugins=EditorToolset,AutomationTestToolset,ToolsetRegistry', '-multihome=127.0.0.1',
        '-ProjectJOnlineLoginFixture', '-ProjectJOnlineChatFixture', '-corelimit=2', '-ExecCmds=t.MaxFPS 30')
    $serverArgs = $common + @('-server', "-port=$port", "-UserDir=$scratch/ChatServer", "-ABSLOG=$scratch/ChatServer.log",
        "-ProjectJChatSender=$Sender", "-ProjectJChatReceiver=$Receiver", "-ProjectJOnlineOutboxPath=$scratch/ChatOutbox.sqlite",
        "-ini:Game:[/Script/Project_J.Project_JGatewaySubsystem]:GatewayUrl=$uri",
        '-ini:Game:[/Script/Project_J.Project_JNakamaServicePortSubsystem]:RuntimeMode=LocalDockerNakama',
        "-ini:Game:[/Script/Project_J.Project_JGameMode]:OnlineAdvertisedEndpoint=127.0.0.1:$port",
        "-ini:Game:[/Script/Project_J.Project_JGameMode]:OnlineChannelId=$channel")
    $children = @()
    try {
        $server = Start-Process -FilePath $UnrealEditor -ArgumentList ($serverArgs | ForEach-Object { '"' + $_ + '"' }) `
            -PassThru -WindowStyle Hidden -Environment @{ PROJECT_J_NAKAMA_HTTP_KEY = $nakamaKey; PGPASSWORD = $null }
        $children += $server
        $null = $server.Handle
        $deadline = [DateTime]::UtcNow.AddSeconds(90)
        do {
            if ($server.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw "Chat server was not ready. See $scratch/ChatServer.log" }
            Start-Sleep -Milliseconds 300
            $registry = Invoke-CommandRpc world_channel_list @{ zone = 'Entry' } ''
        } until (@($registry.channels | Where-Object channel_id -eq $channel).Count -eq 1)
        foreach ($peer in @(@{ Role = 'ChatSender'; Character = $Sender; Token = $SenderToken },
                            @{ Role = 'ChatReceiver'; Character = $Receiver; Token = $ReceiverToken })) {
            $clientArgs = $common + @('-game', "-UserDir=$scratch/$($peer.Role)", "-ABSLOG=$scratch/$($peer.Role).log",
                "-ini:Game:[/Script/Project_J.Project_JOnlineWorldSubsystem]:NakamaClientUrl=$uri")
            $client = Start-Process -FilePath $UnrealEditor -ArgumentList ($clientArgs | ForEach-Object { '"' + $_ + '"' }) `
                -PassThru -WindowStyle Hidden -Environment @{ PROJECT_J_NAKAMA_HTTP_KEY = $null; PGPASSWORD = $null
                    PROJECT_J_TEST_LOGIN_CHARACTER = $peer.Character; PROJECT_J_TEST_LOGIN_ACCESS = $peer.Token
                    PROJECT_J_TEST_CHAT_SENDER = $Sender }
            $children += $client
            $null = $client.Handle
        }
        $serverLogPath = Join-Path $scratch 'ChatServer.log'
        $startupTime = [System.Diagnostics.Stopwatch]::StartNew()
        do {
            if (($children | Where-Object HasExited) -or $startupTime.Elapsed.TotalSeconds -ge 120) {
                throw "Both chat clients did not become ready in the same world. See $scratch/Chat*.log"
            }
            Start-Sleep -Milliseconds 200
            $chatStarted = (Test-Path -LiteralPath $serverLogPath) -and
                (Select-String -LiteralPath $serverLogPath -SimpleMatch 'ONLINE_CHAT_SAME_WORLD_STARTED' -Quiet)
        } until ($chatStarted)
        $exerciseTime = [System.Diagnostics.Stopwatch]::StartNew()
        while ($children | Where-Object { !$_.HasExited }) {
            if (($children | Where-Object { $_.HasExited -and $_.ExitCode -ne 0 }) -or $exerciseTime.Elapsed.TotalSeconds -ge 90) {
                throw "The two-client chat exchange failed or timed out. See $scratch/Chat*.log"
            }
            Start-Sleep -Milliseconds 200
        }
        if ($children | Where-Object ExitCode -ne 0) { throw "A chat process failed. See $scratch/Chat*.log" }
        $senderLog = Get-Content -LiteralPath "$scratch/ChatSender.log" -Raw
        $receiverLog = Get-Content -LiteralPath "$scratch/ChatReceiver.log" -Raw
        $received = '(?m)ONLINE_CHAT_WORLD_RECEIVED (Before|WhileBlocked|After)\s*$'
        $senderMessages = ([regex]::Matches($senderLog, $received) | ForEach-Object { $_.Groups[1].Value }) -join ','
        $receiverMessages = ([regex]::Matches($receiverLog, $received) | ForEach-Object { $_.Groups[1].Value }) -join ','
        if ($senderMessages -ne 'Before,WhileBlocked,After' -or $receiverMessages -ne 'Before,After' -or
            $receiverLog -notmatch '(?s)ONLINE_CHAT_ACK Block.*ONLINE_CHAT_MUTED_CHECKPOINT.*ONLINE_CHAT_ACK Unblock' -or
            !(Select-String -LiteralPath "$scratch/ChatServer.log" -SimpleMatch 'ONLINE_CHAT_SAME_WORLD_COMPLETE' -Quiet)) {
            throw "World chat receipt or block/unblock evidence was incomplete. See $scratch/Chat*.log"
        }
        $blockRemains = Invoke-Postgres @('-c', "SELECT EXISTS (SELECT 1 FROM pj_social_relations WHERE owner_char_id='$Receiver' AND target_char_id='$Sender' AND relation_type='BLOCKED');")
        if ($blockRemains -ne 'f') { throw 'The chat fixture unblock did not clear the durable relation.' }
    } finally {
        foreach ($child in $children) {
            if (!$child.HasExited) { Stop-Process -InputObject $child; $child.WaitForExit() }
            $child.Dispose()
        }
    }
}

function Invoke-PublicRegionTravel([string]$Character, [string]$AccessToken) {
    if (!$UnrealEditor) { throw 'Public region travel requires the local Unreal executable.' }
    $regions = @(
        @{zone='Test_MMO_Sandbox';map='/Game/Maps/Test_MMO_Sandbox';port=17994;visits=2},
        @{zone='L_RidgeRoute03';map='/Game/World/Validation/RidgeRoute03/L_RidgeRoute03';port=17995;visits=1}
    )
    $common = @("$root/Project_J.uproject", '-unattended', '-nop4', '-nosound', '-NullRHI', '-NoLiveCoding', '-NoDDCCleanup',
        '-DisablePython', '-DisablePlugins=EditorToolset,AutomationTestToolset,ToolsetRegistry', '-ProjectJOnlineLoginFixture', '-multihome=127.0.0.1')
    function Start-PublicRole([string]$Role, [string[]]$Options, [hashtable]$Environment) {
        $arguments = $common + $Options + @("-UserDir=$scratch/Public$Role", "-ABSLOG=$scratch/Public$Role.log")
        $child = Start-Process -FilePath $UnrealEditor -WindowStyle Hidden -PassThru -Environment $Environment `
            -ArgumentList (($arguments | ForEach-Object {'"' + $_ + '"'}) -join ' ')
        $null = $child.Handle
        return $child
    }
    $epochSQL = "SELECT session_epoch FROM pj_characters WHERE character_id='$Character';"
    $before = [long](Invoke-Postgres @('-c', $epochSQL))
    $servers = @()
    $client = $null
    try {
        foreach ($region in $regions) {
            $servers += Start-PublicRole $region.zone @("$($region.map)?game=/Script/Project_J.Project_JOnlineLoginFixture",
                '-server', "-port=$($region.port)", "-ProjectJPublicVisits=$($region.visits)", "-ProjectJOnlineOutboxPath=$scratch/$($region.zone).sqlite",
                "-ini:Game:[/Script/Project_J.Project_JGameMode]:OnlineAdvertisedEndpoint=127.0.0.1:$($region.port)",
                "-ini:Game:[/Script/Project_J.Project_JGatewaySubsystem]:GatewayUrl=$uri",
                '-ini:Game:[/Script/Project_J.Project_JNakamaServicePortSubsystem]:RuntimeMode=LocalDockerNakama') `
                @{PROJECT_J_NAKAMA_HTTP_KEY=$nakamaKey;PGPASSWORD=$null}
        }
        $deadline = [DateTime]::UtcNow.AddSeconds(45)
        do {
            if (($servers | Where-Object HasExited) -or [DateTime]::UtcNow -gt $deadline) { throw "Public region workers failed to register. $scratch" }
            Start-Sleep -Milliseconds 250
            $leases = Invoke-Postgres @('-c', "SELECT COUNT(*) FROM pj_world_channels WHERE (zone_id,endpoint) IN (('Test_MMO_Sandbox','127.0.0.1:17994'),('L_RidgeRoute03','127.0.0.1:17995')) AND NOT is_dungeon AND leased_until>NOW();")
        } until ($leases -eq '2')
        $client = Start-PublicRole Client @('/Engine/Maps/Entry?game=/Script/Project_J.Project_JOnlineLoginFixture', '-game',
            '-ProjectJPublicRoute=Test_MMO_Sandbox,L_RidgeRoute03,Test_MMO_Sandbox',
            "-ini:Game:[/Script/Project_J.Project_JOnlineWorldSubsystem]:NakamaClientUrl=$uri") `
            @{PROJECT_J_TEST_LOGIN_CHARACTER=$Character;PROJECT_J_TEST_LOGIN_ACCESS=$AccessToken;PROJECT_J_NAKAMA_HTTP_KEY=$null;PGPASSWORD=$null}
    } finally {
        # Each owned fixture has its own normal engine watchdog, including startup failures.
        foreach ($role in @($client) + $servers) { if ($role) { $role.WaitForExit() } }
    }
    if ($client.ExitCode -ne 0 -or ($servers | Where-Object ExitCode -ne 0)) { throw "Public region process failed. $scratch" }
    $hops = @(Select-String -LiteralPath "$scratch/PublicClient.log" -Pattern 'PUBLIC_REGION_POSSESSED hop=(\d+) zone=(\w+)' |
        ForEach-Object {$_.Matches[0].Groups[2].Value})
    $epochs = @(foreach ($region in $regions) {
        Select-String -LiteralPath "$scratch/Public$($region.zone).log" -Pattern 'PUBLIC_REGION_ADMISSION zone=(\w+) epoch=(\d+)' |
            ForEach-Object {[long]$_.Matches[0].Groups[2].Value}
    })
    if (($hops -join ',') -ne 'Test_MMO_Sandbox,L_RidgeRoute03,Test_MMO_Sandbox' -or
        ($epochs -join ',') -ne (@(($before+1),($before+3),($before+2)) -join ',') -or [long](Invoke-Postgres @('-c', $epochSQL)) -ne $before+3) {
        throw "Public region possession or session fencing mismatch. $scratch"
    }
    return @{zones=$hops;epochs=@($epochs[0],$epochs[2],$epochs[1]);clientExit=$client.ExitCode;serverExits=@($servers.ExitCode)}
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
        "-ProjectJOnlineOutboxPath=$scratch/SourceOutbox.sqlite",
        "-ini:Game:[/Script/Project_J.Project_JGameMode]:OnlineAdvertisedEndpoint=127.0.0.1:$sourcePort",
        '-ini:Game:[/Script/Project_J.Project_JGameMode]:MaxTrustedSessions=48')) $serverEnv
    $destination = Start-DungeonRole Destination (@($destinationMap) + $serverSettings + @("-port=$destinationPort", '-ProjectJOnlineDungeonDestination',
        "-ProjectJOnlineOutboxPath=$scratch/DestinationOutbox.sqlite",
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
    $fieldRewardQuery = "SELECT count(*) FROM pj_boss_reward_receipts r WHERE r.character_id = '$Character' AND NOT EXISTS (SELECT 1 FROM pj_dungeon_instances i WHERE i.instance_id = r.defeat_id) AND jsonb_array_length(r.response_json->'mail_ids') > 0;"
    $fieldRewardsBefore = [int](Invoke-Postgres @('-c', $fieldRewardQuery))
    $manaQuantityQuery = "SELECT COALESCE(sum(quantity),0) FROM pj_inventory_items WHERE owner_character_id='$Character' AND owner_scope='CHAR_INV' AND definition_id='ManaPotion';"
    if ($VerifyAuthoredSandbox) { $manaBefore = [long](Invoke-Postgres @('-c', $manaQuantityQuery)) }
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
        throw "Dungeon round trip failed. Client/source/destination exit codes $($client.ExitCode)/$($source.ExitCode)/$($destination.ExitCode), epochs $beforeEpoch to $afterEpoch, committed return $finished. See $scratch/DungeonSource.log, DungeonDestination.log and DungeonClient.log"
    }
    if ($VerifyAuthoredSandbox) {
        $craft = Invoke-Postgres @('-c', "SELECT EXISTS (SELECT 1 FROM pj_quest_states q CROSS JOIN LATERAL jsonb_array_elements(q.progress_json->'objectives') g WHERE q.character_id = '$Character' AND q.quest_id = 'prototype.gatherandcraft' AND g->>'recipe' IS NOT NULL AND (g->>'count')::int = 1);")
        $manaAfter = [long](Invoke-Postgres @('-c', $manaQuantityQuery))
        $gather = Invoke-Postgres @('-c', "SELECT count(*) FROM pj_idempotency_receipts WHERE operation = 'online_craft_commit' AND request_json->>'kind' = 'gather' AND request_json->>'character_id' = '$Character';")
        $reward = Invoke-Postgres @('-c', "SELECT count(*) FROM pj_boss_reward_receipts r JOIN pj_dungeon_instances i ON i.instance_id = r.defeat_id WHERE i.dungeon_id = '$dungeonId' AND i.cleared_at IS NOT NULL AND r.character_id = '$Character' AND jsonb_array_length(r.response_json->'mail_ids') > 0;")
        if ($craft -ne 't' -or $manaAfter -ne ($manaBefore + 1) -or $gather -ne '2' -or $reward -ne '1' -or
            ([int](Invoke-Postgres @('-c', $fieldRewardQuery)) -ne (1 + $fieldRewardsBefore)) -or
            !(Select-String -LiteralPath "$scratch/DungeonSource.log" -SimpleMatch 'ONLINE_SANDBOX_FIELD_HIT hp=0' -Quiet) -or
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
    if ($UnrealEditor -and !$VerifyGameLogin -and !$VerifyGameChat -and !$VerifyOnlineDungeon -and !$VerifyAuthoredSandbox -and !$VerifyServerDrain -and !$VerifyPublicRegions) {
        Invoke-UnrealClaim $character $itemIntent.mail_id $itemKey
        $initialReplay = Invoke-CommandRpc mail_claim $itemIntent $itemKey
        if ($initialReplay.status -ne 'REPLAYED') { throw 'Unreal did not commit the tracked command.' }
        $item = $initialReplay.receipt
    } else {
        $item = Invoke-CommandRpc mail_claim $itemIntent $itemKey
    }
    if ($item.status -ne 'COMMITTED' -or !$item.item_instance_id -or $item.quantity -ne 3) { throw 'The item attachment was not claimed.' }
    $questRow = @{ id = 'native.quest.reward'; count = 0; tracked = $true; required = 1; definition_version = 2
        event = 'NativeQuestEvent'; objectives = @() }
    $questAccept = @{ character_id = $character; session_epoch = '1'; kind = 'accept'; quest = $questRow.id; row = $questRow }
    $questProgress = @{ character_id = $character; session_epoch = '1'; kind = 'progress'
        changes = @(@{ row = $questRow; deltas = @(@{ id = ''; amount = 1 }) }) }
    $questClaimKey = [guid]::NewGuid().ToString()
    $questClaim = @{ character_id = $character; session_epoch = '1'; kind = 'claim'; quest = $questRow.id
        definition_version = 2; expected_profile_revision = '1'; reward_level = 2; reward_xp = '15'
        reward_items = @(@{ definition = 'NativeQuestReward'; version = 2; level = 1; quantity = 2; max_stack = 5 }) }
    if ((Invoke-CommandRpc world_quest_edit $questAccept ([guid]::NewGuid().ToString())).status -ne 'COMMITTED' -or
        (Invoke-CommandRpc world_quest_edit $questProgress ([guid]::NewGuid().ToString())).status -ne 'COMMITTED') {
        throw 'The real PostgreSQL quest was not accepted and completed.'
    }
    Invoke-Postgres @('-c', "UPDATE pj_characters SET inventory_capacity = 1 WHERE character_id = '$character';") | Out-Null
    $questFull = Invoke-CommandRpc world_quest_edit $questClaim $questClaimKey
    if ($questFull.status -ne 'INVENTORY_FULL' -or $questFull.xp -ne '0' -or $questFull.quests[0].id -ne $questRow.id) {
        throw 'A full bag did not preserve quest completion eligibility and XP.'
    }
    Invoke-Postgres @('-c', "UPDATE pj_characters SET inventory_capacity = 256 WHERE character_id = '$character';") | Out-Null
    $questPaid = Invoke-CommandRpc world_quest_edit $questClaim $questClaimKey
    $questItem = $questPaid.items | Where-Object definition -eq 'NativeQuestReward'
    if ($questPaid.status -ne 'COMMITTED' -or $questPaid.level -ne 2 -or $questPaid.xp -ne '15' -or
        $questPaid.completed_quest -ne $questRow.id -or $questItem.count -ne 2 -or $questItem.version -ne 2) {
        throw 'The real PostgreSQL quest claim did not commit versioned items and XP.'
    }
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
    $muteIntent = @{ character_id=$onlineCharacter; target=$character; block=$true; block_revision=$redeemed.block_revision; session_epoch=$redeemed.epoch }
    $mute = Invoke-CommandRpc world_set_chat_block $muteIntent ([guid]::NewGuid().ToString())
    if ($mute.status -ne 'OK' -or $mute.blocked_characters -notcontains $character) { throw 'Private block did not persist.' }
    if ($VerifyGameLogin) {
        # A normal, non-travel quest completed before login and before either backend restarts.
        $priorQuest = @{ id = 'native.quest.ownerhistory'; count = 0; tracked = $true; required = 1; definition_version = 1
            event = 'NativePriorQuest'; objectives = @() }
        $priorAccept = @{ character_id = $onlineCharacter; session_epoch = $redeemed.epoch; kind = 'accept'; quest = $priorQuest.id; row = $priorQuest }
        $priorProgress = @{ character_id = $onlineCharacter; session_epoch = $redeemed.epoch; kind = 'progress'
            changes = @(@{ row = $priorQuest; deltas = @(@{ id = ''; amount = 1 }) }) }
        $priorClaim = @{ character_id = $onlineCharacter; session_epoch = $redeemed.epoch; kind = 'claim'; quest = $priorQuest.id
            definition_version = 1; expected_profile_revision = $redeemed.profile_revision; reward_level = 1; reward_xp = '15'; reward_items = @() }
        foreach ($command in @($priorAccept, $priorProgress, $priorClaim)) {
            if ((Invoke-CommandRpc world_quest_edit $command ([guid]::NewGuid().ToString())).status -ne 'COMMITTED') {
                throw 'The previous-session quest fixture did not commit.'
            }
        }
    }
    Stop-Nakama
    Stop-Postgres
    Start-Postgres
    Start-Nakama
    $questReplay = Invoke-CommandRpc world_quest_edit $questClaim $questClaimKey
    $questHistory = Invoke-CommandRpc world_quest_history @{ character_id = $character; session_epoch = '1'; count = 1; ordinal = 0 } ''
    $staleHistory = Invoke-CommandRpc world_quest_history @{ character_id = $character; session_epoch = '1'; count = 0; ordinal = 0 } ''
    if ($questReplay.status -ne 'REPLAYED' -or ($questReplay.items | Where-Object definition -eq 'NativeQuestReward').id -ne $questItem.id -or
        $questReplay.xp -ne '15' -or (Invoke-CommandRpc world_quest_edit $questClaim ([guid]::NewGuid().ToString())).status -ne 'STALE' -or
        $questHistory.status -ne 'OK' -or $questHistory.count -ne 1 -or $questHistory.ids[0] -ne $questRow.id -or
        $staleHistory.status -ne 'STALE' -or $staleHistory.count -ne 1) {
        throw 'The real PostgreSQL quest grant did not survive restart as one claim.'
    }
    if ((Invoke-CommandRpc world_channel_heartbeat $channel '').status -ne 'OK') { throw 'Channel lease could not renew after restart.' }
    $sharedChannels = Invoke-CommandRpc world_channel_list @{ zone = 'LocalWorld' } ''
    if ($sharedChannels.channels.Count -ne 1 -or $sharedChannels.channels[0].channel_id -ne 2) { throw 'The shared channel was missing after restart.' }
    $next = Request-WorldLogin $principal.token $onlineCharacter 2
    $worldIntent.ticket = $next.ticket
    if ((Invoke-CommandRpc world_redeem_login $worldIntent $next.ticket).status -ne 'CONFLICT') { throw 'A consumed return nonce replayed after restart.' }
    $worldIntent.Remove('return_nonce')
    $worldIntent.Remove('return_expires')
    $worldIntent.Remove('return_character')
    $reconnected = Invoke-CommandRpc world_redeem_login $worldIntent $next.ticket
    if ($reconnected.epoch -ne '2' -or $reconnected.blocked_characters -notcontains $character) { throw 'Login or private block projection failed after restart.' }
    if ((Invoke-CommandRpc world_set_chat_block $muteIntent ([guid]::NewGuid().ToString())).status -ne 'STALE_SESSION') { throw 'Old chat lease remained writable.' }
    $publicRegionEpochs = @(Invoke-ChannelRouting $account $principal.token)
    $publicTravel = if ($VerifyPublicRegions) {
        $fieldCharacter = [guid]::NewGuid().ToString()
        Invoke-Postgres @('-c', "INSERT INTO pj_characters(character_id,account_id,char_name,class_id) VALUES ('$fieldCharacter','$account','Field traveler','GreatSword');") | Out-Null
        Invoke-PublicRegionTravel $fieldCharacter $principal.token
    }
    if ($VerifyGameChat) {
        $chatAccount = [guid]::NewGuid().ToString()
        $chatSender = [guid]::NewGuid().ToString()
        Invoke-Postgres @('-c', "INSERT INTO pj_accounts(account_id,external_sub) VALUES ('$chatAccount','nakama:$($unrelated.id)'); INSERT INTO pj_characters(character_id,account_id,char_name,class_id) VALUES ('$chatSender','$chatAccount','Chat sender','GreatSword');") | Out-Null
        Invoke-GameChat $chatSender $unrelated.token $onlineCharacter $principal.token
    }
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
            (Invoke-Postgres @('-c', "SELECT sum(quantity) FROM pj_inventory_items WHERE owner_character_id = '$onlineCharacter' AND owner_scope = 'CHAR_INV' AND definition_id = 'HealthPotion';")) -ne '3') {
            throw 'The replicated game split did not match its PostgreSQL rows.'
        }
        $gameQuest = Invoke-Postgres @('-c', "SELECT (c.xp = 40 AND c.quest_completion_count = 2) AND EXISTS (SELECT 1 FROM pj_inventory_items WHERE owner_character_id = c.character_id AND definition_id = 'ManaPotion' AND quantity = 2 AND owner_scope = 'CHAR_INV') FROM pj_characters c WHERE c.character_id = '$onlineCharacter';")
        if ($gameQuest -ne 't' -or !(Select-String -LiteralPath "$scratch/LoginClient.log" -SimpleMatch 'ONLINE_FIXTURE_QUEST_REWARD_AND_HISTORY_REPLICATED' -Quiet)) {
            throw 'The Unreal client did not claim the item/XP reward and load both completion pages from the normal owner RPC.'
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
    if ($VerifyServerDrain) { Invoke-GameLogin $onlineCharacter $principal.token '' -Drain }
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
        serverOnlyChannelRouting = $true; directAndRoutedTicketsShareCapacity = $true; publicRegionRoundTripEpochs = $publicRegionEpochs
        publicRegionGameTravel = $publicTravel
        onlineBagSplitCommitted = $true
        onlineEquipmentCommitted = $true; onlineCraftEscrowRestored = $true
        onlineQuestItemsAndExperienceCommitted = $true; onlineQuestFullBagRolledBack = $true; onlineQuestReplay = $questReplay.status
        onlineQuestHistoryAfterRestart = $true
        chatBlockAfterRestartAndFencing = $true
        gameLoginAndReplicatedInventory = [bool]$VerifyGameLogin
        gameQuestRewardAndHistory = [bool]$VerifyGameLogin
        gameChatBlockRoundTrip = [bool]$VerifyGameLogin
        gameWorldChatDeliveredAndMuted = [bool]$VerifyGameChat
        connectedPlayerDrain = [bool]$VerifyServerDrain
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
