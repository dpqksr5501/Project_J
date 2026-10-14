$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../../..").Path
$executable="$PSScriptRoot/Packaged2/Windows/Project_J/Binaries/Win64/Project_J.exe"
if(@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count){throw 'Wait for normal engine/build completion.'}
& "$PSScriptRoot/Run-CookedDamage.ps1" -Name CookedDamage2 -PackageName Packaged2
& "$projectRoot/Scripts/Validation/Measure-CookedCombat.ps1" -Executable $executable -RunName Reaudit_Combat2 -UserDirectory "$PSScriptRoot/CookedCombatUser2"
& "$projectRoot/Scripts/Validation/Validate-CookedUI.ps1" -Executable $executable -EvidenceName Reaudit_UI2 -Profile -HeavyWorkload -DynamicWorkload
& "$projectRoot/Scripts/Validation/Measure-RenderedCrowd.ps1" -Executable $executable -RunName Reaudit_Render2
foreach($trial in 4..6){
    & "$projectRoot/Scripts/Validation/Measure-Network.ps1" -RunName "Reaudit_Network$trial" -ClientCount 2 -NPCCount 200 -Animation -CombatInput -PacketLag 50 -PacketLoss 20 -Port (17900+$trial)
}
Write-Output 'RA19 sequential package/RHI/UI/network integration completed.'
