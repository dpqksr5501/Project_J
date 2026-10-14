param([ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Prefix='Reaudit_ABBA', [switch]$TracePair,
    [ValidateRange(1,3600)][int]$WarmupTicks=240, [ValidateRange(1,3600)][int]$MeasuredTicks=600)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../../..").Path
$baselineSet="$PSScriptRoot/BaselineBinaries"
$currentSet="$PSScriptRoot/FinalEditorBinaries"
$modules=(Get-Content "$projectRoot/Project_J.uproject" -Raw | ConvertFrom-Json).Modules
$files=@($modules | ForEach-Object {"UnrealEditor-$($_.Name).dll"})+@('UnrealEditor.modules')
function Assert-Idle {
    if(@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker,UnrealInsights -ErrorAction SilentlyContinue).Count){throw 'Wait for normal engine/build/analysis completion before complete-set switch.'}
}
function Install-Set([string]$SetPath){
    Assert-Idle
    foreach($file in $files){if(!(Test-Path -LiteralPath "$SetPath/$file")){throw "Incomplete set: $file"}}
    foreach($file in $files){Copy-Item -LiteralPath "$SetPath/$file" -Destination "$projectRoot/Binaries/Win64/$file" -Force}
    foreach($file in $files){if((Get-FileHash -LiteralPath "$SetPath/$file").Hash -ne (Get-FileHash -LiteralPath "$projectRoot/Binaries/Win64/$file").Hash){throw "Artifact mismatch: $file"}}
}
$phases=if($TracePair){@('A_trace','B_trace')}else{@('A1','B1','B2','A2')}
Assert-Idle
try{
    foreach($phase in $phases){
        $isBaseline=$phase.StartsWith('A')
        $set=if($isBaseline){$baselineSet}else{$currentSet}
        Install-Set $set
        $run="${Prefix}_$phase"
        & "$projectRoot/Scripts/Validation/Measure-AuthoredAnimation.ps1" -RunName $run -Counts 200 -Repeats 2 -WarmupTicks $WarmupTicks -MeasuredTicks $MeasuredTicks -NoDDCCleanup -Trace:$TracePair
        $out="$projectRoot/Saved/Validation/AuthoredAnimation_20261005/$run"
        [ordered]@{phase=$phase;baseline=$isBaseline;trace=[bool]$TracePair;binarySet=$set;allEightModulesAndModulesFileSwitched=$true;
            warmupTicks=$WarmupTicks;measuredTicks=$MeasuredTicks;workingSourceSnapshotMatchesCompiledBinary=(!$isBaseline);
            compiledSourceManifest=if($isBaseline){'Saved/Validation/Reaudit_20261014/BaselineManifest.json'}else{'Saved/Validation/Reaudit_20261014/FinalSourceHashes.json'};
            note='A is after DS01-18 before RA01-13. B is the frozen RA01-13 source. Identical current assets and invocation scripts across all variants. A binary does not match the fixture working-tree SourceSnapshot; use its compiled baseline manifest. Trace overhead is separate from no-trace ABBA.';
            artifacts=@($files|ForEach-Object{[ordered]@{file=$_;sha256=(Get-FileHash -LiteralPath "$projectRoot/Binaries/Win64/$_").Hash}})
        }|ConvertTo-Json -Depth 5|Set-Content "$out/comparison-provenance.json" -Encoding utf8
    }
}finally{
    Install-Set $currentSet
    Write-Output 'Restored complete final Editor set.'
}
