param(
    [Parameter(Mandatory = $true)][string]$RunRoot,
    [switch]$Network,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$OutputName = 'TraceExport',
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference = 'Stop'
if (@(Get-Process UnrealEditor*,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker,UnrealInsights -ErrorAction SilentlyContinue).Count) { throw 'Wait until engine and trace analysis processes finish.' }
$RunRoot = (Resolve-Path -LiteralPath $RunRoot).Path.Replace('\','/')
$trace = if ($Network) { "$RunRoot/Server.utrace" } else { "$RunRoot/Run.utrace" }
if (!(Test-Path -LiteralPath $trace)) { throw "Missing trace: $trace" }
$outputRoot = "$RunRoot/$OutputName"
if (Test-Path -LiteralPath $outputRoot) { throw 'Trace export already exists; preserve original evidence.' }
New-Item -ItemType Directory -Path $outputRoot | Out-Null
$commands = @(
    ('TimingInsights.ExportThreads "{0}/Threads.csv"' -f $outputRoot),
    ('TimingInsights.ExportTimerStatistics "{0}/AllTimers.csv"' -f $outputRoot)
)
if ($Network) {
    $commands += ('TimingInsights.ExportTimerStatistics "{0}/Timers-{{region}}.csv" -region="ProjectJ.Network.*"' -f $outputRoot)
    $commands += ('TimingInsights.ExportTimingEvents "{0}/Events-{{region}}.csv" -region="ProjectJ.Network.*" -timers="*NetDriver*,*Connection*,*Replication*,*Filtering*" -columns="ThreadId,TimerId,TimerName,StartTime,EndTime,Depth"' -f $outputRoot)
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
Write-Output "Trace analysis/export finished: $outputRoot"
