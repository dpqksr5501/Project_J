#requires -Version 7.4
param(
    [Parameter(Mandatory)][ValidateSet('Server', 'Generator', 'Verify', 'LocalSmoke')][string]$Role,
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]{1,64}$')][string]$RunName,
    [Parameter(Mandatory)][ValidateRange(1,200)][int]$Clients,
    [string]$LocalAddress,
    [string]$PeerAddress,
    [ValidateRange(1025,65535)][int]$Port = 17976,
    [ValidateRange(1,200)][int]$CloseBatchSize = 8,
    [ValidateRange(2,64)][int]$HeadlessCoreLimit,
    [ValidateRange(0,65536)][int]$MemoryReserveMiB = 4096,
    [ValidateRange(1,1000)][double]$MaxBurstTickIntervalP95Ms,
    [ValidateRange(1,199)][int]$VerifyCrowdCap,
    [switch]$LegacyReplication,
    [switch]$UsePerfThreads,
    [switch]$ThinClient,
    [switch]$ServerTrace,
    [string]$GeneratorReportDirectory,
    [string]$UnrealEditor = 'C:/Program Files/Epic Games/UE_5.9/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$report = Join-Path $root "Saved/Validation/Continuation_20261014/${RunName}_SplitHost"
if ($VerifyCrowdCap -and ($LegacyReplication -or $Role -eq 'Generator' -or $VerifyCrowdCap -ge ($Clients - 1))) {
    throw 'VerifyCrowdCap requires Iris, the server or verifier role, and more remote peers than the cap.'
}

function Test-PrivateAddress([string]$Address) {
    $parsed = $null
    return [Net.IPAddress]::TryParse($Address, [ref]$parsed) -and $parsed.AddressFamily -eq 'InterNetwork' -and
        $parsed.ToString() -ceq $Address -and ($Address -eq '127.0.0.1' -or
            $Address -match '^(10\.|172\.(1[6-9]|2\d|3[01])\.|192\.168\.)')
}

function Read-Marker([string]$Log, [string]$Pattern) {
    return Select-String -LiteralPath $Log -Pattern $Pattern | Select-Object -Last 1
}

function Measure-Percentiles([double[]]$Values) {
    if (!$Values.Length) { return $null }
    [Array]::Sort($Values)
    return [ordered]@{
        samples = $Values.Length
        p95 = [Math]::Round($Values[[int][Math]::Ceiling($Values.Length * 0.95) - 1], 3)
        p99 = [Math]::Round($Values[[int][Math]::Ceiling($Values.Length * 0.99) - 1], 3)
        max = [Math]::Round($Values[-1], 3)
    }
}

