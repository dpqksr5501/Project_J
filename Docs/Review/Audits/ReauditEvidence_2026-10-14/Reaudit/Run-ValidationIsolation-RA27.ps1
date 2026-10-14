$ErrorActionPreference='Stop'
$python='C:/Users/I/AppData/Local/Programs/Python/Python314/python.exe'
$projectRoot=(Resolve-Path "$PSScriptRoot/../../..").Path
function Invoke-IsolationStep([string]$Name,[scriptblock]$Action) {
    [ordered]@{version='RA01-27';active=$Name;utc=[DateTime]::UtcNow.ToString('o');productionCode='RA25 unchanged';noAbortOrOverlap=$true} |
        ConvertTo-Json | Set-Content -LiteralPath "$PSScriptRoot/RA27PipelineState.json" -Encoding utf8
    Write-Output "Starting RA27 step: $Name"; & $Action; Write-Output "Completed RA27 step: $Name"
}
Invoke-IsolationStep 'Freeze' { & $python "$PSScriptRoot/Freeze-RA27.py"; if($LASTEXITCODE){throw 'Freeze failed.'} }
foreach($trial in 1..3) {
    Invoke-IsolationStep "ForwardIndexFixed$trial" { & "$PSScriptRoot/Run-Tests.ps1" -Name "ForwardIndexFixed$trial" -Tests ProjectJ.StrafeForward.ProductionData -IsolatedUser }
}
Invoke-IsolationStep 'WholeRefinement2' { & "$PSScriptRoot/Run-Tests.ps1" -Name WholeRefinement2 -Tests ProjectJ -IsolatedUser }
foreach($trial in 10..12) {
    Invoke-IsolationStep "Reaudit_Network$trial" {
        & "$projectRoot/Scripts/Validation/Measure-Network.ps1" -RunName "Reaudit_Network$trial" -ClientCount 2 -NPCCount 200 -Animation -CombatInput -PacketLag 50 -PacketLoss 20 -Port (17900+$trial)
    }
}
[ordered]@{version='RA01-27';active='complete';utc=[DateTime]::UtcNow.ToString('o');allStepsCompleted=$true;productionCode='RA25 unchanged'} |
    ConvertTo-Json | Set-Content -LiteralPath "$PSScriptRoot/RA27PipelineState.json" -Encoding utf8
