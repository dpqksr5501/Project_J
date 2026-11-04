param([Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Name,
 [ValidateSet('Baseline','LagLoss','ReorderLoss')][string]$Condition='Baseline',
 [ValidateRange(20000,60000)][int]$Port=27981,
 [string]$Montage='',
 [switch]$SSROnly,
 [switch]$WarningOnly,
 [switch]$NPCAssemblyOnly,
 [switch]$QuestHistoryOnly,
 [switch]$TrustedSessionOnly,
 [switch]$MountOnly,
 [switch]$MountReplicationRestart,
 [switch]$MountSpatialReentry,
 [switch]$ReverseClientLaunchOrder)
$ErrorActionPreference='Stop'
if($QuestHistoryOnly -and ($NPCAssemblyOnly -or $TrustedSessionOnly -or $WarningOnly -or $SSROnly -or $MountOnly -or $Montage -or $MountReplicationRestart -or $MountSpatialReentry -or $ReverseClientLaunchOrder -or $Condition -notin @('Baseline','LagLoss'))){throw 'QuestHistoryOnly is an isolated Baseline/LagLoss owner-page/foreign-privacy cohort.'}
if($NPCAssemblyOnly -and ($TrustedSessionOnly -or $WarningOnly -or $SSROnly -or $MountOnly -or $Montage -or $MountReplicationRestart -or $MountSpatialReentry -or $ReverseClientLaunchOrder)){throw 'NPCAssemblyOnly uses one initial slot0 and a native-ready late slot1; no other cohort or reversed launch order.'}
if($TrustedSessionOnly -and ($WarningOnly -or $SSROnly -or $MountOnly -or $Montage -or $Condition -ne 'Baseline')){throw 'TrustedSessionOnly uses its own baseline session/ownership cohort without warning, SSR, montage or mount cases.'}
if($WarningOnly -and ($SSROnly -or $MountOnly -or $Montage)){throw 'WarningOnly uses its own cold-profile warning cohort, without SSR, montage or mount cases.'}
if($MountReplicationRestart -and !$MountOnly){throw 'MountReplicationRestart requires MountOnly.'}
if($MountSpatialReentry -and (!$MountOnly -or $MountReplicationRestart)){throw 'MountSpatialReentry requires MountOnly and is separate from the deliberate replication-restart probe.'}
if($MountOnly -and ($SSROnly -or $Montage)){throw 'MountOnly uses a separate owner/audience cohort, without SSR or montage-rate options.'}
$root=(Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
$out="$root/Saved/Audit6850/Round3/Network/$Name"
$active=@(Get-CimInstance Win32_Process|Where-Object {$_.Name -match '^(UnrealEditor|Project_J|UnrealBuildTool|dotnet|LiveCoding|MSBuild|ShaderCompileWorker|Uba).*'})
if($active.Count){throw 'Wait for all engine/build processes to finish normally.'}
if(Test-Path -LiteralPath $out){throw 'Preserve prior evidence.'}
New-Item -ItemType Directory -Path $out|Out-Null
. "$root/Scripts/Validation/Source-Provenance.ps1"
. "$root/Scripts/Audit/Continuation-ScratchPaths.ps1"
$scratch=New-ContinuationScratchRun (Get-ContinuationScratchContext $root) "AuditNetwork_$Name"
$exe='C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$provenance=Get-ProjectJSourceProvenance -ProjectRoot $root -RuntimePaths @($exe)
$packets=switch($Condition){
 'Baseline' {@('-PktLag=0','-PktLagVariance=0','-PktLoss=0','-PktOrder=0','-PktDup=0')}
 'LagLoss' {@('-PktLag=75','-PktLagVariance=25','-PktLoss=10','-PktOrder=0','-PktDup=5')}
 'ReorderLoss' {@('-PktLag=0','-PktLagVariance=0','-PktLoss=10','-PktOrder=1','-PktDup=15')}
}
$fixtureExecCommands=if($WarningOnly -or $NPCAssemblyOnly){'t.MaxFPS 30,Log LogProjectJCombatPresentation Verbose'}else{'t.MaxFPS 30,Log LogProjectJCombatHitValidation Verbose'}
$common=@("$root/Project_J.uproject",'-game','-unattended','-nop4','-nosplash','-nosound','-NoLiveCoding','-NoDDCCleanup','-SaveToUserDir',
 '-ProjectJAuditNetwork','-DisablePlugins=EditorToolset,AutomationTestToolset,ToolsetRegistry','-DisablePython',
 "-ProjectJAuditOutput=$out","-ProjectJDOutput=$out","-ExecCmds=$fixtureExecCommands")+$packets
if($Montage){$common+="-ProjectJAuditMontage=$Montage"}
if($SSROnly){$common+='-ProjectJAuditSSROnly'}
if($WarningOnly){$common+='-ProjectJAuditWarningOnly'}
if($NPCAssemblyOnly){$common+='-ProjectJAuditNPCAssemblyOnly'}
if($QuestHistoryOnly){$common+='-ProjectJAuditQuestHistoryOnly'}
if($TrustedSessionOnly){$common+='-ProjectJAuditTrustedSessionOnly'}
if($MountOnly){$common+='-ProjectJAuditMountOnly'}
if($MountReplicationRestart){$common+='-ProjectJAuditMountReplicationRestart'}
if($MountSpatialReentry){$common+='-ProjectJAuditMountSpatialReentry'}
$clientLaunchOrder=if($ReverseClientLaunchOrder){@(1,0)}else{@(0,1)}
$scope=if($QuestHistoryOnly){'Actual two socket owners; native private quest summary, requested32/32/1 pages, foreign projection exclusion, authoritative append and stale/requery. Server fixture seeds validated immutable values; no reward execution, UI pixels, CCU, GPU, or network persistence claim.'}elseif($NPCAssemblyOnly){'Actual dedicated editor server and two RHI socket clients; plain production NPC missing-component assembly, real late-client initial warning-state recovery, retained-component restart and normal actor retirement. No montage/GE, A07 AOI, arbitrary NPC visuals, capacity or performance claim.'}elseif($TrustedSessionOnly){'Actual dedicated server and two socket clients; trusted in-process test identities, admission hold, character takeover, old PS/ASC RPC permission rejection, and captured application fence only. No external authentication, DB restore, socket reconnect restoration or arbitrary queued-RPC epoch claim.'}elseif($WarningOnly){'Actual dedicated server and two RHI socket clients; production reliable warning snapshots, same-frame pulse entry/final state, and long cold-profile marker lifecycle only. No pulse visibility, damage timing, offscreen HUD, capacity or performance claim.'}elseif($MountSpatialReentry){'Actual dedicated server and two socket clients: opt-in spatial fixture riders/mount from initial spawn, unchanged normal18-row cohort, then detached native view drives ordinary AOI retirement/reentry. Same authoritative actor/fullhandle and owner/private state; new foreign replica incarnation/public reference recovery required. Stored camera observations are not a private Iris consumed-view getter. No Stop/Start, travel, movement quality, capacity or performance claim.'}elseif($MountReplicationRestart){'Actual dedicated server and two socket clients: unchanged normal mount owner/audience cohort followed by one actual Iris stop/re-registration on the same live mounted actor. Requires separate authority native-group/registration lifecycle evidence and both sockets restoring public ASC/private isolation before production owner dismount. Requested inclusion is intent evidence, not a private native allow-bit or remote ACK. No movement-quality, capacity or performance claim.'}elseif($MountOnly){'Actual dedicated server and two socket clients: both mount owners, foreign simulated public ASC/attributes, replicated recipient rejection/recovery, production owner dismount RPCs and owner-only inventory/equipment. Stable fixture kinematics; no UI screen, movement-quality, capacity or performance claim.'}elseif($SSROnly){'Actual dedicated server and two socket clients; six inherited SSR RPC/health cases and exact float64 timestamp round-trip only. Cue, movement-event and montage-rate cases excluded. No performance claim.'}else{'Actual dedicated server and two socket clients; real inherited ServerRequestSSRHit and health effects; cue multicast/property recovery. No performance claim.'}
@{condition=$Condition;packets=$packets;port=$Port;explicitSlotSource='Canonical InitNewPlayer URL Options, exact unique0/1, sorted server client index';clientLaunchOrder=$clientLaunchOrder;reverseAdmissionBarrier=[bool]$ReverseClientLaunchOrder;sourceProvenance=$provenance;trustedSessionOnly=[bool]$TrustedSessionOnly;warningOnly=[bool]$WarningOnly;npcAssemblyOnly=[bool]$NPCAssemblyOnly;questHistoryOnly=[bool]$QuestHistoryOnly;ssrOnly=[bool]$SSROnly;mountOnly=[bool]$MountOnly;mountReplicationRestart=[bool]$MountReplicationRestart;mountSpatialReentry=[bool]$MountSpatialReentry;mountRegistrationTrigger=$(if($MountSpatialReentry){'Native detached view drives ordinary AOI out/in; no Stop/Start or relevance flag transition after spawn'}else{'Public FReplicationSystemUtil Stop/Start; bReplicates/RemoteRole untouched'});scope=$scope;scratchRoot=$scratch.Root;serverUserDirectory="$($scratch.Root)/ServerUser";clientUserDirectories=@("$($scratch.Root)/Client0User","$($scratch.Root)/Client1User")}|ConvertTo-Json -Depth 8|Set-Content "$out/manifest.json" -Encoding utf8
function Start-AuditProcess([string[]]$Arguments){
 Start-Process -FilePath $exe -ArgumentList (($Arguments|ForEach-Object{'"'+$_+'"'})-join ' ') -WindowStyle Hidden -PassThru
}
$serverArgs=$common+@('/Engine/Maps/Entry?game=/Script/Project_J.Project_JAuditNetworkGameMode','-server','-NullRHI',"-port=$Port","-ABSLOG=$out/Server.log","-UserDir=$($scratch.Root)/ServerUser/")
$serverArgs|ConvertTo-Json|Set-Content "$out/server-arguments.json"
$server=$null;$clients=@();$handles=@();$startupError=$null;$sourceRuntimeUnchanged=$false;$afterProvenanceError=$null
$npcAssemblyLaunchBarrier=[ordered]@{requested=[bool]$NPCAssemblyOnly;passed=!$NPCAssemblyOnly;nativeReadyLine=$null}
try {
 $server=Start-AuditProcess $serverArgs
 $handles+=$server.Handle
 $deadline=[DateTime]::UtcNow.AddSeconds(90)
 while(!(Test-Path "$out/Server.log")-or !(Select-String -LiteralPath "$out/Server.log" -SimpleMatch 'LogProjectJAuditNetwork: Display: PACKETS' -Quiet)){
  $server.Refresh()
  if($server.HasExited){throw 'Server exited before readiness; raw log retained.'}
  if([DateTime]::UtcNow -gt $deadline){throw 'Server startup deadline; leave process to its bounded native lifetime, never force-stop.'}
  Start-Sleep -Milliseconds 500
 }
 foreach($slot in $clientLaunchOrder){
  $argsForClient=$common+@("127.0.0.1:${Port}?ProjectJAuditSlot=$slot",'-RenderOffscreen','-windowed','-ResX=320','-ResY=180',"-ProjectJAuditSlot=$slot","-ABSLOG=$out/Client$slot.log","-UserDir=$($scratch.Root)/Client${slot}User/")
  $argsForClient|ConvertTo-Json|Set-Content "$out/client$slot-arguments.json"
  $startedClient=Start-AuditProcess $argsForClient
  $clients+=$startedClient
  $handles+=$startedClient.Handle
  if($NPCAssemblyOnly -and $slot -eq 0){
   # One real late join: first client must observe native assembly/active state before starting slot1.
   $lateJoinDeadline=[DateTime]::UtcNow.AddSeconds(45)
   $nativeReady=@()
   while(!$nativeReady.Count){
    $nativeReady=@(Select-String -LiteralPath "$out/Server.log" -SimpleMatch 'NPC_ASSEMBLY_LATE_JOIN_READY case=70 slot=1 step=1')
    $server.Refresh();$startedClient.Refresh()
    if($server.HasExited -or $startedClient.HasExited -or [DateTime]::UtcNow -gt $lateJoinDeadline){throw 'Native NPC assembly/first-client active acknowledgement did not admit the late client; preserve logs and drain normally.'}
    if(!$nativeReady.Count){Start-Sleep -Milliseconds 100}
   }
   if($nativeReady.Count -ne 1){throw 'NPC late-join readiness must have exactly one actual native producer line.'}
   $npcAssemblyLaunchBarrier.passed=$true;$npcAssemblyLaunchBarrier.nativeReadyLine=$nativeReady[0].Line
  }
  if($ReverseClientLaunchOrder -and $slot -eq 1){
   # Reproduce the actual first-slot1 boundary, not a sleep/order assumption.
   # Both processes retain their native bounded lifetime; never kill on timeout.
   $admissionDeadline=[DateTime]::UtcNow.AddSeconds(45)
   while(!(Select-String -LiteralPath "$out/Server.log" -SimpleMatch 'SLOT_REGISTERED slot=1 count=1 source=LoginOptions' -Quiet)){
    $server.Refresh();$startedClient.Refresh()
    if($server.HasExited -or $startedClient.HasExited -or [DateTime]::UtcNow -gt $admissionDeadline){throw 'Slot1 did not register first; preserve logs and let remaining processes finish their bounded native lifetime.'}
    Start-Sleep -Milliseconds 100
   }
  }
 }
}
catch {
 $startupError=$_.Exception.Message
 @{passed=$false;error=$startupError;phase='startup';startedClients=$clients.Count}|ConvertTo-Json|Set-Content "$out/startup-failure.json" -Encoding utf8
}
finally {
 # Readiness/barrier failures use the same bounded native drain as success.
 # Keep every started Process/Handle alive before waiting; never kill or restart.
 $processes=@(@($server)+$clients|Where-Object{$null -ne $_})
 $processes|Select-Object Id,ProcessName|ConvertTo-Json|Set-Content "$out/processes.json" -Encoding utf8
 Write-Output "Draining $($processes.Count) started audit processes normally: $out"
 foreach($p in $processes){$p.WaitForExit()}
 $exits=@($processes|ForEach-Object{$_.ExitCode})
 $exits|ConvertTo-Json|Set-Content "$out/native-exit-codes.json" -Encoding utf8
 try {
  $after=Get-ProjectJSourceProvenance -ProjectRoot $root -RuntimePaths @($exe)
  $after|ConvertTo-Json -Depth 8|Set-Content -LiteralPath "$out/AfterSourceProvenance.json" -Encoding utf8
  Assert-ProjectJMatchingProvenance -Provenance @($provenance,$after)
  $sourceRuntimeUnchanged=$true
 }
 catch {
  $afterProvenanceError=$_.Exception.Message
  @{passed=$false;error=$afterProvenanceError}|ConvertTo-Json|Set-Content "$out/provenance-failure.json" -Encoding utf8
 }
}
$receipts=@('server.txt','client_0.txt','client_1.txt'|ForEach-Object{
 $file="$out/$_";@{file=$_;exists=(Test-Path -LiteralPath $file);passed=((Test-Path -LiteralPath $file)-and (Get-Content -LiteralPath $file -Raw)-match 'success=1\b')}
})
$timeouts=@(Get-ChildItem -LiteralPath $out -Filter 'timeout*.json')
$mountValidation=[ordered]@{passed=$true;requested=[bool]$MountOnly}
if($MountOnly){
    $registration=[ordered]@{requested=[bool]$MountReplicationRestart;passed=!$MountReplicationRestart;present=$false;rows=0;phases=@{};scope='Requested native inclusion and exact current bridge/group membership; no private allow-bit or remote ACK proof.'}
    $restartMountId=''
    if($MountReplicationRestart){
        $registrationPath="$out/mount-scope-registration.csv";$registrationPresent=Test-Path -LiteralPath $registrationPath -PathType Leaf
        $registrationRows=@(if($registrationPresent){Import-Csv -LiteralPath $registrationPath})
        $phaseOrder=@('warm','stopped','restored','dismounted');$phases=@{};$position=0
        $registrationValid=$registrationPresent -and $registrationRows.Count -eq 4
        foreach($r in $registrationRows){
            $phase=[string]$r.phase
            $registrationValid=$registrationValid -and !$phases.ContainsKey($phase) -and $phase -ceq $phaseOrder[$position] -and $r.observer_slot -ceq '-1' -and $r.owner_slot -ceq '0'
            foreach($field in @('group_token','groups_created','rider_id')){$registrationValid=$registrationValid -and $r.$field -cmatch '^[1-9][0-9]*$'}
            foreach($field in @('member_id','current_id','pc_pawn_id','mount_id')){$registrationValid=$registrationValid -and $r.$field -cmatch '^(0|[1-9][0-9]*)$'}
            foreach($field in @('full_match','ready','identity_matches','group_valid','member_present','allow_requested','pending','admission_failed','mount_replicated')){$registrationValid=$registrationValid -and $r.$field -in @('0','1')}
            $registrationValid=$registrationValid -and $r.group_valid -ceq '1' -and $r.admission_failed -ceq '0'
            if($phase -ceq 'stopped'){
                foreach($field in @('full_match','ready','identity_matches','member_present','current_id','pc_pawn_id','mount_id')){$registrationValid=$registrationValid -and $r.$field -ceq '0'}
                $registrationValid=$registrationValid -and $r.mount_replicated -ceq '1'
                # The current implementation may retain its stale old member ID.
                # Requested allow/pending are observations here, not native allow-bit proof.
            }else{
                foreach($field in @('full_match','ready','identity_matches','member_present','allow_requested','mount_replicated')){$registrationValid=$registrationValid -and $r.$field -ceq '1'}
                $registrationValid=$registrationValid -and $r.pending -ceq '0' -and $r.member_id -cmatch '^[1-9][0-9]*$' -and $r.mount_id -cmatch '^[1-9][0-9]*$'
                $registrationValid=$registrationValid -and $r.member_id -ceq $r.current_id -and $r.current_id -ceq $r.pc_pawn_id
                $expectedPawn=if($phase -ceq 'dismounted'){$r.rider_id}else{$r.mount_id}
                $registrationValid=$registrationValid -and $r.pc_pawn_id -ceq $expectedPawn
            }
            $phases[$phase]=$r;++$position
        }
        if($registrationValid){
            $warm=$phases['warm'];$stopped=$phases['stopped'];$restored=$phases['restored'];$dismounted=$phases['dismounted']
            foreach($r in $registrationRows){foreach($field in @('group_token','groups_created','rider_id')){$registrationValid=$registrationValid -and $r.$field -ceq $warm.$field}}
            $registrationValid=$registrationValid -and $stopped.member_id -in @('0',$warm.member_id) -and $dismounted.mount_id -ceq $restored.mount_id
        }
        if($phases.ContainsKey('restored')){$restartMountId=[string]$phases['restored'].mount_id}
        $registration=[ordered]@{requested=$true;passed=[bool]$registrationValid;present=[bool]$registrationPresent;rows=$registrationRows.Count;phases=$phases;scope=$registration.scope}
    }
    function Test-AuditMountRows([object[]]$InRows,[int]$InObserver,[hashtable]$InAuthority,[bool]$InRestart=$false,[string]$InRestartMountId=''){
        $expectedRows=if($InRestart){24}else{18}
        $valid=$InRows.Count -eq $expectedRows -and (!$InRestart -or $InRestartMountId -cmatch '^[1-9][0-9]*$')
        $table=@{};$riders=@{};$states=@{};$epochs=@{};$resources=@{};$mountId='';$probeId=''
        foreach($r in $InRows){
            $key="$($r.case)|$($r.step)|$($r.rider_slot)"
            $valid=$valid -and !$table.ContainsKey($key) -and $r.passed -ceq '1' -and $r.observer_slot -ceq ([string]$InObserver)
            foreach($field in @('rider_id','state_id','mount_id','expected_avatar_id','actual_avatar_id','epoch')){$valid=$valid -and $r.$field -cmatch '^[1-9][0-9]*$'}
            $valid=$valid -and $r.canonical_same -ceq '1' -and $r.expected_avatar_id -ceq $r.actual_avatar_id
            foreach($field in @('health','max_health')){$number=[double]$r.$field;$valid=$valid -and ![double]::IsNaN($number) -and ![double]::IsInfinity($number)}
            $valid=$valid -and [double]$r.health -gt 0 -and [double]$r.max_health -ge [double]$r.health
            if($riders.ContainsKey($r.rider_slot)){$valid=$valid -and $riders[$r.rider_slot] -ceq $r.rider_id -and $states[$r.rider_slot] -ceq $r.state_id}else{$riders[$r.rider_slot]=$r.rider_id;$states[$r.rider_slot]=$r.state_id}
            $afterRestart=$InRestart -and $r.case -ceq '43' -and $r.step -in @('2','3')
            if($afterRestart){$valid=$valid -and $r.mount_id -ceq $InRestartMountId}
            elseif($mountId){$valid=$valid -and $mountId -ceq $r.mount_id}else{$mountId=$r.mount_id}
            $table[$key]=$r
        }
        $valid=$valid -and $riders.Count -eq 2 -and @($riders.Values|Sort-Object -Unique).Count -eq 2 -and @($states.Values|Sort-Object -Unique).Count -eq 2
        $order=@(@(40,0),@(41,0),@(41,1),@(41,2),@(41,3),@(42,0),@(42,1),@(42,2),@(42,3))
        if($InRestart){$order+=@(@(43,0),@(43,2),@(43,3))}
        $position=0
        foreach($stage in $order){
            $active=$(if($stage[0] -eq 40){-1}elseif($stage[0] -eq 43){0}else{$stage[0]-41});$mounted=$active -ge 0 -and $stage[1] -lt 3
            foreach($slot in 0..1){
                $key="$($stage[0])|$($stage[1])|$slot";$r=$table[$key]
                if($InRestart){$inOrder=$InRows[$position];$valid=$valid -and "$($inOrder.case)|$($inOrder.step)|$($inOrder.rider_slot)" -ceq $key}
                ++$position
                if(!$r){$valid=$false;continue}
                $mine=$InObserver -eq $slot;$authority=$InObserver -eq -1;$isMounted=$mounted -and $active -eq $slot
                $reject=$isMounted -and $stage[1] -eq 1;$advance=$stage[0] -in @(41,42) -and $active -eq $slot -and $stage[1] -in @(1,2)
                $privateExpected=[int]($authority -or $mine);$requestExpected=[int](!$authority -and $stage[0] -gt 40 -and $stage[1] -eq 3 -and $InObserver -eq $active)
                $valid=$valid -and [int]$r.mounted -eq [int]$isMounted -and [int]$r.tuple_rejected -eq [int]$reject
                $valid=$valid -and [int]$r.getter_accepted -eq [int](!$reject) -and [int]$r.attribute_accepted -eq [int](!$reject)
                $valid=$valid -and [int]$r.private_items -eq $privateExpected -and [int]$r.private_equipment -eq $privateExpected -and [int]$r.private_expected -eq $privateExpected
                $valid=$valid -and [int]$r.request_sent -eq $requestExpected -and $r.epoch_rule -ceq $(if($advance){'advance'}else{'stable'})
                $epoch=[uint64]$r.epoch
                if($epochs.ContainsKey($slot)){$valid=$valid -and $(if($advance){$epoch -gt $epochs[$slot]}else{$epoch -eq $epochs[$slot]})}
                $epochs[$slot]=$epoch
                if($resources.ContainsKey($slot)){$v=$resources[$slot];$valid=$valid -and [Math]::Abs([double]$r.health-$v[0]) -lt .001 -and [Math]::Abs([double]$r.max_health-$v[1]) -lt .001}else{$resources[$slot]=@([double]$r.health,[double]$r.max_health)}
                if($reject){
                    $valid=$valid -and $r.expected_avatar_id -cne $r.rider_id
                    if($probeId){$valid=$valid -and $probeId -ceq $r.expected_avatar_id}else{$probeId=$r.expected_avatar_id}
                }else{$valid=$valid -and $r.expected_avatar_id -ceq $r.rider_id}
                if($authority){$valid=$valid -and $r.rider_role -ceq '3' -and $r.mount_role -ceq '3'}
                elseif(!$mine){$valid=$valid -and $r.rider_role -ceq '1'}
                elseif($isMounted){$valid=$valid -and $r.rider_role -in @('1','2')}
                else{$valid=$valid -and $r.rider_role -ceq '2'}
                if(!$authority -and $mounted){$valid=$valid -and [int]$r.mount_role -eq $(if($InObserver -eq $active){2}else{1})}
                $valid=$valid -and [int]$r.rider_controller -eq [int](!$isMounted -and ($authority -or $mine))
                $valid=$valid -and [int]$r.mount_controller -eq [int]($mounted -and ($authority -or $InObserver -eq $active))
                if($InAuthority.Count -gt 0){
                    $a=$InAuthority[$key];$valid=$valid -and $null -ne $a
                    foreach($field in @('rider_id','state_id','mount_id','expected_avatar_id','actual_avatar_id')){$valid=$valid -and $r.$field -ceq $a.$field}
                    $valid=$valid -and [Math]::Abs([double]$r.health-[double]$a.health) -lt .001 -and [Math]::Abs([double]$r.max_health-[double]$a.max_health) -lt .001
                    # Epochs are local runtime fences. Compare ordering locally,
                    # never numeric epoch values between different processes.
                }
            }
        }
        return [ordered]@{passed=$valid;rows=$InRows.Count;table=$table;mountId=$mountId}
    }
    $mountAuthorityPath="$out/mount-authority.csv";$mountAuthorityPresent=Test-Path -LiteralPath $mountAuthorityPath -PathType Leaf
    $mountAuthority=@(if($mountAuthorityPresent){Import-Csv -LiteralPath $mountAuthorityPath})
    $mountAuthorityResult=Test-AuditMountRows $mountAuthority -1 @{} ([bool]$MountReplicationRestart) $restartMountId
    $mountClients=@()
    foreach($observer in 0..1){
        $mountClientPath="$out/mount-client_$observer.csv";$mountClientPresent=Test-Path -LiteralPath $mountClientPath -PathType Leaf
        $mountClientRows=@(if($mountClientPresent){Import-Csv -LiteralPath $mountClientPath})
        $mountClientResult=Test-AuditMountRows $mountClientRows $observer $mountAuthorityResult.table ([bool]$MountReplicationRestart) $restartMountId
        $mountClients+=[ordered]@{observer=$observer;passed=$mountClientResult.passed;rows=$mountClientResult.rows;present=$mountClientPresent}
    }
    $registrationLinked=!$MountReplicationRestart
    if($MountReplicationRestart -and $registration.passed -and $mountAuthorityResult.passed){
        $registrationLinked=$registration.phases['warm'].mount_id -ceq $mountAuthorityResult.mountId
        foreach($stage in @(@('warm',0),@('restored',2),@('dismounted',3))){
            $r=$registration.phases[$stage[0]];$a=$mountAuthorityResult.table["43|$($stage[1])|0"]
            $registrationLinked=$registrationLinked -and $null -ne $a -and $r.mount_id -ceq $a.mount_id -and $r.rider_id -ceq $a.rider_id
        }
    }
    $spatialValidation=[ordered]@{requested=[bool]$MountSpatialReentry;passed=!$MountSpatialReentry;rows=0;participants=@();scope='Ordinary native AOI out/in with fixed server actors/handles and actual foreign replica retirement/replacement. Stored raw camera cache is not a private Iris consumed-view getter.'}
    if($MountSpatialReentry){
        function Read-SpatialNumber($Row,[string]$Field){
            $value=0.0
            if(![double]::TryParse([string]$Row.$Field,[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$value) -or [double]::IsNaN($value) -or [double]::IsInfinity($value)){throw "Nonfinite/missing spatial field: $Field"}
            return $value
        }
        function Test-SpatialUnsigned($Value,[bool]$AllowZero=$false){
            $parsed=[uint64]0
            return ([string]$Value -cmatch '^(0|[1-9][0-9]{0,19})$') -and [uint64]::TryParse([string]$Value,[ref]$parsed) -and ($AllowZero -or $parsed -gt 0)
        }
        $spatialValid=$true;$spatialRows=0;$participants=@();$serverSpatial=@();$subjectMount='';$subjectRider='';$serverGroup='';$serverCreated=''
        foreach($observer in @(-1,0,1)){
            $spatialFileName=if($observer -eq -1){'mount-spatial-authority.csv'}else{"mount-spatial-client_$observer.csv"}
            $path=Join-Path $out $spatialFileName;$present=Test-Path -LiteralPath $path -PathType Leaf
            $rows=@(if($present){Import-Csv -LiteralPath $path})
            $valid=$present -and $rows.Count -eq 4
            $warm=$null;$expectedStep=0
            foreach($row in $rows){
                $valid=$valid -and $row.case -ceq '44' -and $row.step -ceq ([string]$expectedStep) -and $row.observer_slot -ceq ([string]$observer)
                $valid=$valid -and $row.canonical -ceq '1' -and $row.private -ceq '1' -and $row.passed -ceq '1' -and $row.remap_dynamic -ceq '1'
                foreach($field in @('server_mount_id','server_rider_id')){$valid=$valid -and (Test-SpatialUnsigned $row.$field)}
                foreach($field in @('mount_always','rider_always','mount_owner_only','rider_owner_only','mount_use_owner','rider_use_owner','scope_failed')){$valid=$valid -and $row.$field -ceq '0'}
                if($expectedStep -eq 0){
                    $warm=$row
                    if($observer -eq -1){$subjectMount=$row.server_mount_id;$subjectRider=$row.server_rider_id;$serverGroup=$row.scope_group;$serverCreated=$row.scope_created}
                    if($observer -eq -1){
                        $originalSubject=$mountAuthorityResult.table['40|0|0']
                        $valid=$valid -and $null -ne $originalSubject -and $subjectMount -ceq $originalSubject.mount_id -and $subjectRider -ceq $originalSubject.rider_id
                    }
                }
                $valid=$valid -and $row.server_mount_id -ceq $subjectMount -and $row.server_rider_id -ceq $subjectRider
                $mountCull=Read-SpatialNumber $row 'mount_cull_sq';$riderCull=Read-SpatialNumber $row 'rider_cull_sq'
                $valid=$valid -and $mountCull -gt 0 -and $riderCull -gt 0 -and $row.mount_cull_sq -ceq $warm.mount_cull_sq -and $row.rider_cull_sq -ceq $warm.rider_cull_sq
                $cameraTime=Read-SpatialNumber $row 'raw_camera_time'
                foreach($field in @('raw_camera_x','raw_camera_y','raw_camera_z')){$null=Read-SpatialNumber $row $field}
                $valid=$valid -and $cameraTime -gt 0 -and $row.child_connections -ceq '0'
                $far=($expectedStep -eq 1 -and $observer -ne 0)
                $valid=$valid -and $row.far_beyond2cull -ceq $(if($far){'1'}else{'0'})
                if($observer -eq -1){
                    foreach($field in @('has_scope','scope_ready','scope_identity','scope_member','scope_allow_requested')){$valid=$valid -and $row.$field -ceq '1'}
                    $valid=$valid -and (Test-SpatialUnsigned $row.scope_group) -and (Test-SpatialUnsigned $row.scope_created) -and $row.scope_group -ceq $serverGroup -and $row.scope_created -ceq $serverCreated
                    $serverSpatial+=$row
                }else{
                    foreach($field in @('has_scope','scope_ready','scope_identity','scope_member','scope_group','scope_created','scope_allow_requested')){$valid=$valid -and $row.$field -ceq '0'}
                }
                $absent=($observer -eq 1 -and $expectedStep -eq 1)
                if($absent){
                    foreach($field in @('mount_id','rider_id','mount_object_id','rider_object_id','live_mounts','live_riders','mount_mapped','rider_mapped','new_mount','new_rider')){$valid=$valid -and $row.$field -ceq '0'}
                }else{
                    $valid=$valid -and $row.mount_id -ceq $subjectMount -and $row.rider_id -ceq $subjectRider
                    foreach($field in @('mount_object_id','rider_object_id')){$valid=$valid -and (Test-SpatialUnsigned $row.$field)}
                    foreach($field in @('live_mounts','live_riders','mount_mapped','rider_mapped')){$valid=$valid -and $row.$field -ceq '1'}
                }
                $retired=($observer -eq 1 -and $expectedStep -gt 0)
                foreach($field in @('old_mount_retired','old_rider_retired')){$valid=$valid -and $row.$field -ceq $(if($retired){'1'}else{'0'})}
                $newReplica=($observer -eq 1 -and $expectedStep -ge 2)
                foreach($field in @('new_mount','new_rider')){$valid=$valid -and $row.$field -ceq $(if($newReplica){'1'}else{'0'})}
                if($observer -eq 1 -and $expectedStep -gt 0){
                    $valid=$valid -and $row.old_mount_object_id -ceq $warm.mount_object_id -and $row.old_rider_object_id -ceq $warm.rider_object_id
                    # Public weak identity comparison in the native fixture proves
                    # replacement; UObject indices may legitimately be recycled.
                }elseif(!$absent){
                    $valid=$valid -and $row.mount_object_id -ceq $warm.mount_object_id -and $row.rider_object_id -ceq $warm.rider_object_id
                }
                if($observer -ge 0 -and $expectedStep -lt $serverSpatial.Count){
                    $authority=$serverSpatial[$expectedStep]
                    $valid=$valid -and $row.mount_cull_sq -ceq $authority.mount_cull_sq -and $row.rider_cull_sq -ceq $authority.rider_cull_sq
                }
                ++$expectedStep
            }
            $spatialRows+=$rows.Count;$spatialValid=$spatialValid -and $valid
            $participants+=[ordered]@{observer=$observer;file=$spatialFileName;present=$present;rows=$rows.Count;passed=[bool]$valid}
        }
        $spatialValidation=[ordered]@{requested=$true;passed=[bool]$spatialValid;rows=$spatialRows;participants=$participants;serverMountId=$subjectMount;serverRiderId=$subjectRider;scope=$spatialValidation.scope}
    }
    $terminalCase=if($MountSpatialReentry){44}elseif($MountReplicationRestart){43}else{42}
    $mountCompleted=(Test-Path -LiteralPath "$out/server.txt" -PathType Leaf) -and (Get-Content -Raw -LiteralPath "$out/server.txt") -match "success=1\s+case=$terminalCase\s+step=3\b"
    $mountScope=if($MountSpatialReentry -or $MountReplicationRestart){$scope}else{'Actual mount/public-owner contract and production dismount RPCs on two sockets. No TargetHUD screen, terrain/movement quality, capacity or performance claim.'}
    $mountValidation=[ordered]@{passed=($spatialValidation.passed -and $mountAuthorityResult.passed -and @($mountClients|Where-Object{!$_.passed}).Count -eq 0 -and $mountCompleted -and $registration.passed -and $registrationLinked);requested=$true;replicationRestartRequested=[bool]$MountReplicationRestart;authorityRows=$mountAuthorityResult.rows;authorityPresent=$mountAuthorityPresent;clients=$mountClients;completedLastStep=[bool]$mountCompleted;registrationLifecycle=$registration;registrationLinked=[bool]$registrationLinked;spatialLifecycle=$spatialValidation;scope=$mountScope}
}

function Test-WarningWire {
 param([Parameter(Mandatory)][string]$RunDirectory,[ValidateSet('Baseline','LagLoss','ReorderLoss')][string]$NetworkCondition='Baseline')
 $packetExpected=switch($NetworkCondition){'Baseline'{@(0,0,0,0,0)}'LagLoss'{@(75,25,10,0,5)}'ReorderLoss'{@(0,0,10,1,15)}}
 $serverLogPath=Join-Path $RunDirectory 'Server.log'
 $serverPackets=@(if(Test-Path -LiteralPath $serverLogPath -PathType Leaf){Select-String -LiteralPath $serverLogPath -Pattern 'PACKETS lag=([0-9]+) variance=([0-9]+) loss=([0-9]+) order=([0-9]+) duplicate=([0-9]+) netmode=([0-9]+)'|ForEach-Object{$_.Matches[0]}})
 $serverPacketsValid=$serverPackets.Count -eq 1
 if($serverPacketsValid){
  for($i=0;$i -lt 5;++$i){$serverPacketsValid=$serverPacketsValid -and [int]$serverPackets[0].Groups[$i+1].Value -eq $packetExpected[$i]}
  $serverPacketsValid=$serverPacketsValid -and $serverPackets[0].Groups[6].Value -ceq '1'
 }
 $authorityPath=Join-Path $RunDirectory 'warning-authority.csv'
 $authority=@(if(Test-Path -LiteralPath $authorityPath -PathType Leaf){Import-Csv -LiteralPath $authorityPath})
 $authorityHeader='case,step,transition,owner,mode,role,frame,instance,revision,order,warning_count,lease,profile_null,passed'
 $clientHeader='case,step,slot,owner,mode,role,probe_id,frame,instance,revision,order,warning_count,marker,registered,render_state,lines,tick,attached,profile_null,pending_loops,active_niagara,marker_type,passed'
 $events=@('pulse_start','pulse_end','long_start','attack_end')
 $eventSteps=@('1','1','2','3')
 $eventWarnings=@('1','0','1','0')
 $authorityValid=$authority.Count -eq 4
 if($authorityValid){
  $authorityValid=($authority[0].PSObject.Properties.Name -join ',') -ceq $authorityHeader
  for($i=0;$i -lt 4;++$i){
   $row=$authority[$i]
   $numeric=@('frame','instance','revision','order','warning_count','lease')
   $numbersValid=@($numeric|Where-Object{$row.$_ -cnotmatch '^(0|[1-9][0-9]{0,19})$'}).Count -eq 0
   $authorityValid=$authorityValid -and $numbersValid -and $row.case -ceq '50' -and $row.step -ceq $eventSteps[$i] -and $row.transition -ceq $events[$i]
   $authorityValid=$authorityValid -and $row.owner -cmatch '^[A-Za-z0-9_]+$' -and $row.owner -ceq $authority[0].owner -and $row.mode -ceq '1' -and $row.role -ceq '3'
   $authorityValid=$authorityValid -and $row.warning_count -ceq $eventWarnings[$i] -and $row.profile_null -ceq '1' -and $row.passed -ceq '1'
   if($numbersValid){
    foreach($field in @('instance','revision','order','lease')){$authorityValid=$authorityValid -and [decimal]$row.$field -gt 0}
    if($i -gt 0){foreach($field in @('revision','order')){$authorityValid=$authorityValid -and [decimal]$row.$field -gt [decimal]$authority[$i-1].$field}}
   }
  }
  if($authorityValid){
   $authorityValid=$authority[0].frame -ceq $authority[1].frame -and [decimal]$authority[2].frame -gt [decimal]$authority[1].frame -and [decimal]$authority[3].frame -gt [decimal]$authority[2].frame
   $authorityValid=$authorityValid -and $authority[0].instance -ceq $authority[1].instance -and $authority[2].instance -ceq $authority[3].instance -and [decimal]$authority[2].instance -gt [decimal]$authority[0].instance
   $authorityValid=$authorityValid -and $authority[0].lease -ceq $authority[1].lease -and $authority[2].lease -ceq $authority[3].lease -and $authority[2].lease -cne $authority[0].lease
  }
 }
 $clients=@();$superseded=0
 foreach($slot in 0..1){
  $clientPath=Join-Path $RunDirectory "warning-client_$slot.csv"
  $rows=@(if(Test-Path -LiteralPath $clientPath -PathType Leaf){Import-Csv -LiteralPath $clientPath})
  $valid=$authorityValid -and $rows.Count -eq 4
  if($valid){
   $valid=($rows[0].PSObject.Properties.Name -join ',') -ceq $clientHeader
   for($phase=0;$phase -lt 4;++$phase){
    $row=$rows[$phase]
    $numeric=@('probe_id','frame','instance','revision','order','warning_count','lines','pending_loops','active_niagara')
    $numbersValid=@($numeric|Where-Object{$row.$_ -cnotmatch '^(0|[1-9][0-9]{0,19})$'}).Count -eq 0
    $valid=$valid -and $numbersValid -and $row.case -ceq '50' -and $row.step -ceq [string]$phase -and $row.slot -ceq [string]$slot -and $row.mode -ceq '3' -and $row.role -ceq '1'
    $valid=$valid -and $row.owner -cmatch '^[A-Za-z0-9_]+$' -and $row.owner -ceq $rows[0].owner -and $row.probe_id -ceq $rows[0].probe_id -and $row.profile_null -ceq '1' -and $row.passed -ceq '1'
    foreach($field in @('tick','pending_loops','active_niagara')){$valid=$valid -and $row.$field -ceq '0'}
    $active=if($phase -eq 2){'1'}else{'0'}
    foreach($field in @('warning_count','marker','registered','render_state','attached','marker_type')){$valid=$valid -and $row.$field -ceq $active}
    $valid=$valid -and $row.lines -ceq $(if($phase -eq 2){'20'}else{'0'})
    if($numbersValid){
     $valid=$valid -and [decimal]$row.probe_id -gt 0
     if($phase -gt 0){
      $expected=$authority[$phase]
      $valid=$valid -and $row.instance -ceq $expected.instance -and [decimal]$row.revision -gt 0 -and [decimal]$row.order -ge [decimal]$expected.order
      # An ordered end delta may retire the signal before its older recovery
      # snapshot. Require that exact/newer end order, not a stale revision apply.
      if($phase -eq 2){$valid=$valid -and [decimal]$row.revision -ge [decimal]$expected.revision}
      $valid=$valid -and [decimal]$row.frame -gt [decimal]$rows[$phase-1].frame
     }
    }
   }
  }
  $logPath=Join-Path $RunDirectory "Client$slot.log"
  $clientPackets=@(if(Test-Path -LiteralPath $logPath -PathType Leaf){Select-String -LiteralPath $logPath -Pattern 'WARNING_PACKETS slot=([01]) lag=([0-9]+) variance=([0-9]+) loss=([0-9]+) order=([0-9]+) duplicate=([0-9]+) netmode=([0-9]+)'|ForEach-Object{$_.Matches[0]}})
  $clientPacketsValid=$clientPackets.Count -eq 1
  if($clientPacketsValid){
   $g=$clientPackets[0].Groups
   $clientPacketsValid=$g[1].Value -ceq [string]$slot -and $g[7].Value -ceq '3'
   for($i=0;$i -lt 5;++$i){$clientPacketsValid=$clientPacketsValid -and [int]$g[$i+2].Value -eq $packetExpected[$i]}
  }
  $rpc=@(if(Test-Path -LiteralPath $logPath -PathType Leaf){Select-String -LiteralPath $logPath -Pattern 'WARNING_RPC owner=(\S+) mode=([0-9]+) role=([0-9]+) frame=([0-9]+) instance=([0-9]+) revision=([0-9]+) order=([0-9]+) warnings=([0-9]+) previous_revision=([0-9]+) previous_order=([0-9]+) eligible=([01])'|ForEach-Object{$_.Matches[0]}})
  $rpcValid=$valid -and $rpc.Count -eq 4
  if($rpcValid){
   for($i=0;$i -lt 4;++$i){
    $g=$rpc[$i].Groups;$expected=$authority[$i]
    $rpcValid=$rpcValid -and $g[1].Value -ceq $rows[0].owner -and $g[2].Value -ceq '3' -and $g[3].Value -ceq '1'
    $rpcValid=$rpcValid -and $g[5].Value -ceq $expected.instance -and $g[6].Value -ceq $expected.revision -and $g[7].Value -ceq $expected.order -and $g[8].Value -ceq $expected.warning_count
    if($g[11].Value -ceq '0'){++$superseded}
   }
  }
  $clients+=[ordered]@{slot=$slot;present=(Test-Path -LiteralPath $clientPath -PathType Leaf);rows=$rows.Count;stateAndMarkerPassed=[bool]$valid;rpcEntries=$rpc.Count;rpcPassed=[bool]$rpcValid;nativePacketSettingsPassed=[bool]$clientPacketsValid;passed=([bool]$valid -and [bool]$rpcValid -and [bool]$clientPacketsValid)}
 }
 [ordered]@{requested=$true;passed=($authorityValid -and $serverPacketsValid -and @($clients|Where-Object{!$_.passed}).Count -eq 0);authorityRows=$authority.Count;authorityPassed=[bool]$authorityValid;nativeServerPacketSettingsPassed=[bool]$serverPacketsValid;networkCondition=$NetworkCondition;clients=$clients;ineligibleRpcEntries=$superseded;scope='Same-server-frame pulse RPC entry and final empty state; long warning native RHI marker and normal end on both actual sockets. Pulse visibility, offscreen HUD, damage lead time and load capacity are not inferred.'}
}
function Test-NPCAssemblyWire {
 param([Parameter(Mandatory)][string]$RunDirectory,[ValidateSet('Baseline','LagLoss','ReorderLoss')][string]$NetworkCondition='Baseline')
 $packetExpected=switch($NetworkCondition){'Baseline'{@(0,0,0,0,0)}'LagLoss'{@(75,25,10,0,5)}'ReorderLoss'{@(0,0,10,1,15)}}
 $authorityHeader='case,step,mode,role,npc_id,owner,frame,npc_present,hit_count,presentation_count,hit_id,presentation_id,registered,begun,replicated,instance,revision,order,warning_count,lease,helper_owned,grant,profile_null,pending_loops,active_niagara,passed'
 $clientHeader='case,step,slot,mode,role,npc_id,owner,frame,npc_present,hit_count,presentation_count,hit_id,presentation_id,registered,begun,replicated,instance,revision,order,warning_count,marker,render_state,lines,tick,attached,profile_null,pending_loops,active_niagara,passed'
 $readRow={param($Row,[int]$Phase,[bool]$Authority)
  $numeric=@('npc_id','frame','hit_count','presentation_count','hit_id','presentation_id','instance','revision','order','warning_count','pending_loops','active_niagara')
  if($Authority){$numeric+=@('lease','grant')}else{$numeric+='lines'}
  $ok=@($numeric|Where-Object{$Row.$_ -cnotmatch '^(0|[1-9][0-9]{0,19})$'}).Count -eq 0
  $ok=$ok -and $Row.case -ceq '70' -and $Row.step -ceq [string]$Phase -and $Row.mode -ceq $(if($Authority){'1'}else{'3'})
  $ok=$ok -and $Row.role -ceq $(if($Phase -eq 5){'0'}elseif($Authority){'3'}else{'1'}) -and $Row.owner -cmatch '^[A-Za-z0-9_]+$'
  $pair=if($Phase -in 0,5){'0'}else{'1'};$active=if($Phase -in 1,3){'1'}else{'0'}
  $ok=$ok -and $Row.npc_present -ceq $(if($Phase -eq 5){'0'}else{'1'})
  foreach($field in @('hit_count','presentation_count','registered','begun','replicated')){$ok=$ok -and $Row.$field -ceq $pair}
  foreach($field in @('pending_loops','active_niagara')){$ok=$ok -and $Row.$field -ceq '0'}
  $ok=$ok -and $Row.warning_count -ceq $active -and $Row.profile_null -ceq '1' -and $Row.passed -ceq '1'
  if($ok){
   $ok=[decimal]$Row.npc_id -gt 0
   if($Phase -eq 0){$ok=$ok -and $Row.hit_id -ceq '0' -and $Row.presentation_id -ceq '0'}
   else{$ok=$ok -and [decimal]$Row.hit_id -gt 0 -and [decimal]$Row.presentation_id -gt 0 -and $Row.hit_id -cne $Row.presentation_id}
   foreach($field in @('instance','revision','order')){
    if($Phase -in 0,5){$ok=$ok -and $Row.$field -ceq '0'}else{$ok=$ok -and [decimal]$Row.$field -gt 0}
   }
  }
  if($Authority){
   $ok=$ok -and $Row.helper_owned -ceq $active -and $Row.grant -ceq '0'
   if($active -ceq '1'){$ok=$ok -and $Row.lease -cmatch '^[1-9][0-9]{0,19}$'}else{$ok=$ok -and $Row.lease -ceq '0'}
  }else{
   foreach($field in @('marker','render_state','attached')){$ok=$ok -and $Row.$field -ceq $active}
   $ok=$ok -and $Row.lines -ceq $(if($active -ceq '1'){'20'}else{'0'}) -and $Row.tick -ceq '0'
  }
  return [bool]$ok
 }
 $serverLog=Join-Path $RunDirectory 'Server.log'
 $packets=@(if(Test-Path -LiteralPath $serverLog -PathType Leaf){Select-String -LiteralPath $serverLog -Pattern 'PACKETS lag=([0-9]+) variance=([0-9]+) loss=([0-9]+) order=([0-9]+) duplicate=([0-9]+) netmode=([0-9]+)'|ForEach-Object{$_.Matches[0]}})
 $packetsValid=$packets.Count -eq 1
 if($packetsValid){for($i=0;$i -lt 5;++$i){$packetsValid=$packetsValid -and [int]$packets[0].Groups[$i+1].Value -eq $packetExpected[$i]};$packetsValid=$packetsValid -and $packets[0].Groups[6].Value -ceq '1'}
 $ready=@(if(Test-Path -LiteralPath $serverLog -PathType Leaf){Select-String -LiteralPath $serverLog -SimpleMatch 'NPC_ASSEMBLY_LATE_JOIN_READY case=70 slot=1 step=1'})
 $lateRegistration=@(if(Test-Path -LiteralPath $serverLog -PathType Leaf){Select-String -LiteralPath $serverLog -Pattern '\bSLOT_REGISTERED slot=1 count=2 source=LoginOptions\b'})
 $lateJoinValid=$ready.Count -eq 1 -and $lateRegistration.Count -eq 1
 if($lateJoinValid){$lateJoinValid=$ready[0].LineNumber -lt $lateRegistration[0].LineNumber}
 $authorityPath=Join-Path $RunDirectory 'npc-assembly-authority.csv'
 $authority=@(if(Test-Path -LiteralPath $authorityPath -PathType Leaf){Import-Csv -LiteralPath $authorityPath})
 $authorityValid=$authority.Count -eq 6
 if($authorityValid){$authorityValid=($authority[0].PSObject.Properties.Name -join ',') -ceq $authorityHeader}
 if($authorityValid){
  for($phase=0;$phase -le 5;++$phase){
   $row=$authority[$phase];$authorityValid=$authorityValid -and (& $readRow $row $phase $true)
   $authorityValid=$authorityValid -and $row.npc_id -ceq $authority[0].npc_id -and $row.owner -ceq $authority[0].owner
   if($phase -gt 0){$authorityValid=$authorityValid -and [decimal]$row.frame -gt [decimal]$authority[$phase-1].frame -and $row.hit_id -ceq $authority[1].hit_id -and $row.presentation_id -ceq $authority[1].presentation_id}
   if($phase -in 2,3,4){foreach($field in @('revision','order')){$authorityValid=$authorityValid -and [decimal]$row.$field -gt [decimal]$authority[$phase-1].$field}}
  }
  $authorityValid=$authorityValid -and $authority[1].instance -ceq $authority[2].instance -and $authority[3].instance -ceq $authority[4].instance
  $authorityValid=$authorityValid -and [decimal]$authority[3].instance -gt [decimal]$authority[1].instance -and $authority[3].lease -cne $authority[1].lease
 }
 $clients=@()
 foreach($slot in 0..1){
  $path=Join-Path $RunDirectory "npc-assembly-client_$slot.csv";$rows=@(if(Test-Path -LiteralPath $path -PathType Leaf){Import-Csv -LiteralPath $path})
  $firstPhase=if($slot -eq 0){0}else{1};$valid=$authorityValid -and $rows.Count -eq (6-$firstPhase)
  if($valid){$valid=($rows[0].PSObject.Properties.Name -join ',') -ceq $clientHeader}
  if($valid){
   $pairRow=if($slot -eq 0){$rows[1]}else{$rows[0]}
   for($index=0;$index -lt $rows.Count;++$index){
    $phase=$firstPhase+$index;$row=$rows[$index]
    $valid=$valid -and (& $readRow $row $phase $false) -and $row.slot -ceq [string]$slot
    $valid=$valid -and $row.npc_id -ceq $rows[0].npc_id -and $row.owner -ceq $rows[0].owner
    if($index -gt 0){$valid=$valid -and [decimal]$row.frame -gt [decimal]$rows[$index-1].frame}
    if($phase -gt 0){$valid=$valid -and $row.hit_id -ceq $pairRow.hit_id -and $row.presentation_id -ceq $pairRow.presentation_id}
    if($phase -in 1,2,3,4){
     $valid=$valid -and $row.instance -ceq $authority[$phase].instance -and [decimal]$row.order -ge [decimal]$authority[$phase].order
     # A newer end delta may clear a warning before the older recovery snapshot; do not invent revision equality.
     if($phase -in 1,3){$valid=$valid -and [decimal]$row.revision -ge [decimal]$authority[$phase].revision}
    }
   }
  }
  $logPath=Join-Path $RunDirectory "Client$slot.log"
  $clientPackets=@(if(Test-Path -LiteralPath $logPath -PathType Leaf){Select-String -LiteralPath $logPath -Pattern 'NPC_ASSEMBLY_PACKETS slot=([01]) lag=([0-9]+) variance=([0-9]+) loss=([0-9]+) order=([0-9]+) duplicate=([0-9]+) netmode=([0-9]+)'|ForEach-Object{$_.Matches[0]}})
  $clientPacketsValid=$clientPackets.Count -eq 1
  if($clientPacketsValid){$g=$clientPackets[0].Groups;$clientPacketsValid=$g[1].Value -ceq [string]$slot -and $g[7].Value -ceq '3';for($i=0;$i -lt 5;++$i){$clientPacketsValid=$clientPacketsValid -and [int]$g[$i+2].Value -eq $packetExpected[$i]}}
  $rpc=@(if(Test-Path -LiteralPath $logPath -PathType Leaf){Select-String -LiteralPath $logPath -Pattern 'WARNING_RPC owner=(\S+) mode=([0-9]+) role=([0-9]+) frame=([0-9]+) instance=([0-9]+) revision=([0-9]+) order=([0-9]+) warnings=([0-9]+) previous_revision=([0-9]+) previous_order=([0-9]+) eligible=([01])'|ForEach-Object{$_.Matches[0]}})
  $firstStart=@();$restart=@()
  if($authorityValid -and $rows.Count){
   foreach($entry in $rpc){
    $g=$entry.Groups
    if($g[1].Value -cne $rows[0].owner -or $g[2].Value -cne '3' -or $g[3].Value -cne '1' -or $g[8].Value -cne '1'){continue}
    if($g[5].Value -ceq $authority[1].instance -and $g[6].Value -ceq $authority[1].revision -and $g[7].Value -ceq $authority[1].order){$firstStart+=$entry}
    if($g[5].Value -ceq $authority[3].instance -and $g[6].Value -ceq $authority[3].revision -and $g[7].Value -ceq $authority[3].order){$restart+=$entry}
   }
  }
  # Slot0's first delta can precede subobject creation; persistent state is valid recovery there too.
  $rpcValid=$authorityValid -and $restart.Count -gt 0 -and ($slot -eq 0 -or $firstStart.Count -eq 0)
  $clients+=[ordered]@{slot=$slot;rows=$rows.Count;stateAndMarkerPassed=[bool]$valid;nativePacketsPassed=[bool]$clientPacketsValid;firstStartRpcEntries=$firstStart.Count;restartRpcEntries=$restart.Count;rpcScopePassed=[bool]$rpcValid;passed=([bool]$valid -and [bool]$clientPacketsValid -and [bool]$rpcValid)}
 }
 [ordered]@{requested=$true;passed=($authorityValid -and $packetsValid -and $lateJoinValid -and @($clients|Where-Object{!$_.passed}).Count -eq 0);authorityRows=$authority.Count;authorityPassed=[bool]$authorityValid;nativeServerPacketsPassed=[bool]$packetsValid;lateJoinAfterNativeActiveAck=[bool]$lateJoinValid;clients=$clients;scope='Actual native NPC dynamic pair, late-client initial persistent warning recovery without its historical start RPC, retained same-component restart and normal destruction. IDs are stable only within each process; no attack/GE, A07, quality, capacity or performance inference.'}
}
function Test-QuestHistoryWire {
 param([Parameter(Mandatory)][string]$RunDirectory,[ValidateSet('Baseline','LagLoss')][string]$NetworkCondition)
 $authorityHeader='case,step,mode,frame,owner_state,source_current,summary_count,summary_revision,history_count,active_count,expected_ordinal,expected_count,expected_revision,expected_ids,observer_state,observer_count,observer_revision,observer_active_count,passed'
 $clientHeader='case,step,slot,mode,frame,own_state,foreign_state,own_role,source_current,summary_count,summary_revision,active_count,page_has,page_status,page_ordinal,page_count,page_revision,returned_nonce,page_nonce,page_ids,foreign_present,foreign_count,foreign_revision_valid,foreign_active_count,foreign_page_has,foreign_page_count,passed'
 $packetExpected=if($NetworkCondition -eq 'Baseline'){@(0,0,0,0,0)}else{@(75,25,10,0,5)}
 $guid={param([string]$Value) $parsed=[guid]::Empty;if(![guid]::TryParse($Value,[ref]$parsed)-or $parsed -eq [guid]::Empty){return ''};$parsed.ToString('D')}
 $ids={param([string]$Value) if($Value){@($Value -split '\|')}else{@()}}
 try {
  $a=@(Import-Csv -LiteralPath (Join-Path $RunDirectory 'quest-history-authority.csv'))
  if($a.Count -ne 6 -or ($a[0].PSObject.Properties.Name -join ',') -cne $authorityHeader){throw 'Authority history schema/count mismatch'}
  $initial=&$guid $a[0].summary_revision;$changed=&$guid $a[4].summary_revision;$observer=&$guid $a[0].observer_revision
  if(!$initial -or !$changed -or !$observer -or $initial -eq $changed){throw 'Authority valid revision transition missing'}
  for($s=0;$s -le 5;++$s){
   $r=$a[$s];$count=if($s -ge 4){66}else{65};$revision=if($s -ge 4){$changed}else{$initial}
   if($r.case -cne '80' -or $r.step -cne [string]$s -or $r.mode -cne '1' -or $r.source_current -cne '1' -or $r.passed -cne '1' -or
      $r.summary_count -cne [string]$count -or $r.history_count -cne [string]$count -or $r.active_count -cne '1' -or
      (&$guid $r.summary_revision) -ne $revision -or $r.owner_state -cne $a[0].owner_state -or $r.observer_state -cne $a[0].observer_state -or
      $r.observer_count -cne '0' -or $r.observer_active_count -cne '0' -or (&$guid $r.observer_revision) -ne $observer){throw "Authority phase$s mismatch"}
   if($s -gt 0 -and [decimal]$r.frame -le [decimal]$a[$s-1].frame){throw 'Authority frames did not advance'}
   if($s -eq 0){if($r.expected_ordinal -cne '-1' -or $r.expected_count -cne '0' -or $r.expected_ids){throw 'Initial authority page must be empty'};continue}
   $ordinal=if($s -eq 2){1}elseif($s -in 3,4){2}else{0};$n=if($s -in 3,4){1}else{32};$pageRevision=if($s -eq 4){$initial}else{$revision}
   $values=@(&$ids $r.expected_ids)
   if($r.expected_ordinal -cne [string]$ordinal -or $r.expected_count -cne [string]$n -or $values.Count -ne $n -or
      @($values|Sort-Object -Unique).Count -ne $n -or (&$guid $r.expected_revision) -ne $pageRevision){throw "Authority expected page$s mismatch"}
  }
  $initialIds=@(@(&$ids $a[1].expected_ids)+@(&$ids $a[2].expected_ids)+@(&$ids $a[3].expected_ids))
  if($initialIds.Count -ne 65 -or @($initialIds|Sort-Object -Unique).Count -ne 65){throw 'Initial pages are not65 unique IDs'}
  $newIds=@(&$ids $a[5].expected_ids);$oldHead=@(&$ids $a[1].expected_ids)
  if($newIds[0] -in $initialIds -or ($newIds[1..31] -join '|') -cne ($oldHead[0..30] -join '|')){throw 'Append did not change newest page by one exact ID'}
  $serverPackets=@(Select-String -LiteralPath (Join-Path $RunDirectory 'Server.log') -Pattern 'LogProjectJAuditNetwork: Display: PACKETS lag=(\d+) variance=(\d+) loss=(\d+) order=(\d+) duplicate=(\d+) netmode=(\d+)'|ForEach-Object{$_.Matches[0]})
  if($serverPackets.Count -ne 1 -or $serverPackets[0].Groups[6].Value -cne '1'){throw 'Server native packet receipt missing'}
  for($k=0;$k -lt 5;++$k){if([int]$serverPackets[0].Groups[$k+1].Value -ne $packetExpected[$k]){throw 'Server native packet settings mismatch'}}
  $clients=@()
  foreach($slot in 0..1){
   $rows=@(Import-Csv -LiteralPath (Join-Path $RunDirectory "quest-history-client_$slot.csv"))
   if($rows.Count -ne 6 -or ($rows[0].PSObject.Properties.Name -join ',') -cne $clientHeader){throw "Client$slot schema/count mismatch"}
   $previousNonce=''
   for($s=0;$s -le 5;++$s){
    $r=$rows[$s];$count=if($slot -eq 0){if($s -ge 4){66}else{65}}else{0};$revision=if($slot -eq 0){if($s -ge 4){$changed}else{$initial}}else{$observer}
    if($r.case -cne '80' -or $r.step -cne [string]$s -or $r.slot -cne [string]$slot -or $r.mode -cne '3' -or $r.source_current -cne '1' -or $r.passed -cne '1' -or
       $r.own_role -notin @('1','2') -or $r.summary_count -cne [string]$count -or (&$guid $r.summary_revision) -ne $revision -or
       $r.active_count -cne $(if($slot -eq 0){'1'}else{'0'}) -or !$r.own_state -or $r.own_state -ceq $r.foreign_state -or
       $r.own_state -cne $rows[0].own_state -or $r.foreign_state -cne $rows[0].foreign_state -or $r.foreign_state -ceq 'None' -or
       $r.foreign_present -cne '1' -or $r.foreign_count -cne '0' -or $r.foreign_revision_valid -cne '0' -or $r.foreign_active_count -cne '0' -or $r.foreign_page_has -cne '0' -or $r.foreign_page_count -cne '0'){throw "Client$slot phase$s owner/privacy mismatch"}
    if($s -gt 0 -and [decimal]$r.frame -le [decimal]$rows[$s-1].frame){throw "Client$slot frames did not advance"}
    if($s -eq 0){if($r.page_has -cne '0' -or $r.page_count -cne '0' -or $r.page_ids -or $r.returned_nonce -cne '0' -or $r.page_nonce -cne '0'){throw 'Unqueried initial page not empty'};continue}
    $nonce=&$guid $r.returned_nonce
    if(!$nonce -or (&$guid $r.page_nonce) -ne $nonce -or $r.page_has -cne '1'){throw "Client$slot phase$s actual response nonce mismatch"}
    if($slot -eq 0){
     $status=if($s -eq 4){'3'}else{'2'}
     if($r.page_status -cne $status -or $r.page_ordinal -cne $a[$s].expected_ordinal -or $r.page_count -cne $a[$s].expected_count -or
        (&$guid $r.page_revision) -ne (&$guid $a[$s].expected_revision) -or $r.page_ids -cne $a[$s].expected_ids){throw "Owner phase$s actual page differs from authority values"}
     if($s -in 2,3,5 -and $nonce -eq $previousNonce){throw 'Owner reused the previous request nonce'}
     if($s -eq 4 -and $nonce -ne $previousNonce){throw 'Stale observation forged another request'}
    }else{
     if($r.page_status -cne '2' -or $r.page_ordinal -cne '0' -or $r.page_count -cne '0' -or $r.page_ids -or (&$guid $r.page_revision) -ne $observer){throw 'Observer own-empty response positive control missing'}
     if($s -gt 1 -and $nonce -ne $previousNonce){throw 'Observer issued an unexpected additional query'}
    }
    $previousNonce=$nonce
   }
   $packets=@(Select-String -LiteralPath (Join-Path $RunDirectory "Client$slot.log") -Pattern 'QUEST_HISTORY_PACKETS slot=([01]) lag=(\d+) variance=(\d+) loss=(\d+) order=(\d+) duplicate=(\d+) netmode=(\d+)'|ForEach-Object{$_.Matches[0]})
   if($packets.Count -ne 1 -or $packets[0].Groups[1].Value -cne [string]$slot -or $packets[0].Groups[7].Value -cne '3'){throw "Client$slot native packet receipt missing"}
   for($k=0;$k -lt 5;++$k){if([int]$packets[0].Groups[$k+2].Value -ne $packetExpected[$k]){throw "Client$slot packet settings mismatch"}}
   if((Get-Content -LiteralPath (Join-Path $RunDirectory "quest-history-cleanup_$slot.txt") -Raw).Trim() -cne 'cleanup_passed=1 step=5 normal_finish=1'){throw "Client$slot normal cleanup missing"}
   $clients+=[ordered]@{slot=$slot;rows=6;ownerPageAndForeignPrivacyPassed=$true;nativePacketsPassed=$true;normalCleanupPassed=$true}
  }
  return [ordered]@{requested=$true;passed=$true;authorityRows=6;clients=$clients;scope='Actual owner page RPC and foreign owner-only projection only; validated server seed, no reward, UI pixel, CCU or network persistence claim.'}
 } catch {return [ordered]@{requested=$true;passed=$false;error=$_.Exception.Message;scope='Incomplete/failed actual page and privacy evidence preserved.'}}
}

function Test-TrustedSessionWire {
 param([Parameter(Mandatory)][string]$RunDirectory)
 $summaryPath=Join-Path $RunDirectory 'session-summary.json'
 $summary=if(Test-Path -LiteralPath $summaryPath -PathType Leaf){Get-Content -LiteralPath $summaryPath -Raw|ConvertFrom-Json}else{$null}
 $summaryValid=$null -ne $summary -and $summary.schema -eq 1
 foreach($field in @('success','cleanup_passed','captured_application_attempted')){$summaryValid=$summaryValid -and $summary.$field -is [bool] -and $summary.$field -eq $true}
 foreach($field in @('captured_application_applied','external_credential_verification_claimed','network_persistence_claimed')){$summaryValid=$summaryValid -and $summary.$field -is [bool] -and $summary.$field -eq $false}
 $header='phase,slot,mode,frame,state_id,pawn_id,asc_object_id,account,character,lease_epoch,actor_info_revision,session_current,pawn_owned,avatar_matches,grant,sprint_active,prediction_key,rpc_sent,rpc_calls,rpc_allowed,rpc_denied,stale_complete_rejected,stale_revoke_rejected,app_attempted,app_applied,passed'
 $numberFields=@('phase','slot','mode','frame','state_id','pawn_id','asc_object_id','actor_info_revision','session_current','pawn_owned','avatar_matches','grant','sprint_active','prediction_key','rpc_sent','rpc_calls','rpc_allowed','rpc_denied','stale_complete_rejected','stale_revoke_rejected','app_attempted','app_applied','passed')
 function Test-SessionRow {
  param($Row,[string]$Mode,[int]$Phase,[int]$Slot)
  if(($Row.PSObject.Properties.Name -join ',') -cne $header){return $false}
  if(@($numberFields|Where-Object{$Row.$_ -cnotmatch '^(0|[1-9][0-9]{0,19})$'}).Count){return $false}
  foreach($field in @('account','character','lease_epoch')){
   if($Row.$field -and $Row.$field -cnotmatch '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'){return $false}
   if($Row.$field -ceq '00000000-0000-0000-0000-000000000000'){return $false}
  }
  if($Row.phase -cne [string]$Phase -or $Row.slot -cne [string]$Slot -or $Row.mode -cne $Mode -or $Row.passed -cne '1'){return $false}
  if([decimal]$Row.state_id -le 0 -or [decimal]$Row.asc_object_id -le 0){return $false}
  $expectedActive=($Phase -eq 1 -and $Slot -eq 0) -or ($Phase -in @(2,3) -and $Slot -eq 1)
  foreach($field in @('session_current','pawn_owned','avatar_matches','sprint_active')){
   if($Mode -ceq '3' -and $field -ceq 'session_current'){if($Row.$field -cne '0'){return $false};continue}
   if($Row.$field -cne $(if($expectedActive){'1'}else{'0'})){return $false}
  }
  if(($expectedActive -and [decimal]$Row.pawn_id -le 0) -or (!$expectedActive -and $Row.pawn_id -cne '0')){return $false}
  if($expectedActive -and ([decimal]$Row.actor_info_revision -le 0 -or [decimal]$Row.prediction_key -le 0)){return $false}
  return $true
 }
 $authorityPath=Join-Path $RunDirectory 'session-authority.csv'
 $authority=@(if(Test-Path -LiteralPath $authorityPath -PathType Leaf){Import-Csv -LiteralPath $authorityPath})
 $authorityValid=$authority.Count -eq 10
 $serverPacketsPath=Join-Path $RunDirectory 'Server.log'
 $serverPackets=@(if(Test-Path -LiteralPath $serverPacketsPath -PathType Leaf){Select-String -LiteralPath $serverPacketsPath -Pattern 'PACKETS lag=([0-9]+) variance=([0-9]+) loss=([0-9]+) order=([0-9]+) duplicate=([0-9]+) netmode=([0-9]+)'|ForEach-Object{$_.Matches[0]}})
 $serverPacketsValid=$serverPackets.Count -eq 1
 if($serverPacketsValid){for($i=1;$i -le 5;++$i){$serverPacketsValid=$serverPacketsValid -and $serverPackets[0].Groups[$i].Value -ceq '0'};$serverPacketsValid=$serverPacketsValid -and $serverPackets[0].Groups[6].Value -ceq '1'}
 if($authorityValid){
  for($phase=0;$phase -le 4;++$phase){foreach($slot in 0..1){
   $row=$authority[$phase*2+$slot]
   $rowValid=Test-SessionRow -Row $row -Mode '1' -Phase $phase -Slot $slot
   $authorityValid=$authorityValid -and $rowValid
   if(!$rowValid){continue}
   $first=$authority[$slot]
   $authorityValid=$authorityValid -and $row.state_id -ceq $first.state_id -and $row.asc_object_id -ceq $first.asc_object_id
   $hasIdentity=($slot -eq 0 -and $phase -ge 1) -or ($slot -eq 1 -and $phase -ge 2)
   foreach($field in @('account','character','lease_epoch')){
    if($hasIdentity){$source=$authority[$(if($slot -eq 0){2}else{5})];$authorityValid=$authorityValid -and !!$row.$field -and $row.$field -ceq $source.$field}
    else{$authorityValid=$authorityValid -and !$row.$field}
   }
   if($hasIdentity){$source=$authority[$(if($slot -eq 0){2}else{5})];$authorityValid=$authorityValid -and [decimal]$row.grant -gt 0 -and $row.grant -ceq $source.grant}
   else{$authorityValid=$authorityValid -and $row.grant -ceq '0'}
   if($phase -gt 0){$previous=$authority[($phase-1)*2+$slot];$authorityValid=$authorityValid -and [decimal]$row.frame -gt [decimal]$previous.frame -and [decimal]$row.actor_info_revision -ge [decimal]$previous.actor_info_revision}
   $late=if($phase -ge 3){'1'}else{'0'}
   foreach($field in @('stale_complete_rejected','stale_revoke_rejected','app_attempted')){$authorityValid=$authorityValid -and $row.$field -ceq $late}
   $authorityValid=$authorityValid -and $row.app_applied -ceq '0'
  }}
  if($authorityValid){
   $authorityValid=$authority[0].state_id -cne $authority[1].state_id -and $authority[0].asc_object_id -cne $authority[1].asc_object_id
   foreach($field in @('account','character')){$authorityValid=$authorityValid -and $authority[2].$field -ceq $authority[5].$field}
   $authorityValid=$authorityValid -and $authority[2].lease_epoch -cne $authority[5].lease_epoch -and $authority[2].pawn_id -cne $authority[5].pawn_id -and $authority[2].grant -cne $authority[5].grant
   # An actual invocation of the unchanged production permission predicate is
   # required; a client send or an inactive ability alone cannot prove denial.
   $oldBefore=$authority[4];$oldAfter=$authority[6]
   $calls=[decimal]$oldAfter.rpc_calls-[decimal]$oldBefore.rpc_calls
   $denied=[decimal]$oldAfter.rpc_denied-[decimal]$oldBefore.rpc_denied
   $authorityValid=$authorityValid -and $calls -gt 0 -and $denied -eq $calls -and $oldAfter.rpc_allowed -ceq $oldBefore.rpc_allowed
   $authorityValid=$authorityValid -and $authority[8].rpc_calls -ceq $oldAfter.rpc_calls -and $authority[8].rpc_denied -ceq $oldAfter.rpc_denied
  }
 }
 $clients=@()
 foreach($slot in 0..1){
  $path=Join-Path $RunDirectory "session-client_$slot.csv"
  $rows=@(if(Test-Path -LiteralPath $path -PathType Leaf){Import-Csv -LiteralPath $path})
  $valid=$authorityValid -and $rows.Count -eq 5
  if($valid){for($phase=0;$phase -le 4;++$phase){
   $row=$rows[$phase];$server=$authority[$phase*2+$slot]
   $rowValid=Test-SessionRow -Row $row -Mode '3' -Phase $phase -Slot $slot
   $valid=$valid -and $rowValid
   if(!$rowValid){continue}
   foreach($field in @('state_id','pawn_id','account','character','grant')){$valid=$valid -and $row.$field -ceq $server.$field}
   $valid=$valid -and $row.state_id -ceq $rows[0].state_id -and $row.asc_object_id -ceq $rows[0].asc_object_id -and !$row.lease_epoch
   foreach($field in @('rpc_calls','rpc_allowed','rpc_denied','stale_complete_rejected','stale_revoke_rejected','app_attempted','app_applied')){$valid=$valid -and $row.$field -ceq '0'}
   if($phase -gt 0){$valid=$valid -and [decimal]$row.frame -gt [decimal]$rows[$phase-1].frame -and [decimal]$row.actor_info_revision -ge [decimal]$rows[$phase-1].actor_info_revision}
  }}
  if($valid){
   $activationPhase=if($slot -eq 0){1}else{2}
   $valid=[decimal]$rows[$activationPhase].rpc_sent -gt 0
   if($slot -eq 0){$valid=$valid -and [decimal]$rows[3].rpc_sent -gt [decimal]$rows[2].rpc_sent}
  }
  $logPath=Join-Path $RunDirectory "Client$slot.log"
  $packets=@(if(Test-Path -LiteralPath $logPath -PathType Leaf){Select-String -LiteralPath $logPath -Pattern 'SESSION_PACKETS slot=([01]) lag=([0-9]+) variance=([0-9]+) loss=([0-9]+) order=([0-9]+) duplicate=([0-9]+) netmode=([0-9]+)'|ForEach-Object{$_.Matches[0]}})
  $packetsValid=$packets.Count -eq 1
  if($packetsValid){$g=$packets[0].Groups;$packetsValid=$g[1].Value -ceq [string]$slot -and $g[7].Value -ceq '3';for($i=2;$i -le 6;++$i){$packetsValid=$packetsValid -and $g[$i].Value -ceq '0'}}
  $clients+=[ordered]@{slot=$slot;rows=$rows.Count;statePassed=[bool]$valid;nativePacketsPassed=[bool]$packetsValid;passed=([bool]$valid -and [bool]$packetsValid)}
 }
 [ordered]@{requested=$true;passed=($authorityValid -and $serverPacketsValid -and $summaryValid -and @($clients|Where-Object{!$_.passed}).Count -eq 0);authorityRows=$authority.Count;authorityPassed=[bool]$authorityValid;nativeServerPacketsPassed=[bool]$serverPacketsValid;cleanupAndCapturedApplicationPassed=[bool]$summaryValid;clients=$clients;scope='Trusted in-process admission and connection takeover, actual old PS/ASC RPC permission denial and captured application fence. No external authentication, network DB restore or arbitrary same-connection RPC epoch claim.'}
}
$questHistoryValidation=if($QuestHistoryOnly){Test-QuestHistoryWire -RunDirectory $out -NetworkCondition $Condition}else{[ordered]@{requested=$false;passed=$true}}
$trustedSessionValidation=if($TrustedSessionOnly){Test-TrustedSessionWire -RunDirectory $out}else{[ordered]@{requested=$false;passed=$true}}
$warningValidation=if($WarningOnly){Test-WarningWire -RunDirectory $out -NetworkCondition $Condition}else{[ordered]@{requested=$false;passed=$true}}
$npcAssemblyValidation=if($NPCAssemblyOnly){Test-NPCAssemblyWire -RunDirectory $out -NetworkCondition $Condition}else{[ordered]@{requested=$false;passed=$true}}
$slotRecords=@(if(Test-Path -LiteralPath "$out/Server.log" -PathType Leaf){Select-String -LiteralPath "$out/Server.log" -Pattern '\bSLOT_REGISTERED slot=([01]) count=([12]) source=LoginOptions\b'})
$registeredSlots=@($slotRecords|ForEach-Object{[int]$_.Matches[0].Groups[1].Value})
$slotIdentityPassed=$slotRecords.Count -eq 2 -and @($registeredSlots|Sort-Object -Unique).Count -eq 2
if($ReverseClientLaunchOrder){$slotIdentityPassed=$slotIdentityPassed -and $registeredSlots.Count -eq 2 -and $registeredSlots[0] -eq 1 -and $registeredSlots[1] -eq 0}
$nativeHandlesCapturedBeforeWait=$handles.Count -eq $processes.Count
$passed=(!$startupError) -and $processes.Count -eq 3 -and $nativeHandlesCapturedBeforeWait -and $slotIdentityPassed -and $sourceRuntimeUnchanged -and @($exits|Where-Object{$null -eq $_ -or $_ -ne 0}).Count -eq 0 -and @($receipts|Where-Object{!$_.passed}).Count -eq 0 -and !$timeouts.Count -and (!$MountOnly -or $mountValidation.passed) -and $warningValidation.passed -and $trustedSessionValidation.passed -and $npcAssemblyValidation.passed -and $npcAssemblyLaunchBarrier.passed -and $questHistoryValidation.passed
@{passed=$passed;startupError=$startupError;startedProcessCount=$processes.Count;afterProvenanceError=$afterProvenanceError;slotIdentityPassed=$slotIdentityPassed;registrationOrder=$registeredSlots;sourceRuntimeUnchanged=$sourceRuntimeUnchanged;mountValidation=$mountValidation;warningValidation=$warningValidation;trustedSessionValidation=$trustedSessionValidation;npcAssemblyValidation=$npcAssemblyValidation;questHistoryValidation=$questHistoryValidation;npcAssemblyLaunchBarrier=$npcAssemblyLaunchBarrier;exitCodes=$exits;receipts=$receipts;timeouts=$timeouts.Count;nativeHandlesCapturedBeforeWait=$nativeHandlesCapturedBeforeWait}|ConvertTo-Json -Depth 5|Set-Content "$out/verification.json"
if(!$passed){throw "Actual network validation failed/incomplete: $out"}
Write-Output "Actual network assertions passed: $out"
