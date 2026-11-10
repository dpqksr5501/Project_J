param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName,
    [ValidateRange(2,8)][int]$ClientCount = 2,
    [ValidateRange(100,2048)][int]$NPCCount = 100,
    [switch]$AllRelevant,
    [switch]$ParallelNet,
    [switch]$ServerPerformanceThreads,
    [switch]$Animation,
    [switch]$CombatInput,
    [switch]$CanonicalMelee,
    [switch]$CanonicalMeleeMovingRecipient,
    [switch]$Cleave,
    [switch]$CleaveThreat,
    [ValidateSet(0,32)][int]$CleaveFanout = 0,
    [switch]$MoverBuffers,
    [switch]$RawReceives,
    [switch]$Quests,
    [switch]$WorldClock,
    [switch]$AttributeAudience,
    [switch]$Companion,
    [switch]$EquipmentInterest,
    [switch]$PartyRoster,
    [switch]$CraftingJobs,
    [switch]$PlayerStateAudience,
    [switch]$PlayerStateAudienceLifetime,
    [switch]$PlayerStateAudienceOwnerView,
    [switch]$PartyUI,
    [switch]$NoTrace,
    [ValidateRange(0,200)][int]$PacketLag = 0,
    [ValidateRange(0,20)][int]$PacketLoss = 0,
    [int]$Port = 17871,
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8',
    [string]$Executable,
    [switch]$RenderedCrowd,
    [switch]$RenderedClientCSV,
    [switch]$RenderedClothExecutionObservation,
    [ValidateRange(-1,4000000)][int]$RenderedClothVertexBudget = -1,
    [ValidateSet('hnh','nnn')][string]$RenderedClothTaskPriority,
    [ValidateRange(1,2)][int]$RenderedFlyingReuseLevel = 1,
    [switch]$IrisCSV,
    [switch]$RenderedEndpoints,
    [switch]$RenderedFixedEndpoints,
    [switch]$NPBlobObservation,
    [ValidateRange(-1,1)][int]$NPBlobDelta = -1,
    [ValidateRange(0,2000000)][int]$ConnectionBytesPerSecond = 0,
    [ValidateRange(-1,64)][int]$IrisPacketsPerFullSend = -1
)
# Pure log reader: also callable offline by loading only this function's AST.
function Get-ProjectJCanonicalSSRObservation {
 param([string]$Log,[string]$Source,[string]$Target,[int]$PredictionKey,[bool]$MovingRecipient)
 $duplicate=[regex]::IsMatch($Log,'SSR active attack rejected\. Requester='+[regex]::Escape($Source)+' Target='+[regex]::Escape($Target)+' Node=\S+ Reason=DuplicateTarget')
 $geometry=$null;$acceptedSubmission=$null
 $nativeSSR='(?m)^(?:\[[^\]\r\n]*\])*\s*LogProjectJCombatHitValidation: Verbose: SSR '
 $boundsPattern=$nativeSSR+'history bounds Requester=(?<source>\S+) Target=(?<target>\S+) Query=(?<query>\S+) HasBounds=(?<has>[01]) Oldest=(?<oldest>\S+) Newest=(?<newest>\S+) Sequence=(?<seq>[0-9]+) PredictionKey=(?<key>[0-9]+)\r?$'
 foreach($bounds in [regex]::Matches($Log,$boundsPattern)){
  $seq=$key=0;$query=$oldest=$newest=0.0
  if($bounds.Groups['source'].Value -cne $Source -or $bounds.Groups['target'].Value -cne $Target -or $bounds.Groups['has'].Value -cne '1' -or
     ![int]::TryParse($bounds.Groups['seq'].Value,[ref]$seq) -or $seq -le 0 -or ![int]::TryParse($bounds.Groups['key'].Value,[ref]$key) -or $key -le 0 -or $key -ne $PredictionKey){continue}
  $style=[Globalization.NumberStyles]::Float;$culture=[Globalization.CultureInfo]::InvariantCulture
  if(![double]::TryParse($bounds.Groups['query'].Value,$style,$culture,[ref]$query) -or ![double]::TryParse($bounds.Groups['oldest'].Value,$style,$culture,[ref]$oldest) -or
     ![double]::TryParse($bounds.Groups['newest'].Value,$style,$culture,[ref]$newest) -or ![double]::IsFinite($query) -or ![double]::IsFinite($oldest) -or ![double]::IsFinite($newest) -or
     $oldest -gt $query -or $query -gt $newest){continue}
  # Never pair a bound with a later request across another native SSR diagnostic.
  $next=[regex]::Match($Log.Substring($bounds.Index+$bounds.Length),$nativeSSR+'(?<message>[^\r\n]+)\r?$')
  if($MovingRecipient){
   # Only the complete legacy line or this exact typed-clock suffix is supported.
   $submitted=[regex]::Match($next.Groups['message'].Value,'^decision=Accepted Reason=SpecSubmitted Sequence=(?<seq>[0-9]+) PredictionKey=(?<key>[0-9]+)(?: Query=(?<query>\S+) SourceQuery=(?<sourceQuery>\S+) Selected=(?<selected>\S+) ActiveStart=(?<activeStart>\S+) ClientStart=(?<clientStart>\S+) NodeVisit=(?<visit>[0-9]+))?$')
   if($submitted.Success -and $submitted.Groups['seq'].Value -ceq $bounds.Groups['seq'].Value -and $submitted.Groups['key'].Value -ceq $bounds.Groups['key'].Value){
    $clock=$null
    if($submitted.Groups['query'].Success){
     $values=@{};$finite=$true;$visit=0
     foreach($field in @('query','sourceQuery','selected','activeStart','clientStart')){
      $value=0.0
      if(![double]::TryParse($submitted.Groups[$field].Value,$style,$culture,[ref]$value) -or ![double]::IsFinite($value)){$finite=$false;break}
      $values[$field]=$value
     }
     if(!$finite -or ![int]::TryParse($submitted.Groups['visit'].Value,[ref]$visit) -or $visit -le 0){continue}
     $mapped=$values.activeStart+($values.query-$values.clientStart)
     # 1ns is only a decimal-log arithmetic comparison, never a gameplay tolerance.
     if($values.query -ne $query -or $values.clientStart -lt 0 -or $values.activeStart -lt 0 -or $values.query -lt $values.clientStart -or
        $values.selected -lt $values.activeStart -or ![double]::IsFinite($mapped) -or [Math]::Abs($values.sourceQuery-$mapped) -gt 1.e-9){continue}
     $clock=[ordered]@{sourceQuery=$values.sourceQuery;selected=$values.selected;activeStart=$values.activeStart;clientStart=$values.clientStart;nodeVisit=$visit;mappingVerified=$true}
    }
    $acceptedSubmission=[ordered]@{source=$Source;target=$Target;sequence=$seq;predictionKey=$key;query=$query;oldest=$oldest;newest=$newest;queryWithinHistory=$true;decision='Accepted';reason='SpecSubmitted'}
    if($null -ne $clock){$acceptedSubmission['nodeClock']=$clock}
    break
   }
  }
  $decision=[regex]::Match($next.Groups['message'].Value,'^decision=Rejected Reason=GeometryUnconfirmed Sequence=(?<seq>[0-9]+) PredictionKey=(?<key>[0-9]+)$')
  if(!$decision.Success -or $decision.Groups['seq'].Value -cne $bounds.Groups['seq'].Value -or $decision.Groups['key'].Value -cne $bounds.Groups['key'].Value){continue}
  $geometry=[ordered]@{source=$Source;target=$Target;sequence=$seq;predictionKey=$key;hasBounds=$true;query=$query;oldest=$oldest;newest=$newest;queryWithinHistory=$true;decision='GeometryUnconfirmed'}
  if(!$MovingRecipient){break}
 }
 return [ordered]@{duplicate=$duplicate;geometry=$geometry;acceptedSubmission=$acceptedSubmission;tracePending=(($null -ne $acceptedSubmission) -or !($duplicate -or $null -ne $geometry))}
}
$ErrorActionPreference = 'Stop'
if ($ServerPerformanceThreads -and $Executable) { throw 'ServerPerformanceThreads requires this runner dedicated-server mode; packaged Game execution is a listen server.' }
. "$PSScriptRoot/Source-Provenance.ps1"
. "$PSScriptRoot/../Audit/Continuation-ScratchPaths.ps1"
if ($CleaveFanout -and (!$Cleave -or !$Animation -or $ClientCount -ne 2 -or $Executable -or $CleaveThreat -or $WorldClock -or $NoTrace)) { throw 'Fanout32 requires the dedicated authored Cleave cohort and its server-only late trace.' }
if ($CleaveThreat -and !$Cleave) { throw 'CleaveThreat requires the unchanged Cleave baseline cohort.' }
if ($Cleave -and (!$Animation -or $ClientCount -ne 2 -or $CombatInput -or $Companion -or $EquipmentInterest -or $PartyRoster -or $Quests -or $AttributeAudience)) { throw 'Cleave requires exactly2 authored clients in a separate cohort.' }
if ($CombatInput -and !$Animation) { throw 'CombatInput requires the authored Animation fixture.' }
if($CanonicalMeleeMovingRecipient -and !$CanonicalMelee){throw 'MovingRecipient requires the existing CanonicalMelee cohort.'}
if($CanonicalMelee -and (!$Animation -or !$CombatInput -or $ClientCount -ne 2 -or $Cleave -or $Companion -or $EquipmentInterest -or $PartyRoster -or $CraftingJobs -or $AttributeAudience -or $RenderedCrowd -or $NoTrace)){throw 'CanonicalMelee requires two Animation+CombatInput clients and the existing native trace in its separate cohort.'}
if ($RawReceives -and !$MoverBuffers) { throw 'RawReceives requires the existing bounded MoverBuffers capture.' }
if ($MoverBuffers -and !$Animation) { throw 'MoverBuffers requires the authored Animation fixture.' }
if ($AttributeAudience -and !$Animation) { throw 'AttributeAudience requires actual authored player avatars.' }
if ($Companion -and (!$Animation -or $ClientCount -ne 2 -or $CombatInput)) { throw 'Companion requires exactly2 authored clients and a separate combat cohort.' }
if ($PartyRoster -and (!$Animation -or $ClientCount -ne 2 -or $CombatInput -or $Companion)) { throw 'PartyRoster requires exactly2 authored clients in its separate cohort.' }
if ($EquipmentInterest -and (!$Animation -or $ClientCount -ne 2 -or $CombatInput -or $Companion)) { throw 'EquipmentInterest requires exactly2 authored clients without combat/companion phases.' }
if ($PlayerStateAudience -and (!$PartyRoster -or $EquipmentInterest -or $AllRelevant)) { throw 'PlayerStateAudience requires the separate PartyRoster spatial cohort.' }
if ($PlayerStateAudienceLifetime -and !$PlayerStateAudience) { throw 'PlayerStateAudienceLifetime requires the existing PlayerStateAudience cohort.' }
if ($PlayerStateAudienceOwnerView -and !$PlayerStateAudienceLifetime) { throw 'PlayerStateAudienceOwnerView requires the existing PlayerStateAudienceLifetime cohort.' }
if ($CraftingJobs -and ($ClientCount -ne 2 -or !$NoTrace -or $Animation -or $CombatInput -or $Cleave -or $Companion -or $EquipmentInterest -or $PartyRoster -or $Quests -or $AttributeAudience -or $RenderedCrowd)) { throw 'CraftingJobs requires exactly2 NoTrace clients in its separate no-provider cohort.' }
if ($PartyUI -and !$PartyRoster) { throw 'PartyUI renders/captures only the explicit PartyRoster cohort.' }
if ($RenderedCrowd -and (!$Animation -or !$NoTrace -or $ClientCount -ne 2 -or $Cleave -or $CombatInput -or $Companion -or $EquipmentInterest -or $PartyRoster -or $Quests -or $AttributeAudience -or $MoverBuffers)) { throw 'RenderedCrowd requires two authored owners, NoTrace, and a separate cohort without buffer capture.' }
if ($RenderedClientCSV -and !$RenderedCrowd) { throw 'RenderedClientCSV requires the existing RenderedCrowd/NoTrace cohort.' }
if ($RenderedClothExecutionObservation -and !$RenderedClientCSV) { throw 'RenderedClothExecutionObservation requires the actual Client1 RHI CSV cohort.' }
if ($RenderedClothVertexBudget -ge 0 -and !$RenderedClothExecutionObservation) { throw 'RenderedClothVertexBudget requires the actual cloth observation/RHI CSV cohort.' }
if ($RenderedClothTaskPriority -and !$RenderedClientCSV) { throw 'RenderedClothTaskPriority requires the existing Client1 RHI CSV cohort.' }
# ValidateSet is case-insensitive; the engine's three-character priority parser is not.
if ($RenderedClothTaskPriority) { $RenderedClothTaskPriority = $RenderedClothTaskPriority.ToLowerInvariant() }
if ($RenderedEndpoints -and !$RenderedCrowd) { throw 'RenderedEndpoints requires the existing RenderedCrowd/NoTrace cohort.' }
if ($RenderedFixedEndpoints -and !$RenderedEndpoints) { throw 'RenderedFixedEndpoints requires existing actual endpoint observations.' }
if ($IrisCSV -and (!$RenderedCrowd -or !$NoTrace)) { throw 'IrisCSV requires the separate RenderedCrowd/NoTrace cohort.' }
if ($NPBlobDelta -ge 0 -and !$IrisCSV) { throw 'NPBlobDelta A/B requires the same rendered cohort with IrisCSV on both runs.' }
if ($NPBlobObservation -and !$IrisCSV) { throw 'NPBlobObservation requires the server Iris CSV capture.' }
if (($ConnectionBytesPerSecond -gt 0 -or $IrisPacketsPerFullSend -ge 0) -and !$IrisCSV) { throw 'Explicit send-envelope observations require the rendered Iris CSV cohort.' }
if ($ConnectionBytesPerSecond -gt 0 -and $ConnectionBytesPerSecond -lt 1800) { throw 'Connection speed must meet the native1800B/s minimum.' }
if ($IrisPacketsPerFullSend -eq 0) { throw 'Normal Iris packet allowance must be positive; never disable the limit.' }
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\','/')
Set-Location -LiteralPath $projectRoot
$busy = @(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCodingConsole,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue)
if ($busy.Count) { throw 'Engine processes active; finish them normally before another run/build.' }
if ($Port -lt 1024 -or $Port -gt 65535) { throw 'Invalid local test port.' }
$runRoot = "$projectRoot/Saved/Validation/GroupD_20260910/$RunName"
if (Test-Path -LiteralPath $runRoot) { throw 'Run directory already exists; choose a unique name.' }
$scratchContext=Get-ContinuationScratchContext $projectRoot
$scratch=New-ContinuationScratchRun $scratchContext "Network_$RunName"
$serverUserDirectory=Join-Path $scratch.Root 'ServerUser'
$clientUserDirectories=@{}
foreach($slot in 1..$ClientCount){$clientUserDirectories[[string]$slot]=Join-Path $scratch.Root "Client${slot}User"}
New-Item -ItemType Directory -Path $runRoot | Out-Null
$files = @(& git -c core.fsmonitor=false ls-files --modified --others --exclude-standard -- Source Scripts Config Plugins Project_J.uproject)
$snapshot = @($files | Sort-Object -Unique | ForEach-Object {
    $sourcePath = Join-Path $projectRoot $_
    if (Test-Path -LiteralPath $sourcePath -PathType Leaf) {
        $destination = "$runRoot/SourceSnapshot/$_"
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $sourcePath -Destination $destination
        [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash}
    }
})
$exe = if ($Executable) { (Resolve-Path -LiteralPath $Executable).Path } else { "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" }
if (!(Test-Path -LiteralPath $exe -PathType Leaf) -or [IO.Path]::GetExtension($exe) -ine '.exe') { throw 'Select an actual runtime .exe file.' }
$executableSHA256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
# The installed packaged Game target hosts a listen server; Editor -server remains dedicated.
$serverNetworkMode = if ($Executable) { 'ListenServer' } else { 'DedicatedServer' }
$expectedExcludedLocalHosts = if ($Executable) { 1 } else { 0 }
$provenance=Get-ProjectJSourceProvenance -ProjectRoot $projectRoot -RuntimePaths @($exe)
[ordered]@{run=$RunName;scratchRoot=$scratch.Root;serverUserDirectory=$serverUserDirectory;clientUserDirectories=$clientUserDirectories;connectionBytesPerSecondRequested=$(if($ConnectionBytesPerSecond){$ConnectionBytesPerSecond}else{$null});irisPacketsPerFullSendRequested=$(if($IrisPacketsPerFullSend -ge 0){$IrisPacketsPerFullSend}else{$null});sendEnvelopeScope='Invocation-only shared sender-resource comparison; bytes and normal full-batch packet allowance are independent native constraints, no delivery guarantee.';serverNetworkMode=$serverNetworkMode;serverPerformanceThreads=[bool]$ServerPerformanceThreads;serverPerformanceThreadsScope='Server argv only: supported -useperfthreads, no client/Hz/NP/animation-quality override.';expectedExcludedLocalHosts=$expectedExcludedLocalHosts;packaged=[bool]$Executable;executable=$exe;executableSHA256=$executableSHA256;allRelevant=[bool]$AllRelevant;parallelNet=[bool]$ParallelNet;animation=[bool]$Animation;rawReceives=[bool]$RawReceives;authorityEncode=[bool]$RawReceives;combatInput=[bool]$CombatInput;canonicalMelee=[bool]$CanonicalMelee;canonicalMeleeMovingRecipient=[bool]$CanonicalMeleeMovingRecipient;cleave=[bool]$Cleave;cleaveFanout=$CleaveFanout;cleaveFanoutScope='32 real authority casters, matching GE/Health and cancellation; clients retain initial AOI checks only. CPU-only trace starts on server after preparation, excluding trace tail.';cleaveThreat=[bool]$CleaveThreat;quests=[bool]$Quests;worldClock=[bool]$WorldClock;attributeAudience=[bool]$AttributeAudience;companion=[bool]$Companion;equipmentInterest=[bool]$EquipmentInterest;equipmentClientsUseRHI=[bool]$EquipmentInterest;equipmentVisualScope='EquipmentInterest asserts physical cosmetic weapons; both clients require RHI while the server remains NullRHI.';craftingJobs=[bool]$CraftingJobs;craftingJobsScope='Explicit synthetic owner-summary projection and real no-provider rejection only; not online durable ownership/authentication.';partyRoster=[bool]$PartyRoster;playerStateAudience=[bool]$PlayerStateAudience;playerStateAudienceOwnerView=[bool]$PlayerStateAudienceOwnerView;playerStateAudienceOwnerViewScope='Separate explicit detached-camera owner-Pawn scope contract: phase29 slot1 views the retained near old Pawn through native SetViewTarget while its current creation-dependent Pawn remains far; stored camera distances must exceed2xcull for8seconds on client and authority. Not a reproduction or fix of LifetimeLoss1.';playerStateAudienceLifetime=[bool]$PlayerStateAudienceLifetime;playerStateAudienceLifetimeScope=$(if($PlayerStateAudienceOwnerView){'Explicit owner-view variant: phase29 slot1 remains near the old Pawn while its current owned Pawn stays far; restore current view before phase31 old destruction. RetiredSource remains containment.'}else{'Separate actual replacement cohort; retains old Pawn alive near then far, destroys it while far, and requires observable broad RetiredSource fallback to remain. This fallback is containment, not completed retirement optimization.'});partyUI=[bool]$PartyUI;renderedCrowd=[bool]$RenderedCrowd;renderedClientCSV=[bool]$RenderedClientCSV;renderedClothExecutionObservation=[bool]$RenderedClothExecutionObservation;renderedClothVertexBudgetRequested=$(if($RenderedClothVertexBudget -ge 0){$RenderedClothVertexBudget}else{$null});renderedClothTaskPriorityRequested=$(if($RenderedClothTaskPriority){$RenderedClothTaskPriority}else{$null});renderedClientCSVScope='Client1 RHI stock CSV only; supported blocking finalization after measured phases, no gameplay policy change.';irisCSV=[bool]$IrisCSV;npBlobDeltaRequested=$(if($NPBlobDelta -ge 0){$NPBlobDelta}else{$null});serverNPObjectDeltaClassRequested=$(if($NPBlobDelta -ge 0){'/Script/NetworkPrediction.NetworkPredictionComponent'}else{$null});renderedEndpoints=[bool]$RenderedEndpoints;renderedFixedEndpoints=[bool]$RenderedFixedEndpoints;npBlobObservation=[bool]($NPBlobObservation -or $NPBlobDelta -ge 0);renderedPopulation=$(if($RenderedCrowd){200}else{0});renderedSocketConnections=$(if($RenderedCrowd){2}else{0});renderedFlyingReuseLevel=$(if($RenderedCrowd){$RenderedFlyingReuseLevel}else{$null});trace=(!$NoTrace);packetLag=$PacketLag;packetLoss=$PacketLoss;port=$Port;head=(& git -c core.fsmonitor=false rev-parse HEAD);cpu=(Get-CimInstance Win32_Processor).Name;clientCount=$ClientCount;npcCount=$NPCCount;scenario=($serverNetworkMode + ' plus separate real socket clients and static AOI counter probes. Server is NullRHI; EquipmentInterest uses RHI for both clients, and optional presentation modes record their own client capability. Packaged listen-server runs exclude the normal local host from fixture participants; only the Editor mode is a dedicated-server checkpoint; authored animation uses actual Mover player avatars. Includes production PlayerState owner-only inventory and equipment FastArrays. Optional WorldClock checks replicated clock values without the authoring asset and synchronized weather. Optional Quests checks production owner-only progress and tracking updates and rejects foreign quest data. Optional authored player animation: Start, Stop, landing, hidden cancellation, coalesced boundaries; simulated proxies, no fake roles. Optional AttributeAudience checks owner-only combat stats and public health/mana on real player ASCs and one native GAS NPC, five initial/delta stages. Total driver bytes include fixture control traffic.');sourceFiles=$snapshot;sourceProvenance=$provenance} |
    ConvertTo-Json -Depth 5 | Set-Content "$runRoot/manifest.json" -Encoding utf8