function Verify-Cohort([string]$ServerDir, [string]$GeneratorDir) {
    $server = Get-Content -LiteralPath "$ServerDir/role.json" -Raw | ConvertFrom-Json
    $generator = Get-Content -LiteralPath "$GeneratorDir/role.json" -Raw | ConvertFrom-Json
    $serverLog = Join-Path $ServerDir 'Server.log'
    $generatorLog = Join-Path $GeneratorDir 'Generator.log'
    $boundServer = Read-Marker $serverLog ('COHORT_SPLIT_HOST role=server local={0} peer={1} run={2}\s*$' -f
        [regex]::Escape($server.localAddress), [regex]::Escape($server.peerAddress), [regex]::Escape($RunName))
    $boundGenerator = Read-Marker $generatorLog ('COHORT_SPLIT_HOST role=generator local={0} peer={1} run={2}\s*$' -f
        [regex]::Escape($generator.localAddress), [regex]::Escape($generator.peerAddress), [regex]::Escape($RunName))
    $matchedRoles = $server.role -ceq 'Server' -and $generator.role -ceq 'Generator' -and
        $server.run -ceq $RunName -and $generator.run -ceq $RunName -and $server.requested -eq $Clients -and $generator.requested -eq $Clients -and
        $server.port -eq $generator.port -and $server.localAddress -eq $generator.peerAddress -and $server.peerAddress -eq $generator.localAddress -and
        $server.replication -ceq $generator.replication -and $server.fixtureSha -ceq $generator.fixtureSha -and
        $server.editorSha -ceq $generator.editorSha -and $server.arenaSha -ceq $generator.arenaSha -and $server.configSha -ceq $generator.configSha -and
        $boundServer -and $boundGenerator
    $finished = Read-Marker $serverLog 'COHORT_FINISHED passed=1 peak=(\d+) strikes=(\d+) clients=(\d+)\s*$'
    $closed = Read-Marker $serverLog 'COHORT_CLOSED confirmed=(\d+) released=(\d+) closed=(\d+) batch=(\d+)\s*$'
    $measured = Read-Marker $serverLog 'COHORT_MEASURED accepted=(\d+) expected=(\d+) violations=(\d+) net_tick_rate=(\d+) samples=(\d+) boss_health=(-?\d+)\s*$'
    $ready = Read-Marker $serverLog "COHORT_BOSS pawns=$Clients ready=$Clients owner_only_peers=0 confirmed_damage=([\d.]+)\s*$"
    $aim = Read-Marker $serverLog "COHORT_BOSS_AIM accepted=$Clients expected=$Clients\s*$"
    $arena = Read-Marker $serverLog 'COHORT_FIELD_ARENA encounter=Sandbox_FieldBoss_01(\s|$)'
    $generatorFinished = Read-Marker $generatorLog "COHORT_FINISHED passed=1 peak=0 strikes=0 clients=$Clients\s*$"
    $iris = [bool](Read-Marker $serverLog 'LogIris: Display: Iris ReplicationSystem\[\d+\].* is created')
    $replication = if ($iris) { 'Iris' } else { 'Generic' }
    $requestedCrowdCap = [int]$server.requestedCrowdCap
    $crowdSnapshot = $null
    $crowdVerified = !$requestedCrowdCap
    if ($requestedCrowdCap) {
        $crowdMarker = Read-Marker $serverLog 'COHORT_SPLIT_CROWD ccu=(\d+) tracked=(\d+) effective_cap=(\d+) allowed=(\d+) culled=(\d+)\s*$'
        if ($crowdMarker) {
            $c = $crowdMarker.Matches[0].Groups
            $crowdSnapshot = [ordered]@{ ccu = [int]$c[1].Value; trackedObjects = [int]$c[2].Value; effectiveCap = [int]$c[3].Value
                allowedPairs = [long]$c[4].Value; culledPairs = [long]$c[5].Value }
            $crowdVerified = $iris -and $crowdSnapshot.ccu -eq $Clients -and $crowdSnapshot.trackedObjects -ge $Clients -and
                $crowdSnapshot.effectiveCap -gt 0 -and $crowdSnapshot.effectiveCap -le $requestedCrowdCap -and
                $crowdSnapshot.allowedPairs -gt 0 -and $crowdSnapshot.culledPairs -gt 0
        }
    }
    if ($VerifyCrowdCap -and $VerifyCrowdCap -ne $requestedCrowdCap) { $crowdVerified = $false }
    $metricsVerified = $false
    $burst = $null
    $ticksPath = Join-Path $ServerDir 'combat-ticks.csv'
    $strikesPath = Join-Path $ServerDir 'combat-strikes.csv'
    if ($measured -and $closed -and (Test-Path -LiteralPath $ticksPath) -and (Test-Path -LiteralPath $strikesPath)) {
        $ticks = @(Import-Csv -LiteralPath $ticksPath)
        $strikes = @(Import-Csv -LiteralPath $strikesPath)
        $burstTicks = @($ticks | Where-Object phase -CEQ Burst)
        $activeTicks = @($burstTicks | Where-Object { [int]$_.strikes -gt 0 })
        $reportedClosed = ($ticks | Where-Object phase -CEQ Close | Measure-Object -Property closed -Sum).Sum
        $clockSafe = @($strikes | Where-Object { $_.damage_replication_ms }).Count -eq 0
        $closure = $closed.Matches[0].Groups
        $metrics = $measured.Matches[0].Groups
        $metricsVerified = $ticks.Count -eq [int]$metrics[5].Value -and $strikes.Count -eq $Clients -and
            $metrics[3].Value -eq '0' -and $metrics[1].Value -eq $Clients -and $metrics[2].Value -eq $Clients -and
            ($burstTicks | Measure-Object -Property strikes -Sum).Sum -eq $Clients -and $reportedClosed -eq $Clients -and
            $closure[1].Value -eq $Clients -and $closure[2].Value -eq $Clients -and $closure[3].Value -eq $Clients -and $clockSafe
        $burst = [ordered]@{
            tickIntervalMs = Measure-Percentiles @($burstTicks | ForEach-Object { [double]$_.tick_interval_ms })
            worldTickMs = Measure-Percentiles @($burstTicks | ForEach-Object { [double]$_.world_tick_ms })
            strikeTickMs = Measure-Percentiles @($activeTicks | ForEach-Object { [double]$_.world_tick_ms })
            serverDispatchToConfirmedHitMs = Measure-Percentiles @($strikes | ForEach-Object { [double]$_.round_trip_ms })
            serverDispatchToServerHitWindowMs = Measure-Percentiles @($strikes | Where-Object { [double]$_.attack_to_hit_window_ms -ge 0 } | ForEach-Object { [double]$_.attack_to_hit_window_ms })
            serverHitWindowToConfirmedHitMs = Measure-Percentiles @($strikes | Where-Object { [double]$_.server_authority_hit_ms -ge 0 } | ForEach-Object { [double]$_.server_authority_hit_ms })
            damageReplicationMs = $null
        }
    }
    $passed = $matchedRoles -and $server.passed -and $generator.passed -and $server.processExit -eq 0 -and $generator.processExit -eq 0 -and
        $finished -and $generatorFinished -and $ready -and $aim -and
        $arena -and $replication -ceq $server.replication -and $metricsVerified -and $crowdVerified -and
        [int]$finished.Matches[0].Groups[1].Value -eq $Clients -and [int]$finished.Matches[0].Groups[2].Value -eq $Clients
    if ($MaxBurstTickIntervalP95Ms) {
        $passed = $passed -and $null -ne $burst.tickIntervalMs -and $burst.tickIntervalMs.p95 -le $MaxBurstTickIntervalP95Ms
    }
    [ordered]@{
        passed = [bool]$passed; matchedRoles = [bool]$matchedRoles; metricsVerified = [bool]$metricsVerified
        scope = 'Dev-only field arena with the authored boss class, one measured client-owned melee attack per participant'
        clock = 'Server-only monotonic timing. Dispatch time includes client transport and ability playback. Cross-host one-way replication delay is unmeasured.'
        requested = $Clients; replication = $replication; arenaSpawnVerified = [bool]$arena
        requestedCrowdCap = if ($requestedCrowdCap) { $requestedCrowdCap } else { $null }
        observedCrowdMetrics = $crowdSnapshot
        crowdCapVerified = if ($requestedCrowdCap -or $VerifyCrowdCap) { [bool]$crowdVerified } else { $null }
        separateAddresses = $server.localAddress -ne $generator.localAddress
        maxBurstTickIntervalP95Ms = if ($MaxBurstTickIntervalP95Ms) { $MaxBurstTickIntervalP95Ms } else { $null }
        burst = $burst; server = $server; generator = $generator
    } | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath "$report/verification.json" -Encoding utf8
    Get-Content -LiteralPath "$report/verification.json" -Raw
    if (!$passed) { exit 1 }
}

