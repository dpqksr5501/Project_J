# Executes the real comparison script with disposable synthetic trace inputs.
# No performance claim, engine process, Git commit, or project asset mutation.
$ErrorActionPreference='Stop'
$repo=(Resolve-Path "$PSScriptRoot/../..").Path
$parent=Join-Path $repo "Saved/Audit6850/Round3/ProvenanceTests/$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $parent|Out-Null
$bad=@()
foreach($case in @('Same','DifferentHead','MissingProvenance','ConfigChange','BinaryChange','DeletedSource')){
 $root=Join-Path $parent $case
 foreach($run in @('A','B','C','M')){
  $dir=Join-Path $root $run
  New-Item -ItemType Directory -Path "$dir/TraceExportFinal","$dir/Server","$dir/Metrics" -Force|Out-Null
  @{passed=$true}|ConvertTo-Json|Set-Content "$dir/verification.json" -Encoding utf8
  $provenance=[ordered]@{schema=1;head=('a'*40);files=@(@{path='Source/One.cpp';sha256=('1'*64);deleted=$false},@{path='Config/DefaultGame.ini';sha256=('2'*64);deleted=$false});binaries=@(@{path='Binaries/Win64/Project_J.exe';sha256=('3'*64)})}
  if($run -eq 'B'){
   switch($case){
    'DifferentHead' {$provenance.head='b'*40}
    'MissingProvenance' {$provenance=$null}
    'ConfigChange' {$provenance.files[1].sha256='4'*64}
    'BinaryChange' {$provenance.binaries[0].sha256='5'*64}
    'DeletedSource' {$provenance.files[0].deleted=$true;$provenance.files[0].sha256=$null}
   }
  }
  @{head=if($case -eq 'DifferentHead' -and $run -eq 'B'){'b'*40}else{'a'*40};sourceFiles=@();sourceProvenance=$provenance}|ConvertTo-Json -Depth 8|Set-Content "$dir/manifest.json" -Encoding utf8
  @('Id,Name,Group','1,GameThread,Game','2,Worker,ThreadPool')|Set-Content "$dir/TraceExportFinal/Threads.csv"
  @('stage,seconds,connections,out_bytes','1,1,2,100','2,1,2,100','3,1,2,100','4,1,2,100','5,1,2,100')|Set-Content "$dir/Server/network-phases.csv"
  foreach($stage in 1..5){@('TimerName,ThreadId,StartTime,EndTime','NetDriver TickFlush,1,0,0.01','STAT_NetDriver_TickClientConnections,1,0,0.02','NetConnection Tick,2,0,0.03')|Set-Content "$dir/TraceExportFinal/Events-ProjectJ.Network.Stage$stage.csv"}
  @('count,mode,joined_step_ms','100,baseline,1')|Set-Content "$dir/Metrics/mass-values.csv"
  @('TimerName,ThreadId,StartTime,EndTime','ProjectJ_MassMovement_Chunk,2,0,0.01')|Set-Content "$dir/TraceExportFinal/MassEvents.csv"
 }
 $threw=$false;$message=''
 try{& "$repo/Scripts/Validation/Compare-DStage.ps1" -Root $root -MassRun M -SpatialRun A -AllRelevantRun B -ParallelRun C|Out-Null}catch{$threw=$true;$message=$_.Exception.Message}
 $expected=$case -ne 'Same'
 [pscustomobject]@{scenario=$case;expectedRejected=$expected;rejected=$threw;message=$message;fixture=$root}
 if($threw -ne $expected){$bad+=$case}
}
if($bad.Count){throw "Provenance regression failed: $($bad -join ', ')"}
