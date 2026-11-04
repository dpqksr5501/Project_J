param(
 [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
 [Parameter(Mandatory)][ValidateSet('Cook','Package')][string]$Mode,
 [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Game,
 [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Cook,
 [string]$SelectionManifest,
 [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$AssetValidation
)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
$saved="$projectRoot/Saved/Validation/Continuation_20261014"
. "$PSScriptRoot/AssetValidation-Provenance.ps1"
$active=@(Get-CimInstance Win32_Process | Where-Object { $_.Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|UnrealInsights|Uba).*' })
if($active.Count){throw 'Wait for normal engine/build/trace completion.'}
if(!$SelectionManifest -or !$AssetValidation){throw 'Cook/Package requires a declared selection manifest and completed strict AssetValidation run.'}
$validationRun="$saved/$AssetValidation"
$admission=Assert-ProjectJSelectedAssetValidation -ProjectRoot $projectRoot -ValidationRunDirectory $validationRun -SelectionManifest $SelectionManifest
$identity=Get-Content -LiteralPath "$saved/$Game/GameVersion.json" -Raw | ConvertFrom-Json
if(!$identity.actual_game_source_matches_frozen){throw 'Game has no verified compiled-source identity.'}
foreach($artifact in $identity.artifacts){
 if((Get-FileHash -LiteralPath "$projectRoot/$($artifact.path)").Hash -ne $artifact.sha256){throw 'Game artifacts changed; do not restore automatically.'}
}
& python "$PSScriptRoot/Check-ContinuationProvenance.py" "$saved/$Game/GameVersion.json" $Mode
if($LASTEXITCODE){throw 'Source/plugin/binary provenance changed after compilation.'}
$out="$saved/$Name"
if(Test-Path -LiteralPath $out){throw 'Preserve prior run; choose a new name.'}
if($Mode -eq 'Package'){
 if(!$Cook){throw 'Package needs the verified configured Cook run.'}
 $cookProcess=Get-Content -LiteralPath "$saved/$Cook/process.json" -Raw | ConvertFrom-Json
 if($null -eq $cookProcess.exitCode -or $cookProcess.exitCode -ne 0 -or !$cookProcess.normalCompletion){throw 'Cook failed/incomplete.'}
 if(!$cookProcess.assetValidation -or $cookProcess.assetValidation.status -ne 'passed'){throw 'Cook has no matching strict validation admission.'}
 foreach($field in @('schema','status','manifestSHA256','receiptSHA256','validationRun','assetCount','inputCount')){if($cookProcess.assetValidation.$field -cne $admission.$field){throw 'Cook validation admission differs from the selected completed run.'}}
 if($cookProcess.sourceVersion -ne $identity.editor_frozen_version -or $cookProcess.game -ne $Game){throw 'Cook and Game checkpoint differ.'}
 $cookRoot="$saved/$Cook/Cooked"
 if(!(Test-Path -LiteralPath "$cookRoot/Windows/ue.projectstore") -and !(Test-Path -LiteralPath "$cookRoot/Windows/Project_J/Metadata/packagestore.manifest")){
  throw 'Expected single Windows platform layout/package store is absent; never duplicate Windows/Windows.'
 }
}
New-Item -ItemType Directory -Path $out | Out-Null
if($Mode -eq 'Cook'){
 $arguments=@("$projectRoot/Project_J.uproject",'-run=Cook','-TargetPlatform=Windows',
  '-Map=/Game/ThirdPerson/Lvl_ThirdPerson+/Engine/Maps/Entry',"-OutputDir=$out/Cooked/[Platform]",
  '-unattended','-nop4','-NoDDCCleanup','-NoLiveCoding',"-UserDir=$out/User","-ABSLOG=$out/Cook.log",'-UTF8Output')
 $arguments | ConvertTo-Json | Set-Content -LiteralPath "$out/arguments.json" -Encoding utf8
 $process=Start-Process 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -ArgumentList (($arguments | ForEach-Object {'"'+$_+'"'}) -join ' ') -WindowStyle Hidden -PassThru
 $handle=$process.Handle
 Write-Output "Started configured Cook PID=$($process.Id)"
 $process.WaitForExit()
 $exitCode=$process.ExitCode
 $nativeExited=$process.HasExited
}else{
 $arguments=@('BuildCookRun',"-project=$projectRoot/Project_J.uproject",'-noP4','-installed','-platform=Win64','-clientconfig=Development',
  '-skipbuild','-skipcook','-nocompile','-nocompileuat',"-CookOutputDir=$cookRoot",'-stage','-pak','-iostore','-package',
  "-stagingdirectory=$out/Package",'-utf8output','-unattended','-nocleanstage')
 $arguments | ConvertTo-Json | Set-Content -LiteralPath "$out/arguments.json" -Encoding utf8
 Write-Output 'Started package-only AutomationTool; C++ compile and Cook disabled.'
 & 'C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/RunUAT.bat' @arguments *> "$out/Stage.log"
 $exitCode=$LASTEXITCODE
 $nativeExited=$true # Synchronous AutomationTool invocation has returned.
}
[ordered]@{exitCode=$exitCode;mode=$Mode;game=$Game;sourceVersion=$identity.editor_frozen_version;configuredCookAll=$false;nativeCPPBuild=$false;normalCompletion=$nativeExited;assetValidation=$null} |
 ConvertTo-Json | Set-Content -LiteralPath "$out/process.json" -Encoding utf8
if(!$nativeExited -or $null -eq $exitCode -or $exitCode -ne 0){throw 'Deployment step failed/incomplete; preserve first error and all raw evidence.'}
$completed=Get-Content -LiteralPath "$out/process.json" -Raw|ConvertFrom-Json
$completed.assetValidation=Assert-ProjectJSelectedAssetValidation -ProjectRoot $projectRoot -ValidationRunDirectory $validationRun -SelectionManifest $SelectionManifest
$completed|ConvertTo-Json -Depth 8|Set-Content -LiteralPath "$out/process.json" -Encoding utf8
Write-Output "Completed $Mode normally: $out"
