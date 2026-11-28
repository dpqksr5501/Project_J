param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName,
    [ValidateRange(1,200)][int]$Clients = 200,
    [switch]$LegacyReplication,
    [switch]$MeasureCombat,
    [switch]$BossCombat,
    [switch]$StableBossLayout,
    [switch]$BossHitDiagnostics,
    [switch]$GroundWarningProbe,
    [switch]$GroundPulse,
    [switch]$LateGroundWarning,
    [switch]$DungeonCombat,
    [switch]$FieldArena,
    [ValidatePattern('^/Game/(?:[A-Za-z0-9_-]+/)*[A-Za-z0-9_-]+$')][string]$EncounterMap,
    [ValidatePattern('^[A-Za-z0-9_.-]{1,128}$')][string]$EncounterId,
    [switch]$RenderedClient,
    [ValidateRange(0,4)][int]$RenderedEffectsQuality,
    [switch]$OwnerOnlyPeers,
    [switch]$SplitGenerators,
    [switch]$ServerTrace,
    [ValidateSet('Observe','EngineDefaults','Optimized')][string]$ServerTickScheduler,
    [switch]$ServerWave2Audit,
    [switch]$RenderedTrace,
    [ValidateSet('Off','SerialChaos','LargerClothBatches','ReducedRemote','FixedClothIterations','ParkOptInRemote')][string]$RenderedClothProbe,
    [ValidateRange(2,64)][int]$HeadlessCoreLimit,
    [ValidateRange(0,240)][int]$HeadlessFrameCap = 0,
    [ValidateRange(0,65536)][int]$MemoryReserveMiB=0,
    [switch]$ThinClient,
    [switch]$UsePerfThreads,
    [long]$ServerAffinityMask = 0,
    [long]$GeneratorAffinityMask = 0,
    [long]$RenderedAffinityMask = 0,
    [ValidateRange(0,1000000)][int]$ClothVertexCap,
    [ValidateSet(0,10,15,20,30)][int]$CloakHz=0,
    [switch]$VerifyRemoteCloak,
    [ValidateRange(1,200)][int]$CloseBatchSize = 200,
    [ValidateRange(0,65535)][int]$ServerMetricsPort = 0,
    [ValidateRange(1,199)][int]$VerifyCrowdCap,
    [ValidateRange(1,1000)][double]$MaxBurstTickIntervalP95Ms,
    [ValidateRange(1025,65535)][int]$Port = 17976,
    [string]$UnrealEditor = 'C:/Program Files/Epic Games/UE_5.9/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
)
$ErrorActionPreference = 'Stop'
if ($SplitGenerators -and (!$MeasureCombat -or $Clients -lt 2)) { throw 'Split measurement requires at least two combat clients.' }
if ($PSBoundParameters.ContainsKey('CloseBatchSize') -and !$MeasureCombat) { throw 'CloseBatchSize requires MeasureCombat.' }
if ($BossCombat -and !$MeasureCombat) { throw 'The authored boss workload requires MeasureCombat.' }
if ($MaxBurstTickIntervalP95Ms -and !$MeasureCombat) { throw 'A tick interval budget requires MeasureCombat.' }
if ($VerifyCrowdCap -and (!$BossCombat -or $LegacyReplication -or !$ServerMetricsPort -or
    $RenderedClient -or $VerifyCrowdCap -ge ($Clients - 1))) {
    throw 'VerifyCrowdCap requires an Iris headless boss run, a metrics port, and more remote peers than the cap.'
}
if ($StableBossLayout -and (!$BossCombat -or $Clients -lt 32 -or $Clients -gt 64)) { throw 'StableBossLayout requires the 32–64-client authored boss workload.' }
if ($BossHitDiagnostics -and !$BossCombat) { throw 'BossHitDiagnostics requires the authored boss workload.' }
if ($GroundWarningProbe -and (!$RenderedClient -or !$BossCombat)) { throw 'GroundWarningProbe requires a rendered boss cohort.' }
if ($GroundPulse -and !$BossCombat) { throw 'GroundPulse requires a boss combat run.' }
if ($PSBoundParameters.ContainsKey('RenderedEffectsQuality') -and !$RenderedClient) { throw 'RenderedEffectsQuality requires a rendered client.' }
if ($LateGroundWarning -and (!$GroundWarningProbe -or $LegacyReplication)) { throw 'LateGroundWarning requires GroundWarningProbe with Iris.' }
if (($DungeonCombat -or $FieldArena) -and !$BossCombat) { throw 'A placed arena requires the authored BossCombat workload.' }
if ($DungeonCombat -and $FieldArena) { throw 'Choose one placed arena.' }
if ([bool]$EncounterMap -ne [bool]$EncounterId -or ($EncounterMap -and (!$BossCombat -or $DungeonCombat -or $FieldArena))) {
    throw 'An explicit placed encounter requires BossCombat, EncounterMap and EncounterId.'
}
if ($EncounterMap -and $SplitGenerators) { throw 'A placed encounter uses independent game client processes.' }
if ($OwnerOnlyPeers -and (!$BossCombat -or !$LegacyReplication)) { throw 'OwnerOnlyPeers requires Generic replication and the authored boss workload.' }
if ($RenderedClient -and (!$BossCombat -or $Clients -lt 2 -or $OwnerOnlyPeers)) { throw 'RenderedClient requires at least two boss owners with peer visibility.' }
$clothExperiment = $PSBoundParameters.ContainsKey('ClothVertexCap')
if ($VerifyRemoteCloak -and (!$RenderedClient -or $CloakHz -or $RenderedClothProbe -or $clothExperiment)) {
    throw 'VerifyRemoteCloak requires a rendered client with the default cloth settings.'
}
if ($CloakHz -and (!$RenderedClient -or $RenderedClothProbe -or $clothExperiment)) { throw 'CloakHz requires a separate rendered client with ordinary cloth quality.' }
if ($clothExperiment -and !$RenderedClient) { throw 'ClothVertexCap requires RenderedClient.' }
if ($ServerTrace -and !$MeasureCombat) { throw 'ServerTrace requires MeasureCombat.' }
if ($RenderedTrace -and !$RenderedClient) { throw 'RenderedTrace requires RenderedClient.' }
if ($RenderedClothProbe -and !$RenderedClient) { throw 'RenderedClothProbe requires RenderedClient.' }
if ($RenderedClothProbe -eq 'Off' -and $clothExperiment) { throw 'The cloth-off diagnostic cannot use an execution cap.' }
if ($RenderedClothProbe -eq 'ParkOptInRemote' -and $ClothVertexCap -gt 0) { throw 'The optional parking rollback probe requires a zero vertex cap.' }
$busy = @(Get-CimInstance Win32_Process | Where-Object Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|Uba).*')
if ($busy.Count) { throw 'An engine or build process is still active.' }
if (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) { throw 'The loopback port is already in use.' }
if ($ServerMetricsPort -and (Get-NetTCPConnection -State Listen -LocalPort $ServerMetricsPort -ErrorAction SilentlyContinue)) {
    throw 'The loopback metrics port is already in use.'
}
$headlessCount = $Clients - [int][bool]$RenderedClient
if ($RenderedClient -or $EncounterMap) {
    if (!$MemoryReserveMiB) { $MemoryReserveMiB = 4096 }
    $memory = Get-CimInstance Win32_OperatingSystem
    $freeMiB = [Math]::Min($memory.FreePhysicalMemory, $memory.FreeVirtualMemory) / 1KB
    $requiredMiB = $MemoryReserveMiB + 8192
    if ($EncounterMap) {
        # Uncooked WP clients cannot share editor world contexts with the dedicated server.
        # Budget separately for each complete NullRHI game process.
        $requiredMiB = $MemoryReserveMiB + 6144 + (4096 * $headlessCount)
        if ($RenderedClient) { $requiredMiB += 10240 }
    }
    if ($freeMiB -lt $requiredMiB) {
        throw "Insufficient memory to start the cohort. Available: $([int]$freeMiB) MiB, planned: $requiredMiB MiB."
    }
}
$root = (Resolve-Path "$PSScriptRoot/../..").Path
. "$root/Scripts/Audit/Continuation-ScratchPaths.ps1"
$scratch = (New-ContinuationScratchRun (Get-ContinuationScratchContext $root) $RunName).Root
$report = Join-Path $root "Saved/Validation/Continuation_20261014/$RunName"
New-Item -ItemType Directory -Path $report | Out-Null
$map = '/Engine/Maps/Entry?game=/Script/Project_J.Project_JLoopbackCohort'
if ($BossCombat) { $map = '/Game/Tests/Combat/L_CanonicalNetwork?game=/Script/Project_J.Project_JLoopbackCohort' }
if ($DungeonCombat) { $map = '/Game/Maps/Test_MMO_Dungeon?game=/Script/Project_J.Project_JLoopbackCohort' }
if ($FieldArena) { $map = '/Game/Maps/Test_MMO_Sandbox?game=/Script/Project_J.Project_JLoopbackCohort' }
if ($EncounterMap) { $map = "$EncounterMap" + '?game=/Script/Project_J.Project_JLoopbackCohort' }
$common = @("$root/Project_J.uproject", '-unattended', '-nosound', '-nop4', '-NullRHI', '-NoLiveCoding', '-NoDDCCleanup',
    '-DisablePlugins=EditorToolset,AutomationTestToolset,ToolsetRegistry', '-multihome=127.0.0.1')
