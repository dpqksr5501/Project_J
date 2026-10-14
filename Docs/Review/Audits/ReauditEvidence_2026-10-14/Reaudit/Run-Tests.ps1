param([ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,[string]$Tests='ProjectJ.Reaudit',[switch]$RHI,[switch]$IsolatedUser)
$ErrorActionPreference='Stop'
if (@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count){throw 'Wait for engine/build completion.'}
$projectRoot=(Resolve-Path "$PSScriptRoot/../../..").Path.Replace('\','/')
$out="$PSScriptRoot/$Name"
if(Test-Path -LiteralPath $out){throw 'Preserve previous evidence.'}
New-Item -ItemType Directory -Path $out | Out-Null
$arguments=@("$projectRoot/Project_J.uproject",'-unattended','-nosound','-nop4','-NoDDCCleanup','-NoLiveCoding',
    "-ABSLOG=$out/Run.log","-ReportExportPath=$out",'-TestExit=Automation Test Queue Empty',"-ExecCmds=a.Budget.BudgetMs 0.1,Automation RunTests $Tests; Quit")
if($RHI){$arguments+=@('-RenderOffscreen','-windowed','-ResX=1280','-ResY=720')}else{$arguments+='-NullRHI'}
if($IsolatedUser){$arguments+="-UserDir=$out/User"}
$arguments | ConvertTo-Json | Set-Content -LiteralPath "$out/arguments.json" -Encoding utf8
$process=Start-Process 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -ArgumentList (($arguments|ForEach-Object {'"'+$_+'"'}) -join ' ') -WindowStyle Hidden -PassThru
$nativeHandle=$process.Handle
Write-Output "Started $Name PID=$($process.Id)"
$process.WaitForExit()
[ordered]@{exitCode=$process.ExitCode;hasExited=$process.HasExited;nativeHandleCapturedBeforeWait=$true} | ConvertTo-Json | Set-Content -LiteralPath "$out/process.json" -Encoding utf8
if($null -eq $process.ExitCode -or $process.ExitCode -ne 0){throw "Engine exited with code $($process.ExitCode); inspect the first error in $out/Run.log. Native process receipt retained."}
if(!(Test-Path -LiteralPath "$out/index.json")){throw "Engine exited without an automation report: $out/Run.log"}
$report=Get-Content -LiteralPath "$out/index.json" -Raw | ConvertFrom-Json
$report | Select-Object succeeded,succeededWithWarnings,failed,notRun,inProcess,totalDuration
if($null -eq $process.ExitCode -or $process.ExitCode -ne 0 -or $report.failed -or $report.notRun -or $report.inProcess -or ($report.succeeded+$report.succeededWithWarnings -eq 0)){throw 'Regression failed or incomplete; evidence retained.'}
