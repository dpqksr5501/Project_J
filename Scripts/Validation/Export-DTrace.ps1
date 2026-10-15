param(
    [Parameter(Mandatory = $true)][string]$RunRoot,
    [switch]$Network,
    [switch]$Animation,
    [switch]$AnimationEvents,
    [switch]$NotifyCounters,
    [switch]$NotifyLifecycle,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$OutputName = 'TraceExport',
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference = 'Stop'
if (@(Get-Process UnrealEditor*,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker,UnrealInsights -ErrorAction SilentlyContinue).Count) { throw 'Wait until engine and trace analysis processes finish.' }
$RunRoot = (Resolve-Path -LiteralPath $RunRoot).Path.Replace('\','/')
if ($Network -and $Animation) { throw 'Select one trace workload.' }
if ($AnimationEvents -and !$Animation) { throw 'AnimationEvents requires Animation.' }
if ($NotifyCounters -and !$Animation) { throw 'NotifyCounters requires Animation.' }
if ($NotifyLifecycle -and !$Animation) { throw 'NotifyLifecycle requires Animation.' }
$trace = if ($Network) { "$RunRoot/Server.utrace" } elseif ($Animation) { "$RunRoot/CPU.utrace" } else { "$RunRoot/Run.utrace" }
if (!(Test-Path -LiteralPath $trace)) { throw "Missing trace: $trace" }
$outputRoot = "$RunRoot/$OutputName"
if (Test-Path -LiteralPath $outputRoot) { throw 'Trace export already exists; preserve original evidence.' }
New-Item -ItemType Directory -Path $outputRoot | Out-Null
$commands = @(
    ('TimingInsights.ExportThreads "{0}/Threads.csv"' -f $outputRoot),
    ('TimingInsights.ExportTimerStatistics "{0}/AllTimers.csv"' -f $outputRoot)
)
if ($NotifyCounters) {
    # UE 5.8 ExportCounters passes the entire remainder as the filename;
    # unlike ExportCounterValues it does not tokenize/strip quotes.
    $commands += ('TimingInsights.ExportCounters {0}/Counters.csv' -f $outputRoot)
    $notifyNames = @('WeaponMotion/Begins','WeaponMotion/Ticks','WeaponMotion/Ends','WeaponMotion/WeakMeshVisits','WeaponMotion/InstanceVisits',
        'GroundContact/Begins','GroundContact/Ends','GroundContact/WeakMeshVisits','GroundContact/InstanceVisits')
    foreach ($name in $notifyNames) {
        $filename = $name.Replace('/','-')
        $commands += ('TimingInsights.ExportCounterValues "{0}/Counter-{1}.csv" -counter="ProjectJ/Notify/{2}"' -f $outputRoot,$filename,$name)
        $counterRegions = if ($NotifyLifecycle) { @('ProjectJ.NotifyLifecycle.*') } else { @('ProjectJ.AuthoredCPU_*','ProjectJ.AuthoredAttackDispatch_*') }
        foreach ($region in $counterRegions) {
            $commands += ('TimingInsights.ExportCounterValues "{0}/Counter-{1}-{{region}}.csv" -counter="ProjectJ/Notify/{2}" -region="{3}"' -f $outputRoot,$filename,$name,$region)
        }
    }
}
if ($Network) {
    $commands += ('TimingInsights.ExportTimerStatistics "{0}/Timers-{{region}}.csv" -region="ProjectJ.Network.*"' -f $outputRoot)
    $commands += ('TimingInsights.ExportTimingEvents "{0}/Events-{{region}}.csv" -region="ProjectJ.Network.*" -timers="*NetDriver*,*Connection*,*Replication*,*Filtering*" -columns="ThreadId,TimerId,TimerName,StartTime,EndTime,Depth"' -f $outputRoot)
} elseif ($Animation) {
    if ($NotifyLifecycle) {
        $commands += ('TimingInsights.ExportTimerStatistics "{0}/Timers-{{region}}.csv" -region="ProjectJ.NotifyLifecycle.*"' -f $outputRoot)
    } else {
        $commands += ('TimingInsights.ExportTimerStatistics "{0}/Timers-{{region}}.csv" -region="ProjectJ.AuthoredCPU_*"' -f $outputRoot)
        $commands += ('TimingInsights.ExportTimerStatistics "{0}/Timers-{{region}}.csv" -region="ProjectJ.AuthoredAttackDispatch_*"' -f $outputRoot)
    }
    if ($AnimationEvents) {
        $commands += ('TimingInsights.ExportTimingEvents "{0}/Events-{{region}}.csv" -region="ProjectJ.AuthoredCPU_*" -timers="ProjectJ_AnimProxy_*,Project_J_AnimNativeUpdate,ProjectJ_AuthoredAnimation_*,IK Retarget Processor Run,FAnimNode_RigidBody::EvaluateSkeletalControl_AnyThread,WaitForTasks" -columns="ThreadId,TimerId,TimerName,StartTime,EndTime,Depth"' -f $outputRoot)
    }
} else {
    $commands += ('TimingInsights.ExportTimingEvents "{0}/MassEvents.csv" -timers="ProjectJ_Mass*" -columns="ThreadId,TimerId,TimerName,StartTime,EndTime,Depth"' -f $outputRoot)
}
$commands | Set-Content "$outputRoot/Export.rsp" -Encoding utf8
$arguments = @("-OpenTraceFile=$trace",'-AutoQuit','-NoUI','-unattended',"-ExecOnAnalysisCompleteCmd=@=$outputRoot/Export.rsp","-ABSLOG=$outputRoot/Insights.log")
$quoted = @($arguments | ForEach-Object {
    # Insights parses the raw command line: leave option names unquoted, quote only values.
    if ($_ -match '^([^=]+)=(.*)$') { $Matches[1] + '="' + $Matches[2] + '"' } else { $_ }
}) -join ' '
$analysis = Start-Process -FilePath "$EngineRoot/Engine/Binaries/Win64/UnrealInsights.exe" -ArgumentList $quoted -WindowStyle Hidden -Wait -PassThru
if ($analysis.ExitCode -ne 0 -or !(Test-Path "$outputRoot/AllTimers.csv") -or !(Test-Path "$outputRoot/Threads.csv")) { throw 'Insights export failed; inspect Insights.log.' }
if ($Network -and @(Get-ChildItem -LiteralPath $outputRoot -Filter 'Events-*.csv').Count -ne 5) { throw 'Expected all five measured network regions.' }
if ($Animation -and @(Get-ChildItem -LiteralPath $outputRoot -Filter 'Timers-*.csv').Count -eq 0) { throw 'No authored CPU measurement regions were exported.' }
if ($AnimationEvents) {
    $timerRegions = @(Get-ChildItem -LiteralPath $outputRoot -Filter 'Timers-ProjectJ.AuthoredCPU_*.csv')
    $eventRegions = @(Get-ChildItem -LiteralPath $outputRoot -Filter 'Events-ProjectJ.AuthoredCPU_*.csv')
    if (!$timerRegions.Count -or $eventRegions.Count -ne $timerRegions.Count) { throw 'Animation timing events are missing for one or more measurement regions.' }
}
Write-Output "Trace analysis/export finished: $outputRoot"