if ($LegacyReplication) { $common += '-UseIrisReplication=0' }
if ($MemoryReserveMiB) { $common += "-ProjectJCohortMemoryReserveMiB=$MemoryReserveMiB" }
if ($EncounterId) { $common += "-ProjectJCohortEncounterId=$EncounterId" }
if ($GroundWarningProbe) { $common += '-ProjectJGroundWarningProbe' }
if ($GroundPulse) { $common += '-ProjectJCohortGroundPulse' }
if ($LateGroundWarning) { $common += '-ProjectJLateGroundWarning' }

$appliedAffinity = [ordered]@{}
function Start-CohortRole([string]$Role, [int]$Count, [string[]]$RoleArgs, [bool]$Render = $false, [long]$AffinityMask = 0) {
    $all = @($common | Where-Object { !$Render -or $_ -ne '-NullRHI' }) +
        @("-ProjectJCohort=$Count", "-UserDir=$scratch/$Role", "-ABSLOG=$report/$Role.log") + $RoleArgs
    if (!$Render -and $HeadlessCoreLimit) { $all += "-corelimit=$HeadlessCoreLimit" }
    $process = Start-Process -FilePath $UnrealEditor -ArgumentList ($all | ForEach-Object { '"' + $_ + '"' }) -PassThru -WindowStyle Hidden
    $null = $process.Handle
    if ($AffinityMask -gt 0) {
        try {
            $process.ProcessorAffinity = [IntPtr]$AffinityMask
            $process.Refresh()
            $appliedAffinity[$Role] = $process.ProcessorAffinity.ToInt64()
        } catch {
            $appliedAffinity[$Role] = 0
            Write-Warning "Could not apply CPU affinity to $Role."
        }
    }
    return $process
}