$cleaveAssetFiles=@(
    'Content/DataAssetSets/NPC/DA_NPC_PrototypeCleaveActivation.uasset',
    'Content/DataAssetSets/AttackDefinition/NPC/DA_Attack_NPC_PrototypeCleave.uasset',
    'Content/GAs/GA_NPC_PrototypeCleave.uasset',
    'Content/NPCs/BP_NPC_Test.uasset',
    'Content/GAs/GA_NPC_TestAttack.uasset',
    'Content/Anim_Assets/Great_Sword/Animations/Sword/Montage/AM_Greatsword_LMB1.uasset',
    'Content/Animation_Logic/ABPs/ABP_Player_Master.uasset')
$cleaveAssetsBefore=@()
if($Cleave){
    $cleaveAssetsBefore=@($cleaveAssetFiles|ForEach-Object {Get-FileHash -LiteralPath (Join-Path $projectRoot $_) -Algorithm SHA256}|Select-Object Path,Hash)
    $cleaveAssetsBefore|ConvertTo-Json|Set-Content -LiteralPath "$runRoot/CleaveAssetsBefore.json" -Encoding utf8
}
$canonicalAssetFiles=@(
 'Content/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.uasset',
 'Content/DataAssetSets/CombatStyle/DA_CombatStyle_Spellblade.uasset',
 'Content/DataAssetSets/Combo/DA_Combo_Spellblade.uasset',
 'Content/DataAssetSets/AttackDefinition/Spellblade/DA_Attack_Spellblade_LMB1.uasset',
 'Content/DataAssetSets/Animation_Profiles/Equip/DA_Spellblade_CanonicalEquip.uasset',
 'Content/DataAssetSets/CombatStyle/DA_CombatStyle_Spellblade_Canonical.uasset',
 'Content/DataAssetSets/Combo/DA_Combo_Spellblade_Canonical.uasset',
 'Content/DataAssetSets/AttackDefinition/Spellblade/DA_Attack_Spellblade_LMB1_Canonical.uasset')
