param([ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name='Cook1')
$ErrorActionPreference='Stop'
if(@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count){throw 'Wait for engine/build completion.'}
$projectRoot=(Resolve-Path "$PSScriptRoot/../../..").Path.Replace('\','/')
$out="$PSScriptRoot/$Name"
if(Test-Path $out){throw 'Preserve previous evidence.'}
New-Item -ItemType Directory -Path $out | Out-Null
$arguments=@("$projectRoot/Project_J.uproject",'-run=Cook','-TargetPlatform=Windows','-Map=/Game/ThirdPerson/Lvl_ThirdPerson+/Engine/Maps/Entry','-unattended','-nop4','-NoDDCCleanup','-NoLiveCoding',"-ABSLOG=$out/Cook.log",'-UTF8Output','-stdout')
$arguments | ConvertTo-Json | Set-Content "$out/arguments.json" -Encoding utf8
$process=Start-Process 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -ArgumentList (($arguments|ForEach-Object {'"'+$_+'"'}) -join ' ') -WindowStyle Hidden -PassThru
$nativeHandle=$process.Handle
"Cook PID=$($process.Id)"
$process.WaitForExit()
@{exitCode=$process.ExitCode;hasExited=$process.HasExited;nativeHandleCapturedBeforeWait=$true;scope='Windows default map + Entry + configured AssetManager/AlwaysCook dependency closure. Not unused sample/plugin CookAll.'} | ConvertTo-Json | Set-Content "$out/process.json" -Encoding utf8
if($null -eq $process.ExitCode -or $process.ExitCode -ne 0){throw 'Cook failure retained; inspect first error.'}
