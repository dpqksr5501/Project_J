param(
 [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName,
 [string]$Filters = 'ProjectJ.Crowd.+ProjectJ.GroupD.+ProjectJ.NPCDecision.+ProjectJ.GroupA.+ProjectJ.Combat.WeaponPresentation+ProjectJ.Integrated.VisualAssetSharing',
 [switch]$Rendered,
 [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Suite='CrowdE_20260910',
 [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\','/')
Set-Location -LiteralPath $projectRoot
if (@(Get-Process UnrealEditor*,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count) {throw 'Engine busy; wait for normal completion.'}
$runRoot="$projectRoot/Saved/Validation/$Suite/$RunName"
if(Test-Path -LiteralPath $runRoot){throw 'Choose a unique RunName.'}
New-Item -ItemType Directory -Path $runRoot | Out-Null
$files=@(& git -c core.fsmonitor=false ls-files --modified --others --exclude-standard -- Source Scripts Config Project_J.uproject | Sort-Object -Unique | ForEach-Object {
 if(Test-Path -LiteralPath $_ -PathType Leaf) {
  $target="$runRoot/SourceSnapshot/$_"; New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null; Copy-Item -LiteralPath $_ -Destination $target
  [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}
 }
})
[ordered]@{head=(& git -c core.fsmonitor=false rev-parse HEAD);filters=$Filters;rendered=[bool]$Rendered;files=$files;cpu=(Get-CimInstance Win32_Processor).Name} | ConvertTo-Json -Depth 5 | Set-Content "$runRoot/manifest.json" -Encoding utf8
$extra=if($Rendered){@('-windowed','-ResX=1280','-ResY=720')}else{@('-NullRHI')}
$setup=if($Filters.Contains('ProjectJ.GroupB.')){'a.Budget.Enabled 1,a.Budget.BudgetMs 0.1,'}else{''}
& "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$projectRoot/Project_J.uproject" -unattended -nop4 -nosplash -nosound -statnamedevents "-ExecCmds=${setup}Automation RunTests $Filters" '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$runRoot/Automation" "-ABSLOG=$runRoot/Automation.log" '-trace=cpu,frame,bookmark,region,counters,task,log,gpu' "-tracefile=$runRoot/Run.utrace" "-ProjectJCrowdOutput=$runRoot/Metrics" "-ProjectJMassOutput=$runRoot/MassMetrics" "-ProjectJNavOutput=$runRoot/NavMetrics" @extra
$exitCode=$LASTEXITCODE
if(!(Test-Path "$runRoot/Automation/index.json")){throw "Missing report: $runRoot/Automation.log"}
$report=Get-Content "$runRoot/Automation/index.json" -Raw | ConvertFrom-Json
$errors=[int](($report.tests | Measure-Object errors -Sum).Sum)
$warnings=[int](($report.tests | Measure-Object warnings -Sum).Sum)
$missing=@($Filters.Split('+') | Where-Object { $filter=$_; !@($report.tests | Where-Object { $_.fullTestPath.StartsWith($filter) }).Count })
$passed=$exitCode -eq 0 -and $errors -eq 0 -and $report.failed -eq 0 -and $report.notRun -eq 0 -and $report.inProcess -eq 0 -and $missing.Count -eq 0
[ordered]@{passed=$passed;exitCode=$exitCode;succeeded=$report.succeeded;succeededWithWarnings=$report.succeededWithWarnings;errors=$errors;warnings=$warnings;missing=$missing;tests=@($report.tests | Select-Object fullTestPath,state,errors,warnings)} | ConvertTo-Json -Depth 5 | Set-Content "$runRoot/verification.json" -Encoding utf8
if(!$passed){throw "Test failed: $runRoot/verification.json"}
Write-Output "Completed $runRoot; success=$($report.succeeded) withWarnings=$($report.succeededWithWarnings)"
