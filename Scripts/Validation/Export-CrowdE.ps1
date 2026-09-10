param(
 [Parameter(Mandatory=$true)][string]$RunRoot,
 [string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference='Stop'
if (@(Get-Process UnrealEditor*,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker,UnrealInsights -ErrorAction SilentlyContinue).Count) {throw 'Wait for engine/analysis completion.'}
$RunRoot=(Resolve-Path -LiteralPath $RunRoot).Path.Replace('\','/')
$trace="$RunRoot/Run.utrace"; if (!(Test-Path -LiteralPath $trace)) {throw 'Missing trace.'}
$outputRoot="$RunRoot/CrowdTraceExport"; if(Test-Path -LiteralPath $outputRoot){throw 'Preserve existing export.'}
New-Item -ItemType Directory -Path $outputRoot | Out-Null
@(
 ('TimingInsights.ExportThreads "{0}/Threads.csv"' -f $outputRoot),
 ('TimingInsights.ExportTimerStatistics "{0}/AllTimers.csv"' -f $outputRoot),
 ('TimingInsights.ExportTimerStatistics "{0}/Timers-{{region}}.csv" -region="ProjectJ.*"' -f $outputRoot),
 ('TimingInsights.ExportTimingEvents "{0}/WorkEvents.csv" -timers="ProjectJ_Mass*,ProjectJ_PresentationBudget*,ProjectJ_CombatVFX*" -columns="ThreadId,TimerId,TimerName,StartTime,EndTime,Depth"' -f $outputRoot),
 ('TimingInsights.ExportTimingEvents "{0}/NiagaraEvents.csv" -timers="*Niagara*" -columns="ThreadId,TimerId,TimerName,StartTime,EndTime,Depth"' -f $outputRoot),
 ('TimingInsights.ExportTimingEvents "{0}/AnimationEvents.csv" -timers="EvaluateGraphExposedInputs_*,ExecuteUbergraph_ABP*,FParallelAnimationEvaluationTask,Project_J_Anim*,ProjectJ_Anim*" -columns="ThreadId,TimerId,TimerName,StartTime,EndTime,Depth"' -f $outputRoot)
) | Set-Content "$outputRoot/Export.rsp" -Encoding utf8
$arguments=@("-OpenTraceFile=$trace",'-AutoQuit','-NoUI','-unattended',"-ExecOnAnalysisCompleteCmd=@=$outputRoot/Export.rsp","-ABSLOG=$outputRoot/Insights.log")
$quoted=@($arguments | ForEach-Object {if($_ -match '^([^=]+)=(.*)$'){$Matches[1]+'="'+$Matches[2]+'"'}else{$_}}) -join ' '
$process=Start-Process -FilePath "$EngineRoot/Engine/Binaries/Win64/UnrealInsights.exe" -ArgumentList $quoted -WindowStyle Hidden -Wait -PassThru
if($process.ExitCode -ne 0 -or !(Test-Path "$outputRoot/AllTimers.csv") -or !(Test-Path "$outputRoot/WorkEvents.csv")){throw 'Trace export failed.'}
Write-Output "Exported $outputRoot"
