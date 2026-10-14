param([ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name='GameBuild1')
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../../..").Path
if(@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count){throw 'Wait for engine/build completion.'}
if(Test-Path "$PSScriptRoot/$Name.log"){throw 'Preserve previous evidence.'}
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' Project_J Win64 Development "-Project=$projectRoot/Project_J.uproject" -WaitMutex -NoHotReloadFromIDE -NoUBA -DisableUnity -MaxParallelActions=4 "-Log=$PSScriptRoot/$Name.log"
if($LASTEXITCODE){throw 'Game compiler failure retained; inspect first error.'}
