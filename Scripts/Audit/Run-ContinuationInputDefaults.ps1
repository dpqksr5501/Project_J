param([ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name='InputDefaults1')
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
$active=@(Get-CimInstance Win32_Process -ErrorAction Stop|Where-Object {$_.Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|Uba|UnrealInsights).*'})
if($active.Count){throw 'Wait for normal engine/build completion.'}
$out="$projectRoot/Saved/Validation/Continuation_20261014/$Name"
if(Test-Path -LiteralPath $out){throw 'Preserve earlier evidence.'}
New-Item -ItemType Directory -Path $out|Out-Null
$arguments=@("$projectRoot/Project_J.uproject",'-NullRHI','-unattended','-nosound','-NoLiveCoding','-NoDDCCleanup','-nop4',"-UserDir=$out/User",'-SaveToUserDir',"-ABSLOG=$out/Run.log",'-culture=en',"-ProjectJAuditReadName=$Name","-ExecutePythonScript=$projectRoot/Scripts/Audit/Read-ContinuationInputDefaults.py")
$arguments|ConvertTo-Json|Set-Content "$out/arguments.json" -Encoding utf8
$process=Start-Process 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -ArgumentList (($arguments|ForEach-Object {'"'+$_+'"'}) -join ' ') -WindowStyle Hidden -PassThru
$nativeHandle=$process.Handle
$process.WaitForExit()
[ordered]@{exitCode=$process.ExitCode;hasExited=$process.HasExited;publicPropertyRead=$true;assetSave=$false}|ConvertTo-Json|Set-Content "$out/process.json" -Encoding utf8
if($null -eq $process.ExitCode -or $process.ExitCode -ne 0 -or !(Test-Path "$out/AssetConnections.json")){throw 'Selected property read failed; preserve raw log.'}
