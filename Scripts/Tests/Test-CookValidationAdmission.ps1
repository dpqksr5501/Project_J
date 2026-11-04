param([string]$ProjectRoot,[string]$ConsumerRoot)
# Consumer wiring only: no engine, dotnet, UAT, Python or real asset is executed.
$ErrorActionPreference='Stop'
if(!$ProjectRoot){$ProjectRoot=(Resolve-Path "$PSScriptRoot/../..").Path}
$ProjectRoot=(Resolve-Path -LiteralPath $ProjectRoot).Path
if(!(Test-Path -LiteralPath "$ProjectRoot/Project_J.uproject" -PathType Leaf)){throw 'Pass the repository ProjectRoot when running a Saved candidate.'}
if(!$ConsumerRoot){$ConsumerRoot=$ProjectRoot}
$source=(Resolve-Path -LiteralPath "$ConsumerRoot/Scripts/Audit/Run-ContinuationPackaging.ps1").Path
$testRoot=Join-Path $ProjectRoot "Saved/ArchitectureBatch144CookValidation/ConsumerMockTests/$([guid]::NewGuid().ToString('N'))"
$mockRoot=Join-Path $testRoot 'Project'
$audit=Join-Path $mockRoot 'Scripts/Audit'
New-Item -ItemType Directory -Force -Path $audit | Out-Null
$wrapper=Join-Path $audit 'Run-ContinuationPackaging.ps1'
$text=Get-Content -LiteralPath $source -Raw
# Absolute engine calls bypass command-name mocks. Replace the unexecuted Stage
# pipeline in the isolated copy with an unconditional failure, never a success.
$tokens=$null;$parseErrors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseInput($text,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'Consumer must parse before mock isolation.'}
$absolute=@($ast.FindAll({param($node) $node -is [System.Management.Automation.Language.CommandAst] -and $node.GetCommandName() -match '(?i)[/\\](dotnet\.exe|RunUAT\.bat)$'},$true))
if($absolute.Count -ne 1){throw 'Expected exactly one absolute Stage engine command; review isolation before running.'}
$pipeline=$absolute[0].Parent
if($pipeline -isnot [System.Management.Automation.Language.PipelineAst]){throw 'Unexpected absolute Stage command shape.'}
$text=$text.Remove($pipeline.Extent.StartOffset,$pipeline.Extent.EndOffset-$pipeline.Extent.StartOffset).Insert($pipeline.Extent.StartOffset,"throw 'Mock harness forbids every native Stage invocation.'")
Set-Content -LiteralPath $wrapper -Value $text -Encoding utf8

@'
function Get-ContinuationScratchContext($ProjectRoot){return [pscustomobject]@{Root="$ProjectRoot/Scratch"}}
function Resolve-ContinuationCookDirectory($Context,$CookDirectory){return $CookDirectory}
function New-ContinuationScratchRun($Context,$Name){
 $global:ProjectJCookAdmissionMock.ScratchCalls++
 if($global:ProjectJCookAdmissionMock.ForbidScratch){throw 'Stage reached scratch allocation before rejecting its bad Cook receipt.'}
 $root="$($Context.Root)/$Name"
 New-Item -ItemType Directory -Force -Path "$root/Cooked","$root/User" | Out-Null
 return [pscustomobject]@{Root=$root;CookDirectory="$root/Cooked";UserDirectory="$root/User";PackageDirectory="$root/Package"}
}
'@ | Set-Content -LiteralPath "$audit/Continuation-ScratchPaths.ps1" -Encoding utf8
@'
function Assert-ProjectJSelectedAssetValidation {
 param($ProjectRoot,$ValidationRunDirectory,$SelectionManifest)
 $state=$global:ProjectJCookAdmissionMock;$state.Checks++
 if([IO.Path]::GetFullPath($ProjectRoot) -ine [IO.Path]::GetFullPath($state.ProjectRoot) -or
    [IO.Path]::GetFullPath($ValidationRunDirectory) -ine [IO.Path]::GetFullPath("$ProjectRoot/Saved/Validation/Continuation_20261014/Validated") -or
    $SelectionManifest -cne 'selection.json'){throw 'Consumer passed the wrong validation recipe.'}
 if($state.Scenario -in @('StaleInputs','MismatchedRecipe')){throw "Rejected $($state.Scenario)"}
 if($state.Changed){throw 'Validation inputs changed while the mocked native Cook ran.'}
 return [pscustomobject]@{schema=1;status='passed';manifestSHA256=('1'*64);receiptSHA256=('2'*64);validationRun=$ValidationRunDirectory;assetCount=2;inputCount=3}
}
'@ | Set-Content -LiteralPath "$audit/AssetValidation-Provenance.ps1" -Encoding utf8

function Get-CimInstance {[CmdletBinding()]param([string]$ClassName)}
function Start-Process {
 [CmdletBinding()]param([string]$FilePath,[string]$ArgumentList,[string]$WindowStyle,[switch]$PassThru)
 $state=$global:ProjectJCookAdmissionMock
 $state.Starts++
 if($WindowStyle -ne 'Hidden' -or !$PassThru -or $FilePath -notmatch '[/\\]UnrealEditor-Cmd\.exe$' -or $ArgumentList -notmatch '"-run=Cook"'){
  throw 'Unexpected mocked native invocation.'
 }
 $exitCode=if($state.Scenario -eq 'NullExit'){$null}elseif($state.Scenario -eq 'NonzeroExit'){7}else{0}
 $process=[pscustomobject]@{Id=43210;ExitCode=$exitCode;HasExited=($state.Scenario -ne 'NotExited')}
 $process | Add-Member ScriptProperty Handle {$global:ProjectJCookAdmissionMock.Handles++;return 1}
 $process | Add-Member ScriptMethod WaitForExit {
  $global:ProjectJCookAdmissionMock.Waits++
  if($global:ProjectJCookAdmissionMock.Scenario -eq 'PostChanged'){$global:ProjectJCookAdmissionMock.Changed=$true}
 }
 return $process
}

