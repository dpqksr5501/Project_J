param([Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$RunName, [switch]$Trace, [string]$Executable, [switch]$DisableEditorPython, [switch]$LocalPlayer,
 [ValidateRange(1,200)][int]$Count=200,[switch]$Movement,[switch]$CloseView,[switch]$SecondaryMotionBudgetExperiment,[switch]$CanonicalProof,[switch]$TrajectoryMetrics,[switch]$NPCMetrics,[string]$ConsoleCommands='',
 [ValidateRange(0,32)][int]$NPCCombatCount=0,[switch]$MixedFXResources,[switch]$NPCAuthoredPath,
 [ValidateRange(-1,1)][int]$NaniteMode=-1,
 [ValidateRange(-1,1)][int]$WeaponVisibility=-1,[string]$ExpectedWeaponMesh='',
 [ValidateRange(1,16384)][int]$ResolutionX=1280,[ValidateRange(1,16384)][int]$ResolutionY=720,[switch]$ForceResolution,[switch]$PlayerPoseReadiness,[switch]$LeaderBoundsProof,[switch]$SharedPhysicsBoundsProof)
$ErrorActionPreference='Stop'
if($NPCAuthoredPath -and !$NPCCombatCount){throw 'Authored NPC path requires the actual NPC combat cohort.'}
if($MixedFXResources -and ($Count -ne 200 -or $NPCCombatCount -ne 32 -or !$Movement -or !$LocalPlayer -or $CloseView -or $LeaderBoundsProof -or $SharedPhysicsBoundsProof -or $SecondaryMotionBudgetExperiment -or $CanonicalProof -or $NaniteMode -ne -1 -or $WeaponVisibility -ne -1 -or $ExpectedWeaponMesh)){throw 'Mixed FX requires the unchanged 200-player/32-NPC local movement workload and ordinary quality.'}
if($NPCCombatCount -gt 0){
 if(!$Movement -or $CloseView -or $LeaderBoundsProof -or $SharedPhysicsBoundsProof -or $SecondaryMotionBudgetExperiment){throw 'NPC combat cohort requires Movement without close-view/bounds/quality experiment modes.'}
 if($ConsoleCommands -match '(?i)\bProjectJ\.NPCDecision\.Start\b'){throw 'Use the actual NPC cohort separately from the meshless decision sandbox.'}
 $NPCMetrics=$true
}
if($LeaderBoundsProof -and $SharedPhysicsBoundsProof){throw 'LeaderBoundsProof and SharedPhysicsBoundsProof are mutually exclusive.'}
if(($LeaderBoundsProof -or $SharedPhysicsBoundsProof) -and ($Executable -or $Count -ne 1 -or !$Movement -or !$CanonicalProof -or !$PlayerPoseReadiness -or $SecondaryMotionBudgetExperiment)){
 throw 'LeaderBoundsProof requires Editor host Count1 Movement CanonicalProof PlayerPoseReadiness without quality experiments.'
}
if($PlayerPoseReadiness -and $Executable){throw 'Player PoseSearch readiness currently requires the default Editor host; cooked Game uses its existing prepared data contract.'}
if($CloseView -and $Count -ne 1){throw 'Close-view quality review requires Count=1.'}
if($CloseView -and !$Movement){throw 'Close-view quality review requires Movement for the floor and live Mover workload.'}
. "$PSScriptRoot/Source-Provenance.ps1"
if(@(Get-Process UnrealEditor*,Project_J,UnrealBuildTool,dotnet,LiveCoding*,MSBuild,ShaderCompileWorker -ErrorAction SilentlyContinue).Count){throw 'Wait for engine/build completion.'}
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\','/')
$out="$root/Saved/Validation/WholeProjectAudit_20261013/$RunName"
if(Test-Path -LiteralPath $out){throw 'Preserve prior evidence; choose a unique name.'}
New-Item -ItemType Directory -Path $out|Out-Null
if ($NaniteMode -ge 0) {
 # This native -game fixture cannot host editor-only Python toolsets. Do not
 # initialize them in headless Nanite profiling; normal editor/MCP stays unchanged.
 $DisableEditorPython = $true
 if($ConsoleCommands -match '(?i)r\.SkinnedMesh\.RenderNanite'){throw 'Use NaniteMode as the sole render-path setting.'}
 $ConsoleCommands = ($ConsoleCommands.TrimEnd(',') + ",r.SkinnedMesh.RenderNanite $NaniteMode").TrimStart(',')
}
$exe = if ($Executable) { (Resolve-Path -LiteralPath $Executable).Path } else { 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' }
$arguments=@('/Engine/Maps/Entry?game=/Script/Engine.GameModeBase','-game','-unattended','-nop4','-nosplash','-nosound','-NoDDCCleanup','-NoLiveCoding',
 '-RenderOffscreen','-windowed',"-ResX=$ResolutionX","-ResY=$ResolutionY",'-ProjectJRenderedCrowdFixture',"-ProjectJRenderedCrowdOutput=$out","-ABSLOG=$out/Run.log",
 "-ExecCmds=t.MaxFPS 0,r.VSync 0,$ConsoleCommands", '-statnamedevents', "-UserDir=$out/User/", '-SaveToUserDir',"-ProjectJRenderedCrowdCount=$Count")
if ($Movement) { $arguments += '-ProjectJRenderedCrowdMovement' }
if ($ForceResolution) { $arguments += '-ForceRes' } # Native GameEngine option bypasses desktop-area clamping for this viewport.
if ($NaniteMode -ge 0) { $arguments += "-ProjectJRenderedCrowdNanite=$NaniteMode" }
if ($WeaponVisibility -ge 0) {
 $arguments += "-ProjectJRenderedCrowdWeaponVisibility=$WeaponVisibility"
 if ($ExpectedWeaponMesh) {
  if($ExpectedWeaponMesh -notmatch '^/Game/[A-Za-z0-9_/.]+$'){throw 'ExpectedWeaponMesh must be a project asset path.'}
  $arguments += "-ProjectJRenderedCrowdWeaponMesh=$ExpectedWeaponMesh"
 }
}
if (!$Executable) { $arguments = @("$root/Project_J.uproject") + $arguments }
if ($DisableEditorPython -and !$Executable) { $arguments += '-DisablePython' }
if ($LocalPlayer) { $arguments += '-ProjectJRenderedCrowdLocalPlayer' }
if ($CloseView) { $arguments += '-ProjectJRenderedCrowdCloseView' }
if ($SecondaryMotionBudgetExperiment) { $arguments += '-ProjectJRenderedCrowdSecondaryMotionBudgetExperiment' }
if ($TrajectoryMetrics) { $arguments += '-ProjectJRenderedCrowdTrajectoryMetrics' }
if ($NPCMetrics) { $arguments += '-ProjectJRenderedCrowdNPCMetrics' }
if ($NPCCombatCount -gt 0) { $arguments += "-ProjectJRenderedCrowdNPCCombatCount=$NPCCombatCount" }
if ($MixedFXResources) { $arguments += "-ProjectJRenderedCrowdMixedFX" }
if ($NPCAuthoredPath) { $arguments += "-ProjectJRenderedCrowdNPCAuthoredPath" }
if ($PlayerPoseReadiness) { $arguments += '-ProjectJRenderedCrowdPlayerPoseReadiness' }
if ($LeaderBoundsProof) { $arguments += '-ProjectJRenderedCrowdLeaderBoundsProof' }
if ($SharedPhysicsBoundsProof) { $arguments += '-ProjectJRenderedCrowdSharedPhysicsBoundsProof' }
if ($CanonicalProof) { $arguments += @('-ProjectJRenderedCrowdCanonicalProof','-Deterministic','-FPS=60') }
if($Trace){$arguments+=@('-trace=cpu,frame,gpu,bookmark,region,counters,task',"-tracefile=$out/Render.utrace")}
$files=@('Project_J.uproject','Content/Character_BPs/Spellblade/BP_Spellblade.uasset',
 'Content/Animation_Logic/ABPs/Spellblade/ABP_Spellblade_Woman_RunTIme.uasset','Content/Animation_Logic/ABPs/ABP_Player_Master.uasset')
# Current compatible body route and its contact/physics assets are part of the measured workload.
foreach ($folder in @('Animation','Materials','Meshes','Physics','Profiles','Rigs')) {
 $files += @(Get-ChildItem -LiteralPath "$root/Content/Characters/Spellblade/$folder" -Recurse -Filter '*.uasset' -File | Sort-Object FullName | ForEach-Object { $_.FullName.Substring($root.Length+1).Replace('\','/') })
}
if (!$Executable) { $files += @('Binaries/Win64/UnrealEditor-Project_J.dll','Binaries/Win64/UnrealEditor-Project_JCharacter.dll') }
if ($NaniteMode -ge 0) {
 $files += 'Content/Characters/Spellblade/Spellblade_Woman.uasset'
 foreach($relativeFolder in @('Content/Characters/Spellblade/Clothes','Content/Characters/Spellblade/Clothes/MTs')) {
  $files += @(Get-ChildItem -LiteralPath "$root/$relativeFolder" -Filter '*.uasset' -File | Sort-Object Name | ForEach-Object { "$relativeFolder/$($_.Name)" })
 }
}
if ($WeaponVisibility -ge 0) {
 $files += @('Content/Character_BPs/GreatSword/BP_Greatsword_Basic.uasset','Content/DataAssetSets/Presentation/Greatsword/DA_Greatsword_Presentation.uasset','Content/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.uasset')
 $files += @(Get-ChildItem -LiteralPath "$root/Content/Weapons/Greatsword/Basic" -Filter '*.uasset' -File | Sort-Object Name | ForEach-Object { "Content/Weapons/Greatsword/Basic/$($_.Name)" })
}
if ($NPCCombatCount -gt 0) {
 $files += @('Content/NPCs/BP_NPC_Test.uasset','Content/NPCs/Animation/ABP_NPC_Mover_Unarmed.uasset',
  'Content/DataAssetSets/NPC/DA_NPC_PrototypeCleaveActivation.uasset','Content/DataAssetSets/AttackDefinition/NPC/DA_Attack_NPC_PrototypeCleave.uasset',
  'Content/GAs/GA_NPC_PrototypeCleave.uasset')
}
if($MixedFXResources){$files+=@('Content/Effects/ProjectJ/Combat/NS_CombatTrail.uasset','Content/Effects/ProjectJ/Combat/NET_CombatTrail.uasset')}
if($NPCAuthoredPath){$files+=@('Content/DataAssetSets/AttackDefinition/NPC/DA_Attack_NPC_PrototypeCleave_Canonical.uasset','Content/DataAssetSets/NPC/DA_NPC_PrototypeCleaveCanonicalActivation.uasset')}
$files=@($files|Sort-Object -Unique)
$hashes=@($files|ForEach-Object{[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath "$root/$_").Hash}})
[ordered]@{head=(& git -C $root -c core.fsmonitor=false rev-parse HEAD);cpu=(Get-CimInstance Win32_Processor).Name;gpu=(Get-CimInstance Win32_VideoController).Name;
 closeView=[bool]$CloseView;
 npcAuthoredPath=[bool]$NPCAuthoredPath;npcGameplayGeometry=if($NPCAuthoredPath){'Saved canonical mesh-local path mapped through observed physical-root endpoints'}else{'Legacy live finalized pose chord'};
 npcCombatCount=$NPCCombatCount;passiveNativeRecipients=$NPCCombatCount;npcCombatScope='Opt-in authored non-MM NPC and separate passive native recipients. Natural combat/animation budget lifecycle; no NPC Nav, networking or cosmetic playback qualification.';
 screenshotEncoding=if($NPCCombatCount -gt 0){'Engine ImageWriteQueue; same requested frame; one inflight immutable BGRA buffer; five future-confirmed PNG writes'}else{'Engine default synchronous screenshot encoding'};
 mixedFXResources=[bool]$MixedFXResources;syntheticFXResourceOwners=if($MixedFXResources){132}else{0};fxResourceScope='Separately counted synthetic cosmetic resource cohort; shared normal creation budget and unchanged saved NS/NET, camera/body/NPC quality. Active/culled is not rendered particles, GPU increment or mandatory-warning proof.';
 forceResolutionRequested=[bool]$ForceResolution;
 actors=$Count;movement=[bool]$Movement;leaderBoundsProof=[bool]$LeaderBoundsProof;sharedPhysicsBoundsProof=[bool]$SharedPhysicsBoundsProof;secondaryMotionBudgetExperiment=[bool]$SecondaryMotionBudgetExperiment;canonicalProof=[bool]$CanonicalProof;trajectoryMetrics=[bool]$TrajectoryMetrics;npcMetrics=[bool]$NPCMetrics;playerPoseReadiness=[bool]$PlayerPoseReadiness;npcMetricsScope='Opt-in read-only NPC snapshots at the existing frame sampler; batch compute/Core delivery totals are wall elapsed, not summed CPU or worker wait.';canonicalFixedDeltaSeconds=if($CanonicalProof){1.0/60.0}else{$null};consoleCommands=$ConsoleCommands;naniteMode=$NaniteMode;weaponVisibility=$WeaponVisibility;expectedWeaponMesh=$ExpectedWeaponMesh;editorPythonDisabled=[bool]($DisableEditorPython -and !$Executable);sourceProvenance=(Get-ProjectJSourceProvenance -ProjectRoot $root -RuntimePaths @($exe));localPlayer=[bool]$LocalPlayer;connections=0;nullRHI=$false;offscreenViewport=$true;resolution="${ResolutionX}x${ResolutionY}";requestedResolution=[ordered]@{x=$ResolutionX;y=$ResolutionY};resolutionScope='Command-line request; phase2/phase3 PNG dimensions must be checked separately before claiming observed viewport output size.';fpsCap=0;vsync=0;trace=[bool]$Trace;packaged=[bool]$Executable;executable=$exe;executableSHA256=(Get-FileHash -LiteralPath $exe).Hash;arguments=$arguments;artifacts=$hashes}|
 ConvertTo-Json -Depth 8|Set-Content "$out/manifest.json" -Encoding utf8
$quoted=($arguments|ForEach-Object{'"'+$_+'"'}) -join ' '
$p=Start-Process $exe -WorkingDirectory (Split-Path -Parent $exe) -ArgumentList $quoted -WindowStyle Hidden -PassThru
$nativeHandle=$p.Handle
Write-Output "Started rendered crowd PID=$($p.Id)"
$peakWorking=0L;$peakPrivate=0L
while(!$p.WaitForExit(500)){
 $p.Refresh()
 if(!$p.HasExited){$peakWorking=[Math]::Max($peakWorking,$p.WorkingSet64);$peakPrivate=[Math]::Max($peakPrivate,$p.PrivateMemorySize64)}
}
$p.WaitForExit()
[ordered]@{exitCode=$p.ExitCode;nativeHandleCapturedBeforeWait=$true;hasExited=$p.HasExited;sampledPeakWorkingSetBytes=$peakWorking;sampledPeakPrivateBytes=$peakPrivate;memoryScope='Entire editor game process including startup, sampled every 500ms; not incremental crowd memory.'}|
 ConvertTo-Json|Set-Content "$out/process.json" -Encoding utf8
if($null -eq $p.ExitCode){throw "Rendered crowd exit code unavailable: $out"}
if($p.ExitCode -ne 0 -or !(Test-Path "$out/result.txt")){throw "Rendered crowd process failed: $out"}
$result=Get-Content "$out/result.txt" -Raw
if($result -notmatch 'success=1' -or !(Test-Path "$out/phase2.png") -or !(Test-Path "$out/phase3.png")){throw "Rendered crowd assertions/captures failed: $result"}
$observedOutput=@('phase2.png','phase3.png'|ForEach-Object {
 $bytes=[IO.File]::ReadAllBytes("$out/$_")
 if($bytes.Length -lt 24 -or [BitConverter]::ToString($bytes,0,8) -cne '89-50-4E-47-0D-0A-1A-0A' -or [Text.Encoding]::ASCII.GetString($bytes,12,4) -cne 'IHDR'){throw "Invalid viewport PNG: $_"}
 $width=([int]$bytes[16] -shl 24) -bor ([int]$bytes[17] -shl 16) -bor ([int]$bytes[18] -shl 8) -bor [int]$bytes[19]
 $height=([int]$bytes[20] -shl 24) -bor ([int]$bytes[21] -shl 16) -bor ([int]$bytes[22] -shl 8) -bor [int]$bytes[23]
 if($width -le 0 -or $height -le 0){throw "Invalid viewport PNG dimensions: $_"}
 [ordered]@{file=$_;width=$width;height=$height;matchesRequest=($width -eq $ResolutionX -and $height -eq $ResolutionY)}
})
$outputMatchesRequest=@($observedOutput|Where-Object {!$_.matchesRequest}).Count -eq 0
[ordered]@{requestedWidth=$ResolutionX;requestedHeight=$ResolutionY;forceResolutionRequested=[bool]$ForceResolution;observedScreenshots=$observedOutput;matchesRequest=$outputMatchesRequest;scope='Two regular viewport screenshot output sizes. Does not establish internal raster resolution or every frame.'}|ConvertTo-Json -Depth 5|Set-Content "$out/viewport-output.json" -Encoding utf8
if($ForceResolution -and !$outputMatchesRequest){throw 'Forced viewport output differs from the request; preserve this run and do not label its performance by the requested resolution.'}
if($Executable -and $result -notmatch 'cooked=1'){throw 'Packaged run did not require cooked data.'}
if($LocalPlayer -and ($result -notmatch 'local_player=1' -or $result -notmatch 'local_quality_samples=[1-9][0-9]*')){throw 'No verified local full-quality frames.'}
if ($NPCMetrics -and !(Test-Path -LiteralPath "$out/npc_decisions.csv" -PathType Leaf)){throw 'Opt-in NPC decision metrics evidence is missing.'}
if ($NPCCombatCount -gt 0) {
 $shots=Get-Content -LiteralPath "$out/screenshots.txt" -Raw
 foreach($line in @('mode=ImageWriteQueue_single_inflight','started=5','completed=5','completed_phase_mask=124','pending_write=0','failure=')) {
  if($shots -notmatch ('(?m)^'+[regex]::Escape($line)+'\r?$')) { throw 'Bounded asynchronous screenshots failed/incomplete.' }
 }
 $npc=Get-Content -LiteralPath "$out/npc-combat.json" -Raw|ConvertFrom-Json
 if($npc.schema -ne 1 -or !$npc.success -or $npc.authored_npcs -ne $NPCCombatCount -or $npc.passive_native_recipients -ne $NPCCombatCount -or !$npc.retired -or !$npc.cleanup_passed -or !$npc.dirty_unchanged -or !(Test-Path -LiteralPath "$out/npc-combat.csv" -PathType Leaf)){
  throw 'Actual NPC cohort failed/incomplete; preserve per-NPC/budget/retirement evidence.'
 }
 if($NPCAuthoredPath -and (!$npc.authored_melee_path -or $npc.authored_path_keys -lt 2 -or !$npc.authored_path_stamp)){throw 'Saved authored NPC source evidence is incomplete.'}
}
if ($MixedFXResources) {
 $fx=Get-Content -LiteralPath "$out/fx-resource.json" -Raw|ConvertFrom-Json
 if($fx.schema -ne 1 -or !$fx.success -or $fx.resource_owners -ne 132 -or !$fx.retired -or !$fx.cleanup_passed -or !$fx.dirty_unchanged -or !(Test-Path -LiteralPath "$out/fx-resource.csv" -PathType Leaf)){
  throw 'Mixed FX resource cohort failed/incomplete; preserve actual component/view/queue/retirement evidence.'
 }
}
if ($PlayerPoseReadiness) {
 $playerPoseReceipt=Get-Content -LiteralPath "$out/player-pose-readiness.json" -Raw|ConvertFrom-Json
 if($playerPoseReceipt.schema -ne 1 -or $playerPoseReceipt.scope -cne 'source_player_chooser_input_candidates' -or
    !$playerPoseReceipt.readiness_success -or !$playerPoseReceipt.measurement_admitted -or
    !$playerPoseReceipt.input_membership_unchanged -or !$playerPoseReceipt.dirty_unchanged -or
    $playerPoseReceipt.player_count -ne $Count -or $playerPoseReceipt.database_count -lt 1){
  throw 'Player Chooser/PoseSearch input readiness is missing, incomplete or changed; preserve the original receipt.'
 }
}
if($LeaderBoundsProof -or $SharedPhysicsBoundsProof){
 $proofPath="$out/bounds_native_proof.json";$pairPath="$out/bounds_native_pairs.csv"
 if(!(Test-Path -LiteralPath $proofPath) -or !(Test-Path -LiteralPath $pairPath)){throw 'Native bounds proof evidence missing.'}
 $proof=Get-Content -LiteralPath $proofPath -Raw|ConvertFrom-Json
 $expectedBoundsMode=if($SharedPhysicsBoundsProof){'shared_physics_cache'}else{'native_leader_bounds'}
 if($proof.comparison_mode -cne $expectedBoundsMode -or [int]$proof.csv_columns -ne 60 -or !$proof.shared_cache_restored){throw 'Native bounds comparison mode/schema/cache restoration mismatch.'}
 if($SharedPhysicsBoundsProof -and ($proof.shared_cache_control -cne 'ProjectJ.Bounds.SharedPhysicsCache' -or
    $null -eq $proof.shared_cache_original_value -or $null -eq $proof.shared_cache_original_set_by -or
    [int]$proof.shared_cache_original_value -notin @(0,1) -or [uint32]$proof.shared_cache_original_set_by -ne 0)){throw 'Shared-cache original value/Constructor rank is missing or unsupported.'}
 if(!$proof.run_completed -or !$proof.qualified -or !$proof.original_flags_restored -or $proof.unknown -or
    [int]$proof.violations -ne 0 -or [int]$proof.identity_invalid -ne 0 -or [int]$proof.pairs -le 0 -or @($proof.parts).Count -eq 0){throw 'Native bounds envelope qualification failed; preserve scientific failure/partial evidence.'}
 $pairs=@(Import-Csv -LiteralPath $pairPath)
 if($pairs.Count -ne [int]$proof.pairs -or $pairs.Count -gt 10240){throw 'Native bounds pair count mismatch.'}
 $nativeBoundsColumns=@($pairs[0].PSObject.Properties.Name)
 if($nativeBoundsColumns.Count -ne 60 -or @($nativeBoundsColumns|Select-Object -Unique).Count -ne 60 -or
    (($nativeBoundsColumns|Select-Object -Last 8)-join ',') -cne 'comparison_mode,old_shared_cache,new_shared_cache,cache_restored,old_cache_hit_delta,old_cache_miss_delta,new_cache_hit_delta,new_cache_miss_delta'){throw 'Native bounds CSV field schema mismatch.'}
 foreach($part in @($proof.parts)){
  foreach($phase in 2..6){
   $phasePairCount=[int]$part.phase_pairs.$phase
   if($phasePairCount -le 0 -or @($pairs|Where-Object {$_.part -ceq $part.part -and $_.source -ceq $part.source -and [int]$_.phase -eq $phase}).Count -ne $phasePairCount){throw 'Selected part phase coverage does not match actual pair rows.'}
  }
 }
 function Read-LeaderBoundsNumber([string]$Value){
  $parsed=0.0
  if(![double]::TryParse($Value,[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$parsed) -or [double]::IsNaN($parsed) -or [double]::IsInfinity($parsed)){throw 'Nonfinite or missing native bounds number.'}
  return $parsed
 }
 $equivalent=0;$maxBoxDelta=0.0;$maxRadiusDelta=0.0;$cacheHitPairs=0;$cacheMissPairs=0
 foreach($row in $pairs){
  $minMargin=[double]::PositiveInfinity;$distanceSquared=0.0;$same=$true
  foreach($axis in @('x','y','z')){
   $o=Read-LeaderBoundsNumber $row.('old_'+$axis);$n=Read-LeaderBoundsNumber $row.('new_'+$axis)
   $oe=Read-LeaderBoundsNumber $row.('old_e'+$axis);$ne=Read-LeaderBoundsNumber $row.('new_e'+$axis)
   if($oe -lt 0 -or $ne -lt 0){throw 'Negative native box extent.'}
   $minMargin=[math]::Min($minMargin,[math]::Min(($o-$oe)-($n-$ne),($n+$ne)-($o+$oe)))
   $maxBoxDelta=[math]::Max($maxBoxDelta,[math]::Max([math]::Abs(($o-$oe)-($n-$ne)),[math]::Abs(($n+$ne)-($o+$oe))))
   $distanceSquared+=($n-$o)*($n-$o);$same=$same -and [math]::Abs($n-$o) -le 0.001 -and [math]::Abs($ne-$oe) -le 0.001
  }
  $oldRadius=Read-LeaderBoundsNumber $row.old_radius;$newRadius=Read-LeaderBoundsNumber $row.new_radius
  $sphereMargin=$newRadius-$oldRadius-[math]::Sqrt($distanceSquared);$radiusDelta=[math]::Abs($newRadius-$oldRadius)
  if($oldRadius -lt 0 -or $newRadius -lt 0 -or $minMargin -lt -0.001 -or $sphereMargin -lt -0.001 -or
     [math]::Abs($minMargin-(Read-LeaderBoundsNumber $row.box_min_margin)) -gt 1e-6 -or [math]::Abs($sphereMargin-(Read-LeaderBoundsNumber $row.sphere_margin)) -gt 1e-6 -or
     $row.old_use_leader -cne '0' -or $row.new_use_leader -cne $(if($SharedPhysicsBoundsProof){'0'}else{'1'})){throw 'Native old/new envelope raw containment or flag pair mismatch.'}
  $same=$same -and $radiusDelta -le 0.001
  if($row.comparison_mode -cne $expectedBoundsMode -or $row.cache_restored -cne '1'){throw 'Native bounds row mode/cache restoration mismatch.'}
  $oldHit=Read-LeaderBoundsNumber $row.old_cache_hit_delta;$oldMiss=Read-LeaderBoundsNumber $row.old_cache_miss_delta
  $newHit=Read-LeaderBoundsNumber $row.new_cache_hit_delta;$newMiss=Read-LeaderBoundsNumber $row.new_cache_miss_delta
  if($oldHit -ne 0 -or $oldMiss -ne 0 -or $newHit -notin @(0,1) -or $newMiss -notin @(0,1)){throw 'Unsupported shared-cache counter deltas.'}
  if($SharedPhysicsBoundsProof){
   if(!$same -or $row.old_shared_cache -cne '0' -or $row.new_shared_cache -cne '1' -or $newHit+$newMiss -ne 1){throw 'Shared-cache equivalence/CVar0-to1/actual hit-or-miss evidence failed.'}
  }elseif($row.old_shared_cache -cne '-1' -or $row.new_shared_cache -cne '-1' -or $newHit -ne 0 -or $newMiss -ne 0){throw 'Leader-only proof unexpectedly used shared-cache control.'}
  $cacheHitPairs += [int]$newHit;$cacheMissPairs += [int]$newMiss
  if([int]$row.equivalent -ne [int]$same){throw 'Native equivalent flag does not match the old/new envelope.'}
  $equivalent += [int]$same;$maxRadiusDelta=[math]::Max($maxRadiusDelta,$radiusDelta)
 }
 if($equivalent -ne [int]$proof.equivalent_pairs -or [math]::Abs($maxBoxDelta-[double]$proof.max_box_coordinate_delta_cm) -gt 1e-6 -or [math]::Abs($maxRadiusDelta-[double]$proof.max_radius_delta_cm) -gt 1e-6){throw 'Native equivalence/delta summary does not match actual raw pairs.'}
 if($cacheHitPairs -ne [int]$proof.cache_hit_pairs -or $cacheMissPairs -ne [int]$proof.cache_miss_pairs -or
    ($SharedPhysicsBoundsProof -and ($equivalent -ne $pairs.Count -or $cacheHitPairs -le 0 -or $cacheHitPairs+$cacheMissPairs -ne $pairs.Count))){throw 'Native shared-cache observed/equivalent totals do not match raw pairs.'}
 if(@($pairs|Where-Object {[int]$_.phase -lt 2 -or [int]$_.phase -gt 6 -or [double]$_.phase_age -lt 2 -or [double]$_.phase_age -gt 7 -or [int]$_.restored -ne 1 -or [int]$_.contained -ne 1}).Count){throw 'Native bounds pair scope/restoration/containment mismatch.'}
}
$rows=@(Import-Csv "$out/frames.csv")
if ($WeaponVisibility -ge 0) {
 $weaponRows=@($rows|Where-Object {[int]$_.phase -gt 1 -and [double]$_.phase_game_s -gt 2})
 if(!$weaponRows.Count -or @($weaponRows|Where-Object {[int]$_.weapon_meshes -ne $Count -or [int]$_.weapon_mesh_matches -ne $Count -or [int]$_.weapon_visible -ne $(if($WeaponVisibility){$Count}else{0})}).Count){throw 'Equipped weapon comparison did not exercise every participant.'}
}
if ($NaniteMode -ge 0) {
 $expectedModularParts=@(Import-Csv "$out/pose_routes.csv"|Where-Object {[int]$_.modular -eq 1}).Count
 $measured=@($rows|Where-Object { [int]$_.phase -gt 1 })
 if(!$expectedModularParts -or !$measured.Count -or @($measured|Where-Object { [int]$_.nanite_candidates -le 0 -or [int]$_.modular_parts -ne $expectedModularParts -or [int]$_.cloth_parts -ne $Count -or [int]$_.cloth_tick_parts -ne $Count }).Count){throw 'Nanite comparison lost the authored modular/cloth workload.'}
 if(@($measured|Where-Object { [int]$_.nanite_parts -ne $(if($NaniteMode){[int]$_.nanite_candidates}else{0}) }).Count){throw 'Saved Nanite settings did not select the expected actual render path.'}
}
$attackPhase=if($Movement){5}else{3}
if(!@($rows|Where-Object {[int]$_.phase -eq $attackPhase -and [int]$_.montages -eq $Count}).Count){throw 'No observed simultaneous authored R montages.'}
if($Movement){
 $return=@($rows|Where-Object {[int]$_.phase -eq 6})
 if(!$return.Count -or @($return|Where-Object {$(if($_.PSObject.Properties.Name -contains 'phase_game_s'){[double]$_.phase_game_s -ge 1}else{[double]$_.elapsed_s -ge ([double]$return[0].elapsed_s+1)}) -and [int]$_.montages -ne 0}).Count){throw 'Attack montages did not drain on return to locomotion.'}
 $work=@(Import-Csv "$out/workload.csv")
 if($work.Count -ne $Count -or @($work|Where-Object {[double]$_.travel_cm -le 100 -or [int]$_.reversals -lt 1 -or [int]$_.landings -lt 1}).Count){throw 'Moving/pivot/jump workload was not exercised for every participant.'}
}
[ordered]@{success=$true;exitCode=$p.ExitCode;frames=$rows.Count;scope='Real offscreen viewport/RHI and actual constructed bodies (pose route recorded in pose_routes.csv); simplified camera/light scene; no remote clients. GPU counters may be delayed. Captures affect frame timing.'}|
 ConvertTo-Json|Set-Content "$out/verification.json" -Encoding utf8
Write-Output "Completed rendered crowd: $out"
