param([Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
 [ValidatePattern('^[A-Za-z0-9_]+$')][string[]]$AssetTypes=@('Project_JLocomotionProfile','AnimMontage'))
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
$out="$projectRoot/Saved/Validation/Continuation_20261014/$Name"
if(Test-Path -LiteralPath $out){throw 'Preserve evidence.'}
New-Item -ItemType Directory -Path $out|Out-Null
$failures=@()
foreach($assetType in $AssetTypes){
 $active=@(Get-CimInstance Win32_Process -ErrorAction Stop|Where-Object {$_.Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|UnrealInsights|Uba).*'})
 if($active.Count){throw 'Wait for normal process completion.'}
 $run="$out/$assetType"
 New-Item -ItemType Directory -Path $run|Out-Null
 $arguments=@("$projectRoot/Project_J.uproject",'-run=Project_JAssetValidation',"-AssetType=$assetType",'-IncludeOnlyOnDiskAssets','-NullRHI','-unattended','-NoLiveCoding','-NoDDCCleanup','-nop4','-nosound',"-UserDir=$run/User","-ABSLOG=$run/Run.log",'-UTF8Output','-culture=en')
 $arguments|ConvertTo-Json|Set-Content "$run/arguments.json" -Encoding utf8
 $process=Start-Process 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -ArgumentList (($arguments|ForEach-Object{'"'+$_+'"'}) -join ' ') -WindowStyle Hidden -PassThru
 $nativeHandle=$process.Handle
 $process.WaitForExit()
 $summary=$null
 $validationWarnings=$null
 $summaryLines=@()
 if(Test-Path -LiteralPath "$run/Run.log"){
  $validationLog=Get-Content -LiteralPath "$run/Run.log" -Raw
  $warningLines=[regex]::Matches($validationLog,'Validation Warnings: (\d+)')
  if($warningLines.Count){$validationWarnings=[int]$warningLines[$warningLines.Count-1].Groups[1].Value}
  $summaryLines=[regex]::Matches($validationLog,'Files Checked: (\d+), Passed: (\d+), Failed: (\d+), Skipped: (\d+), Unable to validate: (\d+)')
  if($summaryLines.Count){
   $last=$summaryLines[$summaryLines.Count-1]
   $summary=[ordered]@{checked=[int]$last.Groups[1].Value;passed=[int]$last.Groups[2].Value;failed=[int]$last.Groups[3].Value;skipped=[int]$last.Groups[4].Value;unableToValidate=[int]$last.Groups[5].Value}
  }
 }
 $validationStatus='failed-or-incomplete'
 if($process.ExitCode -eq 0 -and $summary -and $null -ne $validationWarnings -and $summary.checked -gt 0 -and $summary.failed -eq 0 -and $summary.passed -gt 0 -and $summary.unableToValidate -eq 0){
  $validationStatus=if($summary.skipped){'partial'}elseif($validationWarnings -gt 0){'passed-with-warnings'}else{'passed'}
 }
 [ordered]@{exitCode=$process.ExitCode;hasExited=$process.HasExited;assetType=$assetType;validationStatus=$validationStatus;validationSummary=$summary;validationWarnings=$validationWarnings;scope='Engine DataValidation selection/validators with completion-count override; exact class/subclasses; no asset edits/saves.'}|ConvertTo-Json -Depth 4|Set-Content "$run/process.json" -Encoding utf8
 # A missing actual exit receipt cannot authorize starting another engine process.
 if($null -eq $process.ExitCode -or !$process.HasExited){throw 'No actual completed exit receipt; preserve evidence and inspect process state.'}
 if($process.ExitCode -ne 0 -or $null -eq $summary -or $null -eq $validationWarnings -or $summary.checked -le 0 -or $summary.failed -gt 0 -or $summary.passed -le 0 -or $summary.unableToValidate -gt 0){$failures+=$assetType}
 if($summaryLines.Count){$summaryLines[$summaryLines.Count-1].Value}
}
if($failures.Count){throw "Native asset validation failed/incomplete: $($failures -join ', '). Inspect retained process.json and Run.log."}
