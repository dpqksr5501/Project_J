param([ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../../..").Path
if (@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count) {throw 'Wait for engine/build completion.'}
if(Test-Path -LiteralPath "$PSScriptRoot/$Name.log"){throw 'Preserve previous build evidence.'}
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' Project_JEditor Win64 Development "-Project=$projectRoot/Project_J.uproject" -WaitMutex -NoHotReloadFromIDE -NoUBA -DisableUnity -MaxParallelActions=4 "-Log=$PSScriptRoot/$Name.log"
if($LASTEXITCODE){throw 'Build failure retained; inspect the first compiler error.'}
