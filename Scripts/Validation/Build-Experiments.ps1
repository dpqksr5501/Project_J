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
# Build the complete project target with the opt-in plugin. A foreign -Plugin
# build narrows the target makefile and can hide stale project objects on UE 5.8.
& "$EngineRoot/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" Project_JEditor Win64 Development "-Project=$root/Project_J.uproject" -EnablePlugin=ProjectJExperiments -WaitMutex -NoHotReloadFromIDE -NoUBA "-Log=$out/Build.log"
if($LASTEXITCODE -ne 0){throw "Build failed: $out/Build.log"}
if((Get-Item "$root/Plugins/ProjectJExperiments/Binaries/Win64/UnrealEditor-ProjectJExperiments.dll").LastWriteTimeUtc -lt
   (Get-ChildItem "$root/Plugins/ProjectJExperiments/Source" -Recurse -File|Sort-Object LastWriteTimeUtc -Descending|Select-Object -First 1).LastWriteTimeUtc){throw 'Experiments binary predates its source; plugin was not rebuilt.'}