if ($Role -eq 'Verify') {
    if (!$GeneratorReportDirectory) { $GeneratorReportDirectory = Join-Path $report 'Generator' }
    Verify-Cohort (Join-Path $report 'Server') $GeneratorReportDirectory
    exit 0
}

if ($Role -eq 'LocalSmoke') {
    if ($LocalAddress -or $PeerAddress) { throw 'LocalSmoke uses the exact loopback addresses.' }
    $LocalAddress = $PeerAddress = '127.0.0.1'
}
if (!(Test-PrivateAddress $LocalAddress) -or !(Test-PrivateAddress $PeerAddress) -or
    (($LocalAddress -eq '127.0.0.1') -ne ($PeerAddress -eq '127.0.0.1'))) {
    throw 'Provide two literal private IPv4 addresses, or the exact loopback pair for a local smoke.'
}
if ($LocalAddress -ne '127.0.0.1' -and !(Get-NetIPAddress -AddressFamily IPv4 -IPAddress $LocalAddress -ErrorAction SilentlyContinue)) {
    throw "The requested local address $LocalAddress is not assigned on this host."
}
$busy = @(Get-CimInstance Win32_Process | Where-Object Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|Uba).*')
if ($busy.Count) { throw 'An engine or build process is still active. Use LocalSmoke to start both local roles together.' }
if ($Role -in @('Server', 'LocalSmoke') -and (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue)) {
    throw 'The requested server port is already bound locally.'
}

