param(
 [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
 [Parameter(Mandatory)][string]$BaselineSet,
 [Parameter(Mandatory)][string]$BaselineSourceManifest,
 [string]$ComparisonSet,
 [string]$ComparisonSourceManifest,
 [ValidateSet('CPU','Render')][string]$Workload='CPU',
 [ValidateRange(1,3600)][int]$WarmupTicks=240,
 [ValidateRange(1,3600)][int]$MeasuredTicks=600,
 [switch]$TracePair
)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../..").Path
$out=Join-Path $projectRoot "Saved/Validation/Continuation_20261014/$Name"
$moduleNames=(Get-Content "$projectRoot/Project_J.uproject" -Raw | ConvertFrom-Json).Modules.Name
$files=@($moduleNames | ForEach-Object {"UnrealEditor-$_.dll"})+@('UnrealEditor.modules')
function Assert-Idle {
 $active=@(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {$_.Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|UnrealInsights|Uba).*'})
 if($active.Count){throw 'Engine/build/trace analysis is active; wait for normal completion.'}
}
function Get-SetHashes([string]$Path) {
 foreach($file in $files){
  $artifact=Join-Path $Path $file
  if(!(Test-Path -LiteralPath $artifact -PathType Leaf)){throw "Incomplete Editor set: $artifact"}
  [ordered]@{file=$file;sha256=(Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash}
 }
}
function Install-Set([string]$Path,$Expected) {
 Assert-Idle
 $actual=@(Get-SetHashes $Path)
 foreach($row in $actual){if($row.sha256 -ne ($Expected|Where-Object file -EQ $row.file).sha256){throw 'Stored binary set changed.'}}
 foreach($file in $files){Copy-Item -LiteralPath (Join-Path $Path $file) -Destination "$projectRoot/Binaries/Win64/$file" -Force}
 foreach($row in @(Get-SetHashes "$projectRoot/Binaries/Win64")){if($row.sha256 -ne ($Expected|Where-Object file -EQ $row.file).sha256){throw 'Installed binary hash mismatch.'}}
}
Assert-Idle
if(Test-Path -LiteralPath $out){throw 'Use a new evidence name.'}
$BaselineSet=(Resolve-Path -LiteralPath $BaselineSet).Path
$BaselineSourceManifest=(Resolve-Path -LiteralPath $BaselineSourceManifest).Path
$baselineHashes=@(Get-SetHashes $BaselineSet)
New-Item -ItemType Directory -Path "$out/RestoreEditorSet" | Out-Null
$restoreHashes=@(Get-SetHashes "$projectRoot/Binaries/Win64")
foreach($file in $files){Copy-Item -LiteralPath "$projectRoot/Binaries/Win64/$file" -Destination "$out/RestoreEditorSet/$file"}
$restoreHashes|ConvertTo-Json|Set-Content "$out/RestoreEditorHashes.json" -Encoding utf8
Copy-Item -LiteralPath $BaselineSourceManifest -Destination "$out/BaselineCompiledSourceManifest.json"
$explicitComparison = ![string]::IsNullOrWhiteSpace($ComparisonSet)
if($explicitComparison -ne (![string]::IsNullOrWhiteSpace($ComparisonSourceManifest))){throw 'An explicit B set requires its compiled source manifest.'}
$compiledSources=@(& git -c core.fsmonitor=false ls-files --cached --others --exclude-standard -- Source Plugins/ProjectJExperiments/Source Config Project_J.uproject Scripts)
$sourceHashes=@($compiledSources|Sort-Object -Unique|ForEach-Object {
 if(Test-Path -LiteralPath "$projectRoot/$_" -PathType Leaf){[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath "$projectRoot/$_").Hash}}
})
$sourceHashes|ConvertTo-Json|Set-Content "$out/CurrentCompiledSourceManifest.json" -Encoding utf8
if($explicitComparison){
 $ComparisonSet=(Resolve-Path -LiteralPath $ComparisonSet).Path
 $ComparisonSourceManifest=(Resolve-Path -LiteralPath $ComparisonSourceManifest).Path
 $comparisonHashes=@(Get-SetHashes $ComparisonSet)
 Copy-Item -LiteralPath $ComparisonSourceManifest -Destination "$out/ComparedCompiledSourceManifest.json"
}else{
 $ComparisonSet="$out/RestoreEditorSet"
 $comparisonHashes=$restoreHashes
 Copy-Item -LiteralPath "$out/CurrentCompiledSourceManifest.json" -Destination "$out/ComparedCompiledSourceManifest.json"
}
# The current set is captured at invocation, never a hard-coded RA13 restoration.
$phases=if($TracePair){@('A_trace','B_trace')}else{@('A1','B1','B2','A2')}
[ordered]@{order=$phases;workload=$Workload;baselineSet=$BaselineSet;currentSet=$ComparisonSet;restoreSet="$out/RestoreEditorSet";explicitComparedB=$explicitComparison;warmupTicks=$WarmupTicks;measuredTicks=$MeasuredTicks;
 scope=if($Workload -eq 'CPU'){'NullRHI CPU; 200 authored BP actors; zero remote sockets.'}else{'D3D12 offscreen 200 body actors; fixed camera/light/resolution/quality; zero remote sockets.'};
 trace=[bool]$TracePair;restoration='Traced pair is separate from no-trace ABBA. All nine Editor artifacts restored to invocation version.';
 baselineArtifacts=$baselineHashes;currentArtifacts=$comparisonHashes;restoreArtifacts=$restoreHashes}|ConvertTo-Json -Depth 6|Set-Content "$out/ComparisonManifest.json" -Encoding utf8
try {
 foreach($phase in $phases){
  $baseline=$phase.StartsWith('A')
  if($baseline){Install-Set $BaselineSet $baselineHashes}else{Install-Set $ComparisonSet $comparisonHashes}
  $run="${Name}_$phase"
  if($Workload -eq 'CPU'){
   & "$projectRoot/Scripts/Validation/Measure-AuthoredAnimation.ps1" -RunName $run -Counts 200 -Repeats 2 -WarmupTicks $WarmupTicks -MeasuredTicks $MeasuredTicks -NoDDCCleanup -Trace:$TracePair
   $result="$projectRoot/Saved/Validation/AuthoredAnimation_20261005/$run"
  }else{
   & "$projectRoot/Scripts/Validation/Measure-RenderedCrowd.ps1" -RunName $run -Trace:$TracePair -DisableEditorPython
   $result="$projectRoot/Saved/Validation/WholeProjectAudit_20261013/$run"
  }
  [ordered]@{phase=$phase;baseline=$baseline;compiledSourceManifest=if($baseline){"$out/BaselineCompiledSourceManifest.json"}else{"$out/ComparedCompiledSourceManifest.json"};
   installedArtifacts=@(Get-SetHashes "$projectRoot/Binaries/Win64");workingSourceMatchesCompiledVariant=(!$baseline -and !$explicitComparison);
   meaning='Fixture SourceSnapshot is the working tree; explicit historical A/B provenance uses the compiled manifests. Restoration always uses invocation-time artifacts.'}|ConvertTo-Json -Depth 6|Set-Content "$result/comparison-provenance.json" -Encoding utf8
 }
} finally {
 # Refuse to switch an artifact if a host process remains after an unexpected harness exception.
 Install-Set "$out/RestoreEditorSet" $restoreHashes
 [ordered]@{restored=$true;artifacts=@(Get-SetHashes "$projectRoot/Binaries/Win64")}|ConvertTo-Json -Depth 5|Set-Content "$out/RestoreReceipt.json" -Encoding utf8
}
