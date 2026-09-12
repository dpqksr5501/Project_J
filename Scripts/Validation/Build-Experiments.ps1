param(
 [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName,
 [string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\','/')
if(@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count){throw 'Wait for all engine/build processes to finish.'}
$out="$root/Saved/Validation/Experiments/Build/$RunName"
if(Test-Path -LiteralPath $out){throw 'Use a new RunName to preserve evidence.'}
New-Item -ItemType Directory $out|Out-Null
& "$EngineRoot/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" Project_JEditor Win64 Development "-Project=$root/Project_J.uproject" "-Plugin=$root/Plugins/ProjectJExperiments/ProjectJExperiments.uplugin" -WaitMutex -NoHotReloadFromIDE -NoUBA "-Log=$out/Build.log"
if($LASTEXITCODE -ne 0){throw "Build failed: $out/Build.log"}