. "$root/Scripts/Audit/Continuation-ScratchPaths.ps1"
$scratch = (New-ContinuationScratchRun (Get-ContinuationScratchContext $root) "${RunName}_SplitHost_$Role").Root
New-Item -ItemType Directory -Force -Path $report | Out-Null
$fixtureSha = (Get-FileHash -LiteralPath "$root/Binaries/Win64/UnrealEditor-Project_J.dll" -Algorithm SHA256).Hash
$editorSha = (Get-FileHash -LiteralPath $UnrealEditor -Algorithm SHA256).Hash
$arenaSha = (Get-FileHash -LiteralPath "$root/Content/Maps/Test_MMO_Sandbox.umap" -Algorithm SHA256).Hash
$configSha = (Get-FileHash -LiteralPath "$root/Config/DefaultEngine.ini" -Algorithm SHA256).Hash
$replication = if ($LegacyReplication) { 'Generic' } else { 'Iris' }
$ownedProcesses = [Collections.Generic.List[object]]::new()

function Start-CohortProcess([string]$ProcessRole) {
    $dir = Join-Path $report $ProcessRole
    New-Item -ItemType Directory -Path $dir | Out-Null
    $log = Join-Path $dir "$ProcessRole.log"
    $launch = @("$root/Project_J.uproject", '-unattended', '-nosound', '-nop4', '-NullRHI', '-NoLiveCoding', '-NoDDCCleanup',
        '-DisablePlugins=EditorToolset,AutomationTestToolset,ToolsetRegistry', "-multihome=$LocalAddress",
        '-ProjectJCohortSplitHost', "-ProjectJCohortPeerIpv4=$PeerAddress", "-ProjectJCohortRunId=$RunName",
        "-ProjectJCohort=$Clients", "-UserDir=$scratch/$ProcessRole", "-ABSLOG=$log")
    if ($LegacyReplication) { $launch += '-UseIrisReplication=0' }
    if ($HeadlessCoreLimit) { $launch += "-corelimit=$HeadlessCoreLimit" }
    if ($MemoryReserveMiB) { $launch += "-ProjectJCohortMemoryReserveMiB=$MemoryReserveMiB" }
    if ($ProcessRole -eq 'Server') {
        $launch += @('/Game/Maps/Test_MMO_Sandbox?game=/Script/Project_J.Project_JLoopbackCohort', '-server', '-DisablePython', "-port=$Port",
            '-ProjectJCohortBossCombat', '-ProjectJCohortFieldArena', "-ProjectJCohortCombatMetrics=$dir", "-ProjectJCohortCloseBatchSize=$CloseBatchSize")
        if ($UsePerfThreads) { $launch += '-useperfthreads' }
        if ($VerifyCrowdCap) { $launch += @('-ProjectJCohortCrowdGroups', "-ExecCmds=ProjectJ.Net.MaxVisibleRemoteActors $VerifyCrowdCap") }
        if ($ServerTrace) { $launch += @('-trace=cpu,frame,bookmark', '-statnamedevents', "-tracefile=$dir/Server.utrace") }
    } else {
        $launch += @("-ExecutePythonScript=$root/Scripts/Validation/Start-LoopbackCohort.py", "-ProjectJCohortEndpoint=${PeerAddress}:$Port",
            '-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameDefaultMap=/Engine/Maps/Entry', '-ProjectJCohortMeasuredClient', '-ProjectJCohortBossClient')
        if ($ThinClient) { $launch += '-ExecCmds=p.ClothPhysics 0,ProjectJ.ClothBudget.Enabled 0,a.URO.Enable 1,a.URO.ForceInterpolation 1,DDCvar.FootPlacementMode 0' }
    }
    $proc = Start-Process -FilePath $UnrealEditor -ArgumentList ($launch | ForEach-Object { '"' + $_ + '"' }) -PassThru -WindowStyle Hidden
    $null = $proc.Handle
    $ownedProcesses.Add($proc)
    return [pscustomobject]@{ Role = $ProcessRole; Directory = $dir; Log = $log; Process = $proc; Stopwatch = [Diagnostics.Stopwatch]::StartNew() }
}

