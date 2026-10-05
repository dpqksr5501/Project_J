param(
 [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName,
 [string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference='Stop'
$maturityRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\','/')
if(@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count){throw 'Wait for all engine/build processes to finish.'}
$maturityOutput="$maturityRoot/Saved/Validation/Maturity/$RunName"
if(Test-Path -LiteralPath $maturityOutput){throw 'Use a new RunName to preserve evidence.'}
New-Item -ItemType Directory -Path $maturityOutput | Out-Null
$maturityFilters='ProjectJ.Maturity+ProjectJ.Modernization+ProjectJ.Architecture+ProjectJ.Presentation+ProjectJ.Combat+ProjectJ.Animation+ProjectJ.NPCAction+ProjectJ.NPCDecision+ProjectJ.NPCPath+ProjectJ.GroupD+ProjectJ.Internal.Mount+ProjectJ.Authoring.Bundle+ProjectJ.GroupB.CombatPosePolicy+ProjectJ.GroupE.AnimationMigrationPreflight+ProjectJ.MMO'
# Preflight uses transient copies. No asset-saving commandlet is invoked.
& "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$maturityRoot/Project_J.uproject" -EnablePlugins=ProjectJExperiments -unattended -NullRHI -nosound -NoLiveCoding -nop4 -nosplash "-ExecCmds=Automation RunTests $maturityFilters" '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$maturityOutput/Automation" "-abslog=$maturityOutput/Run.log"
$maturityExit=$LASTEXITCODE
$maturityIndex="$maturityOutput/Automation/index.json"
if($maturityExit -ne 0 -or !(Test-Path -LiteralPath $maturityIndex)){throw "Automation process/report failure: $maturityOutput"}
$maturityReport=Get-Content -LiteralPath $maturityIndex -Raw | ConvertFrom-Json
foreach($maturityField in 'failed','notRun','inProcess'){
 if($null -eq $maturityReport.$maturityField -or $maturityReport.$maturityField -ne 0){throw "Automation $maturityField is nonzero/missing: $maturityIndex"}
}
$maturityRequired=@(
 'ProjectJ.Maturity.Backend.MutationOutcome',
 'ProjectJ.Maturity.Combat.HitWindowOwnership',
 'ProjectJ.Maturity.Editor.GuidedIKCompileValidation',
 'ProjectJ.Maturity.Editor.WeaponSocketFrame',
 'ProjectJ.Maturity.GAS.ResourceInvariants',
 'ProjectJ.Maturity.Handover.ApplyAndAdmission',
 'ProjectJ.Maturity.Input.SharedMappingLease',
 'ProjectJ.Maturity.Mount.LandingRecovery',
 'ProjectJ.Maturity.NPC.ActionSuspension',
 'ProjectJ.Maturity.Social.GroupSnapshot',
 'ProjectJ.Maturity.Tools.MigrationReceipt',
 'ProjectJ.Modernization.EquipmentLoadLifecycle'
)
foreach($maturityName in $maturityRequired){
 $maturityTests=@($maturityReport.tests | Where-Object {$_.fullTestPath -eq $maturityName})
 if($maturityTests.Count -ne 1 -or $maturityTests[0].state -ne 'Success' -or $maturityTests[0].errors -ne 0){throw "Required regression missing/failed: $maturityName"}
}
[PSCustomObject]@{tests=$maturityReport.tests.Count;succeeded=$maturityReport.succeeded;succeededWithWarnings=$maturityReport.succeededWithWarnings;failed=$maturityReport.failed;report=$maturityIndex} | ConvertTo-Json -Compress
