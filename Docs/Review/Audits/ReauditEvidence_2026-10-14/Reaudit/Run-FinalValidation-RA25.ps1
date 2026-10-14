$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../../..").Path
$python='C:/Users/I/AppData/Local/Programs/Python/Python314/python.exe'
function Invoke-ReviewStep([string]$Name,[scriptblock]$Action) {
    [ordered]@{version='RA01-25';active=$Name;utc=[DateTime]::UtcNow.ToString('o');noAbortOrOverlap=$true} |
        ConvertTo-Json | Set-Content -LiteralPath "$PSScriptRoot/RA25PipelineState.json" -Encoding utf8
    Write-Output "Starting RA25 step: $Name"
    & $Action
    Write-Output "Completed RA25 step: $Name"
}
Invoke-ReviewStep 'Freeze' { & $python "$PSScriptRoot/Freeze-RA25.py"; if($LASTEXITCODE){throw 'Freeze failed.'} }
Invoke-ReviewStep 'WholeRefinement1' { & "$PSScriptRoot/Run-Tests.ps1" -Name WholeRefinement1 -Tests ProjectJ -IsolatedUser }
foreach($trial in 7..9) {
    Invoke-ReviewStep "Reaudit_Network$trial" {
        & "$projectRoot/Scripts/Validation/Measure-Network.ps1" -RunName "Reaudit_Network$trial" -ClientCount 2 -NPCCount 200 -Animation -CombatInput -PacketLag 50 -PacketLoss 20 -Port (17900+$trial)
    }
}
Invoke-ReviewStep 'GameBuild4' { & "$PSScriptRoot/Run-GameBuild.ps1" -Name GameBuild4 }
Invoke-ReviewStep 'Cook3' { & "$PSScriptRoot/Run-Cook.ps1" -Name Cook3 }
Invoke-ReviewStep 'Packaged3' { & "$PSScriptRoot/Run-Stage.ps1" -Name Packaged3 }
$executable="$PSScriptRoot/Packaged3/Windows/Project_J/Binaries/Win64/Project_J.exe"
Invoke-ReviewStep 'CookedDamage3' { & "$PSScriptRoot/Run-CookedDamage.ps1" -Name CookedDamage3 -PackageName Packaged3 }
Invoke-ReviewStep 'Reaudit_Combat3' { & "$projectRoot/Scripts/Validation/Measure-CookedCombat.ps1" -Executable $executable -RunName Reaudit_Combat3 -UserDirectory "$PSScriptRoot/CookedCombatUser3" }
Invoke-ReviewStep 'Reaudit_UI3' { & "$projectRoot/Scripts/Validation/Validate-CookedUI.ps1" -Executable $executable -EvidenceName Reaudit_UI3 -Profile -HeavyWorkload -DynamicWorkload }
Invoke-ReviewStep 'Reaudit_Render3' { & "$projectRoot/Scripts/Validation/Measure-RenderedCrowd.ps1" -Executable $executable -RunName Reaudit_Render3 }
[ordered]@{version='RA01-25';active='complete';utc=[DateTime]::UtcNow.ToString('o');allStepsCompleted=$true} |
    ConvertTo-Json | Set-Content -LiteralPath "$PSScriptRoot/RA25PipelineState.json" -Encoding utf8
Write-Output 'RA25 native/network/build/cook/package/UI/render sequence completed normally.'