$results=@();$failures=@()
try {
 foreach($scenario in @('MissingManifest','MissingRun','StaleInputs','MismatchedRecipe','Success','PostChanged','NonzeroExit','NullExit','NotExited','MissingCook','MismatchedCookManifest','MismatchedCookReceipt','MismatchedCookCount','IncompleteCook')){
  $global:ProjectJCookAdmissionMock=@{ProjectRoot=$mockRoot;Scenario=$scenario;Starts=0;Checks=0;ScratchCalls=0;Handles=0;Waits=0;Changed=$false;ForbidScratch=($scenario -in @('MissingCook','MismatchedCookManifest','MismatchedCookReceipt','MismatchedCookCount','IncompleteCook'))}
  $state=$global:ProjectJCookAdmissionMock
  $argsForConsumer=@{Mode='Cook';Name=$scenario;SelectionManifest='selection.json';AssetValidation='Validated'}
  if($scenario -eq 'MissingManifest'){$argsForConsumer.Remove('SelectionManifest')}
  if($scenario -eq 'MissingRun'){$argsForConsumer.Remove('AssetValidation')}
  if($state.ForbidScratch){
   $argsForConsumer.Mode='Stage'
   $cookName="Origin-$scenario";$cookDirectory="$mockRoot/Scratch/$cookName/Cooked"
   New-Item -ItemType Directory -Force -Path $cookDirectory | Out-Null
   $argsForConsumer.CookDirectory=$cookDirectory
   if($scenario -ne 'MissingCook'){
    $receiptFolder="$mockRoot/Saved/Validation/Continuation_20261014/$cookName"
    New-Item -ItemType Directory -Force -Path $receiptFolder | Out-Null
    $prior=[ordered]@{exitCode=0;hasExited=$true;output=$cookDirectory;assetValidation=[ordered]@{schema=1;status='passed';manifestSHA256=('1'*64);receiptSHA256=('2'*64);validationRun="$mockRoot/Saved/Validation/Continuation_20261014/Validated";assetCount=2;inputCount=3}}
    if($scenario -eq 'MismatchedCookManifest'){$prior.assetValidation.manifestSHA256='3'*64}
    if($scenario -eq 'MismatchedCookReceipt'){$prior.assetValidation.receiptSHA256='4'*64}
    if($scenario -eq 'MismatchedCookCount'){$prior.assetValidation.inputCount=4}
    if($scenario -eq 'IncompleteCook'){$prior.hasExited=$false}
    $prior|ConvertTo-Json -Depth 8|Set-Content -LiteralPath "$receiptFolder/process.json" -Encoding utf8
   }
  }
  $threw=$false;$message=''
  try {& $wrapper @argsForConsumer | Out-Null}catch{$threw=$true;$message=$_.Exception.Message}
  $out="$mockRoot/Saved/Validation/Continuation_20261014/$scenario"
  $receipt=if(Test-Path -LiteralPath "$out/process.json" -PathType Leaf){Get-Content -LiteralPath "$out/process.json" -Raw|ConvertFrom-Json}else{$null}
  $ran=$scenario -in @('Success','PostChanged','NonzeroExit','NullExit','NotExited')
  $okay=$threw -eq ($scenario -ne 'Success') -and $state.Starts -eq [int]$ran -and $state.ScratchCalls -eq [int]$ran
  if($ran){
   $expectedChecks=if($scenario -in @('Success','PostChanged')){2}else{1}
   $okay=$okay -and $state.Checks -eq $expectedChecks -and $state.Handles -eq 1 -and $state.Waits -eq 1 -and $null -ne $receipt
   if($scenario -eq 'Success'){
    $okay=$okay -and $receipt.exitCode -eq 0 -and $receipt.hasExited -and $receipt.assetValidation.status -eq 'passed' -and $receipt.assetValidation.inputCount -eq 3
   }else{
    $okay=$okay -and $null -eq $receipt.assetValidation
    if($scenario -eq 'NullExit'){$okay=$okay -and $null -eq $receipt.exitCode}
    elseif($scenario -eq 'NonzeroExit'){$okay=$okay -and $receipt.exitCode -eq 7}
    else{$okay=$okay -and $receipt.exitCode -eq 0}
    if($scenario -eq 'NotExited'){$okay=$okay -and !$receipt.hasExited}
   }
  }else{
   $expectedChecks=if($scenario -in @('MissingManifest','MissingRun')){0}else{1}
   $okay=$okay -and $state.Checks -eq $expectedChecks -and !(Test-Path -LiteralPath $out)
  }
  if(!$okay){$failures+=$scenario}
  $results+=[ordered]@{scenario=$scenario;passed=$okay;threw=$threw;starts=$state.Starts;checks=$state.Checks;scratch=$state.ScratchCalls;nativeReceipt=($null -ne $receipt);message=$message}
 }
 [ordered]@{schema=1;success=($failures.Count -eq 0);consumerSHA256=(Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash;absoluteStageNativeReplacedWithFailure=$true;nativeProcessesStarted=0;cases=$results}|
  ConvertTo-Json -Depth 8|Set-Content -LiteralPath "$testRoot/result.json" -Encoding utf8
 $results|ForEach-Object{[pscustomobject]$_}
 Write-Output "Mock evidence: $testRoot/result.json"
 if($failures.Count){throw "Cook validation consumer regressions failed: $($failures -join ', ')"}
}finally{
 Remove-Variable -Name ProjectJCookAdmissionMock -Scope Global -ErrorAction SilentlyContinue
}
