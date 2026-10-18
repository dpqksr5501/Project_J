# Native process boundaries are mocked; no Unreal process or real asset is opened.
$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path "$PSScriptRoot/../..").Path
$testRoot = Join-Path $repository "Saved/Audit6850/Round2/WrapperTests/$([guid]::NewGuid().ToString('N'))"
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
 $process = [pscustomobject]@{ ExitCode=$exit; HasExited=$true; Handle=1 }
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
