param(
    [Parameter(Mandatory = $true)][string]$RunName,
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8',
    [switch]$Build,
    [switch]$Regression
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Set-Location -LiteralPath $projectRoot
if ($RunName -notmatch '^[a-zA-Z0-9_-]+$') { throw 'RunName must be a simple unique directory name.' }
$runRoot = Join-Path $projectRoot "Saved/Validation/GroupC_20260909/$RunName"
if (Test-Path -LiteralPath $runRoot) { throw "Refusing to overwrite existing evidence: $runRoot" }
function Assert-EngineIdle {
    $busy = Get-Process UnrealEditor*,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue
    if ($busy) { throw "Build/editor processes are active; wait for them to finish: $($busy.Id -join ',')" }
}
Assert-EngineIdle
New-Item -ItemType Directory -Path $runRoot | Out-Null
$manifest = [ordered]@{
    utc = (Get-Date).ToUniversalTime().ToString('o')
    head = (& git -c core.fsmonitor=false rev-parse HEAD)
    sourceStatus = @(& git -c core.fsmonitor=false status --short -- Source Config Project_J.uproject)
    engine = $EngineRoot
    scenario = 'Transient native 100 Characters / real Dynamic Recast / NullRHI / fixed simulation step 0.05'
    statNamedEvents = $true
    exclusions = 'No authored BP/ABP, combat, rendering, crowd avoidance, network connections or FPS claim'
    cpu = (Get-CimInstance Win32_Processor | Select-Object -ExpandProperty Name)
    filters = if ($Regression) { 'ProjectJ.GroupC.+ProjectJ.NPCAction.+ProjectJ.GroupA.PathPressure+ProjectJ.Integrated.NativeNavMovement+ProjectJ.Integrated.MovingTargetPursuit+ProjectJ.Integrated.ActionBudget+ProjectJ.NPCGameplay.GroundRootMotion+ProjectJ.GroupB.Character' } else { 'ProjectJ.GroupC.' }
}
$sourceFiles = @(& git -c core.fsmonitor=false ls-files --modified --others --exclude-standard -- Source Scripts Config Project_J.uproject)
$manifest.sourceFiles = @($sourceFiles | Sort-Object -Unique | ForEach-Object {
    $sourcePath = Join-Path $projectRoot $_
    if (Test-Path -LiteralPath $sourcePath -PathType Leaf) {
        $snapshotPath = Join-Path $runRoot "SourceSnapshot/$_"
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $snapshotPath) | Out-Null
        Copy-Item -LiteralPath $sourcePath -Destination $snapshotPath
        [ordered]@{ path = $_; sha256 = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash }
    }
})
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runRoot 'manifest.json') -Encoding utf8
& git -c core.fsmonitor=false diff -- Source Config Project_J.uproject | Set-Content -LiteralPath (Join-Path $runRoot 'source.patch') -Encoding utf8
if ($Build) {
    & "$EngineRoot/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" Project_JEditor Win64 Development "-Project=$projectRoot/Project_J.uproject" -WaitMutex -NoHotReloadFromIDE "-Log=$runRoot/Build.log"
    if ($LASTEXITCODE -ne 0) { throw "UBT failed ($LASTEXITCODE). Read first error and Application Event Log before retrying." }
}
Assert-EngineIdle
& "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$projectRoot/Project_J.uproject" -unattended -nop4 -nosplash -NullRHI -nosound -statnamedevents "-ExecCmds=Automation RunTests $($manifest.filters)" '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$runRoot/Automation" "-ABSLOG=$runRoot/Automation.log" '-trace=cpu,frame,bookmark,region,counters,task,log' "-tracefile=$runRoot/Run.utrace" "-ProjectJNavOutput=$runRoot/Metrics"
$editorExit = $LASTEXITCODE
$reportPath = Join-Path $runRoot 'Automation/index.json'
if (!(Test-Path -LiteralPath $reportPath)) {
    [ordered]@{ editorExit = $editorExit; reportAvailable = $false; passed = $false; reason = 'No automation report; inspect Automation.log for the first failure/crash.' } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'verification.json') -Encoding utf8
    throw "Editor exit=$editorExit without automation report. Inspect $runRoot/Automation.log; no pass result is available."
}
$report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
$reportErrors = [int](($report.tests | Measure-Object errors -Sum).Sum)
$reportWarnings = [int](($report.tests | Measure-Object warnings -Sum).Sum)
$requiredTests = @('ProjectJ.GroupC.NavigationLoad100', 'ProjectJ.GroupC.AutomaticDirtyRecovery')
$missingTests = @($requiredTests | Where-Object { $_ -notin $report.tests.fullTestPath })
$metricsAvailable = (Test-Path -LiteralPath (Join-Path $runRoot 'Metrics/frames.csv')) -and (Test-Path -LiteralPath (Join-Path $runRoot 'Metrics/requests.csv'))
[ordered]@{
    editorExit = $editorExit; succeeded = $report.succeeded; succeededWithWarnings = $report.succeededWithWarnings
    failed = $report.failed; notRun = $report.notRun; inProcess = $report.inProcess
    errors = $reportErrors; warnings = $reportWarnings; missingTests = $missingTests; metricsAvailable = $metricsAvailable
    nonCleanTests = @($report.tests | Where-Object { $_.state -ne 'Success' -or $_.errors -gt 0 -or $_.warnings -gt 0 } | Select-Object fullTestPath,state,errors,warnings,entries)
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'verification.json') -Encoding utf8
if ($metricsAvailable) { & (Join-Path $PSScriptRoot 'Summarize-Navigation.ps1') -RunRoot $runRoot }
if ($editorExit -ne 0 -or $reportErrors -ne 0 -or $missingTests.Count -gt 0 -or !$metricsAvailable -or $report.failed -ne 0 -or $report.notRun -ne 0 -or $report.inProcess -ne 0 -or ($report.succeeded + $report.succeededWithWarnings) -eq 0) {
    throw "Automation incomplete or failed: exit=$editorExit failed=$($report.failed) errors=$reportErrors missing=$($missingTests.Count). Inspect verification.json and Automation.log."
}
Write-Output "Completed: $runRoot; Success=$($report.succeeded), WithWarnings=$($report.succeededWithWarnings), WarningEvents=$reportWarnings"
$traceExport = (Join-Path $runRoot 'TraceExport').Replace('\','/')
New-Item -ItemType Directory -Path $traceExport | Out-Null
@(
    ('TimingInsights.ExportTimerStatistics "{0}/AllTimers.csv"' -f $traceExport)
    ('TimingInsights.ExportTimerStatistics "{0}/Timers-{{region}}.csv" -region="ProjectJ.NavLoad.*"' -f $traceExport)
    ('TimingInsights.ExportTimingEvents "{0}/Events-{{region}}.csv" -region="ProjectJ.NavLoad.*" -timers="ProjectJ_*,*Nav*,*Recast*,*WaitUntilTasksComplete*" -columns="ThreadId,TimerId,TimerName,StartTime,EndTime,Depth"' -f $traceExport)
    ('TimingInsights.ExportCounterValues "{0}/Phases.csv" -counter=ProjectJ/NavLoad/Phase' -f $traceExport)
) | Set-Content -LiteralPath (Join-Path $traceExport 'Export.rsp') -Encoding utf8
Write-Output 'Prepared TraceExport/Export.rsp. Trace export/analysis has NOT been executed by this script.'
