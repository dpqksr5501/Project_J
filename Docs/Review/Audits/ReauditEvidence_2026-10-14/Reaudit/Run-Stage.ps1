param([ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name='Packaged1')
$ErrorActionPreference='Stop'
if(@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count){throw 'Wait for engine/build completion.'}
$projectRoot=(Resolve-Path "$PSScriptRoot/../../..").Path
$stage="$PSScriptRoot/$Name"
if(Test-Path $stage){throw 'Preserve previous evidence.'}
$arguments=@('BuildCookRun',"-project=$projectRoot/Project_J.uproject",'-noP4','-installed','-platform=Win64','-clientconfig=Development','-skipbuild','-skipcook','-nocompile','-nocompileuat','-stage','-pak','-iostore','-package',"-stagingdirectory=$stage",'-utf8output','-unattended','-nocleanstage')
$arguments | ConvertTo-Json | Set-Content "$PSScriptRoot/${Name}_Arguments.json" -Encoding utf8
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe' 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/DotNET/AutomationTool/AutomationTool.dll' @arguments *> "$PSScriptRoot/${Name}_Stage.log"
$exitCode=$LASTEXITCODE
@{exitCode=$exitCode;scope='Stage/package only. C++ direct UBT and separate Cook; fresh directory, no clean of prior stages.'} | ConvertTo-Json | Set-Content "$PSScriptRoot/${Name}_Stage.json" -Encoding utf8
if($exitCode -ne 0){throw 'Stage failure retained; inspect first error.'}