if($CanonicalMeleeMovingRecipient){$canonicalAssetFiles+=@('Content/Tests/Combat/L_CanonicalNetwork.umap')}
$canonicalAssetsBefore=if($CanonicalMelee){@($canonicalAssetFiles|ForEach-Object{[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $projectRoot $_) -Algorithm SHA256).Hash.ToLowerInvariant()}})}else{@()}
if($CanonicalMelee){$canonicalAssetsBefore|ConvertTo-Json|Set-Content -LiteralPath "$runRoot/CanonicalAssetsBefore.json" -Encoding utf8}
$renderedAssetFiles=@('Content/Character_BPs/Spellblade/BP_Spellblade.uasset','Content/Animation_Logic/ABPs/ABP_Player_Master.uasset','Content/Animation_Logic/ABPs/Spellblade/ABP_Spellblade_Woman_RunTIme.uasset','Content/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.uasset','Content/DataAssetSets/Presentation/Greatsword/DA_Greatsword_Presentation.uasset')
$renderedAssetsBefore=@()
if($RenderedCrowd){
    $renderedAssetsBefore=@($renderedAssetFiles|ForEach-Object{Get-FileHash -LiteralPath (Join-Path $projectRoot $_) -Algorithm SHA256}|Select-Object Path,Hash)
    $renderedAssetsBefore|ConvertTo-Json|Set-Content -LiteralPath "$runRoot/RenderedAssetsBefore.json" -Encoding utf8
}
$common = @('-game','-unattended','-nop4','-nosplash','-NullRHI','-nosound','-NoLiveCoding','-NoDDCCleanup','-SaveToUserDir','-ProjectJNetworkFixture',"-ProjectJFixtureClients=$ClientCount","-ProjectJFixtureNPCs=$NPCCount",'-statnamedevents','-trace=cpu,frame,bookmark,region,counters,net','-NetTrace=1','-ExecCmds=t.MaxFPS 30')
if (!$Executable) { $common = @("$projectRoot/Project_J.uproject") + $common }
if ($ConnectionBytesPerSecond -gt 0) {
    # One explicit B/s request fans out to native advertise and both local/server
    # caps. Scalar tokens are separate; no config-file writes or rate bypass.
    $common+=@(
        "-ini:Engine:[/Script/Engine.Player]:ConfiguredInternetSpeed=$ConnectionBytesPerSecond",
        "-ini:Engine:[/Script/Engine.Player]:ConfiguredLanSpeed=$ConnectionBytesPerSecond",
        "-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:MaxClientRate=$ConnectionBytesPerSecond",
        "-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:MaxInternetClientRate=$ConnectionBytesPerSecond"
    )
}
if ($Animation) { $common += '-ProjectJAnimationFixture' }
if ($Cleave) { $common += '-ProjectJNPCCleaveFixture' }
if ($CleaveFanout) { $common += "-ProjectJNPCCleaveFanout=$CleaveFanout" }
if ($CleaveThreat) { $common += '-ProjectJNPCDamageThreatFixture' }
if ($RenderedCrowd) { $common += '-ProjectJRenderedClientFixture' }
if ($Quests) { $common += '-ProjectJQuestFixture' }
if ($WorldClock) { $common += '-ProjectJWorldClockFixture' }
if ($AttributeAudience) { $common += '-ProjectJAttributeAudienceFixture' }
if ($Companion) { $common += '-ProjectJCompanionAudienceFixture' }
if ($EquipmentInterest) { $common += '-ProjectJEquipmentInterestFixture' }
if ($CraftingJobs) { $common += '-ProjectJCraftingJobsFixture' }
if ($PartyRoster) { $common += '-ProjectJPartyRosterFixture' }
if ($PlayerStateAudience) { $common += '-ProjectJPlayerStateAudience' }
if ($PlayerStateAudienceLifetime) { $common += '-ProjectJPlayerStateAudienceLifetime' }
if ($PlayerStateAudienceOwnerView) { $common += '-ProjectJPlayerStateAudienceOwnerView' }
if ($MoverBuffers) {
    $common += '-ProjectJCaptureMoverBuffers'
}
if ($RawReceives) { $common += '-ProjectJCaptureRawReceives' }
if ($CombatInput) {
    $common += '-ProjectJCombatInputFixture'
    $common = @($common | ForEach-Object { if ($_ -like '-ExecCmds=*') { $_ + ',Project_J.Combat.ComboDebug 1,ProjectJ.Combat.Presentation.Debug 1' } else { $_ } })
}
if($CanonicalMelee){
 $common+='-ProjectJCanonicalMeleeFixture'
 if($CanonicalMeleeMovingRecipient){$common+='-ProjectJCanonicalMeleeMovingRecipient'}
 $common=@($common|ForEach-Object{if($_ -like '-ExecCmds=*'){$_+',Log LogProjectJCombatHitValidation Verbose'}else{$_}})
}
$common += @("-PktLag=$PacketLag","-PktLoss=$PacketLoss")
# These EditorOnly toolsets require editor UClasses. This host runs -game/-server;
# normal editor/MCP and native Automation RunTests keep their project plugin configuration.
$disabledEditorToolsets = @()
if (!$Executable) {
    $disabledEditorToolsets = @('EditorToolset','AutomationTestToolset','ToolsetRegistry')
    $common += "-DisablePlugins=$($disabledEditorToolsets -join ',')"
    # ToolsetRegistry can be compiled into the Editor host despite plugin selection.
    $common += '-DisablePython'
}
ConvertTo-Json -InputObject $disabledEditorToolsets | Set-Content -LiteralPath "$runRoot/disabled-editor-toolsets.json" -Encoding utf8
# Start-Process needs quotes retained around arguments containing spaces.
function Start-Fixture($arguments) {
    # Filter the final argv, including per-server/client tracefile additions.
    # Functional CSV/PNG cohorts can opt out; the default traced argv is intact.
    $actualArguments = @(foreach($argument in $arguments) {
        if(($NoTrace -or $CleaveFanout) -and $argument -match '^-(?:trace=|tracefile=|NetTrace=|statnamedevents(?:=|$))'){continue}
        $argument
    })
    if($NoTrace -or $CleaveFanout){
        $logs=@($actualArguments|Where-Object {$_ -like '-ABSLOG=*'})
        if($logs.Count -ne 1){throw 'NoTrace launch requires one explicit process log.'}
        $role=[IO.Path]::GetFileNameWithoutExtension($logs[0].Substring('-ABSLOG='.Length))
        if($role -notmatch '^(Server|Client[1-8])$'){throw 'Unexpected network fixture log role.'}
        $actualArguments|ConvertTo-Json|Set-Content -LiteralPath "$runRoot/$role-effective-arguments.json" -Encoding utf8
    }
    $quoted = @($actualArguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    Start-Process -FilePath $exe -ArgumentList $quoted -WindowStyle Hidden -PassThru
}
$serverMap = if($CanonicalMeleeMovingRecipient){'/Game/Tests/Combat/L_CanonicalNetwork?game=/Script/Project_J.Project_JNetworkLoadGameMode'}else{'/Engine/Maps/Entry?game=/Script/Project_J.Project_JNetworkLoadGameMode'}
if ($Executable) { $serverMap += '?listen' }
$serverArgs = $common + @($serverMap,"-port=$Port","-ABSLOG=$runRoot/Server.log","-ProjectJDOutput=$runRoot/Server","-tracefile=$runRoot/Server.utrace","-UserDir=$serverUserDirectory/")
if (!$Executable) { $serverArgs += '-server' }
# Supported engine process policy, server argv only. Default and client argv remain unchanged.
if ($ServerPerformanceThreads) { $serverArgs += '-useperfthreads' }
if ($RenderedEndpoints) { $serverArgs += '-ProjectJRenderedDecodedEndpoints' }
if ($IrisPacketsPerFullSend -ge 0) {
    $serverArgs=@($serverArgs|ForEach-Object {if($_ -like '-ExecCmds=*'){$_+",net.Iris.ReplicationWriterMaxAllowedPacketsIfNotHugeObject $IrisPacketsPerFullSend,net.Iris.ReplicationWriterMaxAllowedPacketsIfNotHugeObject"}else{$_}})
}
# Existing RawReceives optin also observes the authority model-encode boundary.
# Server-only deferred command; retain packaged -Executable and every client argument.
if ($RawReceives) {
    if (@($serverArgs | Where-Object { $_ -like '-ExecCmds=*' }).Count -ne 1) { throw 'Authority capture requires exactly one existing deferred command argument.' }
    $serverArgs = @($serverArgs | ForEach-Object { if ($_ -like '-ExecCmds=*') { $_ + ',ProjectJ.CaptureMoverBuffers 120 AuthorityEncode' } else { $_ } })
}
# Only the server writes this optional Iris CSV. Preserve the existing command
# and every client argument; the fixture stops it on both success and timeout.
if ($IrisCSV) {
    if (@($serverArgs | Where-Object { $_ -like '-ExecCmds=*' }).Count -ne 1) { throw 'IrisCSV requires exactly one existing deferred command argument.' }
    $serverArgs += '-ProjectJIrisCSVFixture'
    # Without per-class mapping the stock counters fall back to Undefined.
    $serverArgs += '-ini:Engine:[SystemSettings]:net.Iris.UseVeryVerboseIrisCsvStats=1'
    $serverArgs = @($serverArgs | ForEach-Object { if ($_ -like '-ExecCmds=*') { $_ + ',csvprofile START' } else { $_ } })
}
if ($NPBlobDelta -ge 0) {
    # Encoder choice is server-only; all peers retain the same always-supported
    # decoder. Both A/B runs enable the same optional category and capture cost.
    # Explicit experiment policy for NP subobjects, separate from their Pawn root.
    $serverArgs += '-ini:Engine:[/Script/IrisCore.ObjectReplicationBridgeConfig]:+DeltaCompressionConfigs=(ClassName=/Script/NetworkPrediction.NetworkPredictionComponent,bEnableDeltaCompression=true)'
    $serverArgs = @($serverArgs | ForEach-Object { if ($_ -like '-ExecCmds=*') { $_.Replace(',csvprofile START',",np.ProjectJ.ReplicationBlobDelta $NPBlobDelta,np.ProjectJ.ReplicationBlobDelta,csvprofile START") } else { $_ } })
}
if ($NPBlobObservation -or $NPBlobDelta -ge 0) {
    $serverArgs = @($serverArgs | ForEach-Object { if ($_ -like '-ExecCmds=*') { $_.Replace(',csvprofile START',',CsvCategory ProjectJNPBlobDelta enable,csvprofile START') } else { $_ } })
    if($NPBlobDelta -lt 0){
        $serverArgs=@($serverArgs|ForEach-Object {if($_ -like '-ExecCmds=*'){$_.Replace(',csvprofile START',',np.ProjectJ.ReplicationBlobDelta,csvprofile START')}else{$_}})
    }
}
if ($AllRelevant) { $serverArgs += '-ProjectJDAllRelevant' }
if ($ParallelNet) {
    $serverArgs += @(
        '-ini:Engine:[/Script/Engine.Engine]:+IrisNetDriverConfigs=(NetDriverName=GameNetDriver,bCanUseIris=true,bCanUseParallelNetConnectionTick=true)',
        '-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:ReplicationSystemConfigServer=(bAllowParallelTasks=true,MinConnectionsForParallelTick=2)',
        '-ini:Engine:[SystemSettings]:net.iris.AllowParallelNetTick=1'
    )
}
$serverArgs | ConvertTo-Json | Set-Content "$runRoot/server-arguments.json" -Encoding utf8
$server = Start-Fixture $serverArgs
$deadline = [DateTime]::UtcNow.AddSeconds(120)
while (!(Test-Path "$runRoot/Server.log") -or !(Select-String -LiteralPath "$runRoot/Server.log" -SimpleMatch 'READY Iris=1' -Quiet)) {
    $server.Refresh()
    if ($server.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw "Server did not become ready. Inspect $runRoot/Server.log; do not terminate remaining engine processes." }
    Start-Sleep -Milliseconds 500
}
$serverModeVerified = Select-String -LiteralPath "$runRoot/Server.log" -SimpleMatch "READY Iris=1 NetMode=$serverNetworkMode " -Quiet
$excludedLocalHosts = @(Select-String -LiteralPath "$runRoot/Server.log" -SimpleMatch "LISTEN_HOST excluded=1 local=1 connection=0 fixtureSlot=-1").Count
if (!$serverModeVerified -or $excludedLocalHosts -ne $expectedExcludedLocalHosts) { throw "Server mode/local-host isolation does not match $serverNetworkMode. Inspect Server.log; leave the server to exit normally." }
$clients = @(1..$ClientCount | ForEach-Object {
    $clientCommon=$common
    if($EquipmentInterest){
        # This cohort asserts actual remote weapon actors/attachments. Renderer
        # admission must stay real; a headless actor is no longer a visual proof.
        $clientCommon=@($common|Where-Object {$_ -cne '-NullRHI'})+@('-RenderOffscreen','-windowed','-ResX=960','-ResY=540','-NoVSync')
    }
    if($PartyUI -and $_ -eq 1){
        $clientCommon=@($common|Where-Object {$_ -cne '-NullRHI'})+@('-RenderOffscreen','-windowed','-ResX=960','-ResY=540','-NoVSync','-ProjectJCapturePartyUI')
    }
    if($RenderedCrowd -and $_ -eq 1){
        $clientCommon=@($common|Where-Object {$_ -cne '-NullRHI' -and $_ -notlike '-ExecCmds=*'})+@('-RenderOffscreen','-windowed','-ResX=1280','-ResY=720','-NoVSync',"-ExecCmds=t.MaxFPS 0,r.VSync 0,mover.Prediction.ReuseFlyingParams $RenderedFlyingReuseLevel",'-ProjectJRenderedClientObserver','-ProjectJRenderedCrowdTrajectoryMetrics',"-ProjectJRenderedCrowdOutput=$runRoot/Client1/Rendered")
        if($RenderedEndpoints){$clientCommon+='-ProjectJRenderedDecodedEndpoints'}
        if($RenderedFixedEndpoints){$clientCommon+='-ProjectJRenderedFixedEndpointSelection'}
        if($RenderedClientCSV){
            if(@($clientCommon|Where-Object {$_ -like '-ExecCmds=*'}).Count -ne 1){throw 'RenderedClientCSV requires one existing Client1 command argument.'}
            $clientCommon+='-ProjectJRenderedClientCSV'
            # Retain supported blocking finalization after the measured phases.
            # UE5.8's nonblocking stop can re-arm writing while its worker is
            # finalizing, then finalize the already-released writer a second time.
            $clientCommon=@($clientCommon|ForEach-Object {if($_ -like '-ExecCmds=*'){$_+',csv.BlockOnCaptureEnd 1,csv.BlockOnCaptureEnd,CsvCategory Animation enable,csvprofile START'}else{$_}})
            if($RenderedClothExecutionObservation){
                $clientCommon=@($clientCommon|ForEach-Object {if($_ -like '-ExecCmds=*'){$_.Replace(',csvprofile START',',ProjectJ.ClothBudget.ObserveExecution 1,csvprofile START')}else{$_}})
            }
            if($RenderedClothVertexBudget -ge 0){
                $clientCommon+='-ProjectJRenderedCrowdClothExecutionExperiment'
                $clientCommon=@($clientCommon|ForEach-Object {if($_ -like '-ExecCmds=*'){$_.Replace(',csvprofile START',",ProjectJ.ClothBudget.ExecutionVertexCap $RenderedClothVertexBudget,ProjectJ.ClothBudget.ExecutionVertexCap,csvprofile START")}else{$_}})
            }
            if($RenderedClothTaskPriority){
                # Closed two-value scheduling probe only. Setter and readback are
                # Client1-only and precede capture; server/Client2 remain unchanged.
                $clientCommon=@($clientCommon|ForEach-Object {if($_ -like '-ExecCmds=*'){$_.Replace(',csvprofile START',",TaskGraph.TaskPriorities.ParallelClothTask $RenderedClothTaskPriority,TaskGraph.TaskPriorities.ParallelClothTask,csvprofile START")}else{$_}})
            }
        }
    }
    Start-Fixture ($clientCommon + @("127.0.0.1:$Port","-ABSLOG=$runRoot/Client$_.log","-ProjectJDOutput=$runRoot/Client$_","-tracefile=$runRoot/Client$_.utrace","-UserDir=$($clientUserDirectories[[string]$_])/"))
})
# Retain all handles before waiting for the first process. Clients may finish
# while the server wait is still in progress.
$fixtureProcesses = @($server) + $clients
$nativeHandles = @($fixtureProcesses | ForEach-Object { $_.Handle })
@($server) + $clients | Select-Object Id,ProcessName | ConvertTo-Json | Set-Content "$runRoot/processes.json" -Encoding utf8
Write-Output "Started server and $ClientCount clients: $runRoot"
foreach ($process in $fixtureProcesses) { $process.WaitForExit() }
$exitCodes = @($fixtureProcesses | ForEach-Object {$_.ExitCode})
# Persist native completion before result/CSV parsing can fail independently.
[ordered]@{
    nativeHandleCapturedBeforeWait=$true
    processes=@(for ($nativeExitIndex=0; $nativeExitIndex -lt $fixtureProcesses.Count; $nativeExitIndex++) {
        $nativeExitProcess=$fixtureProcesses[$nativeExitIndex]
        [ordered]@{
            role=$(if ($nativeExitIndex -eq 0) {'Server'} else {"Client$nativeExitIndex"})
            id=$nativeExitProcess.Id
            hasExited=$nativeExitProcess.HasExited
            exitCode=$exitCodes[$nativeExitIndex]
        }
    })
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$runRoot/native-process-exits.json" -Encoding utf8
$resultFiles = @('Server/server.json') + @(1..$ClientCount | ForEach-Object {"Client$_/client.json"})
$results = @($resultFiles | ForEach-Object {
    $path = "$runRoot/$_"
    if (!(Test-Path -LiteralPath $path)) { [ordered]@{file=$_;success=$false;reason='Missing result'} }
    else { $result = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json; [ordered]@{file=$_;success=$result.success;reason=$result.reason} }
})
# Archive only bounded, finalized CSV evidence back under the historical
# evidence layout. Large engine UserDir/cache remains outside Documents.
$csvArchives=@()
foreach($role in @($(if($IrisCSV){'Server'}),$(if($RenderedClientCSV){'Client1'}))|Where-Object {$_}) {
    $user=if($role -eq 'Server'){$serverUserDirectory}else{$clientUserDirectories['1']}
    $sourceCSV=Join-Path $user 'Saved/Profiling/CSV'
    $captures=@(Get-ChildItem -LiteralPath $sourceCSV -Filter '*.csv' -File -ErrorAction SilentlyContinue)
    if($captures.Count -eq 1){
        if($captures[0].Length -gt 64MB){throw 'Unexpectedly large CSV; raw Temp evidence preserved, do not replicate it into Documents.'}
        $archive="$runRoot/${role}User/Saved/Profiling/CSV"
        New-Item -ItemType Directory -Path $archive -Force|Out-Null
        $destination=Join-Path $archive $captures[0].Name
        Copy-Item -LiteralPath $captures[0].FullName -Destination $destination -ErrorAction Stop
        $sourceHash=(Get-FileHash -LiteralPath $captures[0].FullName -Algorithm SHA256).Hash
        if($sourceHash -cne (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash){throw 'CSV evidence copy changed.'}
        $csvArchives += [ordered]@{role=$role;source=$captures[0].FullName;archive=$destination;bytes=$captures[0].Length;sha256=$sourceHash}
    }
}
ConvertTo-Json -InputObject @($csvArchives) -Depth 4|Set-Content -LiteralPath "$runRoot/csv-archives.json" -Encoding utf8
# Existing buffer/endpoint consumers need the capture's complete closure,
# including actor metadata and summary, not only its main CSV.
$moverArchiveRequests=@()
if($MoverBuffers){
    if($RawReceives){$moverArchiveRequests+=@{role='Server';user=$serverUserDirectory}}
    foreach($slot in 1..$ClientCount){$moverArchiveRequests+=@{role="Client$slot";user=$clientUserDirectories[[string]$slot]}}
}elseif($RenderedEndpoints){$moverArchiveRequests+=@{role='Client1';user=$clientUserDirectories['1']}}
$moverArchives=@()
foreach($request in $moverArchiveRequests){
    $captureDirectory=Join-Path $request.user 'Saved/Validation/MoverBuffers'
    if(!(Test-Path -LiteralPath $captureDirectory)){continue}
    Assert-ContinuationNoReparse $captureDirectory
    $captureFiles=@(Get-ChildItem -LiteralPath $captureDirectory -File)
    if($captureFiles.Count -gt 128 -or ($captureFiles|Measure-Object Length -Sum).Sum -gt 128MB){throw 'Unexpectedly large buffer evidence; preserve Temp originals.'}
    $destinationDirectory="$runRoot/$($request.role)User/Saved/Validation/MoverBuffers"
    New-Item -ItemType Directory -Path $destinationDirectory -Force|Out-Null
    foreach($capture in $captureFiles){
        if(($capture.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Unexpected buffer evidence reparse point.'}
        $destination=Join-Path $destinationDirectory $capture.Name
        $sourceHash=(Get-FileHash -LiteralPath $capture.FullName -Algorithm SHA256).Hash
        if(Test-Path -LiteralPath $destination){
            if((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -cne $sourceHash){throw 'Preserve different existing buffer evidence.'}
        }else{Copy-Item -LiteralPath $capture.FullName -Destination $destination -ErrorAction Stop}
        if((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -cne $sourceHash){throw 'Buffer evidence copy changed.'}
        $moverArchives += [ordered]@{role=$request.role;source=$capture.FullName;archive=$destination;bytes=$capture.Length;sha256=$sourceHash}
    }
}
ConvertTo-Json -InputObject @($moverArchives) -Depth 4|Set-Content -LiteralPath "$runRoot/mover-buffer-archives.json" -Encoding utf8
$networkBudgetValidation=$null
if($ConnectionBytesPerSecond -gt 0 -or $IrisPacketsPerFullSend -ge 0){
    $budgetRows=@();$budgetValid=$true;$budgetErrors=@()
    try{
        $preparation=Get-Content -LiteralPath "$runRoot/Server/rendered-preparation.json" -Raw|ConvertFrom-Json
        if(@($preparation.server_connection_policies).Count -ne 2){throw 'Expected two actual server parent connections.'}
        foreach($policy in $preparation.server_connection_policies){
            $budgetRows += [ordered]@{role='ServerParent';slot=$policy.fixture_slot;bytesPerSecond=$policy.current_net_speed_bytes_per_second;maxPacketBytes=$policy.max_packet_bytes}
            if($ConnectionBytesPerSecond -gt 0 -and $policy.current_net_speed_bytes_per_second -ne $ConnectionBytesPerSecond){throw 'Server negotiated speed differs from request.'}
        }
        foreach($slot in 1..2){
            $policy=Get-Content -LiteralPath "$runRoot/Client$slot/connection-policy.json" -Raw|ConvertFrom-Json
            if(!$policy.complete -or $policy.max_packet_bytes -le 0){throw 'Client native connection observation missing.'}
            $budgetRows += [ordered]@{role="Client$slot";slot=$policy.fixture_slot;bytesPerSecond=$policy.current_net_speed_bytes_per_second;maxPacketBytes=$policy.max_packet_bytes}
            if($ConnectionBytesPerSecond -gt 0 -and $policy.current_net_speed_bytes_per_second -ne $ConnectionBytesPerSecond){throw 'Client native speed differs from request.'}
        }
        foreach($boundary in @('start','end')){
            $policy=Get-Content -LiteralPath "$runRoot/Server/iris-selected-phase4-$boundary.json" -Raw|ConvertFrom-Json
            if(!$policy.complete -or $policy.bandwidth_throttling_disabled -ne 0){throw 'Selected native budget observation missing or throttling bypass enabled.'}
            if($ConnectionBytesPerSecond -gt 0 -and $policy.current_net_speed_bytes_per_second -ne $ConnectionBytesPerSecond){throw 'Selected parent speed drifted.'}
            if($IrisPacketsPerFullSend -ge 0 -and $policy.normal_writer_packet_cap -ne $IrisPacketsPerFullSend){throw 'Actual normal full-batch packet allowance differs from request.'}
            $budgetRows += [ordered]@{role="SelectedParent_$boundary";bytesPerSecond=$policy.current_net_speed_bytes_per_second;normalPacketCap=$policy.normal_writer_packet_cap;maxBunchBits=$policy.max_single_bunch_bits;throttlingDisabled=$policy.bandwidth_throttling_disabled}
        }
    }catch{$budgetValid=$false;$budgetErrors+=$_.Exception.Message}
    $networkBudgetValidation=[ordered]@{passed=$budgetValid;requestedBytesPerSecond=$ConnectionBytesPerSecond;requestedNormalPacketCap=$IrisPacketsPerFullSend;observations=$budgetRows;errors=$budgetErrors;scope='Actual native connection speeds on both peers plus selected parent start/end normal packet allowance; combined shared resource envelope, not bytes-only causality, packet delivery or hard deadline.'}
    $networkBudgetValidation|ConvertTo-Json -Depth 7|Set-Content -LiteralPath "$runRoot/network-budget-validation.json" -Encoding utf8
}
$irisCSVValidation=$null
if ($IrisCSV) {
    $captures=@(Get-ChildItem -LiteralPath "$runRoot/ServerUser/Saved/Profiling/CSV" -Filter '*.csv' -File -ErrorAction SilentlyContinue)
    $completed=Select-String -LiteralPath "$runRoot/Server.log" -SimpleMatch 'Capture Ended. Writing CSV to file :' -Quiet
    $csvSaved=$completed -and $captures.Count -eq 1 -and $captures[0].Length -gt 0
    $npCategoryEnabled=(!($NPBlobObservation -or $NPBlobDelta -ge 0)) -or (Select-String -LiteralPath "$runRoot/Server.log" -SimpleMatch "CsvProfiler: category 'ProjectJNPBlobDelta' is now enabled." -Quiet)
    $irisCSVValidation=[ordered]@{passed=($csvSaved -and $npCategoryEnabled);npBlobCategoryEnabled=$(if($NPBlobObservation -or $NPBlobDelta -ge 0){$npCategoryEnabled}else{$null});captureCount=$captures.Count;capture=$(if($captures.Count -eq 1){$captures[0].FullName}else{$null});sha256=$(if($captures.Count -eq 1){(Get-FileHash -LiteralPath $captures[0].FullName -Algorithm SHA256).Hash}else{$null});bytes=$(if($captures.Count -eq 1){$captures[0].Length}else{0});finalizedLog=$completed;scope='Server-only stock Iris CSV; saved-file completion is separate from functional cohort success and does not identify a PS writer failure by itself.'}
    $irisCSVValidation|ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$runRoot/iris-csv-validation.json" -Encoding utf8
}
$clientCSVValidation=$null
if ($RenderedClientCSV) {
    $clientCaptures=@(Get-ChildItem -LiteralPath "$runRoot/Client1User/Saved/Profiling/CSV" -Filter '*.csv' -File -ErrorAction SilentlyContinue)
    $clientCompleted=(Test-Path -LiteralPath "$runRoot/Client1.log" -PathType Leaf) -and (Select-String -LiteralPath "$runRoot/Client1.log" -SimpleMatch 'Capture Ended. Writing CSV to file :' -Quiet)
    $clientHeaderValid=$false;$clientMetadataValid=$false;$clientSamplerOrdinals=@();$clientHeaders=@();$clientFinalHeader=-1;$clientParseError=$null
    if($clientCaptures.Count -eq 1 -and $clientCaptures[0].Length -gt 0){
        try{
            # Final summary is authoritative. Preserve duplicate stock labels by
            # position; never feed the incomplete initial header to Import-Csv.
            $clientLines=@(Get-Content -LiteralPath $clientCaptures[0].FullName)
            $clientHeaderRows=@(for($lineIndex=0;$lineIndex -lt $clientLines.Count;$lineIndex++){if($clientLines[$lineIndex].StartsWith('EVENTS,')){$lineIndex}})
            if($clientHeaderRows.Count -eq 2 -and $clientHeaderRows[0] -eq 0){
                $clientFinalHeader=$clientHeaderRows[-1]
                if($clientFinalHeader -ge 2 -and $clientFinalHeader+2 -eq $clientLines.Count -and $clientLines[$clientFinalHeader+1] -match '^\[HasHeaderRowAtEnd\],1(?:,|$)'){
                    $clientMetadataValid=$true
                    $clientHeaderReader=[Microsoft.VisualBasic.FileIO.TextFieldParser]::new([IO.StringReader]::new($clientLines[$clientFinalHeader]))
                    $clientHeaderReader.SetDelimiters(',');$clientHeaderReader.HasFieldsEnclosedInQuotes=$true
                    try{$clientHeaders=@($clientHeaderReader.ReadFields())}finally{$clientHeaderReader.Dispose()}
                    $clientSamplerOrdinals=@(for($column=0;$column -lt $clientHeaders.Count;$column++){if($clientHeaders[$column] -ceq 'Exclusive/GameThread/ProjectJRenderedCrowdObserver'){$column}})
                    $clientHeaderValid=$clientSamplerOrdinals.Count -eq 1
                }
            }
        }catch{$clientParseError=$_.Exception.Message}
    }
    $clientCSVValidation=[ordered]@{passed=($clientCompleted -and $clientHeaderValid);role='Client1';captureCount=$clientCaptures.Count;capture=$(if($clientCaptures.Count -eq 1){$clientCaptures[0].FullName}else{$null});sha256=$(if($clientCaptures.Count -eq 1){(Get-FileHash -LiteralPath $clientCaptures[0].FullName -Algorithm SHA256).Hash}else{$null});bytes=$(if($clientCaptures.Count -eq 1){$clientCaptures[0].Length}else{0});finalizedLog=[bool]$clientCompleted;hasHeaderRowAtEnd=$clientMetadataValid;finalHeaderRawRow=$clientFinalHeader;finalHeaderColumns=$clientHeaders.Count;samplerScopeOrdinal=$(if($clientSamplerOrdinals.Count -eq 1){$clientSamplerOrdinals[0]}else{$null});parseError=$clientParseError;scope='Client1 actual RHI stock CSV only; final summary header by ordinal and finalized metadata/log, separate from server Iris CSV and functional quality verdict.'}
    $clientCSVValidation|ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$runRoot/client-csv-validation.json" -Encoding utf8
}
$renderedClothTaskPriorityValidation=$null
if($RenderedClothTaskPriority){
    # Engine ConsoleManager query prints the actual value with LastSetBy;
    # the setter/argv echo alone cannot satisfy this readback gate.
    $priorityPattern='TaskGraph\.TaskPriorities\.ParallelClothTask = "([hnb]{3})"\s+LastSetBy:'
    $priorityReadbacks=@(if(Test-Path -LiteralPath "$runRoot/Client1.log" -PathType Leaf){
        Select-String -LiteralPath "$runRoot/Client1.log" -Pattern $priorityPattern | ForEach-Object {
            $match=[regex]::Match($_.Line,$priorityPattern)
            [ordered]@{line=$_.LineNumber;actual=$match.Groups[1].Value;text=$_.Line}
        }
    })
    $priorityActual=if($priorityReadbacks.Count -eq 1){$priorityReadbacks[0].actual}else{$null}
    $renderedClothTaskPriorityValidation=[ordered]@{passed=($priorityReadbacks.Count -eq 1 -and $priorityActual -ceq $RenderedClothTaskPriority);role='Client1';requested=$RenderedClothTaskPriority;actual=$priorityActual;readbackCount=$priorityReadbacks.Count;readbacks=$priorityReadbacks;scope='Startup actual supported ParallelClothTask queue-priority CVar readback only; engine-owned completion, solver quality and cadence unchanged. Not a frame-time improvement assertion.'}
    $renderedClothTaskPriorityValidation|ConvertTo-Json -Depth 5|Set-Content -LiteralPath "$runRoot/client-cloth-priority-validation.json" -Encoding utf8
}
$renderedClothExecutionValidation=$null
if($RenderedClothVertexBudget -ge 0){
    $executionPattern='ProjectJ\.ClothBudget\.ExecutionVertexCap = "?([0-9]+)"?\s+LastSetBy:'
    $executionReadbacks=@(if(Test-Path -LiteralPath "$runRoot/Client1.log" -PathType Leaf){
        Select-String -LiteralPath "$runRoot/Client1.log" -Pattern $executionPattern | ForEach-Object {
            $match=[regex]::Match($_.Line,$executionPattern)
            [ordered]@{line=$_.LineNumber;actual=[long]$match.Groups[1].Value;text=$_.Line}
        }
    })
    $executionActual=if($executionReadbacks.Count -eq 1){$executionReadbacks[0].actual}else{$null}
    $renderedClothExecutionValidation=[ordered]@{passed=($executionReadbacks.Count -eq 1 -and $executionActual -eq $RenderedClothVertexBudget);role='Client1';requested=$RenderedClothVertexBudget;actual=$executionActual;readbackCount=$executionReadbacks.Count;readbacks=$executionReadbacks;scope='Actual startup CVar query; garment opt-in is identical in both experimental arms. Execution, quality and frame-time outcomes are validated separately.'}
    $renderedClothExecutionValidation|ConvertTo-Json -Depth 5|Set-Content -LiteralPath "$runRoot/client-cloth-execution-validation.json" -Encoding utf8
}
$timeouts = @(Get-ChildItem -LiteralPath $runRoot -Recurse -Filter timeout.json)
$bufferValidations = @()
$bufferDiagnostics = @()
$authorityEncodeValid = !$RawReceives
$authorityEncodeValidation = $null
$authorityCapturePath = $null
if ($MoverBuffers) {
    if ($RawReceives) {
        $authorityCaptures = @(Get-ChildItem -LiteralPath "$runRoot/ServerUser/Saved/Validation/MoverBuffers" -Filter '*.csv' -ErrorAction SilentlyContinue | Where-Object Name -notlike '*.actors.csv')
        if ($authorityCaptures.Count -eq 1) {
            $authorityCapturePath = $authorityCaptures[0].FullName
            & python "$PSScriptRoot/Validate-MoverBufferCapture.py" $authorityCapturePath --authority-encodes --log "$runRoot/Server.log" --raw-report "$runRoot/Server/mover-authority-encodes.json" --output "$runRoot/Server/mover-authority-validation.json"
            $authorityEncodeValid = $LASTEXITCODE -eq 0
        }
        $authorityEncodeValidation = [ordered]@{passed=$authorityEncodeValid;captures=$authorityCaptures.Count;capture=$authorityCapturePath;scope='Actual authority SP model encoding, not per-connection sends; client join requires same-host Windows QPC.'}
    }
    foreach ($slot in 1..$ClientCount) {
        $captures = @(Get-ChildItem -LiteralPath "$runRoot/Client${slot}User/Saved/Validation/MoverBuffers" -Filter '*.csv' -ErrorAction SilentlyContinue | Where-Object Name -notlike '*.actors.csv')
        $valid = $false
        if ($captures.Count -eq 1) {
            $rawArguments=@()
            if($RawReceives){$rawArguments=@('--require-raw-receives','--raw-report',"$runRoot/Client$slot/mover-raw-receives.json")}
            if($RawReceives -and $authorityEncodeValid){$rawArguments+=@('--authority-capture',$authorityCapturePath)}
            & python "$PSScriptRoot/Validate-MoverBufferCapture.py" $captures[0].FullName --log "$runRoot/Client$slot.log" --min-receive-hz 10 --output "$runRoot/Client$slot/mover-buffer-validation.json" @rawArguments
            $valid = $LASTEXITCODE -eq 0
        }
        $bufferValidations += [ordered]@{client=$slot;passed=$valid}
    }
    $bufferDiagnostics = @(Get-ChildItem -LiteralPath $runRoot -Filter '*.log' | Select-String -Pattern 'LogNetworkPrediction: Warning:|LogNetworkPrediction.*Interpolation out of sync|NetworkPrediction.*SystemFault' | ForEach-Object { $_.Line })
}
$attributeAudienceValidation=@()
if($AttributeAudience){
    $attributes=@('MaxHealth','Health','MaxMana','Mana','AttackPower','Defense')
    $authority=@(Import-Csv -LiteralPath "$runRoot/Server/attribute-audience.csv")
    $expectedRows=5*($ClientCount+1)*$attributes.Count
    $authorityKeys=@{}
    $authorityValid=$authority.Count -eq $expectedRows
    foreach($row in $authority){
        $key="$($row.stage)|$($row.actor)|$($row.attribute)"
        $authorityValid=$authorityValid -and !$authorityKeys.ContainsKey($key) -and $row.passed -eq '1' -and $row.audience -eq 'authority' -and $attributes.Contains($row.attribute)
        $authorityKeys[$key]=$row
    }
    foreach($slot in 1..$ClientCount){
        $observed=@(Import-Csv -LiteralPath "$runRoot/Client$slot/attribute-audience.csv")
        $valid=$authorityValid -and $observed.Count -eq $expectedRows
        $seen=@{}
        foreach($row in $observed){
            $key="$($row.stage)|$($row.actor)|$($row.attribute)"
            $serverRow=$authorityKeys[$key]
            $valid=$valid -and !$seen.ContainsKey($key) -and $null -ne $serverRow -and $row.passed -eq '1'
            $seen[$key]=$true
            $privateObserver=$row.attribute -in @('AttackPower','Defense') -and $row.audience -ne 'owner'
            if(!$privateObserver -and $serverRow){
                $valid=$valid -and [math]::Abs([double]$row.base-[double]$serverRow.base) -lt 0.001 -and [math]::Abs([double]$row.current-[double]$serverRow.current) -lt 0.001
            }
        }
        foreach($stage in 1..5){
            $stageRows=@($observed|Where-Object {[int]$_.stage -eq $stage})
            $valid=$valid -and @($stageRows|Where-Object audience -eq 'owner').Count -eq 6 -and @($stageRows|Where-Object audience -eq 'remote').Count -eq (6*($ClientCount-1)) -and @($stageRows|Where-Object audience -eq 'npc').Count -eq 6
        }
        $attributeAudienceValidation+=[ordered]@{client=$slot;passed=$valid;rows=$observed.Count;expectedRows=$expectedRows;authorityRows=$authority.Count;scope='FGameplayAttributeData base/current; five initial/delta stages; real owner/remote player ASCs and native GAS NPC; no late-join or performance A/B claim'}
    }
}
$cleaveThreatValidation=@()
if($CleaveThreat){
    # Additional proof is mandatory for this option; the unchanged nine-row
    # Cleave baseline cannot satisfy it or hide an early Stage6 completion.
    $threatAuthorityPath="$runRoot/Server/cleave-threat-authority.csv"
    $threatAuthorityPresent=Test-Path -LiteralPath $threatAuthorityPath -PathType Leaf
    $authority=@(if($threatAuthorityPresent){Import-Csv -LiteralPath $threatAuthorityPath})
    $valid=$authority.Count -eq 6;$byThreatKey=@{};$threatIds=@{};$threatCasters=@()
    foreach($r in $authority){
        $key="$($r.phase)|$($r.subject_slot)"
        $valid=$valid -and $r.phase -in @('33','34') -and $r.subject_slot -in @('0','1','2') -and !$byThreatKey.ContainsKey($key)
        $valid=$valid -and $r.passed -ceq '1' -and $r.source_current -ceq '1' -and $r.threat_released -ceq '1'
        $valid=$valid -and $r.actor_id -cmatch '^[1-9][0-9]*$' -and $r.caster_id -cmatch '^[1-9][0-9]*$'
        $valid=$valid -and $r.expected_primary_id -cmatch '^[1-9][0-9]*$' -and $r.actual_primary_id -ceq $r.expected_primary_id
        $valid=$valid -and $r.stock_target_id -cmatch '^[1-9][0-9]*$' -and $r.stock_target_id -cne $r.expected_primary_id
        $valid=$valid -and $r.peak_sources -ceq '2' -and $r.accepted_losses -ceq '5' -and ![string]::IsNullOrWhiteSpace($r.montage)
        foreach($field in @('source_total','distance','health','caster_before','caster_after')){
            $number=[double]$r.$field
            $valid=$valid -and ![double]::IsNaN($number) -and ![double]::IsInfinity($number)
        }
        $valid=$valid -and [double]$r.health -gt 0 -and [double]$r.health -le 1000 -and [double]$r.distance -gt 0
        $valid=$valid -and [double]$r.caster_after -gt 0 -and [double]$r.caster_before -gt [double]$r.caster_after
        if($threatIds.ContainsKey($r.subject_slot)){$valid=$valid -and $threatIds[$r.subject_slot] -ceq $r.actor_id}else{$threatIds[$r.subject_slot]=$r.actor_id}
        $byThreatKey[$key]=$r;$threatCasters+=$r.caster_id
    }
    $valid=$valid -and $threatIds.Count -eq 3 -and @($threatIds.Values|Sort-Object -Unique).Count -eq 3 -and @($threatCasters|Sort-Object -Unique).Count -eq 1
    foreach($phase in 33..34){
        $expected=$(if($phase -eq 33){1}else{0});$other=1-$expected;$effects=0;$contacts=0;$damaged=0
        $primary=$byThreatKey["$phase|$expected"];$near=$byThreatKey["$phase|$other"]
        if(!$primary -or !$near){$valid=$false;continue}
        $valid=$valid -and [double]$primary.source_total -gt [double]$near.source_total -and [double]$near.source_total -gt 0
        $valid=$valid -and [double]$primary.distance -gt ([double]$near.distance+5)
        if($phase -eq 33){$valid=$valid -and $primary.effects -ceq '1' -and [double]$primary.health -lt 1000}
        $total=[double]$primary.source_total+[double]$near.source_total
        foreach($slot in 0..2){
            $r=$byThreatKey["$phase|$slot"]
            if(!$r){$valid=$false;continue}
            $valid=$valid -and $r.expected_primary_id -ceq $primary.actor_id -and $r.stock_target_id -in @($threatIds.Values)
            $valid=$valid -and [Math]::Abs(([double]$r.caster_before-[double]$r.caster_after)-$total) -lt .001
            $valid=$valid -and $r.montage -ceq $primary.montage -and [int]$r.effects -in @(0,1) -and [int]$r.contact -in @(0,1)
            $valid=$valid -and $r.attack_completed -ceq '1' -and [int]$r.cancelled -eq [int]($phase -eq 34)
            if($slot -lt 2){
                $valid=$valid -and $r.source_epoch -cmatch '^[1-9][0-9]*$'
                if($phase -eq 34){$valid=$valid -and $r.source_epoch -ceq $byThreatKey["33|$slot"].source_epoch}
            }else{$valid=$valid -and $r.source_epoch -ceq '0' -and [double]$r.source_total -eq 0}
            if($phase -eq 34){$valid=$valid -and [Math]::Abs([double]$r.health-[double]$byThreatKey["33|$slot"].health) -lt .001}
            $effects+=[int]$r.effects;$contacts+=[int]$r.contact;$damaged+=[int]([double]$r.health -lt 1000)
        }
        $valid=$valid -and $effects -eq $(if($phase -eq 33){2}else{0}) -and $contacts -eq $(if($phase -eq 33){3}else{0}) -and $damaged -eq 2
    }
    $observerSlots=@()
    foreach($client in 1..2){
        $threatClientPath="$runRoot/Client$client/cleave-threat-observations.csv"
        $threatClientPresent=Test-Path -LiteralPath $threatClientPath -PathType Leaf
        $rows=@(if($threatClientPresent){Import-Csv -LiteralPath $threatClientPath})
        $clientValid=$valid -and $rows.Count -eq 6;$seen=@{}
        $slot=@($rows.observer_slot|Sort-Object -Unique);$clientValid=$clientValid -and $slot.Count -eq 1 -and $slot[0] -in @('0','1');$observerSlots+=$slot
        foreach($r in $rows){
            $key="$($r.phase)|$($r.subject_slot)";$a=$byThreatKey[$key]
            $clientValid=$clientValid -and !$seen.ContainsKey($key) -and $null -ne $a -and $r.passed -ceq '1'
            $clientValid=$clientValid -and $r.actor_id -ceq $a.actor_id -and $r.caster_id -ceq $a.caster_id -and [Math]::Abs([double]$r.health-[double]$a.health) -lt .001
            if($r.phase -ceq '33'){$clientValid=$clientValid -and $r.observed_attack -ceq '1' -and $r.montage -ceq $a.montage}
            else{$clientValid=$clientValid -and $r.phase -ceq '34' -and $r.observed_attack -ceq '0' -and [string]::IsNullOrEmpty($r.montage)}
            $seen[$key]=$true
        }
        $cleaveThreatValidation+=[ordered]@{client=$client;passed=$clientValid;rows=$rows.Count;authorityRows=$authority.Count;authorityPresent=$threatAuthorityPresent;clientPresent=$threatClientPresent;scope='Additional transient opt-in definition: two actual owners, five authored outgoing GE Health losses, stock-distance inversion and actual NPCAction/NPCAttack primary, normal replicated montage/Health and fresh-lease pre-impact cancellation. No production aggro quality, capacity or performance claim.'}
    }
    if(@($observerSlots|Sort-Object -Unique).Count -ne 2){foreach($row in $cleaveThreatValidation){$row.passed=$false}}
}

$companionValidation=@()
if($Companion){
    $authority=@(Import-Csv -LiteralPath "$runRoot/Server/companion-audience.csv")
    $byKey=@{}; $authorityValid=$authority.Count -eq 14
    foreach($r in $authority){
        $key="$($r.phase)|$($r.subject_slot)"
        $authorityValid=$authorityValid -and !$byKey.ContainsKey($key) -and [int]$r.phase -ge 8 -and [int]$r.phase -le 14 -and [int]$r.subject_slot -in @(0,1) -and $r.passed -eq '1' -and $r.no_player_connection -eq '1'
        $byKey[$key]=$r
        if($r.actor_id -ne '0'){$authorityValid=$authorityValid -and $r.target_character_id -match '^[0-9a-fA-F]{32}$' -and [double]$r.attack_base -gt 6000 -and [double]$r.defense_base -gt 7000}
    }
    foreach($phase in 8..14){foreach($subject in 0..1){
        $row=$byKey["$phase|$subject"]
        $wanted=($phase -eq 9 -and $subject -eq 0) -or $phase -in @(10,11,13) -or ($phase -eq 12 -and $subject -eq 1)
        $authorityValid=$authorityValid -and $null -ne $row -and (($row.actor_id -ne '0') -eq $wanted)
    }}
    $authorityValid=$authorityValid -and $byKey['9|0'].actor_id -eq $byKey['10|0'].actor_id -and $byKey['10|0'].actor_id -eq $byKey['11|0'].actor_id -and $byKey['13|0'].actor_id -ne $byKey['9|0'].actor_id
    foreach($phase in 11..13){$authorityValid=$authorityValid -and $byKey["$phase|1"].actor_id -eq $byKey['10|1'].actor_id}
    $targets0=@($authority|Where-Object {$_.subject_slot -eq '0' -and $_.actor_id -ne '0'}|Select-Object -ExpandProperty target_character_id -Unique)
    $targets1=@($authority|Where-Object {$_.subject_slot -eq '1' -and $_.actor_id -ne '0'}|Select-Object -ExpandProperty target_character_id -Unique)
    $authorityValid=$authorityValid -and $targets0.Count -eq 1 -and $targets1.Count -eq 1 -and $targets0[0] -ne $targets1[0]
    $observers=@()
    foreach($client in 1..2){
        $rows=@(Import-Csv -LiteralPath "$runRoot/Client$client/companion-audience.csv")
        $seen=@{}; $valid=$authorityValid -and $rows.Count -eq 14
        $slots=@($rows.observer_slot|Sort-Object -Unique)
        $valid=$valid -and $slots.Count -eq 1 -and [int]$slots[0] -in @(0,1)
        if($slots.Count -eq 1){$observers+=$slots[0]}
        foreach($r in $rows){
            $key="$($r.phase)|$($r.subject_slot)"; $a=$byKey[$key]
            $valid=$valid -and !$seen.ContainsKey($key) -and $null -ne $a -and $r.passed -eq '1' -and $r.no_player_connection -eq '1' -and $r.foreign_pointer_null -eq '1' -and $r.actor_id -eq $a.actor_id
            $seen[$key]=$true
            $own=($r.observer_slot -eq $r.subject_slot)
            $valid=$valid -and $r.own_pointer_id -eq $(if($own){$a.actor_id}else{'0'})
            if($a.actor_id -ne '0'){
                foreach($field in @('max_health_base','max_health_current','health_base','health_current')){
                    $value=[double]$r.$field; $expected=[double]$a.$field
                    $valid=$valid -and ![double]::IsNaN($value) -and ![double]::IsInfinity($value) -and [Math]::Abs($value-$expected) -lt .001
                }
                # Native observer also compares these to its actual local AttributeSet CDO.
                $valid=$valid -and [Math]::Abs([double]$r.attack_base-[double]$a.attack_base) -gt 1 -and [Math]::Abs([double]$r.defense_base-[double]$a.defense_base) -gt 1
            }
        }
        $companionValidation += [ordered]@{client=$client;passed=$valid;rows=$rows.Count;scope='Seven bounded late-spawn/delta/dismiss/re-summon phases; real canonical inventory VM use, production companion owner RPC, owner-only pointer and public NPC health. No packet-loss, capacity or real-Nav movement claim.'}
    }
    if(@($observers|Sort-Object -Unique).Count -ne 2){foreach($result in $companionValidation){$result.passed=$false}}
}
$partyValidation=@()
$partyScreenshotValidation=@()
if($PartyRoster){
    function Get-PartyExpectedCount([int]$Phase,[int]$Slot){
        if($Phase -eq 20){return 0}
        if($Phase -in @(21,24)){if($Slot -eq 0){return 1}else{return 0}}
        if($Phase -ge 26){return 8}
        return 2
    }
    $extraPartyPhases=if($PlayerStateAudienceLifetime){@(28,30,29,31)}elseif($PlayerStateAudience){@(28,29)}else{@()}
    function Test-AudienceAfterReplacement([int]$Phase){return $PlayerStateAudienceLifetime -and $Phase -in @(30,29,31,27)}
    $authority=@(Import-Csv -LiteralPath "$runRoot/Server/party-members.csv")
    $authorityKeys=@{};$sourceValid=$authority.Count -eq $(if($PlayerStateAudienceLifetime){106}elseif($PlayerStateAudience){74}else{42})
    $memberIds=@{};$groups=@()
    foreach($row in $authority){
        $key="$($row.phase)|$($row.observer_slot)|$($row.order)"
        $sourceValid=$sourceValid -and !$authorityKeys.ContainsKey($key) -and $row.passed -ceq '1'
        $authorityKeys[$key]=$row
    }
    foreach($slot in 0..1){
        $last=if($slot -eq 0){27}else{26}
        foreach($phase in (@(20..$last) + $extraPartyPhases)){
            $count=Get-PartyExpectedCount $phase $slot
            $indices=if($count -eq 0){@(-1)}else{@(0..($count-1))}
            foreach($index in $indices){
                $row=$authorityKeys["$phase|$slot|$index"]
                $sourceValid=$sourceValid -and $null -ne $row
                if(!$row){continue}
                if($count -eq 0){
                    $sourceValid=$sourceValid -and !$row.character_id -and !$row.party_id -and !$row.name -and $row.level -ceq '0' -and $row.health_known -ceq '0' -and $row.permille -ceq '0'
                    continue
                }
                $available=$index -lt 2 -and !($phase -eq 27 -and $index -eq 1)
                $sourceValid=$sourceValid -and $row.character_id -match '^[0-9a-fA-F]{32}$' -and !!$row.party_id -and $row.party_id -cne 'None' -and [int]$row.leader -eq [int]($index -eq 0) -and [int]$row.available -eq [int]$available
                $groups+=$row.party_id
                if($memberIds.ContainsKey($index)){$sourceValid=$sourceValid -and $memberIds[$index] -ceq $row.character_id}else{$memberIds[$index]=$row.character_id}
                if($available){
                    $health=if($index -eq 0){800}elseif($phase -ge 23){600}else{400}
                    $name=if($index -eq 0){'PartyFixtureLeader'}else{'PartyFixtureMember'}
                    $sourceValid=$sourceValid -and $row.name -ceq $name -and $row.level -ceq '1' -and $row.health_known -ceq '1' -and [int]$row.permille -eq $health
                }else{$sourceValid=$sourceValid -and !$row.name -and $row.level -ceq '0' -and $row.health_known -ceq '0' -and $row.permille -ceq '0'}
            }
        }
    }
    $sourceValid=$sourceValid -and @($groups|Sort-Object -Unique).Count -eq 1 -and $memberIds.Count -eq 8 -and @($memberIds.Values|Sort-Object -Unique).Count -eq 8
    $observers=@()
    foreach($client in 1..2){
        $summary=@(Import-Csv -LiteralPath "$runRoot/Client$client/party-summary.csv")
        $rows=@(Import-Csv -LiteralPath "$runRoot/Client$client/party-members.csv")
        $slots=@($summary.observer_slot|Sort-Object -Unique)
        $valid=$sourceValid -and $slots.Count -eq 1 -and [int]$slots[0] -in @(0,1)
        $slot=if($slots.Count -eq 1){[int]$slots[0]}else{-1}
        $observers+=$slot
        $last=if($slot -eq 0){27}else{26};$expectedSummary=$last-19+$extraPartyPhases.Count
        $valid=$valid -and $summary.Count -eq $expectedSummary
        $seen=@{}
        foreach($row in $summary){
            $phase=[int]$row.phase;$count=Get-PartyExpectedCount $phase $slot
            $valid=$valid -and !$seen.ContainsKey($phase) -and $phase -in (@(20..$last)+$extraPartyPhases) -and [int]$row.owner_rows -eq $count -and [int]$row.panel_rows -eq $count -and $row.foreign_rows -ceq '0' -and [int]$row.remote_pawns -eq $(if($PlayerStateAudience -and ($phase -eq 28 -or ($PlayerStateAudienceLifetime -and $phase -eq 30))){1}else{0}) -and $row.passed -ceq '1'
            $seen[$phase]=$true
        }
        $expectedData=@($authority|Where-Object {[int]$_.observer_slot -eq $slot})
        $valid=$valid -and $rows.Count -eq $expectedData.Count
        $seen=@{}
        foreach($row in $rows){
            $key="$($row.phase)|$($row.observer_slot)|$($row.order)";$expected=$authorityKeys[$key]
            $valid=$valid -and !$seen.ContainsKey($key) -and [int]$row.observer_slot -eq $slot -and $null -ne $expected -and $row.passed -ceq '1'
            $seen[$key]=$true
            if($expected){foreach($field in @('character_id','party_id','leader','available','name','level','health_known','permille')){$valid=$valid -and $row.$field -ceq $expected.$field}}
        }
        if($PlayerStateAudience){
            $interest=@(Import-Csv -LiteralPath "$runRoot/Client$client/player-state-interest.csv")
            $valid=$valid -and $interest.Count -eq $expectedSummary
            $interestPhases=@{}
            foreach($entry in $interest){
                $phase=[int]$entry.phase;$near=$phase -eq 28 -or ($PlayerStateAudienceLifetime -and $phase -eq 30)
                $foreign=if($near -or ((Test-AudienceAfterReplacement $phase) -and $slot -eq 0 -and $phase -ne 27)){1}else{0}
                $remote=if($near){1}else{0}
                $valid=$valid -and !$interestPhases.ContainsKey($phase) -and $phase -in $summary.phase -and [int]$entry.observer_slot -eq $slot -and [int]$entry.foreign_states -eq $foreign -and [int]$entry.remote_pawns -eq $remote -and $entry.foreign_private -ceq '0' -and $entry.linked -ceq '1' -and $entry.passed -ceq '1'
                if($near){$valid=$valid -and [float]$entry.health -eq $(if($slot -eq 0){60}else{80})}
                $interestPhases[$phase]=$true
            }
            $bindings=@(Import-Csv -LiteralPath "$runRoot/Server/player-state-bindings.csv")
            $valid=$valid -and $bindings.Count -eq $(if($PlayerStateAudienceLifetime){23}else{19}) -and $serverModeVerified -and $serverNetworkMode -cin @('DedicatedServer','ListenServer')
            $expectedObservers=if($serverNetworkMode -ceq 'ListenServer'){1}else{0}
            $observerIdentity=$null
            $bindingKeys=@{}
            foreach($entry in $bindings){
                $key="$($entry.phase)|$($entry.subject_slot)"
                $phase=[int]$entry.phase;$subject=[int]$entry.subject_slot
                $afterReplacement=Test-AudienceAfterReplacement $phase
                $wantsFallback=$afterReplacement -and $subject -eq 1
                $fallbacks=if($afterReplacement -and $phase -ne 27){1}else{0}
                $spatial=if($phase -eq 27){1}else{2-$fallbacks}
                $policy=if($wantsFallback){$entry.spatial -ceq '0' -and [uint32]$entry.fallback -gt 0}else{$entry.spatial -ceq '1' -and $entry.fallback -ceq '0'}
                $observerSpatial=[int]$entry.observer_spatial;$observerFallback=[int]$entry.observer_fallback
                $observerValid=$entry.observer_binding_valid -ceq '1' -and [int]$entry.observer_count -eq $expectedObservers -and $observerSpatial -in @(0,1) -and $observerFallback -in @(0,1) -and $observerSpatial+$observerFallback -eq $expectedObservers
                if($expectedObservers){
                    $observerValid=$observerValid -and [int]$entry.observer_player_id -eq [int]::MaxValue -and [uint32]$entry.observer_object_index -gt 0 -and [uint32]$entry.observer_object_index -ne [uint32]$entry.object_index -and [uint64]$entry.observer_net_id -gt 0 -and [uint64]$entry.observer_net_serial -gt 0 -and $entry.observer_net_system -match '^\d+$'
                    $identity="$($entry.observer_player_id)|$($entry.observer_object_index)|$($entry.observer_net_id)|$($entry.observer_net_serial)|$([uint32]$entry.observer_net_system)"
                    if($null -eq $observerIdentity){$observerIdentity=$identity}else{$observerValid=$observerValid -and $identity -ceq $observerIdentity}
                }else{
                    foreach($field in @('observer_player_id','observer_object_index','observer_net_id','observer_net_serial','observer_net_system')){$observerValid=$observerValid -and $entry.$field -ceq '0'}
                }
                $valid=$valid -and $observerValid -and !$bindingKeys.ContainsKey($key) -and $subject -in @(0,1) -and $phase -in (@(20..27)+$extraPartyPhases) -and $entry.assigned -ceq '1' -and $policy -and [uint32]$entry.object_index -gt 0 -and [uint32]$entry.pawn_index -gt 0 -and [uint32]$entry.avatar_index -gt 0 -and [uint32]$entry.owner_connection -gt 0 -and [float]$entry.radius -gt 0 -and [uint64]$entry.observed_spawns -ge $(if($afterReplacement){3}else{2}) -and $entry.consumed_spawns -ceq '2' -and $entry.pending_witnesses -ceq '0' -and [int]$entry.fallback_bindings -eq $fallbacks+$observerFallback -and [int]$entry.active_spatial -eq $spatial+$observerSpatial -and $entry.passed -ceq '1'
                $bindingKeys[$key]=$true
            }
            foreach($phase in @(20..26)+$extraPartyPhases){foreach($subject in @(0,1)){$valid=$valid -and $bindingKeys.ContainsKey("$phase|$subject")}}
            $valid=$valid -and $bindingKeys.ContainsKey('27|0')
            if($PlayerStateAudienceLifetime){
                $lifetime=@(Import-Csv -LiteralPath "$runRoot/Client$client/player-state-lifetime.csv")
                $valid=$valid -and $lifetime.Count -eq $expectedSummary
                $lifetimeKeys=@{};$ownWeaponIds=@()
                foreach($entry in $lifetime){
                    $phase=[int]$entry.phase
                    $valid=$valid -and !$lifetimeKeys.ContainsKey($phase) -and $phase -in $summary.phase -and [int]$entry.observer_slot -eq $slot -and [int]$entry.retired_pawns -eq $(if($phase -eq 30 -or ($PlayerStateAudienceOwnerView -and $phase -eq 29 -and $slot -eq 1)){1}else{0}) -and $entry.retired_cleared -ceq '1' -and $entry.own_linked -ceq '1' -and $entry.late_ps_probe -ceq $(if($phase -eq 30){'1'}else{'0'}) -and $entry.late_ps_invariant -ceq $(if($phase -eq 30){'1'}else{'0'}) -and $entry.passed -ceq '1'
                    $ownEquipped=(Test-AudienceAfterReplacement $phase) -and $slot -eq 1
                    $remotePublic=if(($phase -eq 30) -and $slot -eq 0){1}else{0}
                    $valid=$valid -and $entry.own_bag -ceq '0' -and [int]$entry.own_private -eq [int]$ownEquipped -and $entry.own_public -ceq '0' -and [int]$entry.remote_public -eq $remotePublic
                    if($ownEquipped){$valid=$valid -and $entry.weapon_id -match '^[0-9a-fA-F]{32}$';$ownWeaponIds+=$entry.weapon_id}else{$valid=$valid -and !$entry.weapon_id}
                    $lifetimeKeys[$phase]=$true
                }
                $authorityLifetime=@(Import-Csv -LiteralPath "$runRoot/Server/player-state-lifetime.csv")
                $valid=$valid -and $authorityLifetime.Count -eq 23
                $lifetimeKeys=@{};$oldIds=@();$newIds=@();$peerStates=@();$authorityWeaponIds=@()
                foreach($entry in $authorityLifetime){
                    $phase=[int]$entry.phase;$subject=[int]$entry.subject_slot;$key="$phase|$subject"
                    $after=Test-AudienceAfterReplacement $phase
                    $destroyed=$phase -in @(31,27)
                    $wantsFallback=$after -and $subject -eq 1
                    $valid=$valid -and !$lifetimeKeys.ContainsKey($key) -and $bindingKeys.ContainsKey($key) -and [uint64]$entry.current_net_id -gt 0 -and [uint64]$entry.state_net_id -gt 0 -and [int]$entry.retired_fallback -eq [int]$wantsFallback -and $entry.reason -ceq $(if($wantsFallback){'RetiredSource'}else{'None'}) -and $entry.old_cleared -ceq '1' -and $entry.passed -ceq '1'
                    if($after){
                        $valid=$valid -and [uint64]$entry.old_net_id -gt 0 -and [int]$entry.old_alive -eq [int](!$destroyed) -and [int]$entry.old_registered -eq [int](!$destroyed)
                        $oldIds+=$entry.old_net_id
                    }else{$valid=$valid -and $entry.old_net_id -ceq '0' -and $entry.old_alive -ceq '0' -and $entry.old_registered -ceq '0'}
                    $valid=$valid -and $entry.bag_count -ceq '0' -and [int]$entry.private_equipment -eq [int]$wantsFallback -and [int]$entry.public_equipment -eq [int]$wantsFallback
                    if($wantsFallback){$valid=$valid -and $entry.weapon_id -match '^[0-9a-fA-F]{32}$';$authorityWeaponIds+=$entry.weapon_id}else{$valid=$valid -and !$entry.weapon_id}
                    if($subject -eq 1){$peerStates+=$entry.state_net_id;if($after){$newIds+=$entry.current_net_id}}
                    $lifetimeKeys[$key]=$true
                }
                $valid=$valid -and @($oldIds|Sort-Object -Unique).Count -eq 1 -and @($newIds|Sort-Object -Unique).Count -eq 1 -and @($peerStates|Sort-Object -Unique).Count -eq 1 -and $oldIds[0] -cne $newIds[0]
                $valid=$valid -and @($authorityWeaponIds|Sort-Object -Unique).Count -eq 1
                if($slot -eq 1){$valid=$valid -and @($ownWeaponIds|Sort-Object -Unique).Count -eq 1 -and $ownWeaponIds[0] -ceq $authorityWeaponIds[0]}
                $peerBefore=@($authorityLifetime|Where-Object {$_.phase -ceq '28' -and $_.subject_slot -ceq '1'})
                $valid=$valid -and $peerBefore.Count -eq 1 -and $peerBefore[0].current_net_id -ceq $oldIds[0]
                if($PlayerStateAudienceOwnerView){
                    $ownerViewClient=@(Import-Csv -LiteralPath "$runRoot/Client$client/player-state-owner-view.csv")
                    $ownerViewAuthority=@(Import-Csv -LiteralPath "$runRoot/Server/player-state-owner-view.csv")
                    $valid=$valid -and $ownerViewClient.Count -eq 2 -and $ownerViewAuthority.Count -eq 2
                    foreach($source in @(@{rows=$ownerViewClient;observer=$slot},@{rows=$ownerViewAuthority;observer=-1})){
                     $ownerViewKeys=@{}
                     foreach($record in $source.rows){
                        $authorityRecord=$source.observer -eq -1
                        $subject=if($authorityRecord){1}else{$slot};$phase=[int]$record.phase;$detached=$phase -eq 29 -and $subject -eq 1
                        $duration=[double]$record.stable_seconds;$distance=[double]$record.cache_distance_uu;$radius=[double]$record.cull_radius_uu
                        $canonical=@($authorityLifetime|Where-Object{[int]$_.phase -eq $phase -and [int]$_.subject_slot -eq $subject})
                        $valid=$valid -and !$ownerViewKeys.ContainsKey($phase) -and $phase -in @(29,31) -and [int]$record.observer_slot -eq $source.observer -and [int]$record.subject_slot -eq $subject -and $record.passed -ceq '1' -and $record.controller_matches -ceq '1' -and [int]$record.pawn_role -eq $(if($authorityRecord){3}else{2})
                        $valid=$valid -and $canonical.Count -eq 1 -and $record.pc_pawn_id -ceq $canonical[0].current_net_id -and $record.state_pawn_id -ceq $record.pc_pawn_id -and $record.asc_avatar_id -ceq $record.pc_pawn_id
                        $valid=$valid -and ![double]::IsNaN($duration) -and ![double]::IsInfinity($duration) -and $duration -ge $(if($detached){8}else{1}) -and ![double]::IsNaN($distance) -and ![double]::IsInfinity($distance) -and ![double]::IsNaN($radius) -and ![double]::IsInfinity($radius) -and $radius -gt 0 -and [int]$record.detached -eq [int]$detached -and [int]$record.retired_expected -eq [int]$detached
                        if($detached){$valid=$valid -and $distance -gt 2*$radius -and $record.retired_id -ceq $oldIds[0] -and $record.camera_target_id -ceq $record.retired_id -and $record.pc_pawn_id -cne $record.retired_id}
                        else{$valid=$valid -and $distance -lt $radius -and $record.retired_id -ceq '0' -and $record.camera_target_id -ceq $record.pc_pawn_id}
                        $ownerViewKeys[$phase]=$true
                     }
                     $valid=$valid -and $ownerViewKeys.ContainsKey(29) -and $ownerViewKeys.ContainsKey(31)
                    }
                }
            }
        }
        $partyValidation+=[ordered]@{client=$client;observerSlot=$slot;passed=$valid;summaryRows=$summary.Count;memberRows=$rows.Count;authorityRows=$expectedData.Count;scope=$(if($PlayerStateAudienceOwnerView){'Deliberate detached-camera owner-Pawn scope contract: phase29 slot1 client and authority stored cache exceed2xcull for8seconds while own current Pawn/State/ASC/controller/role remain valid; phase31 current-Pawn view restored before old destruction. Existing privacy/gear/RetiredSource assertions retained. Not LifetimeLoss1 reproduction or a production fix.'}elseif($PlayerStateAudienceLifetime){'Actual retained-old Avatar replacement, persistent State/GAS/private owner coherence, near/far old Pawn scope, explicit RetiredSource broad fallback preserved after old destruction. One explicit late old Pawn PS callback injection on each actual client at near phase30 must preserve State.Pawn/current ASC tuple/revision and reject old ASC/attributes/private manager. Callback probe, not packet-reordering evidence. Containment remains; no retirement ACK/mount/CCU/backend claim.'}elseif($PlayerStateAudience){'Actual assigned spatial PS audience; far/near/far PS-Pawn-ASC linkage, latest public health, private data and owner roster/native HUD, disconnect. No mount/replacement/loss/CCU or backend claim.'}else{'Actual owner FastArray/private GUID values, native local-screen row counts, no foreign PS roster, no foreign Pawn at authored CDO cull, join/health/leave/rejoin/offline8/disconnect. No global presence/backend/capacity claim.'})}
    }
    if(@($observers|Sort-Object -Unique).Count -ne 2){foreach($row in $partyValidation){$row.passed=$false}}
    if($PartyUI){
        $png="$runRoot/Client1/party-roster.png";$valid=Test-Path -LiteralPath $png -PathType Leaf
        $width=0;$height=0;$hash=$null
        if($valid){
            $stream=[IO.File]::OpenRead($png)
            try{$header=[byte[]]::new(24);$read=$stream.Read($header,0,24)}finally{$stream.Dispose()}
            $valid=$read -eq 24 -and (($header[0..7]-join ',') -ceq '137,80,78,71,13,10,26,10')
            if($valid){$width=[int]$header[16]*16777216+[int]$header[17]*65536+[int]$header[18]*256+[int]$header[19];$height=[int]$header[20]*16777216+[int]$header[21]*65536+[int]$header[22]*256+[int]$header[23]}
            $hash=(Get-FileHash -LiteralPath $png -Algorithm SHA256).Hash.ToLowerInvariant()
            $valid=$valid -and $width -eq 960 -and $height -eq 540 -and (Get-Item -LiteralPath $png).Length -gt 1024
        }
        $partyScreenshotValidation+=[ordered]@{passed=$valid;file=$png;width=$width;height=$height;sha256=$hash;scope='One requested rendered960x540 actual eight-row HUD frame. Pixel content still requires root visual review; no resolution/performance matrix.'}
    }
}
$equipmentInterestValidation=@()
if($EquipmentInterest){
    $observers=@()
    foreach($client in 1..2){
        $path="$runRoot/Client$client/equipment-interest.csv"
        $rows=@(); if(Test-Path -LiteralPath $path -PathType Leaf){$rows=@(Import-Csv -LiteralPath $path)}
        $phases=@(16,17,18,19,28,29)
        $valid=$rows.Count -eq $phases.Count
        $slot=if($rows.Count){[string]$rows[0].observer_slot}else{''}
        $valid=$valid -and $slot -cmatch '^[01]$'
        $observers+=$slot
        foreach($phase in $phases){
            $match=@($rows|Where-Object{$_.phase -ceq [string]$phase})
            if($match.Count -ne 1){$valid=$false;continue}
            $row=$match[0];$near=$phase -in @(16,19,28)
            $present=$phase -in @(16,17,28,29) -or ($Quests -and $phase -eq 19)
            $expected=[int]$present;$remote=if($near){$expected}else{0}
            $valid=$valid -and $row.observer_slot -ceq $slot -and $row.owner_private -ceq [string]$expected -and $row.owner_public -ceq '0' -and $row.foreign_private -ceq '0' -and $row.remote_pawns -ceq [string][int]$near -and $row.remote_rows -ceq [string]$remote -and $row.remote_presented -ceq [string]$remote -and $row.manager_public -ceq [string]$remote -and $row.remote_visuals -ceq [string]$remote -and $row.passed -ceq '1'
        }
        $equipmentInterestValidation+=[ordered]@{client=$client;observerSlot=$slot;passed=$valid;rows=$rows.Count;scope='Actual owner private/SkipOwner transport, authored finite Pawn cull, no distant public payload, unseen unequip, current re-entry snapshot, real Avatar replacement and existing weapon actor. No polling CPU, per-property bytes, population/FPS or production OSS claim.'}
    }
    if(@($observers|Sort-Object -Unique).Count -ne 2){foreach($row in $equipmentInterestValidation){$row.passed=$false}}
}
# BEGIN actual owner commands: existing EquipmentInterest + Quests only.
$ownerCommandValidation=@()
if($EquipmentInterest -and $Quests){
    $authorityPath="$runRoot/Server/owner-commands.csv"
    $authority=if(Test-Path -LiteralPath $authorityPath -PathType Leaf){@(Import-Csv -LiteralPath $authorityPath)}else{@()}
    $authorityByKey=@{};$authorityValid=$authority.Count -eq 6
    foreach($row in $authority){
        $key="$($row.observer_slot)|$($row.phase)"
        if($authorityByKey.ContainsKey($key)){$authorityValid=$false}
        $authorityByKey[$key]=$row
        $authorityValid=$authorityValid -and $row.observer_slot -in @('0','1') -and $row.phase -in @('16','18','19') -and $row.response -ceq 'state' -and $row.passed -ceq '1'
    }
    $observers=@();$characters=@();$instances=@()
    foreach($client in 1..2){
        $path="$runRoot/Client$client/owner-commands.csv"
        $rows=if(Test-Path -LiteralPath $path -PathType Leaf){@(Import-Csv -LiteralPath $path)}else{@()}
        $valid=$authorityValid -and $rows.Count -eq 3
        $slots=@($rows.observer_slot|Sort-Object -Unique);$valid=$valid -and $slots.Count -eq 1 -and $slots[0] -in @('0','1')
        $slot=if($slots.Count -eq 1){$slots[0]}else{''};$observers+=$slot
        $first=$null
        foreach($phase in @(16,18,19)){
            $match=@($rows|Where-Object{$_.phase -ceq [string]$phase})
            if($match.Count -ne 1){$valid=$false;continue}
            $row=$match[0];$a=$authorityByKey["$slot|$phase"]
            $response=if($phase -eq 16){'BagChanged'}else{'Confirmed'}
            $valid=$valid -and $null -ne $a -and $row.response -ceq $response -and $row.passed -ceq '1' -and $row.foreign_bag -ceq '0' -and $row.foreign_quests -ceq '0'
            $valid=$valid -and $row.equipped -ceq [string][int]($phase -ne 18) -and $row.tracked -ceq [string][int]($phase -eq 19)
            foreach($field in @('character_id','source_id','target_id','weapon_id')){$valid=$valid -and $row.$field -cmatch '^[0-9a-fA-F]{32}$' -and $row.$field -cne ('0'*32)}
            $valid=$valid -and @($row.source_id,$row.target_id,$row.weapon_id|Sort-Object -Unique).Count -eq 3
            foreach($field in @('source_order_before','target_order_before','source_order_after','target_order_after')){$valid=$valid -and $row.$field -cmatch '^[0-9]+$'}
            $valid=$valid -and $row.source_order_before -cne $row.target_order_before -and $row.source_order_after -ceq $row.target_order_before -and $row.target_order_after -ceq $row.source_order_before
            if($a){foreach($field in @('character_id','source_id','target_id','weapon_id','source_order_before','target_order_before','source_order_after','target_order_after','equipped','tracked')){$valid=$valid -and $row.$field -ceq $a.$field}}
            if($first){foreach($field in @('character_id','source_id','target_id','weapon_id','source_order_before','target_order_before')){$valid=$valid -and $row.$field -ceq $first.$field}}else{$first=$row}
        }
        if($first){$characters+=$first.character_id;$instances+=@($first.source_id,$first.target_id,$first.weapon_id)}
        $ownerCommandValidation+=[ordered]@{client=$client;observerSlot=$slot;passed=$valid;rows=$rows.Count;authorityRows=$authority.Count;scope='Actual normal owner VM bag Swap, Unequip/Equip correlated success replies, Quest RequestTracking replicated transitions and matching authority snapshots. Existing prototype in-memory provider only; no durable commit, Use/Claim, old-PS ownership transfer or reconnect claim.'}
    }
    if(@($observers|Sort-Object -Unique).Count -ne 2 -or @($characters|Sort-Object -Unique).Count -ne 2 -or @($instances|Sort-Object -Unique).Count -ne 6){foreach($row in $ownerCommandValidation){$row.passed=$false}}
}
# END actual owner commands.
$cleaveValidation=@()
if($Cleave -and !$CleaveFanout){
    $authority=@(Import-Csv -LiteralPath "$runRoot/Server/cleave-authority.csv")
    $byKey=@{};$valid=$authority.Count -eq 9;$ids=@{};$casters=@()
    foreach($row in $authority){
        $key="$($row.phase)|$($row.subject_slot)"
        $valid=$valid -and !$byKey.ContainsKey($key) -and $row.passed -ceq '1' -and $row.actor_id -cmatch '^[1-9][0-9]*$' -and $row.caster_id -cmatch '^[1-9][0-9]*$'
        $byKey[$key]=$row;$casters+=$row.caster_id
        if($ids.ContainsKey($row.subject_slot)){$valid=$valid -and $ids[$row.subject_slot] -ceq $row.actor_id}else{$ids[$row.subject_slot]=$row.actor_id}
    }
    $valid=$valid -and $ids.Count -eq 3 -and @($ids.Values|Sort-Object -Unique).Count -eq 3 -and @($casters|Sort-Object -Unique).Count -eq 1
    foreach($phase in 30..32){
        $effects=0;$contacts=0;$damaged=0
        foreach($subject in 0..2){
            $r=$byKey["$phase|$subject"]
            if(!$r){$valid=$false;continue}
            $health=[double]$r.health
            $valid=$valid -and ![double]::IsNaN($health) -and ![double]::IsInfinity($health) -and $health -gt 0 -and $health -le 1000
            $effects+=[int]$r.effects;$contacts+=[int]$r.contact;$damaged+=[int]($health -lt 1000)
            $valid=$valid -and [int]$r.effects -in @(0,1) -and [int]$r.contact -in @(0,1) -and [int]$r.attack_completed -eq [int]($phase -ge 31) -and [int]$r.cancelled -eq [int]($phase -eq 32)
            if($phase -eq 32){$valid=$valid -and [Math]::Abs($health-[double]$byKey["31|$subject"].health) -lt .001}
        }
        $valid=$valid -and $effects -eq $(if($phase -eq 31){2}else{0}) -and $contacts -eq $(if($phase -eq 31){3}else{0}) -and $damaged -eq $(if($phase -eq 30){0}else{2})
    }
    $slots=@()
    foreach($client in 1..2){
        $rows=@(Import-Csv -LiteralPath "$runRoot/Client$client/cleave-observations.csv");$seen=@{};$clientValid=$valid -and $rows.Count -eq 9
        $slot=@($rows.observer_slot|Sort-Object -Unique);$clientValid=$clientValid -and $slot.Count -eq 1 -and $slot[0] -in @('0','1');$slots+=$slot
        foreach($r in $rows){
            $key="$($r.phase)|$($r.subject_slot)";$a=$byKey[$key]
            $clientValid=$clientValid -and !$seen.ContainsKey($key) -and $null -ne $a -and $r.passed -ceq '1' -and $r.actor_id -ceq $a.actor_id -and $r.caster_id -ceq $a.caster_id -and [Math]::Abs([double]$r.health-[double]$a.health) -lt .001
            $seen[$key]=$true
        }
        $cleaveValidation+=[ordered]@{client=$client;passed=$clientValid;rows=$rows.Count;authorityRows=$authority.Count;scope='One actual authored NPC, three real notify contacts, bounded two GE recipients once, normal natural end and pre-impact cancellation, actual replicated public Health and existing owner privacy. No Nav, population, CCU or performance claim.'}
    }
    if(@($slots|Sort-Object -Unique).Count -ne 2){foreach($row in $cleaveValidation){$row.passed=$false}}
}
$renderedValidation=$null
if($RenderedCrowd){
    $renderedValid=$true
    $renderedRoot="$runRoot/Client1/Rendered"
    $preparation=$null;$frames=@();$work=@()
    if(Test-Path -LiteralPath "$renderedRoot/network-preparation.json"){$preparation=Get-Content -LiteralPath "$renderedRoot/network-preparation.json" -Raw|ConvertFrom-Json}else{$renderedValid=$false}
    if(Test-Path -LiteralPath "$renderedRoot/frames.csv"){$frames=@(Import-Csv -LiteralPath "$renderedRoot/frames.csv")}else{$renderedValid=$false}
    if(Test-Path -LiteralPath "$renderedRoot/workload.csv"){$work=@(Import-Csv -LiteralPath "$renderedRoot/workload.csv")}else{$renderedValid=$false}
    $renderedResult=if(Test-Path -LiteralPath "$renderedRoot/result.txt"){Get-Content -LiteralPath "$renderedRoot/result.txt" -Raw}else{''}
    $renderedValid=$renderedValid -and $preparation.actors -eq 200 -and $preparation.autonomous -eq 1 -and $preparation.simulated -eq 199 -and $preparation.authority -eq 0 -and $preparation.sample_before_ready -eq $false -and $preparation.flying_reuse_level -eq $RenderedFlyingReuseLevel -and $renderedResult -match '(?m)^success=1$' -and $renderedResult -match '(?m)^local_quality_samples=[1-9][0-9]*$'
    $renderedValid=$renderedValid -and $work.Count -eq 200 -and @($work|Where-Object{[double]$_.travel_cm -le 100 -or [int]$_.reversals -le 0 -or [int]$_.landings -le 0}).Count -eq 0
    $measured=@($frames|Where-Object{[int]$_.phase -ge 2 -and [double]$_.phase_game_s -ge 2 -and [double]$_.phase_game_s -le 7 -and $_.capture_requested -ceq '0'})
    foreach($phase in 2..6){$renderedValid=$renderedValid -and @($measured|Where-Object{[int]$_.phase -eq $phase}).Count -gt 0}
    $renderedValid=$renderedValid -and @($measured|Where-Object{$_.network_observer -cne '1' -or $_.actors -cne '200' -or $_.autonomous_actors -cne '1' -or $_.simulated_actors -cne '199' -or $_.authority_actors -cne '0' -or $_.local_actors -cne '1' -or $_.local_body_lod -cne '0' -or $_.local_max_render_lod -cne '0' -or $_.local_cloth_reduced_parts -cne '0' -or $_.weapon_meshes -cne '200' -or $_.weapon_visible -cne '200'}).Count -eq 0
    $renderedValid=$renderedValid -and @($measured|Where-Object{[int]$_.phase -eq 5 -and [int]$_.montages -eq 200 -and [int]$_.source_gameplay_pose_demands -eq 200}).Count -gt 0
    $authorityPath="$runRoot/Server/rendered-roster.csv"
    $authority=if(Test-Path -LiteralPath $authorityPath){@(Import-Csv -LiteralPath $authorityPath)}else{@()}
    $byId=@{};foreach($a in $authority){if(!$a.actor_id -or $a.actor_id -ceq '0' -or $byId.ContainsKey($a.actor_id)){$renderedValid=$false};$byId[$a.actor_id]=$a}
    $renderedValid=$renderedValid -and $authority.Count -eq 200 -and @($authority|Where-Object{$_.role -cne '3' -or $_.passed -cne '1'}).Count -eq 0
    foreach($client in 1..2){
        $path="$runRoot/Client$client/rendered-roster.csv"
        $clientRows=if(Test-Path -LiteralPath $path){@(Import-Csv -LiteralPath $path)}else{@()}
        $seen=@{}
        foreach($r in $clientRows){$a=$byId[$r.actor_id];$renderedValid=$renderedValid -and !$seen.ContainsKey($r.actor_id) -and $null -ne $a -and $r.class_id -ceq $a.class_id -and $r.passed -ceq '1';$seen[$r.actor_id]=$true}
        $renderedValid=$renderedValid -and $clientRows.Count -eq 200 -and @($clientRows|Where-Object{$_.role -ceq '2'}).Count -eq 1 -and @($clientRows|Where-Object{$_.role -ceq '1'}).Count -eq 199 -and @($clientRows|Where-Object{$_.local -ceq '1'}).Count -eq 1
    }
    $decodedEndpointValidation=$null
    if($RenderedEndpoints){
        $endpointCaptures=@(Get-ChildItem -LiteralPath "$runRoot/Client1User/Saved/Validation/MoverBuffers" -Filter '*.csv' -ErrorAction SilentlyContinue|Where-Object Name -notlike '*.actors.csv')
        $selectionPath="$runRoot/Client1/Rendered/decoded-endpoint-selection.json"
        $endpointValid=$false
        if($endpointCaptures.Count -eq 1 -and (Test-Path -LiteralPath $selectionPath)){
            & python "$PSScriptRoot/Validate-MoverBufferCapture.py" $endpointCaptures[0].FullName --decoded-endpoints --selection $selectionPath --output "$runRoot/Client1/Rendered/decoded-endpoint-observation.json"
            $endpointValid=$LASTEXITCODE -eq 0
        }
        $decodedEndpointValidation=[ordered]@{observationValid=$endpointValid;captures=$endpointCaptures.Count;scope='At most3 actual SPs selected after phase3 and observed through phase4; decoded stored-basis floatXY/velocityXY versus consumed/presentation/Actor values. Serial gaps are missing decode evidence, not packets. No workload pass override.'}
        # Same isolated local run, no new RPC: the server reads the phase3
        # selection at phase4 start/end and uses the actual observer connection.
        # Diagnostic completeness never relaxes the existing gameplay verdict.
        $irisProbeValid=$true;$irisProbes=@();$irisProbeReasons=@();$selectedIds=@();$selection=$null
        try {
            if (!(Test-Path -LiteralPath $selectionPath -PathType Leaf) -or (Get-Item -LiteralPath $selectionPath).Length -gt 4096) { throw 'Selection missing or exceeds4KiB.' }
            $selection=Get-Content -LiteralPath $selectionPath -Raw|ConvertFrom-Json
            $selectedIds=@($selection.selected|ForEach-Object{[string]$_.actor_id})
            $observerId=[string]$selection.observer_pawn_actor_id
            $ownerRoster=@(Import-Csv -LiteralPath "$runRoot/Client1/rendered-roster.csv"|Where-Object{$_.local -ceq '1' -and $_.role -ceq '2'})
            if($selectedIds.Count -lt 1 -or $selectedIds.Count -gt 3 -or @($selectedIds|Sort-Object -Unique).Count -ne $selectedIds.Count -or $ownerRoster.Count -ne 1 -or $observerId -cne $ownerRoster[0].actor_id){throw 'Observer or selected identity contract failed.'}
            foreach($id in @($observerId)+$selectedIds){
                [UInt64]$numericId=0
                if($id -cnotmatch '^[0-9]{1,20}$' -or ![UInt64]::TryParse($id,[ref]$numericId) -or $numericId -eq 0 -or !$byId.ContainsKey($id)){throw 'A selected/observer identity is not in the actual authority roster.'}
            }
            $expectedHash=(Get-FileHash -LiteralPath $selectionPath -Algorithm SHA1).Hash
            foreach($boundary in @('start','end')){
                $probePath="$runRoot/Server/iris-selected-phase4-$boundary.json"
                if(!(Test-Path -LiteralPath $probePath -PathType Leaf) -or (Get-Item -LiteralPath $probePath).Length -gt 4096){throw "Probe $boundary missing or exceeds4KiB."}
                $probe=Get-Content -LiteralPath $probePath -Raw|ConvertFrom-Json
                $probePassed=$probe.complete -eq $true -and $probe.boundary -ceq $boundary -and $probe.phase -eq 4 -and $probe.additional_rpc -eq $false -and $probe.protocol_state_dumped -eq $false
                $probePassed=$probePassed -and $probe.selection_sha1 -ieq $expectedHash -and $probe.observer_pawn_actor_id -ceq $observerId -and $probe.observer_slot -eq [int]$byId[$observerId].actor_index
                $probePassed=$probePassed -and $probe.connection_id -gt 0 -and $probe.replication_system_id -ge 0 -and $probe.objects_printed -eq $selectedIds.Count -and (@($probe.selected_actor_ids) -join ',') -ceq ($selectedIds -join ',')
                $probePassed=$probePassed -and $probe.selection_max_bytes -eq 4096 -and $probe.selection_bytes -gt 0 -and $probe.selection_bytes -le 4096 -and $probe.current_net_speed_bytes_per_second -gt 0
                $probePassed=$probePassed -and $probe.tracked_frames -ge 0 -and $probe.saturated_frames -ge 0 -and $probe.saturated_frames -le $probe.tracked_frames -and $probe.longest_saturated_frame_run -le $probe.saturated_frames
                foreach($id in $selectedIds){
                    $marker="RENDERED_IRIS_OBJECT boundary=$boundary actor_id=$id connection=$($probe.connection_id) system=$($probe.replication_system_id)"
                    $probePassed=$probePassed -and @(Select-String -LiteralPath "$runRoot/Server.log" -SimpleMatch $marker).Count -eq 1
                }
                $irisProbes+=[ordered]@{boundary=$boundary;observationValid=$probePassed;receipt=$probePath;sha256=(Get-FileHash -LiteralPath $probePath -Algorithm SHA256).Hash;data=$probe}
                $irisProbeValid=$irisProbeValid -and $probePassed
            }
            if($irisProbes.Count -ne 2){throw 'Both phase4 probe boundaries are required.'}
            $first=$irisProbes[0].data;$last=$irisProbes[1].data
            $irisProbeValid=$irisProbeValid -and $first.connection_id -eq $last.connection_id -and $first.replication_system_id -eq $last.replication_system_id -and $first.cohort_revision -ceq $last.cohort_revision
            $irisProbeValid=$irisProbeValid -and $last.sample_qpc_seconds -gt $first.sample_qpc_seconds -and $last.sample_world_seconds -ge $first.sample_world_seconds -and $last.tracked_frames -ge $first.tracked_frames -and $last.saturated_frames -ge $first.saturated_frames
        } catch {$irisProbeValid=$false;$irisProbeReasons+=$_.Exception.Message}
        $irisProbeSummaries=@($irisProbes|ForEach-Object{[ordered]@{boundary=$_.boundary;observationValid=$_.observationValid;receipt=$_.receipt;sha256=$_.sha256}})
        $irisProbeValidation=[ordered]@{observationValid=$irisProbeValid;boundaries=$irisProbeSummaries;reasons=$irisProbeReasons;scope='Read-only current selected roots and actual parent connection at phase4 start/end; existing Iris Writer/priority/creation/baseline/huge-object log with no protocol dump, plus cumulative connection saturation/bytes/queue samples. No RPC, scheduling policy change, packet-delivery attribution or gameplay pass override.'}
        $irisProbeValidation|ConvertTo-Json -Depth 7|Set-Content -LiteralPath "$runRoot/Client1/Rendered/iris-selected-observation.json" -Encoding utf8
        $decodedEndpointValidation.irisScheduling=$irisProbeValidation
        $renderedValid=$renderedValid -and $endpointValid -and $irisProbeValid
    }
    $renderedValidation=[ordered]@{passed=$renderedValid;actors=200;connections=2;frames=$frames.Count;measuredFrames=$measured.Count;movementRows=$work.Count;preparation=$preparation;decodedEndpoints=$decodedEndpointValidation;scope='Client1 real RHI/one autonomous and199 simulated proxies; two socket owners plus198 AI-owned authority Avatars. Cold per-instance replication/preparation after warm content family. No200-connection/CCU or standalone timing-equivalence claim.'}
}
$fanoutValidation=$null
if($CleaveFanout){
    $summary=@(Import-Csv -LiteralPath "$runRoot/Server/combat-fanout-summary.csv")
    $targets=@(Import-Csv -LiteralPath "$runRoot/Server/combat-fanout-targets.csv")
    $valid=$summary.Count -eq 1 -and $targets.Count -eq 192
    if($summary.Count -eq 1){
        $s=$summary[0]
        $valid=$valid -and $s.scope -ceq 'authority_actual_combat_not_client_damage' -and [int]$s.groups -eq 32 -and [int]$s.natural_started -eq 32 -and [int]$s.natural_ended -eq 32 -and [int]$s.natural_contacts -eq 96 -and [int]$s.natural_effects -eq 64 -and [int]$s.natural_health_losses -eq 64 -and [int]$s.cancel_started -eq 32 -and [int]$s.cancel_ended -eq 32 -and [int]$s.cancel_contacts -eq 0 -and [int]$s.cancel_effects -eq 0 -and [int]$s.peak_montages -ge 2 -and [int]$s.peak_attack_contexts -ge 2 -and [int]$s.peak_concurrent -ge 2 -and [int]$s.cleanup -eq 1 -and [int]$s.trace_started -eq 1 -and [int]$s.trace_stopped -eq 1 -and [int]$s.passed -eq 1
    }
    $seen=@{};$casterIds=@{};$actorIds=@{};$byKey=@{}
    foreach($r in $targets){
        $key="$($r.phase)|$($r.group)|$($r.subject)";$identity="$($r.group)|$($r.subject)"
        $valid=$valid -and !$seen.ContainsKey($key) -and [int]$r.phase -in 51,52 -and [int]$r.group -in 0..31 -and [int]$r.subject -in 0..2 -and $r.scope -ceq 'authority_actual_combat_not_client_damage' -and [int]$r.started -eq 1 -and [int]$r.ended -eq 1 -and [int]$r.passed -eq 1 -and $r.actor_id -cmatch '^[1-9][0-9]*$' -and $r.caster_id -cmatch '^[1-9][0-9]*$'
        $seen[$key]=$true;$byKey[$key]=$r
        if($actorIds.ContainsKey($identity)){$valid=$valid -and $actorIds[$identity] -ceq $r.actor_id}else{$actorIds[$identity]=$r.actor_id}
        if($casterIds.ContainsKey($r.group)){$valid=$valid -and $casterIds[$r.group] -ceq $r.caster_id}else{$casterIds[$r.group]=$r.caster_id}
        $before=[double]$r.before_health;$health=[double]$r.health
        $valid=$valid -and ![double]::IsNaN($before) -and ![double]::IsInfinity($before) -and ![double]::IsNaN($health) -and ![double]::IsInfinity($health) -and $health -gt 0 -and $health -le 1000 -and [int]$r.effects -in 0,1
        if([int]$r.phase -eq 51){$valid=$valid -and $before -eq 1000 -and [int]$r.contact -eq 1 -and [int]$r.cancelled -eq 0 -and (([int]$r.effects -eq 1) -eq ($health -lt $before))}
        else{$valid=$valid -and [int]$r.contact -eq 0 -and [int]$r.effects -eq 0 -and [int]$r.cancelled -eq 1 -and [Math]::Abs($health-$before) -lt .001}
    }
    foreach($g in 0..31){foreach($phase in 51,52){
        $groupRows=@($targets|Where-Object {[int]$_.group -eq $g -and [int]$_.phase -eq $phase})
        $valid=$valid -and $groupRows.Count -eq 3 -and ($groupRows|Measure-Object -Property effects -Sum).Sum -eq $(if($phase -eq 51){2}else{0})
        foreach($slot in 0..2){$a=$byKey["51|$g|$slot"];$b=$byKey["52|$g|$slot"];$valid=$valid -and $null -ne $a -and $null -ne $b -and [Math]::Abs([double]$b.before_health-[double]$a.health) -lt .001}
    }}
    $valid=$valid -and $actorIds.Count -eq 96 -and @($actorIds.Values|Sort-Object -Unique).Count -eq 96 -and $casterIds.Count -eq 32 -and @($casterIds.Values|Sort-Object -Unique).Count -eq 32 -and @($casterIds.Values|Where-Object {$_ -in $actorIds.Values}).Count -eq 0
    $traceFile=Get-Item -LiteralPath "$runRoot/Server/combat-fanout.utrace" -ErrorAction Stop
    $valid=$valid -and $traceFile.Length -gt 0
    foreach($role in 'Server','Client1','Client2'){
        $argv=@(Get-Content -LiteralPath "$runRoot/$role-effective-arguments.json" -Raw|ConvertFrom-Json)
        $valid=$valid -and @($argv|Where-Object {$_ -match '^-(trace=|tracefile=|NetTrace=)'}).Count -eq 0
    }
    $fanoutValidation=[ordered]@{passed=$valid;authorityRows=$targets.Count;summary=$summary;traceBytes=$traceFile.Length;scope='Authority actual32-caster cohort, observed peak only; matching per-group contacts/GE/Health and cancellation. Clients assert prior AOI stages only; late server-only CPU trace.'}
}
$passed = $serverModeVerified -and $excludedLocalHosts -eq $expectedExcludedLocalHosts -and @($results | Where-Object { !$_.success }).Count -eq 0 -and @($exitCodes | Where-Object { $null -eq $_ -or $_ -ne 0 }).Count -eq 0 -and $timeouts.Count -eq 0
$canonicalMeleeValidation=$null
if($CanonicalMelee){
 $authorityPath="$runRoot/Server/canonical-melee-authority.json"
 $ownerPaths=@(Get-ChildItem -LiteralPath $runRoot -Recurse -File -Filter 'canonical-melee-owner.json')
 $valid=(Test-Path -LiteralPath $authorityPath -PathType Leaf) -and $ownerPaths.Count -eq 1
 $authority=$owner=$null;$duplicate=$false;$geometry=$null;$acceptedSubmission=$null;$tracePending=$true
 if($valid){
  $authority=Get-Content -LiteralPath $authorityPath -Raw|ConvertFrom-Json
  $owner=Get-Content -LiteralPath $ownerPaths[0].FullName -Raw|ConvertFrom-Json
  foreach($row in @($authority,$owner)){
   $requiresRawContact=!$CanonicalMeleeMovingRecipient -or [int]$row.role -ne 3
   $valid=$valid -and $row.schema -eq 1 -and $row.success -and !$row.failure -and $row.prediction_key -gt 0 -and $row.actual_handle -match '^[0-9]+$' -and $row.matching_grant_count -gt 0 -and $row.native_frame -ge 0 -and
    !!$row.trajectory_stamp -and $row.authored_keys -ge 2 -and !!$row.node_tag -and !!$row.attack_tag -and $row.native_active -and $row.montage -and $row.raw_contacts -ge 0 -and (!$requiresRawContact -or $row.raw_contacts -ge 1) -and $row.natural_ends -eq 1 -and $row.attack_finalizations -gt 0 -and $row.delegates_retired -and $row.cleanup_passed -and !$row.rpc_invocation_inferred_from_effect
   $valid=$valid -and $row.equipment -ceq '/Game/DataAssetSets/Animation_Profiles/Equip/DA_Spellblade_CanonicalEquip.DA_Spellblade_CanonicalEquip' -and
    $row.style -ceq '/Game/DataAssetSets/CombatStyle/DA_CombatStyle_Spellblade_Canonical.DA_CombatStyle_Spellblade_Canonical' -and
    $row.combo -ceq '/Game/DataAssetSets/Combo/DA_Combo_Spellblade_Canonical.DA_Combo_Spellblade_Canonical' -and
    $row.attack -ceq '/Game/DataAssetSets/AttackDefinition/Spellblade/DA_Attack_Spellblade_LMB1_Canonical.DA_Attack_Spellblade_LMB1_Canonical'
  }
  $valid=$valid -and $authority.role -eq 3 -and $owner.role -eq 2 -and $owner.locally_controlled -and
   $authority.actual_handle -ceq $owner.actual_handle -and $authority.prediction_key -eq $owner.prediction_key -and $authority.source_mesh -ceq $owner.source_mesh -and $authority.trajectory_stamp -ceq $owner.trajectory_stamp -and $authority.node_tag -ceq $owner.node_tag -and
   $authority.owned_sources_initially_absent -and $authority.owned_sources_retired -and @($authority.owned_grant_sources).Count -gt 0 -and
   $authority.effects -eq 1 -and $authority.health_after -lt $authority.health_before -and $authority.open_atomic_packet_observations -ge 1 -and $authority.max_packet_segments -ge 2
  $log=Get-Content -LiteralPath "$runRoot/Server.log" -Raw
  $observation=Get-ProjectJCanonicalSSRObservation -Log $log -Source $authority.source -Target $authority.target -PredictionKey $authority.prediction_key -MovingRecipient ([bool]$CanonicalMeleeMovingRecipient)
  $duplicate=$observation.duplicate;$geometry=$observation.geometry;$acceptedSubmission=$observation.acceptedSubmission;$tracePending=$observation.tracePending
 }
 $canonicalAssetsAfter=@($canonicalAssetFiles|ForEach-Object{[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $projectRoot $_) -Algorithm SHA256).Hash.ToLowerInvariant()}})
 $assetsUnchanged=($canonicalAssetsBefore|ConvertTo-Json -Compress) -ceq ($canonicalAssetsAfter|ConvertTo-Json -Compress)
 $canonicalAssetsAfter|ConvertTo-Json|Set-Content -LiteralPath "$runRoot/CanonicalAssetsAfter.json" -Encoding utf8
 $valid=$valid -and $assetsUnchanged
 $requiresHistoricalAcceptance=$CanonicalMeleeMovingRecipient -and $null -ne $authority -and [int]$authority.raw_contacts -eq 0
 if($requiresHistoricalAcceptance){
  $valid=$valid -and $authority.packet_observed_independently_of_contact -and $null -ne $acceptedSubmission
 }
 $recipientNavigation=$null
 if($CanonicalMeleeMovingRecipient){
  $navigationPassed=$false
  try{
   $recipientNavigation=Get-Content -LiteralPath "$runRoot/Server/canonical-recipient-navigation.json" -Raw|ConvertFrom-Json
   $navigationPassed=$recipientNavigation.schema -eq 1 -and $recipientNavigation.success -and $recipientNavigation.map -ceq '/Game/Tests/Combat/L_CanonicalNetwork' -and
    $recipientNavigation.target -ceq $authority.target -and $recipientNavigation.native_admitted -and $recipientNavigation.native_motion_observed -and
    $recipientNavigation.native_request_id -gt 0 -and $recipientNavigation.native_path_points -ge 2 -and $recipientNavigation.max_displacement_cm -gt 0 -and
    $recipientNavigation.max_physical_speed_cm_s -gt 0 -and $recipientNavigation.stopped_callbacks -eq 1 -and $recipientNavigation.cleanup_passed -and $recipientNavigation.owned_controller_retired
  }catch{$navigationPassed=$false}
  $valid=$valid -and $navigationPassed
 }
 $canonicalMeleeValidation=[ordered]@{schema=1;functionalPassed=$valid;assetsUnchanged=$assetsUnchanged;rpcInvocationVerified=($duplicate -or $null -ne $geometry -or $null -ne $acceptedSubmission);rpcDecision=if($null -ne $acceptedSubmission){'SpecSubmitted'}elseif($duplicate){'DuplicateTarget'}elseif($null -ne $geometry){'GeometryUnconfirmed'}else{'TraceRequired'};rpcGeometryRejectionObserved=($null -ne $geometry);rpcHistoryEvidence=$geometry;rpcAcceptanceClaimed=$false;traceAnalysisPending=$tracePending;complete=($valid -and $null -eq $acceptedSubmission -and !$requiresHistoricalAcceptance -and ($duplicate -or $null -ne $geometry));scope='Completion is this typed native invocation/outcome observation only, not all A09 or accepted SSR coverage. Covered finite history plus GeometryUnconfirmed proves a geometry rejection, never acceptance. GE may be independent authority damage. SpecSubmitted acceptance still requires Server.utrace SSRRequest/ApplyEffect nesting.';authority=$authority;owner=$owner}
 if($CanonicalMeleeMovingRecipient){
  $canonicalMeleeValidation['movingRecipientNavigation']=$recipientNavigation
  $canonicalMeleeValidation['historicalOnlyAuthorityContact']=$requiresHistoricalAcceptance
  $canonicalMeleeValidation['nativeAcceptedSubmissionObserved']=$null -ne $acceptedSubmission
  $canonicalMeleeValidation['nativeAcceptedSubmissionEvidence']=$acceptedSubmission
  $canonicalMeleeValidation['acceptedTraceNestingRequired']=$null -ne $acceptedSubmission
 }
 $canonicalMeleeValidation|ConvertTo-Json -Depth 7|Set-Content -LiteralPath "$runRoot/canonical-melee-verification.json" -Encoding utf8
 $passed=$passed -and $valid
}
$passed = $passed -and @($bufferValidations | Where-Object { !$_.passed }).Count -eq 0
$passed = $passed -and $authorityEncodeValid
if($RenderedCrowd){
    $renderedAfter=Get-ProjectJSourceProvenance -ProjectRoot $projectRoot -RuntimePaths @($exe)
    $renderedAfter|ConvertTo-Json -Depth 8|Set-Content -LiteralPath "$runRoot/RenderedAfterSourceProvenance.json" -Encoding utf8
    $renderedSourceUnchanged=$true
    try{Assert-ProjectJMatchingProvenance -Provenance @($provenance,$renderedAfter)}catch{$renderedSourceUnchanged=$false}
    $renderedAssetsAfter=@($renderedAssetFiles|ForEach-Object{Get-FileHash -LiteralPath (Join-Path $projectRoot $_) -Algorithm SHA256}|Select-Object Path,Hash)
    $renderedAssetsAfter|ConvertTo-Json|Set-Content -LiteralPath "$runRoot/RenderedAssetsAfter.json" -Encoding utf8
    $renderedAssetsUnchanged=($renderedAssetsBefore|ConvertTo-Json -Compress) -ceq ($renderedAssetsAfter|ConvertTo-Json -Compress)
    $renderedValidation.sourceUnchanged=$renderedSourceUnchanged;$renderedValidation.assetsUnchanged=$renderedAssetsUnchanged
    $renderedValidation.passed=$renderedValidation.passed -and $renderedSourceUnchanged -and $renderedAssetsUnchanged
    $passed=$passed -and $renderedValidation.passed
}
$passed = $passed -and (!$CleaveFanout -or $fanoutValidation.passed)
$passed = $passed -and @($cleaveValidation | Where-Object { !$_.passed }).Count -eq 0
$passed = $passed -and (!$CleaveThreat -or ($cleaveThreatValidation.Count -eq 2 -and @($cleaveThreatValidation | Where-Object { !$_.passed }).Count -eq 0))
$passed = $passed -and @($attributeAudienceValidation | Where-Object { !$_.passed }).Count -eq 0
$passed = $passed -and @($companionValidation | Where-Object { !$_.passed }).Count -eq 0
 $passed = $passed -and @($equipmentInterestValidation | Where-Object { !$_.passed }).Count -eq 0
$passed = $passed -and @($ownerCommandValidation | Where-Object { !$_.passed }).Count -eq 0
$passed = $passed -and @($partyValidation | Where-Object { !$_.passed }).Count -eq 0 -and @($partyScreenshotValidation | Where-Object { !$_.passed }).Count -eq 0
# Client CSV/log validation rejects every diagnostic after the explicit Stage 2
# ready boundary. Preserve startup diagnostics above; the authority has no
# interpolation warmup exemption and all its diagnostics remain failures.
$serverBufferDiagnostics=@()
if($MoverBuffers){$serverBufferDiagnostics=@(Select-String -LiteralPath "$runRoot/Server.log" -Pattern 'LogNetworkPrediction: Warning:|LogNetworkPrediction.*Interpolation out of sync|NetworkPrediction.*SystemFault')}
$passed = $passed -and $serverBufferDiagnostics.Count -eq 0
$cleaveSourceUnchanged=$true;$cleaveAssetsUnchanged=$true
if($Cleave){
    $cleaveAfter=Get-ProjectJSourceProvenance -ProjectRoot $projectRoot -RuntimePaths @($exe)
    $cleaveAfter|ConvertTo-Json -Depth 8|Set-Content -LiteralPath "$runRoot/CleaveAfterSourceProvenance.json" -Encoding utf8
    try{Assert-ProjectJMatchingProvenance -Provenance @($provenance,$cleaveAfter)}catch{$cleaveSourceUnchanged=$false}
    $cleaveAssetsAfter=@($cleaveAssetFiles|ForEach-Object {Get-FileHash -LiteralPath (Join-Path $projectRoot $_) -Algorithm SHA256}|Select-Object Path,Hash)
    $cleaveAssetsAfter|ConvertTo-Json|Set-Content -LiteralPath "$runRoot/CleaveAssetsAfter.json" -Encoding utf8
    $cleaveAssetsUnchanged=($cleaveAssetsBefore|ConvertTo-Json -Compress) -ceq ($cleaveAssetsAfter|ConvertTo-Json -Compress)
    $passed=$passed -and $cleaveSourceUnchanged -and $cleaveAssetsUnchanged
}
$craftingValidation=@()
if($CraftingJobs){
    foreach($role in @('Server','Client1','Client2')){
        $name=if($role -eq 'Server'){'crafting-authority.csv'}else{'crafting-client.csv'}
        $path=Join-Path "$runRoot/$role" $name
        $rows=@(if(Test-Path -LiteralPath $path){Import-Csv -LiteralPath $path})
        $valid=($rows.Count -eq $(if($role -eq 'Server'){8}else{4}))
        foreach($phase in 46..49){
            $phaseRows=@($rows|Where-Object {[int]$_.phase -eq $phase})
            $valid=$valid -and ($phaseRows.Count -eq $(if($role -eq 'Server'){2}else{1}))
        }
        foreach($row in $rows){
            $valid=$valid -and ([int]$row.passed -eq 1) -and ($row.scope -ceq 'synthetic_transport_no_owned_assets')
            $valid=$valid -and ([int]$row.owner_rows -eq $(if([int]$row.phase -eq 48){0}else{1}))
            if($role -eq 'Server'){$valid=$valid -and ([int]$row.profile_ready -eq 0)}
            else{
                $valid=$valid -and ([int]$row.foreign_rows -eq 0) -and ([int]$row.foreign_wire_rows -eq 0) -and ([int]$row.foreign_states -ge 1) -and ([int]$row.owner_wire_rows -eq [int]$row.owner_rows) -and ([int]$row.pending_observed -eq 1) -and ([int]$row.result_stage -eq 2) -and ([int]$row.late_source_safe -eq 1)
                if([int]$row.phase -eq 49 -and [int]$row.slot -eq 0){$valid=$valid -and ([int]$row.source_replaced -eq 1)}
            }
        }
        $craftingValidation += [ordered]@{role=$role;rows=$rows.Count;passed=$valid;scope='Synthetic summaries/actual socket no-provider Rejected/source retirement; no durable assets.'}
    }
    $after=Get-ProjectJSourceProvenance -ProjectRoot $projectRoot -RuntimePaths @($exe)
    $after|ConvertTo-Json -Depth 8|Set-Content -LiteralPath "$runRoot/CraftingAfterSourceProvenance.json" -Encoding utf8
    try{Assert-ProjectJMatchingProvenance -Provenance @($provenance,$after)}catch{$craftingValidation += [ordered]@{role='Source';rows=0;passed=$false;scope='Source/runtime changed during fixture.'}}
    $passed=$passed -and @($craftingValidation|Where-Object {!$_.passed}).Count -eq 0
}
$runtimeBinaryUnchanged=$true
if($Executable){
    $runtimeBinaryUnchanged=(Test-Path -LiteralPath $exe -PathType Leaf) -and (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ceq $executableSHA256
    $passed=$passed -and $runtimeBinaryUnchanged
}
$startupErrors = @(Get-ChildItem -LiteralPath $runRoot -Filter '*.log' | Select-String -Pattern 'LogPython: Error:' | ForEach-Object { $_.Line })
$passed=$passed -and (!$IrisCSV -or $irisCSVValidation.passed)
$passed=$passed -and (!$networkBudgetValidation -or $networkBudgetValidation.passed)
$passed=$passed -and (!$RenderedClientCSV -or $clientCSVValidation.passed) -and (!$RenderedClothTaskPriority -or $renderedClothTaskPriorityValidation.passed)
$passed=$passed -and ($RenderedClothVertexBudget -lt 0 -or $renderedClothExecutionValidation.passed)
[ordered]@{passed=$passed;canonicalMeleeValidation=$canonicalMeleeValidation;fanoutValidation=$fanoutValidation;craftingValidation=$craftingValidation;scratchRoot=$scratch.Root;networkBudgetValidation=$networkBudgetValidation;irisCSVValidation=$irisCSVValidation;clientCSVValidation=$clientCSVValidation;renderedClothTaskPriorityValidation=$renderedClothTaskPriorityValidation;serverNetworkMode=$serverNetworkMode;serverModeVerified=$serverModeVerified;excludedLocalHosts=$excludedLocalHosts;packaged=[bool]$Executable;executable=$exe;executableSHA256=$executableSHA256;runtimeBinaryUnchanged=$runtimeBinaryUnchanged;results=$results;exitCodes=$exitCodes;bufferValidations=$bufferValidations;authorityEncodeValidation=$authorityEncodeValidation;renderedValidation=$renderedValidation;cleaveValidation=$cleaveValidation;cleaveThreatValidation=$cleaveThreatValidation;cleaveSourceUnchanged=$cleaveSourceUnchanged;cleaveAssetsUnchanged=$cleaveAssetsUnchanged;attributeAudienceValidation=$attributeAudienceValidation;companionValidation=$companionValidation;equipmentInterestValidation=$equipmentInterestValidation;ownerCommandValidation=$ownerCommandValidation;partyValidation=$partyValidation;partyScreenshotValidation=$partyScreenshotValidation;bufferDiagnostics=$bufferDiagnostics;nativeHandleCapturedBeforeWait=$true;timeouts=$timeouts.Count;startupPythonErrors=$startupErrors;note='Fixture assertions and exit codes are authoritative. Client buffer capture starts after all clients pass Stage 1; optional authority encode capture starts on server startup and is separately typed; every later NP diagnostic fails CSV/log validation. Raw bufferDiagnostics also retains pre-capture startup diagnostics. A passing fixture does not mean the whole startup log is clean.'} | ConvertTo-Json -Depth 5 | Set-Content "$runRoot/verification.json" -Encoding utf8
if (!$passed) { throw "Network fixture failed. Inspect $runRoot/verification.json and logs." }
Write-Output "All processes completed: $runRoot"
