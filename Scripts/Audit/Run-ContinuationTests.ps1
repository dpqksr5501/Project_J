param([Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,[string]$Tests='ProjectJ.Continuation')
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
$out = "$projectRoot/Saved/Validation/Continuation_20261014/$Name"
$active = @(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object { $_.Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|Uba).*' })
if ($active.Count) { throw 'Wait for engine/build completion.' }
if (Test-Path -LiteralPath $out) { throw 'Preserve prior evidence.' }
New-Item -ItemType Directory -Path $out | Out-Null
# Existing CombatContinuity fixtures require this before creating their worlds (CombatTests.inl:75).
# This invocation-only budget is separate from the 2ms performance comparison and product config.
$arguments = @("$projectRoot/Project_J.uproject",'-unattended','-nosound','-nop4','-NoDDCCleanup','-NoLiveCoding','-NullRHI',"-UserDir=$out/User", "-ABSLOG=$out/Run.log", "-ReportExportPath=$out", '-TestExit=Automation Test Queue Empty', "-ExecCmds=a.Budget.BudgetMs 0.1,Automation RunTests $Tests; Quit")
$arguments | ConvertTo-Json | Set-Content -LiteralPath "$out/arguments.json" -Encoding utf8
$process = Start-Process 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -ArgumentList (($arguments | ForEach-Object { '"'+$_+'"' }) -join ' ') -WindowStyle Hidden -PassThru
$nativeHandle = $process.Handle
Write-Output "Started $Name PID=$($process.Id)"
$process.WaitForExit()
[ordered]@{exitCode=$process.ExitCode;hasExited=$process.HasExited;nativeHandleCapturedBeforeWait=$true} | ConvertTo-Json | Set-Content -LiteralPath "$out/process.json" -Encoding utf8
if (!(Test-Path -LiteralPath "$out/index.json")) { throw 'No automation report; inspect retained log.' }
$report = Get-Content -LiteralPath "$out/index.json" -Raw | ConvertFrom-Json
$report | Select-Object succeeded,succeededWithWarnings,failed,notRun,inProcess,totalDuration
if ($null -eq $process.ExitCode -or $process.ExitCode -ne 0 -or $report.failed -or $report.notRun -or $report.inProcess -or ($report.succeeded+$report.succeededWithWarnings -eq 0)) { throw 'Regression failed/incomplete; raw evidence retained.' }
