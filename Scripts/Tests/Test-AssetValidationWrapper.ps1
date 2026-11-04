# Native process boundaries are mocked; no Unreal process or real asset is opened.
$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path "$PSScriptRoot/../..").Path
$testRoot = Join-Path $repository "Saved/ArchitectureBatch144CookValidation/WrapperTests/$([guid]::NewGuid().ToString('N'))"
$mockRoot = Join-Path $testRoot 'Project'
$wrapper = Join-Path $mockRoot 'Scripts/Audit/Run-ContinuationAssetValidation.ps1'
New-Item -ItemType Directory -Force (Split-Path $wrapper -Parent) | Out-Null
Copy-Item -LiteralPath "$repository/Scripts/Audit/Run-ContinuationAssetValidation.ps1" -Destination $wrapper
$mockState = @{Starts=0;Scenario='Success'}
function Get-CimInstance { [CmdletBinding()] param([string]$ClassName) }
function Start-Process {
 [CmdletBinding()] param([string]$FilePath,[string]$ArgumentList,[string]$WindowStyle,[switch]$PassThru)
 $mockState.Starts++
 if ($WindowStyle -ne 'Hidden') { throw 'Expected a hidden process.' }
 $log = [regex]::Match($ArgumentList,'"-ABSLOG=([^"]+)"').Groups[1].Value
 if (!$log) { throw 'No log argument captured.' }
 $assetMatch=[regex]::Match($ArgumentList,'"-ProjectJAssetPaths=([^"]*)"')
 if($assetMatch.Success){
  $assets=@($assetMatch.Groups[1].Value -split '\+');$n=$assets.Count;$exit=0;$exited=$true
  $checked=$passed=$requested=$n;$failed=$skipped=$unable=$warn=$limit=0
  switch($mockState.Scenario){
   'Nonzero'{$exit=1};'NullExit'{$exit=$null};'NotExited'{$exited=$false};'ZeroChecked'{$checked=$passed=0}
   'PartialSkipped'{$passed--; $skipped=1};'Unable'{$passed--; $unable=1};'Warnings'{$warn=1};'Limit'{$limit=1};'RequestedMismatch'{$requested++}
   'FailedCounter'{$failed=1;$passed--};'InputChanged'{Add-Content -LiteralPath "$mockRoot/Content/Chosen/A.uasset" -Value 'changed'}
   'SourceChanged'{$mockState.SourceVersion='changed'}
  }
  $lines=@("LogTemp: Display: ProjectJ Selected Assets: $n")
  foreach($a in $assets){$lines+="LogTemp: Display: ProjectJ Selected Asset: $a"}
  $lines+="LogTemp: Display: Files Checked: $checked, Passed: $passed, Failed: $failed, Skipped: $skipped, Unable to validate: $unable"
  $lines+="LogTemp: Display: Validation Warnings: $warn, Requested: $requested, Asset limit reached: $limit"
  # An engine MessageLog summary can coexist; strict completion belongs to LogTemp Display only.
  $lines+="AssetCheck: Files Checked: 999, Passed: 999, Failed: 0, Skipped: 0, Unable to validate: 0"
  if($mockState.Scenario -eq 'MissingSummary'){$lines=@($lines|Where-Object {$_ -notlike 'LogTemp: Display: Files Checked:*'})}
  if($mockState.Scenario -eq 'MissingWarnings'){$lines=@($lines|Where-Object {$_ -notlike 'LogTemp: Display: Validation Warnings:*'})}
  if($mockState.Scenario -eq 'MissingSelection'){$lines=@($lines|Where-Object {$_ -notlike 'LogTemp: Display: ProjectJ Selected Asset:*'})}
  Set-Content -LiteralPath $log -Value $lines -Encoding utf8
  $process=[pscustomobject]@{ExitCode=$exit;HasExited=$exited;Handle=1;Id=12345};$process|Add-Member ScriptMethod WaitForExit {};return $process
 }
 $exit = 0
 $summary = 'Files Checked: 2, Passed: 2, Failed: 0, Skipped: 0, Unable to validate: 0'
 if ($mockState.Starts -eq 1) {
  switch ($mockState.Scenario) {
   'Nonzero' { $exit = 1 }
   'NullExit' { $exit = $null }
   'MissingSummary' { $summary = 'Commandlet ended without validation counters' }
   'ZeroChecked' { $summary = 'Files Checked: 0, Passed: 0, Failed: 0, Skipped: 0, Unable to validate: 0' }
   'AllSkipped' { $summary = 'Files Checked: 2, Passed: 0, Failed: 0, Skipped: 2, Unable to validate: 0' }
   'Unable' { $summary = 'Files Checked: 2, Passed: 1, Failed: 0, Skipped: 0, Unable to validate: 1' }
   'PartialSkipped' { $summary = 'Files Checked: 2, Passed: 1, Failed: 0, Skipped: 1, Unable to validate: 0' }
   'FailedCounter' { $summary = 'Files Checked: 2, Passed: 1, Failed: 1, Skipped: 0, Unable to validate: 0' }
  }
 }
 $warningCount=if($mockState.Scenario -eq 'Warnings'){2}else{0}
 $logLines=@($summary)
 if($mockState.Scenario -ne 'MissingWarnings'){$logLines+="Validation Warnings: $warningCount, Requested: 2, Asset limit reached: 0"}
 Set-Content -LiteralPath $log -Value $logLines -Encoding utf8
 $process = [pscustomobject]@{ ExitCode=$exit; HasExited=$true; Handle=1; Id=12345 }
 $process | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value {}
 return $process
}
$failures = @()
foreach ($scenario in @('Success','Warnings','Nonzero','NullExit','MissingSummary','MissingWarnings','ZeroChecked','FailedCounter','AllSkipped','Unable','PartialSkipped')) {
 $mockState.Starts = 0
 $mockState.Scenario = $scenario
 $threw = $false
 try { & $wrapper -Name $scenario -AssetTypes @('First','Second') | Out-Null }
 catch { $threw = $true }
 $expectedFailure = $scenario -notin @('Success','Warnings','PartialSkipped')
 $receipt = "$mockRoot/Saved/Validation/Continuation_20261014/$scenario/First/process.json"
 if ($threw -ne $expectedFailure -or !(Test-Path -LiteralPath $receipt)) {
  $failures += $scenario
 }
 if($scenario -eq 'PartialSkipped' -and (Get-Content -LiteralPath $receipt -Raw|ConvertFrom-Json).validationStatus -ne 'partial'){$failures+='PartialMisreported'}
 if($scenario -eq 'Unable' -and (Get-Content -LiteralPath $receipt -Raw|ConvertFrom-Json).validationStatus -ne 'failed-or-incomplete'){$failures+='UnableMisreported'}
 if($scenario -eq 'Warnings' -and ((Get-Content -LiteralPath $receipt -Raw|ConvertFrom-Json).validationStatus -ne 'passed-with-warnings' -or (Get-Content -LiteralPath $receipt -Raw|ConvertFrom-Json).validationWarnings -ne 2)){$failures+='WarningsMisreported'}
 if ($scenario -eq 'Nonzero' -and $mockState.Starts -ne 2) { $failures += 'NonzeroDidNotRetainBothRuns' }
 [pscustomobject]@{scenario=$scenario;expectedFailure=$expectedFailure;threw=$threw;starts=$mockState.Starts;receipt=(Test-Path -LiteralPath $receipt)}
}
if ($failures.Count) { throw "Wrapper regression failed: $($failures -join ', ')" }

