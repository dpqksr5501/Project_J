param([Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
 [ValidatePattern('^[A-Za-z0-9_]+$')][string[]]$AssetTypes=@('Project_JLocomotionProfile','AnimMontage'),
 [string]$SelectionManifest)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
$out="$projectRoot/Saved/Validation/Continuation_20261014/$Name"
if(Test-Path -LiteralPath $out){throw 'Preserve evidence.'}
if(!$PSBoundParameters.ContainsKey('SelectionManifest')){New-Item -ItemType Directory -Path $out|Out-Null}
if($PSBoundParameters.ContainsKey('SelectionManifest')){
 if(!$SelectionManifest -or $PSBoundParameters.ContainsKey('AssetTypes')){throw 'Strict SelectionManifest is nonempty and cannot be combined with AssetTypes.'}
 . "$PSScriptRoot/AssetValidation-Provenance.ps1"
 $selection=Get-ProjectJSelectedAssetManifest -ProjectRoot $projectRoot -SelectionManifest $SelectionManifest
 $active=@(Get-CimInstance Win32_Process -ErrorAction Stop|Where-Object {$_.Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|UnrealInsights|Uba).*'})
 if($active.Count){throw 'Wait for normal process completion.'}
 $run="$out/Selected";New-Item -ItemType Directory -Path $run -Force|Out-Null
 $editor=Get-ProjectJAssetValidationEditorPath
 $beforeInputs=$afterInputs=$beforeSource=$afterSource=$summary=$null;$failure='';$status='failed'
 $process=$null;$exit=$null;$exited=$false;$processId=$null
 $arguments=@("$projectRoot/Project_J.uproject",'-run=Project_JAssetValidation',"-ProjectJAssetPaths=$($selection.assetPaths -join '+')",'-IncludeOnlyOnDiskAssets','-NullRHI','-unattended','-NoLiveCoding','-NoDDCCleanup','-nop4','-nosound',"-UserDir=$run/User","-ABSLOG=$run/Run.log",'-UTF8Output','-culture=en')
 $arguments|ConvertTo-Json|Set-Content "$run/arguments.json" -Encoding utf8
 try{
  $beforeInputs=@(Get-ProjectJSelectedInputHashes -ProjectRoot $projectRoot -Selection $selection)
  $beforeSource=Get-ProjectJSourceProvenance -ProjectRoot $projectRoot -RuntimePaths @($editor)
  $process=Start-Process $editor -ArgumentList (($arguments|ForEach-Object{'"'+$_+'"'}) -join ' ') -WindowStyle Hidden -PassThru
  $nativeHandle=$process.Handle;$processId=$process.Id;$process.WaitForExit();$exited=$process.HasExited
  if($exited){$exit=$process.ExitCode}
  if(!$exited -or $null -eq $exit -or $exit -ne 0){throw 'No successful completed native selected-validation process.'}
  $summary=Get-ProjectJStrictValidationLog -LogPath "$run/Run.log" -AssetPaths $selection.assetPaths
  $current=Get-ProjectJSelectedAssetManifest -ProjectRoot $projectRoot -SelectionManifest $SelectionManifest
  if($current.manifestSHA256 -cne $selection.manifestSHA256){throw 'Selection manifest changed during native validation.'}
  $afterInputs=@(Get-ProjectJSelectedInputHashes -ProjectRoot $projectRoot -Selection $current)
  if((Get-ProjectJSelectedInputSignature $beforeInputs) -cne (Get-ProjectJSelectedInputSignature $afterInputs)){throw 'Selected package input changed during native validation.'}
  $afterSource=Get-ProjectJSourceProvenance -ProjectRoot $projectRoot -RuntimePaths @($editor)
  Assert-ProjectJMatchingProvenance -Provenance @($beforeSource,$afterSource)
  $status='passed'
 }catch{$failure=$_.Exception.Message}
 $processStatus=if($status -eq 'passed'){'passed'}else{'failed-or-incomplete'}
 [ordered]@{schema=1;mode='strict-selected';processId=$processId;hasExited=$exited;exitCode=$exit;validationStatus=$processStatus;summary=$summary;failure=$failure}|ConvertTo-Json -Depth 6|Set-Content "$run/process.json" -Encoding utf8
 $artifactHash={param($path)if(Test-Path -LiteralPath $path -PathType Leaf){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}else{$null}}
 [ordered]@{schema=1;kind='projectj_selected_asset_validation';status=$status;projectRoot=(Resolve-Path -LiteralPath $projectRoot).Path;validationRun=(Resolve-Path -LiteralPath $out).Path;manifestPath=$selection.manifestPath;manifestSHA256=$selection.manifestSHA256;assetPaths=@($selection.assetPaths);inputsBefore=$beforeInputs;inputsAfter=$afterInputs;sourceProvenanceBefore=$beforeSource;sourceProvenanceAfter=$afterSource;editorExecutable=$editor;native=[ordered]@{processSHA256=(& $artifactHash "$run/process.json");argumentsSHA256=(& $artifactHash "$run/arguments.json");logSHA256=(& $artifactHash "$run/Run.log");processId=$processId;hasExited=$exited;exitCode=$exit;summary=$summary};completedUTC=[DateTime]::UtcNow.ToString('o');failure=$failure;scope='Declared/auto target saved package inputs only; not a transitive asset dependency closure proof.'}|ConvertTo-Json -Depth 14|Set-Content "$out/SelectedValidation.json" -Encoding utf8
 if($status -ne 'passed'){throw "Strict selected asset validation failed: $failure"}
 Assert-ProjectJSelectedAssetValidation -ProjectRoot $projectRoot -ValidationRunDirectory $out -SelectionManifest $SelectionManifest
 return
}
$failures=@()
foreach($assetType in $AssetTypes){
 $active=@(Get-CimInstance Win32_Process -ErrorAction Stop|Where-Object {$_.Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|UnrealInsights|Uba).*'})
 if($active.Count){throw 'Wait for normal process completion.'}
 $run="$out/$assetType"
 New-Item -ItemType Directory -Path $run|Out-Null
 $arguments=@("$projectRoot/Project_J.uproject",'-run=Project_JAssetValidation',"-AssetType=$assetType",'-IncludeOnlyOnDiskAssets','-NullRHI','-unattended','-NoLiveCoding','-NoDDCCleanup','-nop4','-nosound',"-UserDir=$run/User","-ABSLOG=$run/Run.log",'-UTF8Output','-culture=en')
 $arguments|ConvertTo-Json|Set-Content "$run/arguments.json" -Encoding utf8
 $process=Start-Process 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -ArgumentList (($arguments|ForEach-Object{'"'+$_+'"'}) -join ' ') -WindowStyle Hidden -PassThru
 $nativeHandle=$process.Handle
 $process.WaitForExit()
 $summary=$null
 $validationWarnings=$null
 $summaryLines=@()
 if(Test-Path -LiteralPath "$run/Run.log"){
  $validationLog=Get-Content -LiteralPath "$run/Run.log" -Raw
  $warningLines=[regex]::Matches($validationLog,'Validation Warnings: (\d+)')
  if($warningLines.Count){$validationWarnings=[int]$warningLines[$warningLines.Count-1].Groups[1].Value}
  $summaryLines=[regex]::Matches($validationLog,'Files Checked: (\d+), Passed: (\d+), Failed: (\d+), Skipped: (\d+), Unable to validate: (\d+)')
  if($summaryLines.Count){
   $last=$summaryLines[$summaryLines.Count-1]
   $summary=[ordered]@{checked=[int]$last.Groups[1].Value;passed=[int]$last.Groups[2].Value;failed=[int]$last.Groups[3].Value;skipped=[int]$last.Groups[4].Value;unableToValidate=[int]$last.Groups[5].Value}
  }
 }
 $validationStatus='failed-or-incomplete'
 if($process.ExitCode -eq 0 -and $summary -and $null -ne $validationWarnings -and $summary.checked -gt 0 -and $summary.failed -eq 0 -and $summary.passed -gt 0 -and $summary.unableToValidate -eq 0){
  $validationStatus=if($summary.skipped){'partial'}elseif($validationWarnings -gt 0){'passed-with-warnings'}else{'passed'}
 }
 [ordered]@{exitCode=$process.ExitCode;hasExited=$process.HasExited;assetType=$assetType;validationStatus=$validationStatus;validationSummary=$summary;validationWarnings=$validationWarnings;scope='Engine DataValidation selection/validators with completion-count override; exact class/subclasses; no asset edits/saves.'}|ConvertTo-Json -Depth 4|Set-Content "$run/process.json" -Encoding utf8
 # A missing actual exit receipt cannot authorize starting another engine process.
 if($null -eq $process.ExitCode -or !$process.HasExited){throw 'No actual completed exit receipt; preserve evidence and inspect process state.'}
 if($process.ExitCode -ne 0 -or $null -eq $summary -or $null -eq $validationWarnings -or $summary.checked -le 0 -or $summary.failed -gt 0 -or $summary.passed -le 0 -or $summary.unableToValidate -gt 0){$failures+=$assetType}
 if($summaryLines.Count){$summaryLines[$summaryLines.Count-1].Value}
}
if($failures.Count){throw "Native asset validation failed/incomplete: $($failures -join ', '). Inspect retained process.json and Run.log."}
