param([Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
$active=@(Get-CimInstance Win32_Process -ErrorAction Stop|Where-Object {$_.Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|Uba|UnrealInsights).*'})
if($active.Count){throw 'Wait for normal completion.'}
$out="$projectRoot/Saved/Validation/Continuation_20261014/$Name"
if(Test-Path $out){throw 'Preserve prior evidence.'}
New-Item -ItemType Directory -Path $out|Out-Null
$arguments=@("$projectRoot/Project_J.uproject",'-NullRHI','-unattended','-nosound','-NoLiveCoding','-NoDDCCleanup','-nop4',"-UserDir=$out/User",'-SaveToUserDir',"-ABSLOG=$out/Run.log", "-ReportExportPath=$out", "-ProjectJNotifyOutput=$out/NotifyLifecycle.json",'-trace=cpu,frame,region,counters',"-tracefile=$out/CPU.utrace",'-statnamedevents','-TestExit=Automation Test Queue Empty','-ExecCmds=Automation RunTests ProjectJ.Animation.SharedNotifyLifecycle; Quit')
$arguments|ConvertTo-Json|Set-Content "$out/arguments.json" -Encoding utf8
$process=Start-Process 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -ArgumentList (($arguments|ForEach-Object {'"'+$_+'"'}) -join ' ') -WindowStyle Hidden -PassThru
$nativeHandle=$process.Handle; $process.WaitForExit()
[ordered]@{exitCode=$process.ExitCode;hasExited=$process.HasExited}|ConvertTo-Json|Set-Content "$out/process.json" -Encoding utf8
if($null -eq $process.ExitCode -or $process.ExitCode -ne 0 -or !(Test-Path "$out/NotifyLifecycle.json")){throw 'Notify fixture failed; retain first error.'}
$report=Get-Content "$out/index.json" -Raw|ConvertFrom-Json
if($report.failed -or $report.notRun -or $report.inProcess -or $report.succeeded -ne 1){throw 'Notify fixture incomplete or failed.'}
