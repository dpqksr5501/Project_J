param([Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
 [ValidateSet('Project_JEditor','Project_J')][string]$Target='Project_JEditor',
 [ValidatePattern('^([A-Za-z_][A-Za-z0-9_]*(\+[A-Za-z_][A-Za-z0-9_]*)*)?$')][string]$Modules='',
 [switch]$DiagnoseIncremental,
 [switch]$NoSharedPCH)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path "$PSScriptRoot/../..").Path
$out = Join-Path $projectRoot "Saved/Validation/Continuation_20261014/$Name"
$active = @(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object { $_.Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|Uba).*' })
if ($active.Count) { throw 'Wait for engine/build completion.' }
if (Test-Path -LiteralPath $out) { throw 'Preserve prior evidence.' }
New-Item -ItemType Directory -Path $out | Out-Null
# UE 5.8 still constructs UBA storage with -NoUBA (ExecutorFactory.cs).
# Use its official root option inside this workspace; never change ProgramData permissions.
$arguments = @($Target,'Win64','Development',"-Project=$projectRoot/Project_J.uproject",'-WaitMutex','-NoHotReloadFromIDE','-NoUBA',"-UBARootDir=$projectRoot/Saved/UnrealBuildAccelerator",'-DisableUnity','-MaxParallelActions=4',"-Log=$out/UBT.log")
# Official UBT TargetDescriptor -Module selects module output actions. This is
# explicitly scoped validation, never a replacement receipt for a full target.
if ($Modules) { $arguments += "-Module=$Modules" }
if ($NoSharedPCH) { $arguments += '-NoSharedPCH' }
function Capture-IncrementalState([string]$Phase) {
 $intermediate = Join-Path $projectRoot 'Intermediate/Build/Win64'
 $items = @(Get-ChildItem -LiteralPath $intermediate -Recurse -File | Where-Object {
  $_.Extension -in @('.obj','.pch','.rsp') -or $_.Name -in @('Makefile.bin','ActionHistory.bin','DependencyCache.bin') -or $_.Name.EndsWith('.dep.json')
 } | ForEach-Object { [ordered]@{path=$_.FullName.Substring($projectRoot.Length+1).Replace('\','/');bytes=$_.Length;utc=$_.LastWriteTimeUtc.ToString('o')} })
 $items | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath "$out/Incremental$Phase.json" -Encoding utf8
}
if ($DiagnoseIncremental) {
 # Trace level records the actual ActionGraph stale prerequisite/attribute reasons.
 # Do not use -WriteOutdatedActions: UE5.8 deletes outdated products before export.
 $arguments += '-VeryVerbose'
 Capture-IncrementalState 'Before'
 $binaryBackup = Join-Path $out 'EditorBefore'
 New-Item -ItemType Directory -Path $binaryBackup | Out-Null
 $descriptor = Get-Content -LiteralPath "$projectRoot/Project_J.uproject" -Raw | ConvertFrom-Json
 foreach ($module in $descriptor.Modules) {
  Copy-Item -LiteralPath "$projectRoot/Binaries/Win64/UnrealEditor-$($module.Name).dll" -Destination $binaryBackup
 }
 Copy-Item -LiteralPath "$projectRoot/Binaries/Win64/UnrealEditor.modules" -Destination $binaryBackup
}
$arguments | ConvertTo-Json | Set-Content -LiteralPath "$out/arguments.json" -Encoding utf8
$started = Get-Date
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' @arguments
$buildExit = $LASTEXITCODE
if ($DiagnoseIncremental) { Capture-IncrementalState 'After' }
[ordered]@{exitCode=$buildExit; elapsedSeconds=((Get-Date)-$started).TotalSeconds; directUBT=$true; target=$Target; moduleScope=$Modules; fullTarget=(!$Modules)} | ConvertTo-Json | Set-Content -LiteralPath "$out/process.json" -Encoding utf8
if ($buildExit) { throw 'Build completed with failure; inspect first compiler/UBT error.' }
