param([Parameter(Mandatory)][string]$RunRoot,
 [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$OutputName)
$ErrorActionPreference='Stop'
$projectRoot=(Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
$active=@(Get-CimInstance Win32_Process|Where-Object {$_.Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|UnrealInsights|Uba).*'})
if($active.Count){throw 'Wait for engine/build/trace analysis completion.'}
$RunRoot=(Resolve-Path -LiteralPath $RunRoot).Path.Replace('\','/')
if(!$RunRoot.StartsWith("$projectRoot/Saved/Validation/",[StringComparison]::OrdinalIgnoreCase)){throw 'Only existing project validation captures are allowed.'}
$trace="$RunRoot/CPU.utrace"
if(!(Test-Path -LiteralPath $trace -PathType Leaf)){throw 'Missing retained CPU trace.'}
$out="$RunRoot/$OutputName"
if(Test-Path -LiteralPath $out){throw 'Preserve previous export.'}
New-Item -ItemType Directory -Path $out|Out-Null
# UE5.8 TimingProfilerManager.cpp:1063 implements ExportTimerCallees with
# -timers/-region. This exports observed GT ancestry under the world scope,
# without replaying gameplay or swapping/restoring any binary.
$commands=@(
 ('TimingInsights.ExportTimerCallees "'+$out+'/WorldCallees.csv" -timers="ProjectJ_AuthoredAnimation_WorldTick"'),
 ('TimingInsights.ExportTimerCallees "'+$out+'/WorldCallees-{region}.csv" -timers="ProjectJ_AuthoredAnimation_WorldTick" -region="ProjectJ.AuthoredCPU_*"')
)
$commands|Set-Content "$out/Export.rsp" -Encoding utf8
$traceHash=(Get-FileHash -LiteralPath $trace -Algorithm SHA256).Hash
$arguments=@("-OpenTraceFile=$trace",'-AutoQuit','-NoUI','-unattended',"-ExecOnAnalysisCompleteCmd=@=$out/Export.rsp","-ABSLOG=$out/Insights.log")
$arguments|ConvertTo-Json|Set-Content "$out/arguments.json" -Encoding utf8
$quoted=@($arguments|ForEach-Object {if($_ -match '^([^=]+)=(.*)$'){$Matches[1]+'="'+$Matches[2]+'"'}else{$_}})-join ' '
$process=Start-Process 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealInsights.exe' -ArgumentList $quoted -WindowStyle Hidden -PassThru
$nativeHandle=$process.Handle
$process.WaitForExit()
[ordered]@{exitCode=$process.ExitCode;trace=$trace;traceSHA256=$traceHash;traceSHAUnchanged=((Get-FileHash -LiteralPath $trace -Algorithm SHA256).Hash -eq $traceHash);gameplayReplay=$false;binaryMutation=$false;scope='Existing trace GT WorldTick callee trees, full captured world scopes + authored CPU regions. No worker critical-path/active CPU claim.'}|ConvertTo-Json|Set-Content "$out/process.json" -Encoding utf8
if($null -eq $process.ExitCode -or $process.ExitCode -ne 0 -or !(Test-Path "$out/WorldCallees.csv") -or !(Get-ChildItem -LiteralPath $out -Filter 'WorldCallees-*.csv')){throw 'Callee export failed/incomplete; retain log.'}
Write-Output "Completed existing capture callee export: $out"
