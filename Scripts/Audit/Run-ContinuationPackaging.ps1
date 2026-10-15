param([Parameter(Mandatory)][ValidateSet('Cook','Stage')][string]$Mode,
 [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
 [string]$CookDirectory)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
$active=@(Get-CimInstance Win32_Process -ErrorAction Stop|Where-Object {$_.Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|UnrealInsights|Uba).*'})
if($active.Count){throw 'Wait for normal engine/build completion.'}
$out="$projectRoot/Saved/Validation/Continuation_20261014/$Name"
if(Test-Path -LiteralPath $out){throw 'Preserve prior evidence.'}
New-Item -ItemType Directory -Path $out | Out-Null
if($Mode -eq 'Cook'){
 $arguments=@("$projectRoot/Project_J.uproject",'-run=Cook','-TargetPlatform=Windows','-Map=/Game/ThirdPerson/Lvl_ThirdPerson+/Engine/Maps/Entry',"-OutputDir=$out/Cooked/[Platform]",'-unattended','-nop4','-NoDDCCleanup','-NoLiveCoding',"-UserDir=$out/User","-ABSLOG=$out/Cook.log",'-UTF8Output')
 $arguments|ConvertTo-Json|Set-Content "$out/arguments.json" -Encoding utf8
 $process=Start-Process 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -ArgumentList (($arguments|ForEach-Object {'"'+$_+'"'}) -join ' ') -WindowStyle Hidden -PassThru
 $nativeHandle=$process.Handle
 $process.WaitForExit()
 [ordered]@{exitCode=$process.ExitCode;hasExited=$process.HasExited;output="$out/Cooked";scope='Windows default map + Entry + configured AssetManager/AlwaysCook closure. Prior Saved/Cooked and packages are preserved. Not CookAll unused assets.'}|ConvertTo-Json|Set-Content "$out/process.json" -Encoding utf8
 if($null -eq $process.ExitCode -or $process.ExitCode -ne 0){throw 'Cook completed with failure; retain first error.'}
}else{
 $CookDirectory=(Resolve-Path -LiteralPath $CookDirectory).Path
 if(!$CookDirectory.StartsWith((Resolve-Path "$projectRoot/Saved/Validation/Continuation_20261014").Path,[StringComparison]::OrdinalIgnoreCase)){throw 'Cook output must be an explicit continuation run.'}
 if(!(Test-Path -LiteralPath "$CookDirectory/Windows/ue.projectstore") -and !(Test-Path -LiteralPath "$CookDirectory/Windows/Project_J/Metadata/packagestore.manifest")){throw 'Official Windows package store layout missing; do not stage a double-wrapped or incomplete Cook.'}
 $arguments=@('BuildCookRun',"-project=$projectRoot/Project_J.uproject",'-noP4','-installed','-platform=Win64','-clientconfig=Development','-skipbuild','-skipcook','-nocompile','-nocompileuat',"-CookOutputDir=$CookDirectory",'-stage','-pak','-iostore','-package',"-stagingdirectory=$out/Package",'-utf8output','-unattended','-nocleanstage')
 $arguments|ConvertTo-Json|Set-Content "$out/arguments.json" -Encoding utf8
 & 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe' 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/DotNET/AutomationTool/AutomationTool.dll' @arguments *> "$out/Stage.log"
 $stageExit=$LASTEXITCODE
 [ordered]@{exitCode=$stageExit;scope='Stage/package only: direct UBT and separate Cook; unique directory, prior stages preserved.'}|ConvertTo-Json|Set-Content "$out/process.json" -Encoding utf8
 if($stageExit -ne 0){throw 'Stage completed with failure; retain first error.'}
}