function Complete-CohortProcess($run) {
    while (!$run.Process.HasExited -and $run.Stopwatch.Elapsed.TotalSeconds -lt 450) { Start-Sleep -Milliseconds 500 }
    if (!$run.Process.HasExited) { throw "$($run.Role) exceeded its process deadline. See $($run.Log)." }
    $run.Process.WaitForExit()
    $rolePattern = $run.Role.ToLowerInvariant()
    $bound = [bool](Read-Marker $run.Log ('COHORT_SPLIT_HOST role={0} local={1} peer={2} run={3}\s*$' -f
        $rolePattern, [regex]::Escape($LocalAddress), [regex]::Escape($PeerAddress), [regex]::Escape($RunName)))
    $pattern = if ($run.Role -eq 'Server') { "COHORT_FINISHED passed=1 peak=$Clients strikes=$Clients clients=\d+\s*$" }
        else { "COHORT_FINISHED passed=1 peak=0 strikes=0 clients=$Clients\s*$" }
    $passed = $run.Process.ExitCode -eq 0 -and $bound -and [bool](Read-Marker $run.Log $pattern)
    [ordered]@{
        passed = [bool]$passed; processExit = $run.Process.ExitCode; role = $run.Role; run = $RunName; requested = $Clients
        localAddress = $LocalAddress; peerAddress = $PeerAddress; port = $Port; replication = $replication
        machine = [Environment]::MachineName; cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name
        fixtureSha = $fixtureSha; editorSha = $editorSha; arenaSha = $arenaSha; configSha = $configSha
        hostElapsedSeconds = [Math]::Round($run.Stopwatch.Elapsed.TotalSeconds, 3)
        requestedCrowdCap = if ($run.Role -eq 'Server' -and $VerifyCrowdCap) { $VerifyCrowdCap } else { $null }
        usePerfThreads = [bool]$UsePerfThreads; thinClient = [bool]$ThinClient
    } | ConvertTo-Json | Set-Content -LiteralPath "$($run.Directory)/role.json" -Encoding utf8
    Get-Content -LiteralPath "$($run.Directory)/role.json" -Raw | Write-Host
    return [bool]$passed
}

try {
    if ($Role -in @('Server', 'LocalSmoke')) {
        $serverRun = Start-CohortProcess Server
        $serverReady = $false
        while (!$serverRun.Process.HasExited -and $serverRun.Stopwatch.Elapsed.TotalSeconds -lt 60) {
            if ((Test-Path -LiteralPath $serverRun.Log) -and (Read-Marker $serverRun.Log "COHORT_READY count=$Clients capacity=\d+\s*$")) {
                $serverReady = $true
                Write-Host "Ready for the allowlisted generator at ${LocalAddress}:$Port. Run $RunName."
                break
            }
            Start-Sleep -Milliseconds 200
        }
        if (!$serverReady -and !$serverRun.Process.HasExited) { throw "Server never became ready. See $($serverRun.Log)." }
    }
    if ($Role -eq 'Generator' -or ($Role -eq 'LocalSmoke' -and $serverReady)) { $generatorRun = Start-CohortProcess Generator }
    $ok = $true
    if ($serverRun) { $ok = Complete-CohortProcess $serverRun }
    if ($generatorRun) { $generatorOk = Complete-CohortProcess $generatorRun; $ok = $ok -and $generatorOk }
    if ($Role -eq 'LocalSmoke' -and $generatorRun) { Verify-Cohort $serverRun.Directory $generatorRun.Directory }
    if (!$ok) { exit 1 }
} finally {
    foreach ($proc in $ownedProcesses) {
        if (!$proc.HasExited) { Stop-Process -InputObject $proc -ErrorAction SilentlyContinue }
    }
}