# Strict selected-mode boundaries use only disposable package bytes and synthetic source/runtime provenance.
New-Item -ItemType Directory -Force "$mockRoot/Scripts/Validation","$mockRoot/Content/Chosen"|Out-Null
Copy-Item -LiteralPath "$repository/Scripts/Audit/AssetValidation-Provenance.ps1" -Destination "$mockRoot/Scripts/Audit/AssetValidation-Provenance.ps1"
Copy-Item -LiteralPath "$repository/Scripts/Validation/Source-Provenance.ps1" -Destination "$mockRoot/Scripts/Validation/Source-Provenance.ps1"
. "$mockRoot/Scripts/Validation/Source-Provenance.ps1"
$mockState.SourceVersion='same'
function Get-ProjectJSourceProvenance {
 param([string]$ProjectRoot,[string[]]$RuntimePaths)
 [ordered]@{schema=1;head=('a'*40);files=@([ordered]@{path='Source/Mock.cpp';sha256=if($mockState.SourceVersion -eq 'same'){'b'*64}else{'d'*64};deleted=$false});binaries=@([ordered]@{path='EngineHost/UnrealEditor-Cmd.exe';sha256=('c'*64)})}
}
. "$mockRoot/Scripts/Audit/AssetValidation-Provenance.ps1"
$selectionPath=Join-Path $testRoot 'selection.json'
$selection=[ordered]@{schema=1;assetPaths=@('/Game/Chosen/A.A','/Game/Chosen/B.B');inputFiles=@('Content/Chosen/Dependency.uasset')}
$selection|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $selectionPath -Encoding utf8
foreach($scenario in @('Success','Nonzero','NullExit','NotExited','MissingSummary','MissingWarnings','MissingSelection','ZeroChecked','FailedCounter','PartialSkipped','Unable','Warnings','Limit','RequestedMismatch','InputChanged','SourceChanged')){
 foreach($file in @('A','B','Dependency')){Set-Content -LiteralPath "$mockRoot/Content/Chosen/$file.uasset" -Value "original-$file" -Encoding utf8}
 $mockState.SourceVersion='same';$mockState.Starts=0;$mockState.Scenario=$scenario;$threw=$false
 try{& $wrapper -Name "Strict$scenario" -SelectionManifest $selectionPath|Out-Null}catch{$threw=$true}
 $expected=$scenario -ne 'Success';$run="$mockRoot/Saved/Validation/Continuation_20261014/Strict$scenario"
 if($threw -ne $expected -or $mockState.Starts -ne 1 -or !(Test-Path -LiteralPath "$run/Selected/process.json") -or !(Test-Path -LiteralPath "$run/SelectedValidation.json")){$failures+="Strict$scenario"}
 if(!$expected){
  $admission=& { Set-StrictMode -Version Latest; Assert-ProjectJSelectedAssetValidation -ProjectRoot $mockRoot -ValidationRunDirectory $run -SelectionManifest $selectionPath }
  if($admission.assetCount -ne 2 -or $admission.inputCount -ne 3 -or $admission.status -ne 'passed'){$failures+='StrictAdmissionShape'}
 }else{
  $accepted=$true;try{Assert-ProjectJSelectedAssetValidation -ProjectRoot $mockRoot -ValidationRunDirectory $run -SelectionManifest $selectionPath|Out-Null}catch{$accepted=$false}
  if($accepted){$failures+="CookAccepted$scenario"}
 }
 [pscustomobject]@{scenario="Strict$scenario";expectedFailure=$expected;threw=$threw;starts=$mockState.Starts}
}
$mockState.SourceVersion='same';foreach($file in @('A','B','Dependency')){Set-Content -LiteralPath "$mockRoot/Content/Chosen/$file.uasset" -Value "original-$file" -Encoding utf8}
$successRun="$mockRoot/Saved/Validation/Continuation_20261014/StrictSuccess"
$receiptPath="$successRun/SelectedValidation.json";$receiptBytes=[IO.File]::ReadAllBytes($receiptPath);$manifestBytes=[IO.File]::ReadAllBytes($selectionPath)
foreach($scenario in @('MissingRun','FailedReceipt','PartialReceipt','LimitReceipt','WarningsReceipt','StaleManifest','ChangedInput','ChangedSource','ChangedNativeLog','MissingNativeReceipt')){
 $run=$successRun;$nativePath="$successRun/Selected/process.json";$nativeBytes=[IO.File]::ReadAllBytes($nativePath);$logPath="$successRun/Selected/Run.log";$logBytes=[IO.File]::ReadAllBytes($logPath)
 $receipt=[Text.Encoding]::UTF8.GetString($receiptBytes)|ConvertFrom-Json
 switch($scenario){
  'MissingRun'{$run="$successRun-Missing"};'FailedReceipt'{$receipt.status='failed'};'PartialReceipt'{$receipt.native.summary.passed=1}
  'LimitReceipt'{$receipt.native.summary.assetLimitReached=$true};'WarningsReceipt'{$receipt.native.summary.warnings=1}
  'StaleManifest'{Add-Content -LiteralPath $selectionPath -Value ' '};'ChangedInput'{Add-Content -LiteralPath "$mockRoot/Content/Chosen/A.uasset" -Value 'changed'}
  'ChangedSource'{$mockState.SourceVersion='changed'};'ChangedNativeLog'{Add-Content -LiteralPath $logPath -Value 'changed'};'MissingNativeReceipt'{Remove-Item -LiteralPath $nativePath}
 }
 $receipt|ConvertTo-Json -Depth 14|Set-Content -LiteralPath $receiptPath -Encoding utf8
 $beforeStarts=$mockState.Starts;$rejected=$false
 try{Assert-ProjectJSelectedAssetValidation -ProjectRoot $mockRoot -ValidationRunDirectory $run -SelectionManifest $selectionPath|Out-Null}catch{$rejected=$true}
 if(!$rejected -or $mockState.Starts -ne $beforeStarts){$failures+=$scenario}
 [IO.File]::WriteAllBytes($receiptPath,$receiptBytes);[IO.File]::WriteAllBytes($selectionPath,$manifestBytes);[IO.File]::WriteAllBytes($nativePath,$nativeBytes);[IO.File]::WriteAllBytes($logPath,$logBytes)
 Set-Content -LiteralPath "$mockRoot/Content/Chosen/A.uasset" -Value 'original-A' -Encoding utf8;$mockState.SourceVersion='same'
 [pscustomobject]@{scenario=$scenario;expectedRejected=$true;rejected=$rejected;nativeStarts=$mockState.Starts}
}
foreach($scenario in @('Empty','Unknown','Duplicate','Traversal','Extension','MissingInput','DuplicateInputs','EmptyInputs')){
 $bad=[ordered]@{schema=1;assetPaths=@('/Game/Chosen/A.A','/Game/Chosen/B.B');inputFiles=@('Content/Chosen/Dependency.uasset')}
 switch($scenario){'Empty'{$bad.assetPaths=@()};'Unknown'{$bad.extra=$true};'Duplicate'{$bad.assetPaths=@('/Game/Chosen/A.A','/Game/Chosen/A.A')};'Traversal'{$bad.inputFiles=@('Content/../outside.uasset')};'Extension'{$bad.inputFiles=@('Content/Chosen/script.ps1')};'MissingInput'{$bad.inputFiles=@('Content/Chosen/Missing.uasset')};'DuplicateInputs'{$bad.inputFiles=@('Content/Chosen/A.uasset','Content/Chosen/A.uasset')};'EmptyInputs'{$bad.inputFiles=@()}}
 $bad|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $selectionPath -Encoding utf8;$mockState.Starts=0;$rejected=$false
 try{& $wrapper -Name "Invalid$scenario" -SelectionManifest $selectionPath|Out-Null}catch{$rejected=$true}
 if(!$rejected -or $mockState.Starts){$failures+="Invalid$scenario"}
 [pscustomobject]@{scenario="Invalid$scenario";expectedRejected=$true;rejected=$rejected;starts=$mockState.Starts}
}
# A selected package can have either saved payload extension; never guess from its asset name.
New-Item -ItemType Directory -Force "$mockRoot/Content/Payload"|Out-Null
foreach($scenario in @('MapDeclared','MapAuto','AmbiguousPayload','MissingPayload')){
 $package="Content/Payload/$scenario";$asset="/Game/Payload/$scenario.$scenario"
 if($scenario -ne 'MissingPayload'){Set-Content -LiteralPath "$mockRoot/$package.umap" -Value 'synthetic world package bytes' -Encoding utf8}
 if($scenario -eq 'AmbiguousPayload'){Set-Content -LiteralPath "$mockRoot/$package.uasset" -Value 'ambiguous second package payload' -Encoding utf8}
 $declared=if($scenario -eq 'MapDeclared'){@($package+'.umap')}else{@('Content/Chosen/Dependency.uasset')}
 [ordered]@{schema=1;assetPaths=@($asset);inputFiles=@($declared)}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $selectionPath -Encoding utf8
 $mockState.Starts=0;$mockState.SourceVersion='same';$mockState.Scenario='Success';$rejected=$false
 try{& $wrapper -Name $scenario -SelectionManifest $selectionPath|Out-Null}catch{$rejected=$true}
 $expected=$scenario -in @('AmbiguousPayload','MissingPayload')
 if($rejected -ne $expected -or $mockState.Starts -ne [int](!$expected)){$failures+=$scenario}
 if(!$expected -and !$rejected){
  $resolved=& { Set-StrictMode -Version Latest; Get-ProjectJSelectedAssetManifest -ProjectRoot $mockRoot -SelectionManifest $selectionPath }
  $expectedInputs=if($scenario -eq 'MapDeclared'){@($package+'.umap')}else{@('Content/Chosen/Dependency.uasset',($package+'.umap'))}
  $admission=Assert-ProjectJSelectedAssetValidation -ProjectRoot $mockRoot -ValidationRunDirectory "$mockRoot/Saved/Validation/Continuation_20261014/$scenario" -SelectionManifest $selectionPath
  if(($resolved.inputFiles -join "`n") -cne ($expectedInputs -join "`n") -or $admission.assetCount -ne 1 -or $admission.inputCount -ne $expectedInputs.Count){$failures+="${scenario}ResolvedPayload"}
 }
 [pscustomobject]@{scenario=$scenario;expectedRejected=$expected;rejected=$rejected;starts=$mockState.Starts}
}
if($failures.Count){throw "Selected validation regression failed: $($failures -join ', ')"}
