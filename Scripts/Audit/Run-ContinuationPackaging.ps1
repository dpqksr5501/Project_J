param([Parameter(Mandatory)][ValidateSet('Cook','Stage')][string]$Mode,
 [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
 [string]$CookDirectory,
 [ValidatePattern('^/(Game|Engine)/[A-Za-z0-9_/-]+$')][string[]]$Maps=@('/Game/ThirdPerson/Lvl_ThirdPerson','/Engine/Maps/Entry'),
 [ValidateSet('Development','Shipping')][string]$StageConfiguration='Development',
 [string]$SelectionManifest,
 [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$AssetValidation)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
. "$PSScriptRoot/Continuation-ScratchPaths.ps1"
. "$PSScriptRoot/AssetValidation-Provenance.ps1"
$scratchContext=Get-ContinuationScratchContext $projectRoot
if($Mode -eq 'Cook' -and $StageConfiguration -ne 'Development'){throw 'StageConfiguration selects native Stage products; this Cook helper uses an explicit asset closure without a native configuration switch.'}
$active=@(Get-CimInstance Win32_Process -ErrorAction Stop|Where-Object {$_.Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|UnrealInsights|Uba).*'})
if($active.Count){throw 'Wait for normal engine/build completion.'}
if(!$SelectionManifest -or !$AssetValidation){throw 'Cook/Stage requires a declared selection manifest and completed strict AssetValidation run.'}
$validationRun="$projectRoot/Saved/Validation/Continuation_20261014/$AssetValidation"
$admission=Assert-ProjectJSelectedAssetValidation -ProjectRoot $projectRoot -ValidationRunDirectory $validationRun -SelectionManifest $SelectionManifest
$out="$projectRoot/Saved/Validation/Continuation_20261014/$Name"
if(Test-Path -LiteralPath $out){throw 'Preserve prior evidence.'}
if($Mode -eq 'Stage'){
 $CookDirectory=Resolve-ContinuationCookDirectory $scratchContext $CookDirectory
 $cookRunName=Split-Path (Split-Path $CookDirectory -Parent) -Leaf
 if($cookRunName -notmatch '^[A-Za-z0-9_-]+$'){throw 'Cook run identity is invalid.'}
 $cookReceipt="$projectRoot/Saved/Validation/Continuation_20261014/$cookRunName/process.json"
 $cookProcess=Get-Content -LiteralPath $cookReceipt -Raw|ConvertFrom-Json
 if($null -eq $cookProcess.exitCode -or $cookProcess.exitCode -ne 0 -or !$cookProcess.hasExited -or
    [IO.Path]::GetFullPath($cookProcess.output) -ine [IO.Path]::GetFullPath($CookDirectory) -or
    !$cookProcess.assetValidation -or $cookProcess.assetValidation.status -ne 'passed'){throw 'Cook has no matching completed strict validation admission.'}
 foreach($field in @('schema','status','manifestSHA256','receiptSHA256','validationRun','assetCount','inputCount')){if($cookProcess.assetValidation.$field -cne $admission.$field){throw 'Cook validation admission differs from the selected completed run.'}}
}
$scratch=New-ContinuationScratchRun $scratchContext $Name
New-Item -ItemType Directory -Path $out | Out-Null
if($Mode -eq 'Cook'){
 if(!$Maps.Count){throw 'Cook requires explicit nonempty map targets.'}
 $arguments=@("$projectRoot/Project_J.uproject",'-run=Cook','-TargetPlatform=Windows',"-Map=$($Maps -join '+')","-OutputDir=$($scratch.CookDirectory)/[Platform]",'-unattended','-nop4','-NoDDCCleanup','-NoLiveCoding',"-UserDir=$($scratch.UserDirectory)","-ABSLOG=$out/Cook.log",'-UTF8Output')
 $arguments|ConvertTo-Json|Set-Content "$out/arguments.json" -Encoding utf8
 $process=Start-Process 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -ArgumentList (($arguments|ForEach-Object {'"'+$_+'"'}) -join ' ') -WindowStyle Hidden -PassThru
 $nativeHandle=$process.Handle
 $process.WaitForExit()
 [ordered]@{exitCode=$process.ExitCode;hasExited=$process.HasExited;output=$scratch.CookDirectory;userDirectory=$scratch.UserDirectory;scratchRoot=$scratch.Root;scope='Windows explicit maps + configured AssetManager/AlwaysCook closure; declared validation inputs only, not transitive closure coverage; previous outputs preserved.';maps=$Maps;assetValidation=$null}|ConvertTo-Json|Set-Content "$out/process.json" -Encoding utf8
 if(!$process.HasExited -or $null -eq $process.ExitCode -or $process.ExitCode -ne 0){throw 'Cook failed/incomplete; retain first error and native receipt.'}
}else{
 $arguments=@('BuildCookRun',"-project=$projectRoot/Project_J.uproject",'-noP4','-installed','-platform=Win64',"-clientconfig=$StageConfiguration",'-skipbuild','-skipcook','-nocompile','-nocompileuat',"-CookOutputDir=$CookDirectory",'-stage','-pak','-iostore','-package',"-stagingdirectory=$($scratch.PackageDirectory)",'-utf8output','-unattended','-nocleanstage')
 $arguments|ConvertTo-Json|Set-Content "$out/arguments.json" -Encoding utf8
 & 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe' 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/DotNET/AutomationTool/AutomationTool.dll' @arguments *> "$out/Stage.log"
 $stageExit=$LASTEXITCODE
 [ordered]@{exitCode=$stageExit;configuration=$StageConfiguration;packageDirectory=$scratch.PackageDirectory;cookDirectory=$CookDirectory;scratchRoot=$scratch.Root;scope='Stage/package only: direct UBT and separate Cook; unique directory, prior stages preserved.';assetValidation=$null}|ConvertTo-Json|Set-Content "$out/process.json" -Encoding utf8
 if($stageExit -ne 0){throw 'Stage completed with failure; retain first error.'}
}

# Keep the native exit receipt even if inputs changed during Cook/Stage. Only
# publish validation admission after the read-only completion recheck succeeds.
$completed=Get-Content -LiteralPath "$out/process.json" -Raw|ConvertFrom-Json
$completed.assetValidation=Assert-ProjectJSelectedAssetValidation -ProjectRoot $projectRoot -ValidationRunDirectory $validationRun -SelectionManifest $SelectionManifest
$completed|ConvertTo-Json -Depth 8|Set-Content -LiteralPath "$out/process.json" -Encoding utf8
