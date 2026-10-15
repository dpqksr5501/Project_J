param([Parameter(Mandatory)][ValidateSet('Reload','Cook')][string]$Mode,
 [ValidatePattern('^[A-Za-z0-9_]+$')][string]$Name,
 [switch]$IncludeLegacyAnimation)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
$active=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|Uba).*'})
if ($active.Count) {throw 'Wait for existing native work; never overlap or abort.'}
if (!$Name) {$Name="$($Mode)1"}
$out="$projectRoot/Saved/Validation/AssetCleanup_20261015/$Name"
if (Test-Path -LiteralPath $out) {throw 'Preserve prior evidence.'}
New-Item -ItemType Directory -Path $out | Out-Null
$arguments=@("$projectRoot/Project_J.uproject",'-unattended','-nosound','-nop4','-NoLiveCoding','-NoDDCCleanup',"-UserDir=$out/User",'-SaveToUserDir',"-ABSLOG=$out/Run.log",'-culture=en','-UTF8Output',"-ProjectJAssetValidationName=$Name")
if ($Mode -eq 'Reload') {
 $arguments+=@('-NullRHI',"-ExecutePythonScript=$projectRoot/Scripts/Audit/Verify-AssetArchitecture.py")
} else {
 # Cook-only asset boundary. No Game executable build/package/remote/performance claim.
 $arguments+=@('-run=Cook','-TargetPlatform=Windows','-Map=/Game/ThirdPerson/Lvl_ThirdPerson',"-OutputDir=$out/Cooked/[Platform]")
 if ($IncludeLegacyAnimation) {$arguments+="-CookDir=$projectRoot/Content/Animation_Logic/ABPs"}
}
$arguments | ConvertTo-Json | Set-Content "$out/Arguments.json" -Encoding utf8
$p=Start-Process 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -ArgumentList (($arguments | ForEach-Object {'"'+$_+'"'}) -join ' ') -WindowStyle Hidden -PassThru
$handle=$p.Handle
Write-Output "Started asset $Mode PID=$($p.Id)"
$p.WaitForExit()
[ordered]@{exitCode=$p.ExitCode;normalCompletion=$true;mode=$Mode;explicitLegacyAnimationCookRoot=[bool]$IncludeLegacyAnimation;nativeCPPBuild=$false;gameExecutableValidated=$false} | ConvertTo-Json | Set-Content "$out/Process.json" -Encoding utf8
if ($null -eq $p.ExitCode -or $p.ExitCode -ne 0) {throw 'Asset validation failed; inspect the first retained error.'}
if ($Mode -eq 'Reload' -and !(Test-Path "$out/Verification.json")) {throw 'Missing reload receipt.'}
Write-Output "Completed asset $Mode normally."
