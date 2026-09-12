param(
 [Parameter(Mandatory=$true)][string]$RunRoot,
 [string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference='Stop'
if(@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker,UnrealInsights -ErrorAction SilentlyContinue).Count){throw 'Wait for engine/analysis completion.'}
$root=(Resolve-Path -LiteralPath $RunRoot).Path.Replace('\','/')
if(!(Test-Path -LiteralPath "$root/Run.utrace")){throw 'Missing trace.'}
$out="$root/TraceExport"
if(Test-Path -LiteralPath $out){throw 'Preserve existing export.'}
New-Item -ItemType Directory $out|Out-Null
@(
 ('TimingInsights.ExportThreads "{0}/Threads.csv"' -f $out),
 ('TimingInsights.ExportTimerStatistics "{0}/AllTimers.csv"' -f $out),
 ('TimingInsights.ExportTimingEvents "{0}/ExperimentEvents.csv" -timers="ProjectJ_F_*,ProjectJ.F.*,AudioMixer*,*PCG*" -columns="ThreadId,TimerId,TimerName,StartTime,EndTime,Depth"' -f $out)
)|Set-Content "$out/Export.rsp" -Encoding utf8
$arguments=@("-OpenTraceFile=$root/Run.utrace",'-AutoQuit','-NoUI','-unattended',"-ExecOnAnalysisCompleteCmd=@=$out/Export.rsp","-ABSLOG=$out/Insights.log")
$quoted=@($arguments|ForEach-Object {if($_ -match '^([^=]+)=(.*)$'){$Matches[1]+'="'+$Matches[2]+'"'}else{$_}})-join ' '
$process=Start-Process -FilePath "$EngineRoot/Engine/Binaries/Win64/UnrealInsights.exe" -ArgumentList $quoted -WindowStyle Hidden -Wait -PassThru
if($process.ExitCode -ne 0 -or !(Test-Path "$out/ExperimentEvents.csv")){throw "Export failed: $out/Insights.log"}
