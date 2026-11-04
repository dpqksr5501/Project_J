param(
 [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName,
 [ValidateRange(0,1)][int]$Encoder=1,
 [ValidateRange(0,20)][int]$PacketLoss=10,
 [ValidateRange(0,200)][int]$PacketLag=50,
 [ValidateRange(1024,65535)][int]$Port=17941,
 [string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8',
 [string]$Executable
)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/Source-Provenance.ps1"
$projectRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\','/')
Set-Location -LiteralPath $projectRoot
$busy=@(Get-Process UnrealEditor*,Project_J*,UnrealBuildTool,dotnet,LiveCodingConsole,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue)
if($busy.Count){throw 'Engine processes are active. Wait for normal completion; no concurrent build/run or forced termination.'}
$runRoot="$projectRoot/Saved/Validation/NPBlobLifecycle/$RunName"
if(Test-Path -LiteralPath $runRoot){throw 'Choose a new run name; preserve previous receipts.'}
New-Item -ItemType Directory -Path $runRoot | Out-Null
$exe=if($Executable){(Resolve-Path -LiteralPath $Executable).Path}else{"$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"}
if(!(Test-Path -LiteralPath $exe -PathType Leaf) -or [IO.Path]::GetExtension($exe) -ine '.exe'){throw 'Missing runtime executable.'}
$before=Get-ProjectJSourceProvenance -ProjectRoot $projectRoot -RuntimePaths @($exe)
$mode=if($Executable){'ListenServer'}else{'DedicatedServer'}
[ordered]@{schema=1;run=$RunName;cohort='NP lifecycle only, not performance';networkMode=$mode;socketClients=2;npModels=1;referenceActors=6;encoder=$Encoder;serverNPObjectDeltaClassRequested='/Script/NetworkPrediction.NetworkPredictionComponent';packetLoss=$PacketLoss;packetLag=$PacketLag;trace=$false;sourceProvenance=$before} |
 ConvertTo-Json -Depth 7 | Set-Content -LiteralPath "$runRoot/manifest.json" -Encoding utf8
$common=@('-game','-unattended','-nop4','-nosplash','-NullRHI','-nosound','-NoLiveCoding','-NoDDCCleanup','-SaveToUserDir','-ProjectJNetworkFixture','-ProjectJNPBlobLifecycle',"-PktLag=$PacketLag","-PktLoss=$PacketLoss")
if(!$Executable){$common=@("$projectRoot/Project_J.uproject")+$common+@('-DisablePlugins=EditorToolset,AutomationTestToolset,ToolsetRegistry','-DisablePython')}
function Start-NPProbe([string]$Role,[string[]]$Arguments){
 $argv=$common+$Arguments+@("-ABSLOG=$runRoot/$Role.log","-ProjectJDOutput=$runRoot/$Role","-UserDir=$runRoot/${Role}User/")
 $argv|ConvertTo-Json|Set-Content -LiteralPath "$runRoot/$Role-arguments.json" -Encoding utf8
 $quoted=@($argv|ForEach-Object{'"'+$_+'"'}) -join ' '
 Start-Process -FilePath $exe -ArgumentList $quoted -WindowStyle Hidden -PassThru
}
$map='/Engine/Maps/Entry?game=/Script/Project_J.Project_JNPBlobLifecycleGameMode'
if($Executable){$map+='?listen'}
$serverArgs=@($map,"-port=$Port","-ExecCmds=t.MaxFPS 30,np.ProjectJ.ReplicationBlobDelta $Encoder,CsvCategory ProjectJNPBlobDelta enable,csvprofile START")
# This explicit lifecycle experiment uses the same server-only class policy in
# encoder0 and1. Pawn policy does not propagate to its NP subobject.
$serverArgs += '-ini:Engine:[/Script/IrisCore.ObjectReplicationBridgeConfig]:+DeltaCompressionConfigs=(ClassName=/Script/NetworkPrediction.NetworkPredictionComponent,bEnableDeltaCompression=true)'
if(!$Executable){$serverArgs+='-server'}
$server=Start-NPProbe 'Server' $serverArgs
$readyDeadline=[DateTime]::UtcNow.AddSeconds(120)
while(!(Test-Path -LiteralPath "$runRoot/Server.log") -or !(Select-String -LiteralPath "$runRoot/Server.log" -SimpleMatch 'NPBLOB_READY Iris=1 References=6 Models=1 Clients=2 ' -Quiet)){
 $server.Refresh()
 if($server.HasExited -or [DateTime]::UtcNow -gt $readyDeadline){throw "Server readiness failed. Preserve $runRoot; allow existing processes to exit normally."}
 Start-Sleep -Milliseconds 500
}
$clients=@(1..2|ForEach-Object{Start-NPProbe "Client$_" @("127.0.0.1:$Port",'-ExecCmds=t.MaxFPS 30,CsvCategory ProjectJNPBlobDelta enable,csvprofile START')})
$processes=@($server)+$clients
$handles=@($processes|ForEach-Object{$_.Handle}) # Retain every process handle before any wait.
$processes|Select-Object Id,ProcessName|ConvertTo-Json|Set-Content -LiteralPath "$runRoot/processes.json" -Encoding utf8
Write-Output "NP lifecycle only: $runRoot"
foreach($process in $processes){$process.WaitForExit()}
$after=Get-ProjectJSourceProvenance -ProjectRoot $projectRoot -RuntimePaths @($exe)
$provenanceMatches=$false
try{Assert-ProjectJMatchingProvenance -Provenance @($before,$after);$provenanceMatches=$true}catch{}
function Read-NPCsv([string]$Role){
 $files=@(Get-ChildItem -LiteralPath "$runRoot/${Role}User/Saved/Profiling/CSV" -Filter '*.csv' -File -ErrorAction SilentlyContinue)
 if($files.Count -ne 1){return $null}
 # UE finalizes a COMPLETE summary header after numeric rows, then [HasHeaderRowAtEnd] metadata.
 # Series created after capture start can be absent from the first header. Do not parse that incomplete header.
 $lines=@(Get-Content -LiteralPath $files[0].FullName)
 $headerRows=@(for($lineIndex=0;$lineIndex -lt $lines.Count;$lineIndex++){if($lines[$lineIndex].StartsWith('EVENTS,')){$lineIndex}})
 if($headerRows.Count -lt 2){return $null}
 $finalHeader=$headerRows[-1]
 if($finalHeader -lt 2 -or $finalHeader+1 -ge $lines.Count -or $lines[$finalHeader+1] -notmatch '^\[HasHeaderRowAtEnd\],1(?:,|$)'){return $null}
 # Stock UE captures may contain duplicate stat names. Preserve their positions without inventing/dropping columns.
 $headerReader=[Microsoft.VisualBasic.FileIO.TextFieldParser]::new([IO.StringReader]::new($lines[$finalHeader]))
 $headerReader.SetDelimiters(',');$headerReader.HasFieldsEnclosedInQuotes=$true
 try{$headers=@($headerReader.ReadFields())}finally{$headerReader.Dispose()}
 $columnKeys=@(0..($headers.Count-1)|ForEach-Object{"C$_"})
 $data=@($lines[1..($finalHeader-1)]|ConvertFrom-Csv -Header $columnKeys)
 $completed=Select-String -LiteralPath "$runRoot/$Role.log" -SimpleMatch 'Capture Ended. Writing CSV to file :' -Quiet
 if(!$completed){return $null}
 [ordered]@{path=$files[0].FullName;headers=$headers;keys=$columnKeys;rows=$data}
}
function Counter($Csv,[string]$Name,[switch]$Maximum){
 if(!$Csv -or !$Csv.rows.Count){return 0.0}
 $indexes=@(for($column=0;$column -lt $Csv.headers.Count;$column++){if($Csv.headers[$column] -eq "ProjectJNPBlobDelta/$Name" -or $Csv.headers[$column] -eq "ProjectJNPBlobDelta.$Name"){$column}})
 if(!$indexes.Count){return 0.0}
 if($indexes.Count -ne 1){return [double]::NaN} # Ambiguous target counter must fail, not hide duplicate errors.
 $key=$Csv.keys[$indexes[0]]
 $values=@($Csv.rows|ForEach-Object{$v=0.0;if([double]::TryParse([string]$_.$key,[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$v)){$v}})
 if(!$values.Count){return 0.0}
 if($Maximum){return ($values|Measure-Object -Maximum).Maximum}
 ($values|Measure-Object -Sum).Sum
}
$roles=@('Server','Client1','Client2');$receipts=@();$allPassed=$provenanceMatches;$clientRoles=@()
for($i=0;$i -lt 3;$i++){
 $role=$roles[$i];$resultFile="$runRoot/$role/np-lifecycle.json";$rowFile="$runRoot/$role/np-lifecycle.csv"
 $result=if(Test-Path -LiteralPath $resultFile){Get-Content -LiteralPath $resultFile -Raw|ConvertFrom-Json}else{$null}
 $rows=if(Test-Path -LiteralPath $rowFile){@(Import-Csv -LiteralPath $rowFile)}else{@()}
 $csv=Read-NPCsv $role
 $rowCount=if($i -eq 0){26}else{13}
 $casesOk=$rows.Count -eq $rowCount -and @($rows|Where-Object{$_.exact -ne '1'}).Count -eq 0
 $caseKeys=@($rows|ForEach-Object{if($i -eq 0){"$($_.slot):$($_.case)"}else{$_.case}}|Sort-Object -Unique)
 $casesOk=$casesOk -and $caseKeys.Count -eq $rowCount -and @($rows|Where-Object{[int]$_.case -lt 0 -or [int]$_.case -gt 12 -or [int]$_.generation -ne $(if($_.case -eq '12'){2}else{1})}).Count -eq 0
 # API empty witness must be present independently in EACH client's case7; root marker alone cannot pass.
 $emptyOk=if($i -eq 0){@($rows|Where-Object{$_.case -eq '7' -and [uint64]$_.empty_delta -gt 0}).Count -eq 2}else{@($rows|Where-Object{$_.case -eq '7' -and [uint64]$_.empty_delta -gt 0}).Count -eq 1}
 # Login completion order may swap Client1/Client2. Infer each client's real role from its ordinary first receive.
 $observedRole=$null
 if($i -eq 0){
  $rolesOk=@($rows|Where-Object{($_.slot -eq '0' -and $_.role -eq '2') -or ($_.slot -eq '1' -and $_.role -eq '1')}).Count -eq 26 -and @($rows|Where-Object{$_.slot -eq '0'}).Count -eq 13 -and @($rows|Where-Object{$_.slot -eq '1'}).Count -eq 13
 }else{
  $ordinary=@($rows|Where-Object{$_.absent -eq '0'})
  if($ordinary.Count){$observedRole=[int]$ordinary[0].role}
  $rolesOk=$observedRole -in @(1,2) -and @($rows|Where-Object{[int]$_.role -ne $observedRole}).Count -eq 0
  $clientRoles+=@($observedRole)
 }
 $counterEmpty=Counter $csv 'EmptyDecodeCount'
 $maxBits=Counter $csv 'QuantizeBlobBitsMax' -Maximum
 $deltaCount=Counter $csv 'WordDeltaCount'
 $deltaDecodeCount=Counter $csv 'WordDeltaDecodeCount'
 $codecErrors=Counter $csv 'SerializerContextErrorCount'
 $writerOverflows=Counter $csv 'SerializerWriterOverflowAttemptCount'
 $writeRejectedAttempts=Counter $csv 'SerializerErrorOrOverflowCount'
 $categoryEnabled=Select-String -LiteralPath "$runRoot/$role.log" -SimpleMatch "CsvProfiler: category 'ProjectJNPBlobDelta' is now enabled." -Quiet
 $csvOk=$categoryEnabled -and $null -ne $csv -and $codecErrors -eq 0 -and $writeRejectedAttempts -eq ($codecErrors+$writerOverflows) -and $(if($i -eq 0){$maxBits -gt 4096 -and $maxBits -le 16384 -and $(if($Encoder -eq 1){$deltaCount -gt 0}else{$deltaCount -eq 0})}else{$counterEmpty -gt 0 -and $(if($Encoder -eq 1){$deltaDecodeCount -gt 0}else{$deltaDecodeCount -eq 0})})
 $passed=$null -ne $result -and $result.success -and $processes[$i].ExitCode -eq 0 -and $casesOk -and $rolesOk -and $emptyOk -and $csvOk
 $allPassed=$allPassed -and $passed
 $receipts += [ordered]@{role=$role;observedNetworkRole=$observedRole;networkRolesValid=$rolesOk;passed=$passed;exitCode=$processes[$i].ExitCode;rows=$rows.Count;apiEmptyWitness=$emptyOk;csvCategoryEnabled=$categoryEnabled;csvEmptyDecode=$counterEmpty;quantizedBitsMax=$maxBits;wordDeltaCount=$deltaCount;wordDeltaDecodeCount=$deltaDecodeCount;codecErrors=$codecErrors;writerOverflowAttempts=$writerOverflows;writeRejectedAttempts=$writeRejectedAttempts;writeErrorScope='Serializer write context errors; decode validity remains real lifecycle receipts/engine outcomes';reason=$result.reason;profilerCsv=$(if($csv){$csv.path}else{$null})}
}
$roleAssignmentOk=$clientRoles.Count -eq 2 -and @($clientRoles|Where-Object{$_ -eq 1}).Count -eq 1 -and @($clientRoles|Where-Object{$_ -eq 2}).Count -eq 1
$allPassed=$allPassed -and $roleAssignmentOk
[ordered]@{passed=$allPassed;clientRoleAssignmentValid=$roleAssignmentOk;sourceRuntimeUnchanged=$provenanceMatches;receipts=$receipts;limitation='Actual engine ACK/delta baselines and observed loss/reentry convergence; specific lost-baseline identity and long-term leak absence are not asserted. Not a 200-agent performance result.'} |
 ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$runRoot/verification.json" -Encoding utf8
if(!$allPassed){throw "NP lifecycle verification failed: $runRoot/verification.json"}
Write-Output "NP lifecycle passed; performance remains separate: $runRoot/verification.json"