$serverArgs = @($map, '-server', '-DisablePython', "-port=$Port")
if ($UsePerfThreads) { $serverArgs += '-useperfthreads' }
if ($ServerMetricsPort) { $serverArgs += "-ProjectJMetricsPort=$ServerMetricsPort" }
if ($MeasureCombat) { $serverArgs += @("-ProjectJCohortCombatMetrics=$report", "-ProjectJCohortCloseBatchSize=$CloseBatchSize") }
if ($BossCombat) { $serverArgs += '-ProjectJCohortBossCombat' }
if ($StableBossLayout) { $serverArgs += '-ProjectJCohortStableBossLayout' }
$abilityDiagnostics = 'LogAbilitySystem VeryVerbose,LogProjectJCombatCombo Verbose'
if ($BossHitDiagnostics) {
    $serverArgs += @('-ProjectJCohortBossHitDiagnostics', "-LogCmds=$abilityDiagnostics,LogProjectJCombatHitValidation VeryVerbose,LogProjectJAuthoredMelee VeryVerbose,LogProjectJGroundAttack Verbose")
}
if ($DungeonCombat) { $serverArgs += '-ProjectJCohortDungeonCombat' }
if ($FieldArena) { $serverArgs += '-ProjectJCohortFieldArena' }
if ($OwnerOnlyPeers) { $serverArgs += '-ProjectJCohortOwnerOnlyPeers' }
if ($ServerTrace) { $serverArgs += @('-trace=cpu,frame,bookmark', '-statnamedevents', "-tracefile=$report/Server.utrace") }
$serverExec = @()
if ($BossHitDiagnostics) { $serverExec += 'Project_J.Combat.ComboDebug 1' }
if ($VerifyCrowdCap) {
    $serverArgs += '-ProjectJCohortCrowdGroups'
    $serverExec += "ProjectJ.Net.MaxVisibleRemoteActors $VerifyCrowdCap"
}
if ($ServerTickScheduler) {
    $knobs = @('tick.AllowBatchedTicks', 'tick.AllowAsyncTickDispatch', 'tick.AllowAsyncTickCleanup')
    $commands = if ($ServerTickScheduler -eq 'Observe') { @() } else {
        $value = [int]($ServerTickScheduler -eq 'Optimized')
        @($knobs | ForEach-Object { "$_ $value" })
    }
    $commands = @($commands) + $knobs + @('tick.AllowAsyncComponentTicks', 'tick.AllowConcurrentTickQueue', 'tick.AllowBatchedTicksUnordered')
    $serverExec += $commands
}
if ($ServerWave2Audit) {
    $serverExec += @('net.Iris.UseIrisReplication', 'net.MaxRPCPerNetUpdate', 'r.PSOPrecaching',
        'a.Budget.Enabled', 'DDCvar.FootPlacementMode', 'wp.Runtime.BlockOnSlowStreaming',
        'getall Project_JIpNetDriver NetServerMaxTickRate', 'getall Project_JIpNetDriver MaxClientRate',
        'getall GameNetworkManager TotalNetBandwidth')
    if ($BossHitDiagnostics) { throw 'Use a separate run for hit diagnostics and Wave 2 Iris audit.' }
    $serverArgs += '-LogCmds=LogProjectJIrisCombatPriority Verbose'
}
if ($serverExec.Count) { $serverArgs += "-ExecCmds=$($serverExec -join ',')" }
$server = Start-CohortRole Server $Clients $serverArgs $false $ServerAffinityMask
$deadline = [DateTime]::UtcNow.AddSeconds(45)
while (!(Test-Path "$report/Server.log") -or !(Select-String -LiteralPath "$report/Server.log" -SimpleMatch 'COHORT_READY' -Quiet)) {
    if (Test-Path -LiteralPath "$report/Server.log") {
        $capacityRejection = Select-String -LiteralPath "$report/Server.log" -Pattern 'COHORT_CAPACITY_REJECTED requested=(\d+) capacity=(\d+)' | Select-Object -Last 1
        if ($capacityRejection) {
            $server.WaitForExit()
            [ordered]@{
                passed = $false; capacityRejected = $true; generatorsStarted = 0
                requested = [int]$capacityRejection.Matches[0].Groups[1].Value
                driverCapacity = [int]$capacityRejection.Matches[0].Groups[2].Value
                serverExit = $server.ExitCode
            } | ConvertTo-Json | Set-Content -LiteralPath "$report/verification.json" -Encoding utf8
            Get-Content -Raw -LiteralPath "$report/verification.json"
            exit 3
        }
    }
    if ($server.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw "Server failed to initialize. See $report/Server.log" }
    Start-Sleep -Milliseconds 200
}
$generatorArgs = @("-ExecutePythonScript=$root/Scripts/Validation/Start-LoopbackCohort.py",
    "-ProjectJCohortEndpoint=127.0.0.1:$Port", '-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameDefaultMap=/Engine/Maps/Entry')
