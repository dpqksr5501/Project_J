param([Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
 [ValidateSet('Baseline','LagLoss','ReorderLoss')][string]$Condition='Baseline',
 [ValidateRange(20000,60000)][int]$Port=27981,
 [string]$Montage='')
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
$out="$root/Saved/Audit6850/Round3/Network/$Name"
$active=@(Get-CimInstance Win32_Process|Where-Object {$_.Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|Uba).*'})
if($active.Count){throw 'Wait for all engine/build processes to finish normally.'}
if(Test-Path -LiteralPath $out){throw 'Preserve prior evidence.'}
New-Item -ItemType Directory -Path $out|Out-Null
. "$root/Scripts/Validation/Source-Provenance.ps1"
$exe='C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$provenance=Get-ProjectJSourceProvenance -ProjectRoot $root -RuntimePaths @($exe)
$packets=switch($Condition){
 'Baseline' {@('-PktLag=0','-PktLagVariance=0','-PktLoss=0','-PktOrder=0','-PktDup=0')}
 'LagLoss' {@('-PktLag=75','-PktLagVariance=25','-PktLoss=10','-PktOrder=0','-PktDup=5')}
 'ReorderLoss' {@('-PktLag=0','-PktLagVariance=0','-PktLoss=10','-PktOrder=1','-PktDup=15')}
}
$common=@("$root/Project_J.uproject",'-game','-unattended','-nop4','-nosplash','-nosound','-NoLiveCoding','-NoDDCCleanup','-SaveToUserDir',
 '-ProjectJAuditNetwork','-DisablePlugins=EditorToolset,AutomationTestToolset,ToolsetRegistry','-DisablePython',
 "-ProjectJAuditOutput=$out","-ProjectJDOutput=$out",'-ExecCmds=t.MaxFPS 30,Log LogProjectJCombatHitValidation Verbose')+$packets
if($Montage){$common+="-ProjectJAuditMontage=$Montage"}
@{condition=$Condition;packets=$packets;port=$Port;sourceProvenance=$provenance;scope='Actual dedicated server and two socket clients; real inherited ServerRequestSSRHit and health effects; cue multicast/property recovery. No performance claim.'}|ConvertTo-Json -Depth 8|Set-Content "$out/manifest.json" -Encoding utf8
function Start-AuditProcess([string[]]$Arguments){
 Start-Process -FilePath $exe -ArgumentList (($Arguments|ForEach-Object{'"'+$_+'"'})-join ' ') -WindowStyle Hidden -PassThru
}
$serverArgs=$common+@('/Engine/Maps/Entry?game=/Script/Project_J.Project_JAuditNetworkGameMode','-server','-NullRHI',"-port=$Port","-ABSLOG=$out/Server.log","-UserDir=$out/ServerUser/")
$serverArgs|ConvertTo-Json|Set-Content "$out/server-arguments.json"
$server=Start-AuditProcess $serverArgs
$serverHandle=$server.Handle
$deadline=[DateTime]::UtcNow.AddSeconds(90)
while(!(Test-Path "$out/Server.log")-or !(Select-String -LiteralPath "$out/Server.log" -SimpleMatch 'LogProjectJAuditNetwork: Display: PACKETS' -Quiet)){
 $server.Refresh()
 if($server.HasExited){@{exitCode=$server.ExitCode;phase='server-startup'}|ConvertTo-Json|Set-Content "$out/verification.json";throw 'Server exited before readiness; raw log retained.'}
 if([DateTime]::UtcNow -gt $deadline){throw 'Server startup deadline; leave process to its bounded native lifetime, never force-stop.'}
 Start-Sleep -Milliseconds 500
}
$clients=@(0..1|ForEach-Object{
 $slot=$_
 $argsForClient=$common+@("127.0.0.1:$Port",'-RenderOffscreen','-windowed','-ResX=320','-ResY=180',"-ProjectJAuditSlot=$slot","-ABSLOG=$out/Client$slot.log","-UserDir=$out/Client${slot}User/")
 $argsForClient|ConvertTo-Json|Set-Content "$out/client$slot-arguments.json"
 Start-AuditProcess $argsForClient
})
$processes=@($server)+$clients
$handles=@($processes|ForEach-Object{$_.Handle})
$processes|Select-Object Id,ProcessName|ConvertTo-Json|Set-Content "$out/processes.json"
Write-Output "Started dedicated server and two clients: $out"
foreach($p in $processes){$p.WaitForExit()}
$exits=@($processes|ForEach-Object{$_.ExitCode})
$receipts=@('server.txt','client_0.txt','client_1.txt'|ForEach-Object{
 $file="$out/$_";@{file=$_;exists=(Test-Path -LiteralPath $file);passed=((Test-Path -LiteralPath $file)-and (Get-Content -LiteralPath $file -Raw)-match 'success=1\b')}
})
$timeouts=@(Get-ChildItem -LiteralPath $out -Filter 'timeout*.json')
$passed=@($exits|Where-Object{$null -eq $_ -or $_ -ne 0}).Count -eq 0 -and @($receipts|Where-Object{!$_.passed}).Count -eq 0 -and !$timeouts.Count
@{passed=$passed;exitCodes=$exits;receipts=$receipts;timeouts=$timeouts.Count;nativeHandlesCapturedBeforeWait=$true}|ConvertTo-Json -Depth 5|Set-Content "$out/verification.json"
if(!$passed){throw "Actual network validation failed/incomplete: $out"}
Write-Output "Actual network assertions passed: $out"
