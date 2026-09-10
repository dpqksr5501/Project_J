param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName,
    [switch]$Regression,
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\','/')
Set-Location -LiteralPath $projectRoot
if (@(Get-Process UnrealEditor*,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count) { throw 'Engine processes active; wait for normal completion.' }
$runRoot = "$projectRoot/Saved/Validation/GroupD_20260910/$RunName"
if (Test-Path -LiteralPath $runRoot) { throw 'Use a unique evidence directory.' }
New-Item -ItemType Directory -Path $runRoot | Out-Null
$filters = 'ProjectJ.GroupD.'
if ($Regression) { $filters += '+ProjectJ.GroupC.+ProjectJ.NPCAction.+ProjectJ.GroupA.PathPressure+ProjectJ.Integrated.NativeNavMovement+ProjectJ.Integrated.MovingTargetPursuit+ProjectJ.Integrated.ActionBudget+ProjectJ.NPCGameplay.GroundRootMotion+ProjectJ.GroupB.Character' }
$snapshot = @(& git -c core.fsmonitor=false ls-files --modified --others --exclude-standard -- Source Scripts Config Project_J.uproject | Sort-Object -Unique | ForEach-Object {
    $path = Join-Path $projectRoot $_
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $destination = "$runRoot/SourceSnapshot/$_"
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $path -Destination $destination
        [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    }
})
[ordered]@{head=(& git -c core.fsmonitor=false rev-parse HEAD);filters=$filters;sourceFiles=$snapshot;cpu=(Get-CimInstance Win32_Processor).Name;scenario='Actual Mass entity manager serial/parallel values, retained Character handoff, native Recast route. NullRHI; no rendered crowd/memory reduction claim.'} | ConvertTo-Json -Depth 5 | Set-Content "$runRoot/manifest.json" -Encoding utf8
& "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$projectRoot/Project_J.uproject" -unattended -nop4 -nosplash -NullRHI -nosound -statnamedevents "-ExecCmds=Automation RunTests $filters" '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$runRoot/Automation" "-ABSLOG=$runRoot/Automation.log" '-trace=cpu,frame,bookmark,region,counters,task,log' "-tracefile=$runRoot/Run.utrace" "-ProjectJMassOutput=$runRoot/Metrics" "-ProjectJNavOutput=$runRoot/NavMetrics"
$exitCode = $LASTEXITCODE
if (!(Test-Path "$runRoot/Automation/index.json")) { throw "No automation report; inspect $runRoot/Automation.log (exit=$exitCode)." }
$report = Get-Content "$runRoot/Automation/index.json" -Raw | ConvertFrom-Json
$required = @('ProjectJ.GroupD.MassHandoff','ProjectJ.GroupD.MassNativeNavigation','ProjectJ.GroupD.MassValues','ProjectJ.GroupD.MassActionHandoff')
$missing = @($required | Where-Object { $_ -notin $report.tests.fullTestPath })
$errors = [int](($report.tests | Measure-Object errors -Sum).Sum)
$warnings = [int](($report.tests | Measure-Object warnings -Sum).Sum)
$passed = $exitCode -eq 0 -and $errors -eq 0 -and $missing.Count -eq 0 -and $report.failed -eq 0 -and $report.notRun -eq 0 -and $report.inProcess -eq 0 -and (Test-Path "$runRoot/Metrics/mass-values.csv")
[ordered]@{passed=$passed;exitCode=$exitCode;succeeded=$report.succeeded;succeededWithWarnings=$report.succeededWithWarnings;failed=$report.failed;errors=$errors;warnings=$warnings;missing=$missing;tests=@($report.tests | Select-Object fullTestPath,state,errors,warnings)} | ConvertTo-Json -Depth 5 | Set-Content "$runRoot/verification.json" -Encoding utf8
if (!$passed) { throw "Mass/regression failed; inspect $runRoot/verification.json and Automation.log." }
Write-Output "Completed: $runRoot; success=$($report.succeeded) warnings=$warnings"