if ($MeasureCombat) { $generatorArgs += '-ProjectJCohortMeasuredClient' }
if ($BossCombat) { $generatorArgs += '-ProjectJCohortBossClient' }
if ($OwnerOnlyPeers) { $generatorArgs += '-ProjectJCohortOwnerOnlyPeers' }
$generatorExec = @()
if ($ThinClient) {
    # Strip purely cosmetic client work on headless load-generator bots to reduce single-PC CPU/RAM contention.
    $generatorExec += 'p.ClothPhysics 0,ProjectJ.ClothBudget.Enabled 0,a.URO.Enable 1,a.URO.ForceInterpolation 1,DDCvar.FootPlacementMode 0'
}
if ($BossHitDiagnostics) {
    $generatorArgs += "-LogCmds=$abilityDiagnostics"
    $generatorExec += 'Project_J.Combat.ComboDebug 1'
}
if ($HeadlessFrameCap) { $generatorExec += "t.MaxFPS $HeadlessFrameCap,t.MaxFPS" }
if ($generatorExec.Count) { $generatorArgs += "-ExecCmds=$($generatorExec -join ',')" }
$generatorCounts = [ordered]@{ Clients = $headlessCount }
if ($EncounterMap) {
    $generatorCounts = [ordered]@{}
    for ($owner = 1; $owner -le $headlessCount; ++$owner) {
        $generatorCounts["Client$owner"] = 1
    }
} elseif ($SplitGenerators -and $headlessCount -gt 1) {
    $firstCount = [int][Math]::Floor($headlessCount / 2)
    $generatorCounts = [ordered]@{ Clients = $firstCount; Clients2 = $headlessCount - $firstCount }
}
$processes = [ordered]@{ Server = $server }
$clothProbeSettings = @{
    Off = @{ Name = 'p.ClothPhysics'; Value = 0 }
    SerialChaos = @{ Name = 'p.Chaos.DisablePhysicsParallelFor'; Value = 1; Readback = 'true' }
    LargerClothBatches = @{ Name = 'p.ChaosCloth.Solver.MinParallelBatchSize'; Value = 8192 }
    ReducedRemote = @{ Name = 'ProjectJ.ClothBudget.MaxFullRemoteParts'; Value = 0 }
    FixedClothIterations = @{ Name = 'p.ChaosCloth.Solver.DisableTimeDependentNumIterations'; Value = 1; Readback = 'true' }
    ParkOptInRemote = @{ Name = 'ProjectJ.ClothBudget.PauseOptInRemote'; Value = 1 }
}
if ($RenderedClient) {
    New-Item -ItemType Directory -Path "$report/Rendered" | Out-Null
    $commands = 't.MaxFPS 0,r.VSync 0'
    if ($PSBoundParameters.ContainsKey('RenderedEffectsQuality')) {
        $commands += ",sg.EffectsQuality $RenderedEffectsQuality,sg.EffectsQuality"
    }
    $renderArgs = @("127.0.0.1:$Port", '-game', '-DisablePython', '-d3d12', '-RenderOffscreen', '-windowed',
        '-ForceRes', '-ResX=1280', '-ResY=720', '-SaveToUserDir', '-Seconds=145', "-ProjectJCohortRenderedOutput=$report/Rendered")
    if ($BossHitDiagnostics) {
        $renderArgs += "-LogCmds=$abilityDiagnostics"
        $commands += ',Project_J.Combat.ComboDebug 1'
    }
    if ($clothExperiment) {
        $commands += ",ProjectJ.ClothBudget.ExecutionVertexCap $ClothVertexCap"
        $renderArgs += "-ProjectJCohortClothVertexCap=$ClothVertexCap"
    }
    if ($CloakHz) { $renderArgs += "-ProjectJCohortClothHz=$CloakHz" }
    if ($VerifyRemoteCloak) { $renderArgs += '-ProjectJCohortVerifyRemoteCloak' }
    if ($RenderedClothProbe) {
        $probe = $clothProbeSettings[$RenderedClothProbe]
        $commands += ",$($probe.Name) $($probe.Value),$($probe.Name)"
    }
    if ($RenderedClothProbe -eq 'ParkOptInRemote') {
        $commands += ',ProjectJ.ClothBudget.ExecutionVertexCap 0,ProjectJ.ClothBudget.ObserveExecution 1'
        $renderArgs += '-ProjectJCohortVerifyOptionalClothRestore'
    }
    if ($RenderedTrace) { $renderArgs += @('-trace=cpu,frame,bookmark', '-statnamedevents', "-tracefile=$report/Rendered.utrace") }
    $renderArgs += "-ExecCmds=$commands"
    $processes.RenderedClient = Start-CohortRole RenderedClient $Clients $renderArgs $true $RenderedAffinityMask
}
foreach ($entry in $generatorCounts.GetEnumerator()) {
    $clientArgs = $generatorArgs
    if ($EncounterMap) {
        $clientArgs = @("127.0.0.1:$Port", '-game', '-DisablePython', '-Seconds=150', '-ProjectJCohortStandaloneClient')
        $clientArgs += @($generatorArgs | Where-Object { $_.StartsWith('-ExecCmds=') -or $_.StartsWith('-LogCmds=') })
    }
    $processes[$entry.Key] = Start-CohortRole $entry.Key $entry.Value $clientArgs $false $GeneratorAffinityMask
}
Write-Output "Started one server and $($processes.Count - 1) client host(s) for $Clients game connections. $report"
$hostSamples = [Collections.Generic.List[object]]::new()
$metricsSnapshot = $null
$crowdSample = $null
if ($MeasureCombat) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while (@($processes.Values | Where-Object { !$_.HasExited }).Count) {
        $sample = [ordered]@{ seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 3) }
        foreach ($role in $processes.GetEnumerator()) {
            try {
                $role.Value.Refresh()
                if (!$role.Value.HasExited) {
                    $sample["$($role.Key)CpuSeconds"] = $role.Value.TotalProcessorTime.TotalSeconds
                    $sample["$($role.Key)WorkingMiB"] = [Math]::Round($role.Value.WorkingSet64 / 1MB, 1)
                    $sample["$($role.Key)PrivateMiB"] = [Math]::Round($role.Value.PrivateMemorySize64 / 1MB, 1)
                }
            } catch [InvalidOperationException] { }
        }
        $hostSamples.Add($sample)
        if ($ServerMetricsPort -and !$metricsSnapshot -and !$server.HasExited -and
            (Select-String -LiteralPath "$report/Server.log" -SimpleMatch 'COHORT_BOSS_AIM' -Quiet)) {
            try {
                $body = (Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$ServerMetricsPort/metrics" -TimeoutSec 1).Content
                $gauges = @{}
                foreach ($m in [regex]::Matches([string]$body, '(?m)^projectj_(iris_crowd_\w+|server_active_ccu) (\d+)\r?$')) {
                    $gauges[$m.Groups[1].Value] = [long]$m.Groups[2].Value
                }
                $capObserved = $gauges.server_active_ccu -eq $Clients -and $gauges.iris_crowd_tracked_objects -ge $Clients -and
                    $gauges.iris_crowd_effective_cap -gt 0 -and $gauges.iris_crowd_effective_cap -le $VerifyCrowdCap -and
                    $gauges.iris_crowd_allowed_pairs -gt 0 -and $gauges.iris_crowd_culled_pairs -gt 0
                if ($gauges.iris_crowd_tracked_objects -gt 0 -and (!$VerifyCrowdCap -or $capObserved)) {
                    $crowdSample = $gauges
                    $metricsSnapshot = $body
                    Set-Content -LiteralPath "$report/server-metrics.prom" -Value $body -Encoding utf8NoBOM -NoNewline
                }
                elseif ($gauges.server_active_ccu -eq $Clients) {
                    Set-Content -LiteralPath "$report/server-metrics-last-full.prom" -Value $body -Encoding utf8NoBOM -NoNewline
                }
            } catch [System.Net.Http.HttpRequestException] {} catch [System.Threading.Tasks.TaskCanceledException] {}
        }
        Start-Sleep -Milliseconds 1000
    }
    ConvertTo-Json -InputObject $hostSamples.ToArray() | Set-Content -LiteralPath "$report/host-samples.json" -Encoding utf8
}
foreach ($process in $processes.Values) { $process.WaitForExit() }
$result = Select-String -LiteralPath "$report/Server.log" -Pattern 'COHORT_FINISHED passed=(\d+) peak=(\d+) strikes=(\d+) clients=(\d+)' | Select-Object -Last 1
$peak = if ($result) { [int]$result.Matches[0].Groups[2].Value } else { 0 }
$strikes = if ($result) { [int]$result.Matches[0].Groups[3].Value } else { 0 }
$serverVerified = $result -and $result.Matches[0].Groups[1].Value -eq '1'
$irisObserved = [bool](Select-String -LiteralPath "$report/Server.log" -Pattern 'LogIris: Display: Iris ReplicationSystem\[\d+\].* is created' -Quiet)
$replicationVerified = $irisObserved -eq !$LegacyReplication
$generators = @(foreach ($entry in $generatorCounts.GetEnumerator()) {
    $clientLog = "$report/$($entry.Key).log"
    $readyPattern = if ($EncounterMap) { 'COHORT_STANDALONE_CLIENT_FINISHED passed=1' } else {
        "COHORT_FINISHED passed=1 peak=0 strikes=0 clients=$($entry.Value)\s*$"
    }
    [ordered]@{
        role = $entry.Key; clients = $entry.Value; processExit = $processes[$entry.Key].ExitCode
        verified = (Test-Path -LiteralPath $clientLog) -and [bool](Select-String -LiteralPath $clientLog -Pattern $readyPattern -Quiet)
    }
})
$generatorVerified = @($generators | Where-Object { !$_.verified }).Count -eq 0
$generatorExit = ($generators | Where-Object { $_.processExit -ne 0 } | Select-Object -First 1).processExit
$expected = $Clients
function Measure-Distribution([double[]]$Values) {
    if (!$Values.Length) { return $null }
    [Array]::Sort($Values)
    $summary = [ordered]@{}
    foreach ($percent in @(50, 95, 99, 100)) {
        $index = [int][Math]::Ceiling($Values.Length * $percent / 100) - 1
        $summary["p$percent"] = [Math]::Round($Values[$index], 3)
    }
    return $summary
}
function Measure-StrikeSegment($Rows, [string]$Field) {
    $values = @($Rows | Where-Object { $_.$Field -and [double]$_.$Field -ge 0 } | ForEach-Object { [double]$_.$Field })
    if (!$values.Count) { return $null }
    $summary = Measure-Distribution $values
    $summary.max = $summary.p100
    $summary.samples = $values.Count
    return $summary
}
$combat = $null
$metricsVerified = !$MeasureCombat
if ($MeasureCombat) {
    $marker = Select-String -LiteralPath "$report/Server.log" -Pattern 'COHORT_MEASURED accepted=(\d+) expected=(\d+) violations=(\d+) net_tick_rate=(\d+) samples=(\d+) boss_health=(-?\d+)' | Select-Object -Last 1
    $closeMarker = Select-String -LiteralPath "$report/Server.log" -Pattern 'COHORT_CLOSED confirmed=(\d+) released=(\d+) closed=(\d+) batch=(\d+)' | Select-Object -Last 1
    $bossMarker = Select-String -LiteralPath "$report/Server.log" -Pattern 'COHORT_BOSS pawns=(\d+) ready=(\d+) owner_only_peers=([01]) confirmed_damage=([\d.]+)' | Select-Object -Last 1
    if ($marker -and (Test-Path -LiteralPath "$report/combat-ticks.csv") -and (Test-Path -LiteralPath "$report/combat-strikes.csv")) {
        $expected = [int]$marker.Matches[0].Groups[2].Value
        $violations = [int]$marker.Matches[0].Groups[3].Value
        $tickRate = [int]$marker.Matches[0].Groups[4].Value
        $bossHealth = [int]$marker.Matches[0].Groups[6].Value
        $ticks = @(Import-Csv -LiteralPath "$report/combat-ticks.csv")
        $receipts = @(Import-Csv -LiteralPath "$report/combat-strikes.csv")
        $phases = [ordered]@{}
        foreach ($phase in @('Idle', 'Burst', 'Sustained', 'Drain', 'Verify', 'Close')) {
            $samples = @($ticks | Where-Object phase -eq $phase)
            if (!$samples.Count) { continue }
            if ($phase -eq 'Idle') {
                $warmUpEnds = [double]$samples[0].seconds + 3
                $samples = @($samples | Where-Object { [double]$_.seconds -ge $warmUpEnds })
            }
            if (!$samples.Count) { continue }
            $intervals = @($samples | ForEach-Object { [double]$_.tick_interval_ms })
            $work = @($samples | ForEach-Object { [double]$_.world_tick_ms })
            $active = @($samples | Where-Object { [int]$_.strikes -gt 0 })
            $phaseReceipts = @($receipts | Where-Object phase -eq $phase)
            $latencies = @($phaseReceipts | ForEach-Object { [double]$_.round_trip_ms })
            $duration = ($intervals | Measure-Object -Sum).Sum / 1000
            $phases[$phase] = [ordered]@{
                samples = $samples.Count; seconds = [Math]::Round($duration, 3)
                tickIntervalMs = Measure-Distribution $intervals
                worldTickMs = Measure-Distribution $work
                engineIdleMs = Measure-Distribution @($samples | ForEach-Object { [double]$_.engine_idle_ms })
                engineIdleOvershootMs = Measure-Distribution @($samples | ForEach-Object { [double]$_.engine_idle_overshoot_ms })
                over33ms = @($work | Where-Object { $_ -gt (1000 / 30) }).Count
                acceptedStrikes = [int]($samples.strikes | Measure-Object -Sum).Sum
                peakStrikesPerTick = [int]($samples.strikes | Measure-Object -Maximum).Maximum
                closedConnections = [int]($samples.closed | Measure-Object -Sum).Sum
                peakClosedPerTick = [int]($samples.closed | Measure-Object -Maximum).Maximum
                strikeTickMs = Measure-Distribution @($active | ForEach-Object { [double]$_.world_tick_ms })
                strikeRoundTripMs = Measure-Distribution $latencies
                attackToHitWindowMs = Measure-StrikeSegment $phaseReceipts 'attack_to_hit_window_ms'
                serverAuthorityHitMs = Measure-StrikeSegment $phaseReceipts 'server_authority_hit_ms'
                damageReplicationMs = Measure-StrikeSegment $phaseReceipts 'damage_replication_ms'
                repliesOver500ms = @($latencies | Where-Object { $_ -gt 500 }).Count
                inKiBPerSec = [Math]::Round(($samples.in_bytes | Measure-Object -Sum).Sum / ($duration * 1KB), 2)
                outKiBPerSec = [Math]::Round(($samples.out_bytes | Measure-Object -Sum).Sum / ($duration * 1KB), 2)
            }
        }
        $close = if ($closeMarker) {
            $m = $closeMarker.Matches[0].Groups
            [ordered]@{ healthAcknowledged = [int]$m[1].Value; released = [int]$m[2].Value; closed = [int]$m[3].Value; batchSize = [int]$m[4].Value }
        }
        $boss = if ($bossMarker) {
            $m = $bossMarker.Matches[0].Groups
            [ordered]@{ ownedPawns = [int]$m[1].Value; readyOwners = [int]$m[2].Value; ownerOnlyPeers = $m[3].Value -eq '1'; confirmedDamage = [double]$m[4].Value }
        }
        $phaseCount = if ($BossCombat) { 5 } else { 6 }
        $metricsVerified = $violations -eq 0 -and $ticks.Count -eq [int]$marker.Matches[0].Groups[5].Value -and
            $receipts.Count -eq $expected -and $phases.Count -eq $phaseCount -and $phases.Burst.acceptedStrikes -eq $Clients -and
            $close.healthAcknowledged -eq $Clients -and $close.released -eq $Clients -and $close.closed -eq $Clients -and
            $close.batchSize -eq [Math]::Min($CloseBatchSize, $Clients) -and $phases.Close.closedConnections -eq $Clients -and
            $phases.Close.peakClosedPerTick -le $close.batchSize -and
            ($phases.Burst.acceptedStrikes + $phases.Sustained.acceptedStrikes + $phases.Drain.acceptedStrikes) -eq $expected
        if ($BossCombat) {
            $metricsVerified = $metricsVerified -and $boss.ownedPawns -eq $Clients -and $boss.readyOwners -eq $Clients -and
                $boss.ownerOnlyPeers -eq [bool]$OwnerOnlyPeers -and $boss.confirmedDamage -gt 0 -and $expected -eq $Clients
        }
        $steady = if ($BossCombat) { $phases.Drain } else { $phases.Sustained }
        $combat = [ordered]@{
            scope = if ($GroundPulse) { 'Client-owned Spellblade class grant, Ground Pulse tagged input, authoritative boss health loss and replicated owner mana/cooldown.' }
                    elseif ($EncounterMap) { 'Placed world encounter class and arena in a dedicated fixture, with independent game clients and confirmed GAS health loss.' }
                    elseif ($RenderedClient) { 'Dedicated NullRHI server, one RHI player and headless peer owners. One synchronized authored melee attack per owner with confirmed GAS health loss.' }
                    elseif ($FieldArena) { 'NullRHI loopback in Test_MMO_Sandbox with its authored boss class and arena placement. Client-owned attacks, native validation and GAS health loss.' }
                    elseif ($DungeonCombat) { 'NullRHI loopback in Test_MMO_Dungeon with its authored boss class and arena placement. Client-owned attacks, native validation and GAS health loss. Combat timing only.' }
                    elseif ($BossCombat) { 'NullRHI loopback. One simultaneous client-owned Spellblade melee attack per player against BP_FieldBoss_Test, with native ability, hit validation and confirmed GAS health loss. Latency includes ability playback.' }
                    else { 'NullRHI loopback with one replicated native GAS target, synthetic instant damage and no player pawns.' }
            hardware = (Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)
            driverTickRate = $tickRate
            strikeTimingClock = if ($BossCombat) { 'Same-host FPlatformTime::Seconds (Windows QPC). Replication uses the first client GAS Health update crossing its strike threshold. Stage one starts at server request dispatch.' } else { $null }
            strikeTimingComplete = if ($BossCombat) {
                $phases.Burst.attackToHitWindowMs.samples -eq $expected -and
                    $phases.Burst.serverAuthorityHitMs.samples -eq $expected -and $phases.Burst.damageReplicationMs.samples -eq $expected
            } else { $null }
            finalBossHealth = $bossHealth
            authoredBoss = $boss
            connectionClose = $close
            worldTickBudgetMet = $metricsVerified -and $tickRate -eq 30 -and $phases.Idle.worldTickMs.p95 -le 25 -and
                (!$BossCombat -or $phases.Burst.worldTickMs.p95 -le (1000 / 30)) -and
                $phases.Burst.strikeTickMs.p100 -le (1000 / 30) -and $steady.worldTickMs.p95 -le 25 -and $steady.worldTickMs.p99 -le (1000 / 30)
            violations = $violations; phases = $phases
        }
    }
}
$affinityVerified = (!$ServerAffinityMask -or $appliedAffinity.Server -eq $ServerAffinityMask) -and
    (!$GeneratorAffinityMask -or @($generatorCounts.Keys | Where-Object { $appliedAffinity[$_] -ne $GeneratorAffinityMask }).Count -eq 0) -and
    (!$RenderedClient -or !$RenderedAffinityMask -or $appliedAffinity.RenderedClient -eq $RenderedAffinityMask)
