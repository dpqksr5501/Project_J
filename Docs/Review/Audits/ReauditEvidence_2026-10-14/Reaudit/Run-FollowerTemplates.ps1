$ErrorActionPreference='Stop'
if(@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count){throw 'Wait for engine/build completion.'}
$projectRoot=(Resolve-Path "$PSScriptRoot/../../..").Path.Replace('\','/')
$arguments=@("$projectRoot/Project_J.uproject",'-NullRHI','-nosound','-unattended','-nop4','-NoDDCCleanup','-NoLiveCoding','-EnablePlugins=PythonScriptPlugin',"-ABSLOG=$PSScriptRoot/FollowerTemplates1.log","-ExecCmds=py $PSScriptRoot/Read-FollowerTemplates.py")
if(Test-Path "$PSScriptRoot/FollowerTemplates1.json"){throw 'Preserve previous evidence.'}
$arguments | ConvertTo-Json | Set-Content "$PSScriptRoot/FollowerTemplates1_Arguments.json" -Encoding utf8
$process=Start-Process 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -ArgumentList (($arguments|ForEach-Object {'"'+$_+'"'}) -join ' ') -WindowStyle Hidden -PassThru
$nativeHandle=$process.Handle
"Started public template supplement PID=$($process.Id)"
$process.WaitForExit()
@{exitCode=$process.ExitCode;hasExited=$process.HasExited;nativeHandleCapturedBeforeWait=$true} | ConvertTo-Json | Set-Content "$PSScriptRoot/FollowerTemplates1_Process.json" -Encoding utf8
if($null -eq $process.ExitCode -or $process.ExitCode -ne 0){throw 'Editor failure retained.'}
