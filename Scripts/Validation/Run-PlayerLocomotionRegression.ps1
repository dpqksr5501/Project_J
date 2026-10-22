param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName,
    [string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$conflicts=@(Get-CimInstance Win32_Process | Where-Object Name -match '^(UnrealEditor|UnrealEditor-Cmd|UnrealBuildTool|dotnet|LiveCodingConsole|MSBuild|ShaderCompileWorker)\.exe$')
if($conflicts.Count){throw 'Wait for the active engine/build processes to finish. No process was stopped.'}
$out=Join-Path $root "Saved/Validation/PlayerLocomotion/$RunName"
if(Test-Path -LiteralPath $out){throw 'Choose a new RunName to preserve previous evidence.'}
New-Item -ItemType Directory -Path $out | Out-Null
$tests='ProjectJ.PlayerLocomotion+ProjectJ.Mover+ProjectJ.NetworkPrediction+ProjectJ.Warnings+ProjectJ.Foley+ProjectJ.RuntimeOwnership.SprintMovePrediction+ProjectJ.RuntimeOwnership.AnimationProxyPinsQueuedAssets+ProjectJ.RuntimeOwnership.LocomotionTagLease+ProjectJ.SourceAudit2.RemoteJumpDeadline+ProjectJ.SourceAudit3.MovementOwnerAndTimers+ProjectJ.SourceAudit3.BudgetAuthoringContract+ProjectJ.Integrated.NativeNavMovement+ProjectJ.Integrated.MovingTargetPursuit+ProjectJ.Retarget+ProjectJ.Animation.HandContactConversion+ProjectJ.Animation.HandContactRig+ProjectJ.Animation.SecondaryHandContact+ProjectJ.Animation.TwoHandIKTransitionAndCurve+ProjectJ.Animation.HiddenLeaderVisibleFollower+ProjectJ.Animation.HiddenToVisibleFollowerRestore+ProjectJ.Animation.ModularMeshIsolation'
$tests+='+System.Core.Experimental.UnifiedError.CreateErrorMessage+Core.Logging.StructuredLogFormat'
$tests+='+ProjectJ.Architecture.Handover.PlayerCharacterSerialization+ProjectJ.AssetArchitecture.AuthoredCombatInterface+ProjectJ.Reaudit.AuthoredFollowerRole'
$tests+='+ProjectJ.Modernization.EquipmentLoadLifecycle+ProjectJ.GroupA.EquipmentRetry+ProjectJ.Continuation.VisualAssetAdmissions'
$arguments=@("$root/Project_J.uproject",'-NullRHI','-unattended','-nosound','-NoLiveCoding','-nop4','-nosplash','-LogCmds=LogAutomationTest Log',
    "-ExecCmds=Automation RunTests $tests",'-TestExit=Automation Test Queue Empty',
    "-ReportExportPath=$out/Automation","-abslog=$out/Run.log")
$quoted=($arguments | ForEach-Object { '"'+$_+'"' }) -join ' '
$process=Start-Process -FilePath "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" -ArgumentList $quoted -WindowStyle Hidden -PassThru
$nativeHandle=$process.Handle
$process.WaitForExit()
if($null -eq $process.ExitCode -or $process.ExitCode -ne 0){throw "Automation process failed; inspect $out/Run.log"}
$report=Get-Content -LiteralPath "$out/Automation/index.json" -Raw | ConvertFrom-Json
if($report.failed -or $report.notRun -or $report.succeededWithWarnings -or $report.succeeded -lt 87){throw "Incomplete, warning-bearing or failed regression: $out"}
$report.tests | Select-Object fullTestPath,state
Write-Output "Player locomotion regression passed. Actual pose, live input and network fixtures are documented separately: $out"