$passed = $serverVerified -and $generatorVerified -and $replicationVerified -and $affinityVerified -and $server.ExitCode -eq 0 -and
    @($generators | Where-Object { $_.processExit -ne 0 }).Count -eq 0 -and
    $peak -eq $Clients -and $strikes -eq $expected -and $metricsVerified
if ($DungeonCombat) {
    $passed = $passed -and [bool](Select-String -LiteralPath "$report/Server.log" -SimpleMatch 'COHORT_DUNGEON_ARENA encounter=Dungeon_Starter_Boss' -Quiet)
}
if ($FieldArena) {
    $passed = $passed -and [bool](Select-String -LiteralPath "$report/Server.log" -SimpleMatch 'COHORT_FIELD_ARENA encounter=Sandbox_FieldBoss_01' -Quiet)
}
$encounterReplicationVerified = $null
if ($EncounterMap) {
    $encounterPattern = 'COHORT_FIELD_ARENA encounter=' + [regex]::Escape($EncounterId) + ' map=' +
        [regex]::Escape(($EncounterMap -split '/')[-1]) + ' boss='
    $roleLogs = @($processes.Keys | ForEach-Object { "$report/$_.log" })
    $encounterReplicationVerified = @($roleLogs | Where-Object { !(Test-Path -LiteralPath $_) }).Count -eq 0 -and
        !(Select-String -LiteralPath $roleLogs -Pattern 'Broken NetHandle|MissingLevelPackage|Could not find static actor' -Quiet)
    $passed = $passed -and $encounterReplicationVerified -and
        [bool](Select-String -LiteralPath "$report/Server.log" -Pattern $encounterPattern -Quiet)
}
if ($BossCombat) {
    $passed = $passed -and [bool](Select-String -LiteralPath "$report/Server.log" -SimpleMatch "COHORT_BOSS_AIM accepted=$Clients expected=$Clients" -Quiet)
}
if ($VerifyCrowdCap) { $passed = $passed -and [bool]$crowdSample }
if ($MaxBurstTickIntervalP95Ms) {
    $observedInterval = $combat.phases.Burst.tickIntervalMs.p95
    $passed = $passed -and $null -ne $observedInterval -and $observedInterval -le $MaxBurstTickIntervalP95Ms
}
if ($StableBossLayout) {
    $passed = $passed -and [bool](Select-String -LiteralPath "$report/Server.log" -SimpleMatch "COHORT_BOSS_LAYOUT anchor=32 participants=$Clients" -Quiet)
}
$rendered = $null
if ($RenderedClient) {
    $rendered = [ordered]@{ processExit = $processes.RenderedClient.ExitCode; verified = $false }
    $framePath = "$report/Rendered/frames.csv"
    if ((Test-Path -LiteralPath $framePath) -and (Test-Path -LiteralPath "$report/RenderedClient.log")) {
        $frames = @(Import-Csv -LiteralPath $framePath | Where-Object capture_adjacent -eq '0')
        $rendered.phases = [ordered]@{}
        foreach ($phase in @('Before', 'Strike')) {
            $phaseFrames = @($frames | Where-Object { ([double]$_.seconds_from_strike -lt 0) -eq ($phase -eq 'Before') })
            if (!$phaseFrames.Count) { continue }
            $rendered.phases[$phase] = [ordered]@{
                samples = $phaseFrames.Count
                frameMs = Measure-Distribution @($phaseFrames | ForEach-Object { [double]$_.frame_ms })
                gameMs = Measure-Distribution @($phaseFrames | ForEach-Object { [double]$_.game_ms })
                drawMs = Measure-Distribution @($phaseFrames | ForEach-Object { [double]$_.draw_ms })
                gpuMs = Measure-Distribution @($phaseFrames | ForEach-Object { [double]$_.gpu_ms })
                onScreenPlayers = Measure-Distribution @($phaseFrames | ForEach-Object { [int]$_.on_screen_players })
                clothUnsuspended = Measure-Distribution @($phaseFrames | ForEach-Object { [int]$_.cloth_unsuspended })
                clothSuspended = Measure-Distribution @($phaseFrames | ForEach-Object { [int]$_.cloth_suspended })
            }
        }
        $rendered.verified = $rendered.processExit -eq 0 -and $rendered.phases.Count -eq 2 -and
            $rendered.phases.Strike.onScreenPlayers.p50 -gt 1 -and
            $rendered.phases.Strike.gpuMs.p50 -gt 0 -and
            [bool](Select-String -LiteralPath "$report/RenderedClient.log" -Pattern "COHORT_RENDERED_FINISHED passed=1 frames=\d+ min_replicated=$Clients expected=$Clients " -Quiet)
        if ($RenderedClothProbe) {
            $expectedValue = if ($probe.ContainsKey('Readback')) { $probe.Readback } else { $probe.Value }
            $readback = [regex]::Escape(('{0} = "{1}"' -f $probe.Name, $expectedValue)) + '\s+LastSetBy:'
            $rendered.verified = $rendered.verified -and
                [bool](Select-String -LiteralPath "$report/RenderedClient.log" -Pattern $readback -Quiet)
        }
        if ($PSBoundParameters.ContainsKey('RenderedEffectsQuality')) {
            $qualityReadback = ('sg\.EffectsQuality = "{0}"\s+LastSetBy: Console' -f $RenderedEffectsQuality)
            $rendered.effectsQualityVerified = [bool](Select-String -LiteralPath "$report/RenderedClient.log" -Pattern $qualityReadback -Quiet)
            $rendered.verified = $rendered.verified -and $rendered.effectsQualityVerified
        }
        if ($RenderedClothProbe -eq 'ParkOptInRemote') {
            $rendered.verified = $rendered.verified -and $rendered.phases.Strike.clothUnsuspended.p50 -gt 0 -and
                $rendered.phases.Strike.clothSuspended.p50 -gt 0 -and
                [bool](Select-String -LiteralPath "$report/RenderedClient.log" -Pattern 'COHORT_CLOTH_RESTORED parts=[1-9]\d*' -Quiet)
        }
        if ($VerifyRemoteCloak) {
            $pattern = "COHORT_REMOTE_CLOAK verified=1 owner_original=1 remote_proxy=$($Clients - 1) native_cadence=1"
            $rendered.remoteCloakVerified = [bool](Select-String -LiteralPath "$report/RenderedClient.log" -Pattern $pattern -Quiet)
            $rendered.verified = $rendered.verified -and $rendered.remoteCloakVerified
        }
        if ($GroundWarningProbe) {
            $markerLog = Get-Content -Raw -LiteralPath "$report/RenderedClient.log"
            $warningMatches = @([regex]::Matches($markerLog, 'PROJECTJ_GROUND_WARNING visible actor=(?<actor>[^ ]+) remaining=[0-9.]+ radius=(?<radius>350|220)[.]0 render_state=1 caster_owner=0'))
            $natural = $warningMatches | Where-Object { $_.Groups['radius'].Value -eq '350' } | Select-Object -First 1
            $cancel = $warningMatches | Where-Object { $_.Groups['radius'].Value -eq '220' } | Select-Object -First 1
            $rendered.groundWarningVerified = $natural -and $cancel -and
                $markerLog.Contains('PROJECTJ_GROUND_WARNING expired actor=' + $natural.Groups['actor'].Value) -and
                $markerLog.Contains('PROJECTJ_GROUND_WARNING retired actor=' + $cancel.Groups['actor'].Value)
            $rendered.verified = $rendered.verified -and $rendered.groundWarningVerified
            if ($LateGroundWarning) {
                $arrivals = [regex]::Matches($markerLog, 'PROJECTJ_GROUND_WARNING visible actor=\S+ remaining=([0-9.]+) radius=480\.0 render_state=1 caster_owner=0 surface_up_z=([0-9.]+)')
                $rendered.lateWarningVerified = $arrivals.Count -eq 2 -and
                    [Math]::Abs([double]$arrivals[0].Groups[2].Value - [Math]::Cos([Math]::PI / 10)) -lt 0.002 -and
                    [Math]::Abs([double]$arrivals[1].Groups[2].Value - [Math]::Cos([Math]::PI / 10)) -lt 0.002 -and
                    [double]$arrivals[1].Groups[1].Value -lt [double]$arrivals[0].Groups[1].Value -and
                    $markerLog.Contains('PROJECTJ_GROUND_WARNING arrived_expired radius=490.0') -and
                    $markerLog -notmatch 'PROJECTJ_GROUND_WARNING visible [^\r\n]+radius=490\.0'
                $rendered.verified = $rendered.verified -and $rendered.lateWarningVerified
            }
        }
    }
    $passed = $passed -and $rendered.verified
}
[ordered]@{ passed = $passed; serverVerified = $serverVerified; generatorVerified = $generatorVerified
    serverMetricsObserved = if ($ServerMetricsPort) { [bool]$metricsSnapshot } else { $null }
    requestedCrowdCap = if ($VerifyCrowdCap) { $VerifyCrowdCap } else { $null }
    observedCrowdMetrics = $crowdSample
    maxBurstTickIntervalP95Ms = if ($MaxBurstTickIntervalP95Ms) { $MaxBurstTickIntervalP95Ms } else { $null }
    replication = $(if ($irisObserved) { 'Iris' } else { 'Generic' }); replicationVerified = $replicationVerified
    requested = $Clients; peakGameConnections = $peak; confirmedStrikes = $strikes; expectedStrikes = $expected
    serverExit = $server.ExitCode; generatorExit = [int]$generatorExit; generators = $generators; combat = $combat
    dungeonArena = [bool]$DungeonCombat; fieldArena = [bool]$FieldArena; stableBossLayout = [bool]$StableBossLayout; renderedClient = $rendered
    encounterMap = $EncounterMap; encounterId = $EncounterId; encounterReplicationVerified = $encounterReplicationVerified
    groundPulse = [bool]$GroundPulse
    requestedEffectsQuality = if ($PSBoundParameters.ContainsKey('RenderedEffectsQuality')) { $RenderedEffectsQuality } else { $null }
    bossHitDiagnostics = [bool]$BossHitDiagnostics; memoryReserveMiB = $MemoryReserveMiB
    thinClient = [bool]$ThinClient; usePerfThreads = [bool]$UsePerfThreads
    serverAffinityMask = $ServerAffinityMask; generatorAffinityMask = $GeneratorAffinityMask; renderedAffinityMask = $RenderedAffinityMask
    affinityVerified = $affinityVerified; appliedAffinity = $appliedAffinity
    headlessCoreLimit = $HeadlessCoreLimit; headlessFrameCap = $HeadlessFrameCap; renderedClothProbe = $RenderedClothProbe; cloakHz = $CloakHz } |
    ConvertTo-Json -Depth 7 | Set-Content -LiteralPath "$report/verification.json" -Encoding utf8
Get-Content -Raw -LiteralPath "$report/verification.json"
if (!$passed) { exit 1 }
