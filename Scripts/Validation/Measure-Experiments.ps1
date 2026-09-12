param(
 [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName,
 [string]$Filters='ProjectJ.GroupF.+ProjectJ.GroupE.AuthoringAudit+ProjectJ.GroupE.AnimationMigrationPreflight',
 [switch]$Rendered,
 [string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\','/')
Set-Location -LiteralPath $root
if(@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count){throw 'Wait for engine/build completion.'}
$out="$root/Saved/Validation/EF_20260912/$RunName"
if(Test-Path -LiteralPath $out){throw 'Preserve evidence; use a new RunName.'}
New-Item -ItemType Directory $out|Out-Null
$files=@(& git -c core.fsmonitor=false ls-files --modified --others --exclude-standard -- Source Scripts Plugins/ProjectJExperiments Config Project_J.uproject|Sort-Object -Unique|ForEach-Object {
 if(Test-Path -LiteralPath $_ -PathType Leaf){
  $target="$out/SourceSnapshot/$_"; New-Item -ItemType Directory -Force (Split-Path -Parent $target)|Out-Null
  Copy-Item -LiteralPath $_ -Destination $target
  [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash}
 }
})
[ordered]@{head=(& git rev-parse HEAD);filters=$Filters;rendered=[bool]$Rendered;files=$files}|ConvertTo-Json -Depth 5|Set-Content "$out/manifest.json" -Encoding utf8
$extra=if($Rendered){@('-RenderOffscreen','-windowed','-ResX=1280','-ResY=720')}else{@('-NullRHI')}
& "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$root/Project_J.uproject" -EnablePlugins=ProjectJExperiments -unattended -nop4 -nosplash -statnamedevents "-ExecCmds=Automation RunTests $Filters" '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$out/Automation" "-ABSLOG=$out/Run.log" "-ProjectJFOutput=$out/Metrics" '-trace=cpu,frame,bookmark,region,task,gpu' "-tracefile=$out/Run.utrace" @extra
$code=$LASTEXITCODE
[ordered]@{exitCode=$code;reportPresent=(Test-Path "$out/Automation/index.json")}|ConvertTo-Json|Set-Content "$out/process.json" -Encoding utf8
if(!(Test-Path "$out/Automation/index.json")){throw "No report: $out/Run.log"}
$report=Get-Content "$out/Automation/index.json" -Raw|ConvertFrom-Json
$missing=@($Filters.Split('+')|Where-Object {$prefix=$_; !@($report.tests|Where-Object {$_.fullTestPath.StartsWith($prefix)}).Count})
$pass=$code -eq 0 -and $report.failed -eq 0 -and $report.notRun -eq 0 -and $report.inProcess -eq 0 -and $missing.Count -eq 0
[ordered]@{passed=$pass;exitCode=$code;succeeded=$report.succeeded;withWarnings=$report.succeededWithWarnings;missing=$missing;tests=@($report.tests|Select-Object fullTestPath,state,errors,warnings)}|ConvertTo-Json -Depth 5|Set-Content "$out/verification.json" -Encoding utf8
if(!$pass){throw "Failed experiments: $out/verification.json"}
Write-Output "Completed $out"
