#include "Testing/Project_JNetworkLoadFixture.h"
#include "Network/Project_JPlayerStateAudienceFilter.h"
#include "Network/Project_JOwnedPawnScopeComponent.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Navigation/PathFollowingComponent.h"
#include "System/Project_JNPCPathSubsystem.h"
#include "BrainComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameplayEffect.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Animation/Project_JAnimNotifyState_MeleeHit.h"
#include "Combat/Project_JGameplayAbility_NPCAttack.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "AbilitySystem/Project_JAbilitySet.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Components/Project_JNPCActivationComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "Optimization/Project_JTargetScoring.h"
#include "Tests/Project_JMoverFrameFixture.h"
#include "Movement/Project_JMovementQueries.h"
#include "Testing/Project_JAuditNetworkFixture.h"
#include "World/Project_JWorldState.h"
#include "World/Project_JWorldStreamingComponent.h"
#include "World/Project_JWorldArrivalSubsystem.h"
#include "World/Project_JWorldPortal.h"
#include "Interaction/Project_JInteractionTargetComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/GameModeBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/ActorComponent.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "HAL/PlatformMemory.h"
#include "UObject/UObjectGlobals.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/HLOD/HLODRuntimeSubsystem.h"
#include "WorldPartition/HLOD/IWorldPartitionHLODObject.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Level.h"
#include "Engine/AssetManager.h"
#include "UObject/Package.h"
#include "Components/SkeletalMeshComponent.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JQuestComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JCraftingJobsComponent.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Components/SceneComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Internationalization/Text.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/InstantMovementEffects/BasicInstantMovementEffects.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Components/Project_JReplicatedAnimEventComponent.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "UObject/UnrealType.h"
#include "Components/BoxComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JNPCCharacter.h"
#include "Project_JGameplayTags.h"
#include "Components/Project_JSkillInputRouterComponent.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Combat/Project_JComboDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Companion/Project_JCompanionComponent.h"
#include "Companion/Project_JCompanionItemDefinition.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Components/Project_JNPCActionComponent.h"
#include "UI/Project_JInventoryViewModel.h"
#include "UI/Project_JPlayerUIComponent.h"
#include "Social/Project_JSocialSubsystem.h"
#include "UnrealClient.h"
#include "Net/Iris/ReplicationSystem/ReplicationSystemUtil.h"
#include "Net/Iris/ReplicationSystem/EngineReplicationBridge.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"
#include "WorldPartition/WorldPartitionRuntimeLevelStreamingCell.h"
#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"
#include "Engine/LevelStreaming.h"
#include "Engine/GameViewportClient.h"
#include "Camera/CameraActor.h"
#include "DynamicRHI.h"
#include "Testing/Project_JRenderedCrowdFixture.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "World/Project_JWorldEventRegistry.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJDNetwork, Log, All);
namespace
{
bool Enabled()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return FParse::Param(FCommandLine::Get(), TEXT("ProjectJNetworkFixture"));
#endif
}
int32 FixtureClients() { int32 N = 2; FParse::Value(FCommandLine::Get(), TEXT("ProjectJFixtureClients="), N); return FMath::Clamp(N, 2, 8); }
bool AnimationEnabled() { return Enabled() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJAnimationFixture")); }
bool RenderedObserverProcess() { return FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderedClientObserver")); }
bool RenderedClientEnabled() { return AnimationEnabled() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJRenderedClientFixture")); }
bool CombatEnabled() { return AnimationEnabled() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJCombatInputFixture")); }
bool CanonicalMeleeEnabled() { return CombatEnabled() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJCanonicalMeleeFixture")); }
bool CanonicalMovingRecipientEnabled() { return CanonicalMeleeEnabled() && FParse::Param(FCommandLine::Get(),TEXT("ProjectJCanonicalMeleeMovingRecipient")); }
bool CleaveEnabled() { return AnimationEnabled() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJNPCCleaveFixture")); }
int32 CombatFanoutCount() { int32 Count=0; FParse::Value(FCommandLine::Get(),TEXT("ProjectJNPCCleaveFanout="),Count); return Count; }
bool CombatFanoutEnabled() { return CleaveEnabled() && CombatFanoutCount()==32; }
bool CleaveThreatEnabled() { return CleaveEnabled() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJNPCDamageThreatFixture")); }
bool CleaveThreatStage(int32 Phase) { return Phase >= 33 && Phase <= 34 && CleaveThreatEnabled(); }
bool CleaveNaturalStage(int32 Phase) { return Phase == 31 || (CleaveThreatStage(Phase) && Phase == 33); }
bool CleaveCancellationStage(int32 Phase) { return Phase == 32 || (CleaveThreatStage(Phase) && Phase == 34); }
bool QuestsEnabled() { return Enabled() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJQuestFixture")); }
bool WorldClockEnabled() { return Enabled() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJWorldClockFixture")); }
bool AttributeAudienceEnabled() { return Enabled() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJAttributeAudienceFixture")); }
bool CompanionAudienceEnabled() { return AnimationEnabled() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJCompanionAudienceFixture")); }
bool EquipmentInterestEnabled() { return AnimationEnabled() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJEquipmentInterestFixture")); }
bool EquipmentInterestStage(int32 Phase) { return (Phase >= 16 && Phase <= 19) || Phase == 28 || Phase == 29; }
bool CraftingJobsEnabled() { return Enabled() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJCraftingJobsFixture")); }
bool PartyRosterEnabled() { return AnimationEnabled() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJPartyRosterFixture")); }
bool PlayerStateAudienceEnabled() { return PartyRosterEnabled() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJPlayerStateAudience")); }
bool PlayerStateAudienceLifetimeEnabled() { return PlayerStateAudienceEnabled() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJPlayerStateAudienceLifetime")); }
bool PlayerStateAudienceOwnerViewEnabled() { return PlayerStateAudienceLifetimeEnabled() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJPlayerStateAudienceOwnerView")); }
bool PartyOwnerViewDetached(AProject_JNetworkLoadController* PC, APawn* Retired);
bool PartyOwnerViewRestored(AProject_JNetworkLoadController* PC);
FString PartyOwnershipSnapshot(AProject_JNetworkLoadController* PC, int32 Phase, int32 Slot, const TCHAR* Boundary);
bool AudienceLifetimeAfterReplacement(int32 Phase) { return PlayerStateAudienceLifetimeEnabled() && (Phase == 30 || Phase == 29 || Phase == 31 || Phase == 27); }
bool AudienceNearPhase(int32 Phase) { return PlayerStateAudienceEnabled() && (Phase == 28 || (PlayerStateAudienceLifetimeEnabled() && Phase == 30)); }
int32 PartyLastSupportedPhase() { return PlayerStateAudienceLifetimeEnabled() ? 31 : PlayerStateAudienceEnabled() ? 29 : 27; }
// Public native PlayerId is InitialOnly. Mark the fresh listen observer before
// remote participants join; stock GameSession allocation excludes MAX_int32.
constexpr int32 FixtureListenObserverPlayerId = MAX_int32;
constexpr int32 AttributeNPCSlot = 8; // Player slots are bounded to 0..7.
const TCHAR* AttributeNames[] = {TEXT("MaxHealth"), TEXT("Health"), TEXT("MaxMana"), TEXT("Mana"), TEXT("AttackPower"), TEXT("Defense")};
float AttributeBase(int32 Slot, int32 Stage, int32 Index)
{
	const float Values[] = {1000, 500, 800, 300, 10, 20};
	return Values[Index] + Slot * 10 + Stage;
}
TArray<FGameplayAttribute> FixtureAttributes()
{
	return {UProject_JAttributeSet::GetMaxHealthAttribute(), UProject_JAttributeSet::GetHealthAttribute(),
		UProject_JAttributeSet::GetMaxManaAttribute(), UProject_JAttributeSet::GetManaAttribute(),
		UProject_JAttributeSet::GetAttackPowerAttribute(), UProject_JAttributeSet::GetDefenseAttribute()};
}
bool PublishAttributeBases(UAbilitySystemComponent* ASC, int32 Slot, int32 Stage, const FString& Actor, FString& Rows)
{
	if (!ASC || !ASC->IsOwnerActorAuthoritative()) { return false; }
	const auto Attributes = FixtureAttributes();
	// Real aggregator updates preserve equipment effects, resource rules and GAS callbacks.
	for (int32 I = 0; I < Attributes.Num(); ++I) { ASC->SetNumericAttributeBase(Attributes[I], AttributeBase(Slot, Stage, I)); }
	bool bValid = true;
	for (int32 I = 0; I < Attributes.Num(); ++I)
	{
		const float Base = ASC->GetNumericAttributeBase(Attributes[I]), Current = ASC->GetNumericAttribute(Attributes[I]);
		const float Expected = AttributeBase(Slot, Stage, I);
		const bool bPassed = FMath::IsNearlyEqual(Base, Expected) && FMath::IsFinite(Current) && Current >= 0;
		bValid &= bPassed;
		Rows += FString::Printf(TEXT("%d,%s,authority,%s,%.9g,%.9g,%.9g,%d\n"), Stage, *Actor, AttributeNames[I], Base, Current, Expected, bPassed);
	}
	return bValid;
}
const FName FixtureQuestId(TEXT("NetworkFixture.Quest"));
const FName FixtureCompoundQuestId(TEXT("NetworkFixture.Compound"));
const FName FixtureQuestEvent(TEXT("NetworkFixture.Objective"));
const TCHAR* CanonicalEquipPath=TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Spellblade_CanonicalEquip.DA_Spellblade_CanonicalEquip");
const TCHAR* CanonicalStylePath=TEXT("/Game/DataAssetSets/CombatStyle/DA_CombatStyle_Spellblade_Canonical.DA_CombatStyle_Spellblade_Canonical");
const TCHAR* CanonicalComboPath=TEXT("/Game/DataAssetSets/Combo/DA_Combo_Spellblade_Canonical.DA_Combo_Spellblade_Canonical");
const TCHAR* CanonicalAttackPath=TEXT("/Game/DataAssetSets/AttackDefinition/Spellblade/DA_Attack_Spellblade_LMB1_Canonical.DA_Attack_Spellblade_LMB1_Canonical");
bool ReadCanonicalReferences(AProject_JPlayerCharacter* Player, UProject_JEquipmentItemDefinition*& Item,
 UProject_JAttackDefinition*& Attack, TArray<FGameplayAbilitySpecHandle>& Grants, FGameplayTagContainer& HitTags,FString* Failure=nullptr)
{
 const auto Fail=[&](const FString& Why) { if(Failure) *Failure=Why;return false; };
 auto* ASC=Player?Cast<UProject_JAbilitySystemComponent>(Player->GetAbilitySystemComponent()):nullptr;
 auto* Runtime=Player?Player->FindComponentByClass<UProject_JEquipmentRuntimeComponent>():nullptr;
 const auto* Style=Player?Player->GetCombatStyleDefinition():nullptr;
 const auto* Combo=Style?Style->ComboDefinition.Get():nullptr;
 Item=Runtime?Runtime->GetPresentedItemInSlot(EProject_JEquipmentSlot::Weapon):nullptr;
 if(!ASC) return Fail(TEXT("refs.source_asc"));
 if(!Runtime) return Fail(TEXT("refs.runtime"));
 if(!Runtime->GetWeaponRevision()) return Fail(TEXT("refs.weapon_revision_zero"));
 if(!Item) return Fail(TEXT("refs.presented_item_null"));
 if(Item->GetPathName()!=CanonicalEquipPath) return Fail(TEXT("refs.equipment_path actual=")+Item->GetPathName());
 if(!Style) return Fail(TEXT("refs.gameplay_style_null"));
 if(Style!=Item->CombatStyleDefinition) return Fail(TEXT("refs.gameplay_style_identity actual=")+Style->GetPathName()+TEXT(" item_style=")+GetPathNameSafe(Item->CombatStyleDefinition.Get()));
 if(Style->GetPathName()!=CanonicalStylePath) return Fail(TEXT("refs.style_path actual=")+Style->GetPathName());
 if(!Style->IsRuntimeReady()) return Fail(TEXT("refs.style_not_runtime_ready"));
 if(!Combo) return Fail(TEXT("refs.combo_null"));
 if(Combo->GetPathName()!=CanonicalComboPath) return Fail(TEXT("refs.combo_path actual=")+Combo->GetPathName());
 FGameplayTagContainer Tags; ASC->GetOwnedGameplayTags(Tags);
 const auto* Node=Combo->FindStartNode(FProject_JGameplayTags::Get().InputTag_Weapon_LMB,Tags);
 Attack=Node?Node->AttackDefinition.Get():nullptr;
 if(!Attack) return Fail(TEXT("refs.lmb_start_attack_null"));
 if(Attack->GetPathName()!=CanonicalAttackPath) return Fail(TEXT("refs.attack_path actual=")+Attack->GetPathName());
 if(!Attack->MeleeTrajectory.bEnabled) return Fail(TEXT("refs.trajectory_disabled"));
 if(!Attack->Montage) return Fail(TEXT("refs.montage_null"));
 if(!Attack->DamageEffect) return Fail(TEXT("refs.damage_effect_null"));
 if(!Player->GetMesh()) return Fail(TEXT("refs.source_mesh_null"));
 const FString PathFailure=Attack->GetMeleeTrajectoryFailure(Player->GetMesh()->GetSkeletalMeshAsset());
 if(!PathFailure.IsEmpty()) return Fail(TEXT("refs.trajectory_binding: ")+PathFailure+TEXT(" actual_mesh=")+GetPathNameSafe(Player->GetMesh()->GetSkeletalMeshAsset())+
  TEXT(" expected_mesh=")+Attack->MeleeTrajectory.SourceMesh.ToSoftObjectPath().ToString());
 for(const auto& Spec:ASC->GetActivatableAbilities())
  if(!Spec.PendingRemove && Spec.Ability && Spec.Ability->IsA<UProject_JGameplayAbility_Melee>() &&
     Spec.GetDynamicSpecSourceTags().HasTagExact(FProject_JGameplayTags::Get().InputTag_Weapon_LMB))
  {
   if(Grants.Num()>=32) return Fail(TEXT("refs.matching_lmb_grants_exceed_fixture_bound"));
   Grants.Add(Spec.Handle);
   const auto* Property=FindFProperty<FStructProperty>(Spec.Ability->GetClass(),TEXT("MeleeHitEventTag"));
   if(!Property) return Fail(TEXT("refs.hit_tag_property_missing"));
   if(Property->Struct!=FGameplayTag::StaticStruct()) return Fail(TEXT("refs.hit_tag_property_type"));
   const FGameplayTag CandidateTag=*Property->ContainerPtrToValuePtr<FGameplayTag>(Spec.Ability);
   if(CandidateTag.IsValid()) HitTags.AddTag(CandidateTag);
  }
 if(Grants.IsEmpty()) return Fail(TEXT("refs.lmb_grant_missing"));
 if(HitTags.IsEmpty()) return Fail(TEXT("refs.no_melee_hit_event_tags"));
 return true;
}
bool CanonicalCurrent(const FProject_JCanonicalMeleeWitness& W,FString* Failure=nullptr)
{
 const auto* Player=W.Source.Get(); const auto* Target=W.Target.Get();
 const auto Check=[&](bool Value,const TCHAR* Label) { if(!Value && Failure) *Failure=FString(TEXT("current."))+Label;return Value; };
 return Check(!W.bCleaned,TEXT("not_retired")) &&
  Check(IsValid(Player),TEXT("source_valid")) &&
  Check(!Player->IsActorBeingDestroyed(),TEXT("source_not_destroying")) &&
  Check(Player->GetWorld()!=nullptr,TEXT("source_world")) &&
  Check(!Player->GetWorld()->bIsTearingDown,TEXT("source_world_not_teardown")) &&
  Check(IsValid(Target),TEXT("target_valid")) &&
  Check(!Target->IsActorBeingDestroyed(),TEXT("target_not_destroying")) &&
  Check(Target->GetWorld()==Player->GetWorld(),TEXT("same_world")) &&
  Check(W.ASC.IsValid(),TEXT("source_asc_valid")) &&
  Check(W.ASC->IsRegistered(),TEXT("source_asc_registered")) &&
  Check(!W.ASC->IsBeingDestroyed(),TEXT("source_asc_not_destroying")) &&
  Check(Player->GetAbilitySystemComponent()==W.ASC.Get(),TEXT("source_asc_identity")) &&
  Check(W.ASC->GetAvatarActor()==Player,TEXT("source_avatar")) &&
  Check(W.ASC->GetActorInfoRevision()==W.SourceEpoch,TEXT("source_epoch")) &&
  Check(W.TargetASC.IsValid(),TEXT("target_asc_valid")) &&
  Check(W.TargetASC->IsRegistered(),TEXT("target_asc_registered")) &&
  Check(!W.TargetASC->IsBeingDestroyed(),TEXT("target_asc_not_destroying")) &&
  Check(Target->GetAbilitySystemComponent()==W.TargetASC.Get(),TEXT("target_asc_identity")) &&
  Check(W.TargetASC->GetAvatarActor()==Target,TEXT("target_avatar")) &&
  Check(W.TargetASC->GetActorInfoRevision()==W.TargetEpoch,TEXT("target_epoch")) &&
  Check(W.Hit.IsValid(),TEXT("hit_valid")) &&
  Check(W.Hit->IsRegistered(),TEXT("hit_registered")) &&
  Check(!W.Hit->IsBeingDestroyed(),TEXT("hit_not_destroying")) &&
  Check(W.Hit->GetOwner()==Player,TEXT("hit_owner")) &&
  Check(W.Hit->GetSpatialRevision()==W.SpatialRevision,TEXT("spatial_revision")) &&
  Check(W.Mesh.IsValid(),TEXT("mesh_valid")) &&
  Check(W.Mesh->IsRegistered(),TEXT("mesh_registered")) &&
  Check(!W.Mesh->IsBeingDestroyed(),TEXT("mesh_not_destroying")) &&
  Check(W.Mesh.Get()==Player->GetMesh(),TEXT("mesh_identity")) &&
  Check(W.MeshAsset.IsValid(),TEXT("mesh_asset_valid")) &&
  Check(W.Mesh->GetSkeletalMeshAsset()==W.MeshAsset.Get(),TEXT("mesh_asset_identity")) &&
  Check(W.Mesh->GetAnimInstance()==W.Anim.Get(),TEXT("anim_identity")) &&
  Check(W.Anim.IsValid(),TEXT("anim_valid")) &&
  Check(W.Equipment.IsValid(),TEXT("equipment_valid")) &&
  Check(W.Equipment->GetWeaponRevision()==W.WeaponRevision,TEXT("weapon_revision")) &&
  Check(W.Equipment->GetPresentedItemInSlot(EProject_JEquipmentSlot::Weapon)==W.Item.Get(),TEXT("item_identity")) &&
  Check(W.Item.IsValid(),TEXT("item_valid")) &&
  Check(W.Attack.IsValid(),TEXT("attack_valid")) &&
  Check(Player->GetCombatStyleDefinition()==W.Item->CombatStyleDefinition.Get(),TEXT("gameplay_style_identity")) &&
  Check(!W.MatchingGrants.IsEmpty(),TEXT("matching_grants_present")) &&
  Check(!W.Grant.IsValid() || (W.MatchingGrants.Contains(W.Grant) && W.ASC->FindAbilitySpecFromHandle(W.Grant)!=nullptr),TEXT("observed_execution_grant_present"));
}
void ObserveCanonicalActive(FProject_JCanonicalMeleeWitness& W)
{
 if(!CanonicalCurrent(W)) return;
 FGameplayAbilitySpecHandle ActiveHandle;const UGameplayAbility* ActiveAbility=nullptr;
 for(const FGameplayAbilitySpecHandle Handle:W.MatchingGrants)
 {
  const auto* Spec=W.ASC->FindAbilitySpecFromHandle(Handle);
  const auto* Ability=Spec?Spec->GetPrimaryInstance():nullptr;
  if(!Spec || Spec->PendingRemove || !Spec->IsActive() || !Ability || !Ability->IsActive()) continue;
  if(Ability->GetCurrentAbilitySpecHandle()!=Handle || ActiveHandle.IsValid())
  { W.bViolation=true;W.Error=TEXT("Multiple or mismatched native active melee specs");return; }
  ActiveHandle=Handle;ActiveAbility=Ability;
 }
 if(!ActiveAbility || W.Hit->GetActiveAttackDefinition()!=W.Attack.Get()) return;
 // Select actual native execution, never first/last grant. Normal blend-out may later clear animating ability.
 if(!W.Grant.IsValid())
 {
  if(W.ASC->GetAnimatingAbility()!=ActiveAbility) return;
  const auto* Property=FindFProperty<FStructProperty>(ActiveAbility->GetClass(),TEXT("MeleeHitEventTag"));
  const FGameplayTag SelectedTag=Property && Property->Struct==FGameplayTag::StaticStruct()?*Property->ContainerPtrToValuePtr<FGameplayTag>(ActiveAbility):FGameplayTag();
  if(!SelectedTag.IsValid() || !W.MatchingHitTags.HasTagExact(SelectedTag)) { W.bViolation=true;W.Error=TEXT("Native selected melee has no captured hit-event tag");return; }
  W.Grant=ActiveHandle;W.HitTag=SelectedTag;
 }
 else if(W.Grant!=ActiveHandle) { W.bViolation=true;W.Error=TEXT("Canonical native execution handle changed");return; }
 const int32 Key=ActiveAbility->GetCurrentActivationInfo().GetActivationPredictionKey().Current;
 if(Key<=0 || (W.PredictionKey && W.PredictionKey!=Key)) { W.bViolation=true;W.Error=TEXT("Nonzero exact native GAS prediction key unavailable/changed");return; }
 W.PredictionKey=Key;W.bObservedActive=true;W.bMontage|=W.Anim->IsAnyMontagePlaying();
 if(CanonicalMovingRecipientEnabled() && W.Source->HasAuthority())
 {
  const auto Node=W.Hit->GetActiveAttackNodeTag();
  const auto* World=W.Source->GetWorld();const auto* GS=World->GetGameState();
  const double Now=GS?GS->GetServerWorldTimeSeconds():World->GetTimeSeconds();
  FProject_JAuthoritativeSweepRecord Packet;
  if(Node.IsValid() && W.Hit->FindAuthoritativeTracePacketAtTime(Now,Key,Node,Packet) && Packet.bAtomicPath && Packet.bHitWindowOpen && Packet.Segments.Num()>=2)
  {
   ++W.OpenPackets;W.MaxPacketSegments=FMath::Max(W.MaxPacketSegments,Packet.Segments.Num());W.NodeTag=Node.ToString();
   const auto* Mover=W.Source->GetMover();W.NativeFrame=Mover?Mover->GetLastTimeStep().ServerFrame:INDEX_NONE;
  }
 }
}
bool InitCanonicalWitness(FProject_JCanonicalMeleeWitness& W,AProject_JPlayerCharacter* Player,AProject_JNPCCharacter* Target)
{
 UProject_JEquipmentItemDefinition* Item=nullptr; UProject_JAttackDefinition* Attack=nullptr;
 TArray<FGameplayAbilitySpecHandle> Grants; FGameplayTagContainer HitTags;
 W.Error.Reset();
 if(!IsValid(Player)) { W.Error=TEXT("init.source_invalid");return false; }
 if(!IsValid(Target)) { W.Error=TEXT("init.target_invalid");return false; }
 if(!ReadCanonicalReferences(Player,Item,Attack,Grants,HitTags,&W.Error)) return false;
 auto* ASC=Cast<UProject_JAbilitySystemComponent>(Player->GetAbilitySystemComponent());
 auto* TargetASC=Cast<UProject_JAbilitySystemComponent>(Target->GetAbilitySystemComponent());
 auto* Hit=Player->FindComponentByClass<UProject_JCombatHitValidationComponent>();
 auto* Mesh=Player->GetMesh();
 if(!ASC) { W.Error=TEXT("init.source_asc_missing");return false; }
 if(!TargetASC) { W.Error=TEXT("init.target_asc_missing");return false; }
 if(!Hit) { W.Error=TEXT("init.hit_missing");return false; }
 if(!Mesh) { W.Error=TEXT("init.mesh_missing");return false; }
 if(!Mesh->GetSkeletalMeshAsset()) { W.Error=TEXT("init.mesh_asset_missing");return false; }
 if(!Mesh->GetAnimInstance()) { W.Error=TEXT("init.anim_instance_missing");return false; }
 FProject_JCanonicalMeleeWitness Candidate=W;auto& C=Candidate;
 C.Source=Player;C.Target=Target;C.ASC=ASC;C.TargetASC=TargetASC;C.Hit=Hit;C.Mesh=Mesh;C.MeshAsset=Mesh->GetSkeletalMeshAsset();C.Anim=Mesh->GetAnimInstance();
 C.Equipment=Player->FindComponentByClass<UProject_JEquipmentRuntimeComponent>();C.Item=Item;C.Attack=Attack;C.MatchingGrants=MoveTemp(Grants);C.MatchingHitTags=HitTags;
 C.SourceEpoch=ASC->GetActorInfoRevision();C.TargetEpoch=TargetASC->GetActorInfoRevision();C.WeaponRevision=C.Equipment->GetWeaponRevision();C.SpatialRevision=Hit->GetSpatialRevision();
 C.HealthBefore=C.HealthAfter=TargetASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
 for(const FGameplayAbilitySpecHandle Handle:C.MatchingGrants)
  if(const auto* Spec=ASC->FindAbilitySpecFromHandle(Handle))
   C.MatchingSpecSnapshot.Add(FString::Printf(TEXT("handle=%s class=%s source=%s active=%d pending_remove=%d input=%s"),*Handle.ToString(),
    *GetPathNameSafe(Spec->Ability?Spec->Ability->GetClass():nullptr),*GetPathNameSafe(Spec->SourceObject.Get()),int32(Spec->IsActive()),int32(Spec->PendingRemove),*Spec->GetDynamicSpecSourceTags().ToStringSimple()));
 C.SourceStamp=Attack->MeleeTrajectory.SourceStamp;C.AuthoredKeys=Attack->MeleeTrajectory.Keys.Num();C.AttackTag=Attack->AttackTag.ToString();
 C.SourceName=Player->GetName();C.TargetName=Target->GetName();C.MeshPath=GetPathNameSafe(Mesh->GetSkeletalMeshAsset());C.Role=int32(Player->GetLocalRole());C.bLocal=Player->IsLocallyControlled();
 if(!C.SourceEpoch) { W.Error=TEXT("init.source_epoch_zero");return false; }
 if(!C.TargetEpoch) { W.Error=TEXT("init.target_epoch_zero");return false; }
 if(!FMath::IsFinite(C.HealthBefore)) { W.Error=TEXT("init.target_health_nonfinite");return false; }
 if(C.HealthBefore<=0) { W.Error=TEXT("init.target_health_nonpositive");return false; }
 if(!CanonicalCurrent(C,&W.Error)) return false;
 W=MoveTemp(Candidate);return true;
}
void BindCanonicalWitness(UObject* Owner,FProject_JCanonicalMeleeWitness& W)
{
 const TWeakObjectPtr<UObject> WeakOwner(Owner);auto* Witness=&W;const uint64 Revision=W.Revision;
 W.ContactHandle=W.ASC->AddGameplayEventTagContainerDelegate(W.MatchingHitTags,FGameplayEventTagMulticastDelegate::FDelegate::CreateLambda([WeakOwner,Witness,Revision](FGameplayTag EventTag,const FGameplayEventData* Payload)
 {
  const TStrongObjectPtr<UObject> Pin(WeakOwner.Get());if(!Pin || Witness->Revision!=Revision || Witness->bCleaned) return;
  auto& C=*Witness;ObserveCanonicalActive(C);
  if(C.HitTag.IsValid() && !EventTag.MatchesTag(C.HitTag)) return;
  const auto* Hit=Payload && Payload->TargetData.Num()?Payload->TargetData.Get(0)->GetHitResult():nullptr;
  if(!CanonicalCurrent(C) || C.bEnded || !C.bObservedActive || !Hit || Hit->GetActor()!=C.Target.Get() || Hit->TraceStart.ContainsNaN() || Hit->TraceEnd.ContainsNaN())
  { C.bViolation=true;C.Error=TEXT("Native notify contact outside exact canonical ownership");return; }
  ++C.RawContacts;C.TraceStart=Hit->TraceStart;C.TraceEnd=Hit->TraceEnd;C.NodeTag=C.Hit->GetActiveAttackNodeTag().ToString();
  const auto* World=C.Source->GetWorld();const auto* GS=World->GetGameState();C.ContactTimestamp=GS?GS->GetServerWorldTimeSeconds():World->GetTimeSeconds();
  const auto* Mover=C.Source->GetMover();C.NativeFrame=Mover?Mover->GetLastTimeStep().ServerFrame:INDEX_NONE;
  if(C.Source->HasAuthority())
  {
   FProject_JAuthoritativeSweepRecord Packet;
   if(C.Hit->FindAuthoritativeTracePacketAtTime(C.ContactTimestamp,C.PredictionKey,C.Hit->GetActiveAttackNodeTag(),Packet) && Packet.bAtomicPath && Packet.bHitWindowOpen)
   { ++C.OpenPackets;C.MaxPacketSegments=FMath::Max(C.MaxPacketSegments,Packet.Segments.Num()); }
  }
 }));
 W.EndHandle=W.ASC->OnAbilityEnded.AddLambda([WeakOwner,Witness,Revision](const FAbilityEndedData& End)
 {
  const TStrongObjectPtr<UObject> Pin(WeakOwner.Get());if(!Pin || Witness->Revision!=Revision || Witness->bCleaned || End.AbilitySpecHandle!=Witness->Grant) return;
  auto& C=*Witness;
  if(!CanonicalCurrent(C) || End.bWasCancelled || !C.bObservedActive || C.bEnded) { C.bViolation=true;C.Error=TEXT("Canonical ability ended outside its natural exact grant");return; }
  C.bEnded=true;++C.NaturalEnds;C.EndedAt=FPlatformTime::Seconds();
 });
 W.BoneHandle=W.Mesh->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateLambda([WeakOwner,Witness,Revision]
 {
  const TStrongObjectPtr<UObject> Pin(WeakOwner.Get());if(!Pin || Witness->Revision!=Revision || Witness->bCleaned) return;
  ++Witness->Finalizations;
  if(!Witness->bEnded && Witness->Hit.IsValid() && Witness->Hit->GetActiveAttackDefinition()==Witness->Attack.Get()) ++Witness->AttackFinalizations;
  if(CanonicalMovingRecipientEnabled() && Witness->Role==ROLE_Authority && !Witness->bEnded) ObserveCanonicalActive(*Witness);
 }));
}
void RetireCanonicalWitness(FProject_JCanonicalMeleeWitness& W)
{
 if(W.bCleaned) return;W.bCleaned=true;++W.Revision;
 if(auto* ASC=W.ASC.Get()) { ASC->RemoveGameplayEventTagContainerDelegate(W.MatchingHitTags,W.ContactHandle);ASC->OnAbilityEnded.Remove(W.EndHandle); }
 if(auto* Mesh=W.Mesh.Get()) Mesh->UnregisterOnBoneTransformsFinalizedDelegate(W.BoneHandle);
 if(auto* ASC=W.TargetASC.Get()) ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(W.EffectHandle);
 W.ContactHandle.Reset();W.EndHandle.Reset();W.BoneHandle.Reset();W.EffectHandle.Reset();
}
FString OutputRoot(); // Existing native fixture output helper, defined below.
bool WriteCanonicalWitness(const FProject_JCanonicalMeleeWitness& W,const TCHAR* Filename,bool Success,bool Cleanup)
{
 auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("schema"),1);J->SetBoolField(TEXT("success"),Success && !W.bViolation);J->SetStringField(TEXT("failure"),W.Error);
 J->SetStringField(TEXT("scope"),TEXT("real autonomous input/native canonical notify and server gameplay; RPC invocation/decision separately requires native trace or exact verbose evidence"));
 J->SetStringField(TEXT("source"),W.SourceName);J->SetStringField(TEXT("target"),W.TargetName);J->SetStringField(TEXT("source_mesh"),W.MeshPath);
 J->SetStringField(TEXT("equipment"),CanonicalEquipPath);J->SetStringField(TEXT("style"),CanonicalStylePath);J->SetStringField(TEXT("combo"),CanonicalComboPath);J->SetStringField(TEXT("attack"),CanonicalAttackPath);
 J->SetNumberField(TEXT("role"),W.Role);J->SetBoolField(TEXT("locally_controlled"),W.bLocal);J->SetStringField(TEXT("source_epoch"),LexToString(W.SourceEpoch));J->SetStringField(TEXT("target_epoch"),LexToString(W.TargetEpoch));J->SetStringField(TEXT("weapon_revision"),LexToString(W.WeaponRevision));
 J->SetStringField(TEXT("trajectory_stamp"),W.SourceStamp);J->SetNumberField(TEXT("authored_keys"),W.AuthoredKeys);J->SetStringField(TEXT("attack_tag"),W.AttackTag);J->SetStringField(TEXT("node_tag"),W.NodeTag);
 J->SetStringField(TEXT("actual_handle"),W.Grant.ToString());J->SetStringField(TEXT("actual_hit_event_tag"),W.HitTag.ToString());J->SetNumberField(TEXT("matching_grant_count"),W.MatchingGrants.Num());
 TArray<TSharedPtr<FJsonValue>> Specs,Sources;
 for(const FString& Row:W.MatchingSpecSnapshot) Specs.Add(MakeShared<FJsonValueString>(Row));
 for(const FString& Row:W.OwnedGrantSources) Sources.Add(MakeShared<FJsonValueString>(Row));
 J->SetArrayField(TEXT("matching_specs_at_admission"),Specs);J->SetArrayField(TEXT("owned_grant_sources"),Sources);
 J->SetBoolField(TEXT("owned_sources_initially_absent"),W.bOwnedSourcesInitiallyAbsent);J->SetBoolField(TEXT("owned_sources_retired"),W.bOwnedSourcesRetired);
 J->SetNumberField(TEXT("prediction_key"),W.PredictionKey);J->SetNumberField(TEXT("native_frame"),W.NativeFrame);J->SetBoolField(TEXT("native_active"),W.bObservedActive);J->SetBoolField(TEXT("montage"),W.bMontage);
 J->SetNumberField(TEXT("raw_contacts"),W.RawContacts);J->SetNumberField(TEXT("effects"),W.Effects);J->SetNumberField(TEXT("natural_ends"),W.NaturalEnds);J->SetNumberField(TEXT("bone_finalizations"),W.Finalizations);J->SetNumberField(TEXT("attack_finalizations"),W.AttackFinalizations);
 J->SetNumberField(TEXT("open_atomic_packet_observations"),W.OpenPackets);J->SetNumberField(TEXT("max_packet_segments"),W.MaxPacketSegments);J->SetNumberField(TEXT("health_before"),W.HealthBefore);J->SetNumberField(TEXT("health_after"),W.HealthAfter);
 J->SetNumberField(TEXT("contact_server_time"),W.ContactTimestamp);J->SetStringField(TEXT("contact_start"),W.TraceStart.ToString());J->SetStringField(TEXT("contact_end"),W.TraceEnd.ToString());J->SetBoolField(TEXT("delegates_retired"),W.bCleaned);J->SetBoolField(TEXT("cleanup_passed"),Cleanup);
 J->SetBoolField(TEXT("rpc_invocation_inferred_from_effect"),false);
 if(CanonicalMovingRecipientEnabled()) J->SetBoolField(TEXT("packet_observed_independently_of_contact"),W.Role==ROLE_Authority && W.OpenPackets>0);
 FString Json;return FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Json)) && FFileHelper::SaveStringToFile(Json,*(OutputRoot()/Filename));
}

bool ReadCombatTags(AProject_JPlayerCharacter* Player, FGameplayTag& R, FGameplayTag& Target)
{
	const auto* Style = Player ? Player->GetCombatStyleDefinition() : nullptr;
	const auto* Combo = Style ? Style->ComboDefinition.Get() : nullptr;
	const auto& Tags = FProject_JGameplayTags::Get();
	const auto* Node = Combo ? Combo->FindStartNode(Tags.InputTag_Skill_R, FGameplayTagContainer()) : nullptr;
	const auto* Edge = Node ? Combo->FindTransition(*Node, Tags.InputTag_Weapon_RMB, FGameplayTagContainer()) : nullptr;
	const auto* Next = Edge ? Combo->FindNode(Edge->TargetNodeTag) : nullptr;
	if (!Node || !Node->AttackDefinition || !Next || !Next->AttackDefinition) { return false; }
	R = Node->AttackDefinition->AttackTag; Target = Next->AttackDefinition->AttackTag;
	return R.IsValid() && Target.IsValid() && R != Target;
}
void RecordCombat(AProject_JPlayerCharacter* Player, int32 Trial, TSet<FName>& RSeen, TSet<FName>& TargetSeen,
	TSet<FName>& EndSeen, FString* Rows = nullptr)
{
	const auto* State = Player ? Player->GetPlayerState<AProject_JPlayerState>() : nullptr;
	const auto* Presentation = Player ? Player->FindComponentByClass<UProject_JCombatPresentationComponent>() : nullptr;
	FGameplayTag R, Target;
	if (!State || !Presentation || !ReadCombatTags(Player, R, Target)) { return; }
	const FName Id = State->GetPublicClassId();
	const auto Attack = Presentation->GetActiveAttackTag();
	if (Attack == R) { RSeen.Add(Id); }
	if (Attack == Target && RSeen.Contains(Id)) { TargetSeen.Add(Id); }
	if (!Attack.IsValid() && TargetSeen.Contains(Id)) { EndSeen.Add(Id); }
	if (Rows) { *Rows += FString::Printf(TEXT("%d,%s,%d,%s,%d,%d,%d\n"), Trial, *Id.ToString(), int32(Player->GetLocalRole()),
		*Attack.ToString(), RSeen.Contains(Id), TargetSeen.Contains(Id), EndSeen.Contains(Id)); }
}
void EnsureAnimationFloor(UWorld* World)
{
	if (!AnimationEnabled() || !World || !World->IsGameWorld()) { return; }
	for (TActorIterator<AActor> It(World); It; ++It) { if (It->ActorHasTag(TEXT("ProjectJAnimationFixtureFloor"))) { return; } }
	// Entry has no ground. A real collision floor on each process prevents the
	// autonomous pawn from legitimately generating FallOff RPCs during setup.
	FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
	auto* Floor = World->SpawnActor<AActor>(Params);
	if (!Floor) { return; }
	Floor->Tags.Add(TEXT("ProjectJAnimationFixtureFloor"));
	auto* Box = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Box); Floor->AddInstanceComponent(Box);
	Box->SetBoxExtent(FVector(50000, 50000, 100));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionObjectType(ECC_WorldStatic); Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent(); Floor->SetActorLocation(FVector(0, 0, -100));
}
FProject_JReplicatedAnimEventState ReadAnimationEvents(AProject_JPlayerCharacter* Player)
{
	auto* Events = Player ? Player->FindComponentByClass<UProject_JReplicatedAnimEventComponent>() : nullptr;
	auto* Property = Events ? FindFProperty<FStructProperty>(Events->GetClass(), TEXT("ReplicatedAnimEvents")) : nullptr;
	return Property ? *Property->ContainerPtrToValuePtr<FProject_JReplicatedAnimEventState>(Events) : FProject_JReplicatedAnimEventState();
}
int32 FixtureNPCs() { int32 N = 100; FParse::Value(FCommandLine::Get(), TEXT("ProjectJFixtureNPCs="), N); return FMath::Clamp(N / 2 * 2, 100, 2048); }
FString OutputRoot()
{
	FString Root; FParse::Value(FCommandLine::Get(), TEXT("ProjectJDOutput="), Root);
	return Root.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("Validation/GroupD_20260910/Network") : Root;
}
// Optional, single GT observation. No load request, polling, movement or world writer.
// Cell rows/expensive state+bound reads are capped; reaching the cap is incomplete.
// Read one already selected runtime cell, including its persistent HLOD association.
// No level/actor load or ownership survives this synchronous call.
TSharedRef<FJsonObject> ReadSelectedHLODCellSnapshot(UWorld* World, const UWorldPartitionRuntimeCell* Cell)
{
    check(IsInGameThread());
    auto HLODRead = MakeShared<FJsonObject>();
    HLODRead->SetBoolField(TEXT("requested"), true);
    const auto* Partition = World ? World->GetWorldPartition() : nullptr;
    const bool bCurrent = IsValid(Cell) && World && !World->bIsTearingDown &&
        Cell->GetOuterWorld() == World && Partition && Partition->IsInitialized() && Partition->IsStreamingEnabled();
    HLODRead->SetBoolField(TEXT("source_cell_found"), bCurrent);
    HLODRead->SetBoolField(TEXT("bounded_read_complete"), false);
    HLODRead->SetStringField(TEXT("selected_source_name"), bCurrent ? Cell->GetDebugName() : TEXT("None"));
    HLODRead->SetStringField(TEXT("scope"), TEXT("One existing runtime cell; at most8 interfaces and64 component inspections plus1 overflow visit. No load request, forced visibility or GPU/pixel-ready claim."));
    if (!bCurrent) return HLODRead;
    HLODRead->SetBoolField(TEXT("source_cell_found"), true);
    HLODRead->SetStringField(TEXT("source_cell_guid"), Cell->GetGuid().ToString());
    HLODRead->SetStringField(TEXT("source_cell_object"), Cell->GetPathName());
    HLODRead->SetNumberField(TEXT("source_cell_state"), int32(Cell->GetCurrentState()));
    HLODRead->SetBoolField(TEXT("source_cell_visible"), Cell->IsVisible());
    // Coarse Loaded/!IsVisible also includes MakingInvisible. It is not an empty-pixel test.
    const auto* LevelCell = Cast<UWorldPartitionRuntimeLevelStreamingCell>(Cell);
    const auto* Streaming = LevelCell ? LevelCell->GetLevelStreaming() : nullptr;
    HLODRead->SetStringField(TEXT("streaming_level_state"), Streaming ? EnumToString(Streaming->GetLevelStreamingState()) : TEXT("None"));
    const auto* SourceLevel = Cell->GetLevel();
    HLODRead->SetBoolField(TEXT("source_level_present"), SourceLevel != nullptr);
    HLODRead->SetBoolField(TEXT("source_level_visible"), SourceLevel && SourceLevel->bIsVisible);
    HLODRead->SetBoolField(TEXT("source_level_removing"), SourceLevel && SourceLevel->bIsBeingRemoved);
    HLODRead->SetBoolField(TEXT("source_level_full_visibility_required"), SourceLevel && SourceLevel->bRequireFullVisibilityToRender);
    auto* HLODs = World->GetSubsystem<UWorldPartitionHLODRuntimeSubsystem>();
    HLODRead->SetBoolField(TEXT("subsystem_available"), HLODs != nullptr);
    HLODRead->SetBoolField(TEXT("hlod_enabled"), UWorldPartitionHLODRuntimeSubsystem::IsHLODEnabled());
    bool bComplete = HLODs != nullptr;
    TArray<TSharedPtr<FJsonValue>> Proxies;
    if (HLODs)
    {
        const auto& Objects = HLODs->GetHLODObjectsForCell(Cell);
        HLODRead->SetNumberField(TEXT("loaded_associated_objects"), Objects.Num());
        bComplete &= Objects.Num() <= 8;
        for (int32 Index = 0; Index < FMath::Min(Objects.Num(), 8); ++Index)
        {
            const auto* Object = Objects[Index];
            auto Item = MakeShared<FJsonObject>();
            // A valid interface need not be an Actor or even have a UObject:
            // FastGeo's FFastGeoHLOD returns nullptr from GetUObject().
            auto* Level = Object ? Object->GetHLODLevel() : nullptr;
            const bool bLiveInterface = Object && IsValid(Level) && Level->GetWorld() == World;
            Item->SetBoolField(TEXT("live_interface"), bLiveInterface);
            bComplete &= bLiveInterface;
            if (bLiveInterface)
            {
                // Local read pins only; no pointer escapes to a later unload observation.
                const TStrongObjectPtr<ULevel> KeepLevel(Level);
                auto* UObjectValue = Object->GetUObject();
                const bool bLiveUObject = IsValid(UObjectValue) &&
                    !UObjectValue->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed);
                const TStrongObjectPtr<UObject> KeepObject(bLiveUObject ? UObjectValue : nullptr);
                bComplete &= !UObjectValue || bLiveUObject;
                Item->SetStringField(TEXT("interface_name"), Object->GetHLODNameOrLabel());
                Item->SetStringField(TEXT("level"), Level->GetPathName());
                Item->SetBoolField(TEXT("has_uobject"), UObjectValue != nullptr);
                Item->SetBoolField(TEXT("live_uobject"), bLiveUObject);
                Item->SetStringField(TEXT("object"), bLiveUObject ? UObjectValue->GetPathName() : TEXT("None"));
                Item->SetStringField(TEXT("object_class"), bLiveUObject ? UObjectValue->GetClass()->GetPathName() : TEXT("None"));
                Item->SetStringField(TEXT("object_package"), bLiveUObject ? UObjectValue->GetOutermost()->GetName() : TEXT("None"));
                Item->SetStringField(TEXT("representation"), UObjectValue ? TEXT("UObjectInterface") : TEXT("NonUObjectInterface"));
                Item->SetStringField(TEXT("raw_source_cell_guid"), Object->GetSourceCellGuid().ToString());
                Item->SetBoolField(TEXT("raw_source_guid_matches"), Object->GetSourceCellGuid() == Cell->GetGuid());
                Item->SetBoolField(TEXT("standalone"), Object->IsStandalone());
                Item->SetBoolField(TEXT("custom"), Object->IsCustomHLOD());
                Item->SetBoolField(TEXT("requires_warmup"), Object->DoesRequireWarmup());
                const auto* Actor = bLiveUObject ? Cast<AActor>(UObjectValue) : nullptr;
                const auto* DirectComponent = bLiveUObject ? Cast<USceneComponent>(UObjectValue) : nullptr;
                const bool bLiveActor = Actor && !Actor->IsActorBeingDestroyed() && Actor->GetWorld() == World;
                const bool bReadableComponent = DirectComponent && !DirectComponent->IsBeingDestroyed() && DirectComponent->GetWorld() == World;
                bComplete &= (!Actor || bLiveActor) && (!DirectComponent || bReadableComponent);
                Item->SetBoolField(TEXT("live_hlod_actor"), bLiveActor && Actor->IsA<AWorldPartitionHLOD>());
                Item->SetStringField(TEXT("component_inspection"), bLiveActor ? TEXT("ActorOwnedSet") :
                    bReadableComponent ? TEXT("DirectSceneComponent") : TEXT("NotExposedByGenericInterface"));
                Item->SetBoolField(TEXT("component_metadata_supported"), bLiveActor || bReadableComponent);
                TArray<TSharedPtr<FJsonValue>> Meshes;
                int32 ComponentsVisited = 0;
                bool bComponentsComplete = true;
                const auto ReadMesh = [&](const USceneComponent* Component)
                {
                    const auto* Mesh = Cast<UStaticMeshComponent>(Component);
                    if (!Mesh) return;
                    auto Part = MakeShared<FJsonObject>();
                    Part->SetStringField(TEXT("component"), Mesh->GetName());
                    Part->SetStringField(TEXT("mesh"), GetPathNameSafe(Mesh->GetStaticMesh()));
                    Part->SetBoolField(TEXT("registered"), Mesh->IsRegistered());
                    Part->SetBoolField(TEXT("visible_flag"), Mesh->GetVisibleFlag());
                    Part->SetBoolField(TEXT("hidden_in_game"), Mesh->bHiddenInGame);
                    Part->SetBoolField(TEXT("recently_rendered"), Mesh->WasRecentlyRendered(.2f));
                    const auto* Instances = Cast<UInstancedStaticMeshComponent>(Mesh);
                    Part->SetNumberField(TEXT("instances"), Instances ? Instances->GetInstanceCount() : 0);
                    Meshes.Add(MakeShared<FJsonValueObject>(Part));
                };
                if (bLiveActor)
                {
                    // No callback may mutate ownership here. Break after one overflow
                    // sentinel; unlike ForEachComponent's void callback, this stops traversal.
                    for (const UActorComponent* Component : Actor->GetComponents())
                    {
                        if (++ComponentsVisited > 64) { bComponentsComplete = false; break; }
                        ReadMesh(Cast<USceneComponent>(Component));
                    }
                }
                else if (bReadableComponent)
                { ++ComponentsVisited; ReadMesh(DirectComponent); }
                bComplete &= bComponentsComplete;
                Item->SetNumberField(TEXT("component_visits"), ComponentsVisited);
                Item->SetBoolField(TEXT("component_limit_reached"), !bComponentsComplete);
                Item->SetArrayField(TEXT("static_mesh_components"), Meshes);
            }
            Proxies.Add(MakeShared<FJsonValueObject>(Item));
        }
    }
    HLODRead->SetBoolField(TEXT("bounded_read_complete"), bComplete);
    HLODRead->SetArrayField(TEXT("proxies"), Proxies);
    return HLODRead;
}

bool WriteWorldReadinessSnapshot(UWorld* World, const TCHAR* Filename = TEXT("world-readiness.json"),
    TWeakObjectPtr<const UWorldPartitionRuntimeCell>* OutSelectedCell = nullptr)
{
    check(IsInGameThread());
    if (OutSelectedCell) OutSelectedCell->Reset();
    constexpr int32 MaxCells = 128;
    // Optional read of one known source cell only. The normal CPU readiness
    // proof and its spatial-cell filter remain unchanged.
    const bool bReadHLOD = FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditHLODRead"));
    FString HLODSourceName(TEXT("L_MainWorld_MainGrid_L0_X-4_Y-4_Z0"));
    if (bReadHLOD) FParse::Value(FCommandLine::Get(), TEXT("ProjectJAuditHLODSourceCell="), HLODSourceName);
    auto HLODRead = MakeShared<FJsonObject>();
    HLODRead->SetBoolField(TEXT("requested"), bReadHLOD);
    HLODRead->SetBoolField(TEXT("source_cell_found"), false);
    HLODRead->SetStringField(TEXT("selected_source_name"), HLODSourceName);
    HLODRead->SetStringField(TEXT("scope"), TEXT("One queried source cell and at most8 loaded matching HLOD objects/64 components per object; no actor/asset loading, unload handover or pixel/GPU-ready claim. Non-spatial proxy association is queried directly through HLODRuntimeSubsystem."));
    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("scope"), TEXT("One current-source GT snapshot; no traversal/portal, Nav/GPU-ready or HLOD visual/cook certification."));
    Report->SetStringField(TEXT("map"), World->GetOutermost()->GetName());
    auto* PC = World->GetFirstPlayerController();
    APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    auto* Provider = PC ? PC->FindComponentByClass<UProject_JWorldStreamingComponent>() : nullptr;
    auto* Partition = World->GetWorldPartition();
    auto* Subsystem = World->GetSubsystem<UWorldPartitionSubsystem>();
    const bool bPartitionReady = Partition && Partition->IsInitialized() && Partition->IsStreamingEnabled();
    FWorldPartitionStreamingSource Provided;
    const auto GeometryValid = [](const FWorldPartitionStreamingSource& Source)
    {
        if (Source.Name.IsNone() || Source.Location.ContainsNaN() || Source.Rotation.ContainsNaN() ||
            Source.Shapes.IsEmpty() || Source.Shapes.Num() > UProject_JWorldDefinition::MaxPreparationShapes) return false;
        for (const auto& Shape : Source.Shapes)
            if (Shape.bUseGridLoadingRange || !FMath::IsFinite(Shape.Radius) || Shape.Radius <= 0 ||
                Shape.Location.ContainsNaN() || Shape.Rotation.ContainsNaN() ||
                !FMath::IsFinite(Shape.SectorAngle) || Shape.SectorAngle < 0 || Shape.SectorAngle > 360) return false;
        return true;
    };
    const bool bProviderValid = IsValid(Pawn) && !Pawn->IsActorBeingDestroyed() && IsValid(Provider) &&
        Provider->GetStreamingSource(Provided) && GeometryValid(Provided);
    FWorldPartitionStreamingSource Consumed;
    bool bConsumed = false;
    if (bProviderValid && bPartitionReady)
        for (const auto& Source : Partition->GetStreamingSources())
            if (Source.Name == Provided.Name) { Consumed = Source; bConsumed = GeometryValid(Consumed); break; }
    Report->SetBoolField(TEXT("partition_initialized"), Partition && Partition->IsInitialized());
    Report->SetBoolField(TEXT("partition_streaming_enabled"), Partition && Partition->IsStreamingEnabled());
    Report->SetBoolField(TEXT("provider_geometry_valid"), bProviderValid);
    Report->SetBoolField(TEXT("provider_identity_consumed"), bConsumed);
    Report->SetStringField(TEXT("provider_name"), Provided.Name.ToString());
    Report->SetNumberField(TEXT("provider_shape_count"), Provided.Shapes.Num());
    // The engine returns true for missing/empty providers. Do not query it vacuously.
    const bool bCompletionQueried = bProviderValid && bConsumed && bPartitionReady && Subsystem;
    const bool bProviderComplete = bCompletionQueried && Subsystem->IsStreamingCompleted(Provider);
    Report->SetBoolField(TEXT("provider_completion_queried"), bCompletionQueried);
    Report->SetBoolField(TEXT("provider_streaming_complete"), bProviderComplete);
    TArray<TSharedPtr<FJsonValue>> Cells;
    TSet<const UWorldPartitionRuntimeCell*> Seen;
    int32 CellCallbacks = 0;
    bool bLimitReached = false, bBoundsValid = true, bCellsQueried = false;
    const auto VectorValues = [](const FVector& Value)
    {
        return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(Value.X),
            MakeShared<FJsonValueNumber>(Value.Y), MakeShared<FJsonValueNumber>(Value.Z)};
    };
    // StreamingSource::ToString is engine-internal (not exported in installed
    // UE 5.8). Serialize its public values instead of adding an engine patch.
    if (bConsumed)
    {
        Report->SetStringField(TEXT("consumed_source_name"), Consumed.Name.ToString());
        Report->SetArrayField(TEXT("consumed_position"), VectorValues(Consumed.Location));
        Report->SetNumberField(TEXT("consumed_shape_count"), Consumed.Shapes.Num());
        Report->SetNumberField(TEXT("consumed_target_state"), int32(Consumed.TargetState));
        Report->SetNumberField(TEXT("consumed_priority"), int32(Consumed.Priority));
    }
    if (bProviderValid)
    {
        Report->SetArrayField(TEXT("provider_position"), VectorValues(Provided.Location));
        TArray<TSharedPtr<FJsonValue>> Shapes;
        for (const auto& Shape : Provided.Shapes)
        {
            auto Row = MakeShared<FJsonObject>();
            Row->SetArrayField(TEXT("relative_location"), VectorValues(Shape.Location));
            Row->SetNumberField(TEXT("radius"), Shape.Radius);
            Row->SetStringField(TEXT("relative_rotation"), Shape.Rotation.ToString());
            Shapes.Add(MakeShared<FJsonValueObject>(Row));
        }
        Report->SetArrayField(TEXT("provider_shapes"), Shapes);
    }
    if (bCompletionQueried && Partition->RuntimeHash && Partition->GetDataLayerManager())
    {
        bCellsQueried = true;
        FWorldPartitionStreamingQuerySource Query;
        const FTransform WorldToPartition = Partition->GetInstanceTransform().Inverse();
        Query.Location = WorldToPartition.TransformPosition(Provided.Location);
        Query.Rotation = WorldToPartition.TransformRotation(Provided.Rotation.Quaternion()).Rotator();
        Query.TargetBehavior = Provided.TargetBehavior; Query.TargetGrids = Provided.TargetGrids;
        Query.Shapes = Provided.Shapes; Query.bUseGridLoadingRange = true;
        Query.DataLayers = Partition->GetDataLayerManager()->GetEffectiveActiveDataLayerNames().Array();
        if (Provided.TargetState == EStreamingSourceTargetState::Loaded)
            Query.DataLayers.Append(Partition->GetDataLayerManager()->GetEffectiveLoadedDataLayerNames().Array());
        Partition->RuntimeHash->ForEachStreamingCellsQuery(Query, [&](const UWorldPartitionRuntimeCell* Cell)
        {
            // HashSet may call again for another shape/grid after false. Do no
            // further state/bounds reads or allocation after this strict row cap.
            if (bLimitReached) return false;
            ++CellCallbacks;
            if (Cell && Cell->IsSpatiallyLoaded() && !Seen.Contains(Cell))
            {
                Seen.Add(Cell);
                const FBox Bounds = Cell->GetStreamingBounds();
                const bool bFiniteBounds = Bounds.IsValid && !Bounds.Min.ContainsNaN() && !Bounds.Max.ContainsNaN();
                bBoundsValid &= bFiniteBounds;
                auto Row = MakeShared<FJsonObject>();
                Row->SetStringField(TEXT("name"), Cell->GetDebugName());
                if (bReadHLOD && Cell->GetDebugName() == HLODSourceName)
                {
                    HLODRead = ReadSelectedHLODCellSnapshot(World, Cell);
                    if (OutSelectedCell) *OutSelectedCell = Cell;
                }
                Row->SetNumberField(TEXT("state"), int32(Cell->GetCurrentState()));
                Row->SetBoolField(TEXT("hlod"), Cell->GetIsHLOD());
                Row->SetBoolField(TEXT("visible"), Cell->IsVisible());
                Row->SetBoolField(TEXT("bounds_finite"), bFiniteBounds);
                if (bFiniteBounds) { Row->SetArrayField(TEXT("bounds_min"), VectorValues(Bounds.Min)); Row->SetArrayField(TEXT("bounds_max"), VectorValues(Bounds.Max)); }
                Cells.Add(MakeShared<FJsonValueObject>(Row));
            }
            // Conservative at exactly128: completion of this query is not proven.
            if (CellCallbacks >= MaxCells) { bLimitReached = true; return false; }
            return true;
        });
        Report->SetStringField(TEXT("partition_instance_transform"), Partition->GetInstanceTransform().ToString());
    }
    const bool bCellSnapshotComplete = bCellsQueried && !bLimitReached && bBoundsValid && !Cells.IsEmpty();
    Report->SetBoolField(TEXT("cells_queried"), bCellsQueried);
    Report->SetNumberField(TEXT("cell_visit_limit"), MaxCells);
    Report->SetNumberField(TEXT("cell_callbacks_inspected"), CellCallbacks);
    Report->SetBoolField(TEXT("cell_limit_reached"), bLimitReached);
    Report->SetBoolField(TEXT("cell_snapshot_complete"), bCellSnapshotComplete);
    Report->SetStringField(TEXT("cell_bounds_space"), TEXT("Partition-local; active DataLayers and current provider geometry transformed as in the engine completion query. Non-spatial cells are not recorded."));
    Report->SetStringField(TEXT("cell_query_limit"), TEXT("At most128 callbacks inspect state/bounds; engine may test more indexed candidates across shapes/grids after callback false."));
    Report->SetArrayField(TEXT("cells"), Cells);
    if (bReadHLOD) Report->SetObjectField(TEXT("selected_hlod_read"), HLODRead);
    auto* Capsule = Project_J::Movement::FindCapsule(Pawn);
    float Radius = 0, HalfHeight = 0;
    if (Capsule) Capsule->GetScaledCapsuleSize(Radius, HalfHeight);
    const bool bCapsuleValid = IsValid(Pawn) && !Pawn->IsActorBeingDestroyed() && IsValid(Capsule) && !Capsule->IsBeingDestroyed() && Capsule->IsRegistered() && Capsule->IsQueryCollisionEnabled() &&
        FMath::IsFinite(Radius) && FMath::IsFinite(HalfHeight) && Radius > 0 && HalfHeight >= Radius &&
        !Capsule->GetComponentLocation().ContainsNaN() && !Capsule->GetComponentQuat().ContainsNaN() && Capsule->GetComponentQuat().IsNormalized();
    bool bGroundFound = false, bWalkable = false, bCapsuleClear = false;
    Report->SetBoolField(TEXT("capsule_query_valid"), bCapsuleValid);
    if (bCapsuleValid)
    {
        const FVector Origin = Capsule->GetComponentLocation(), Up = Capsule->GetUpVector();
        FCollisionQueryParams Params(SCENE_QUERY_STAT(ProjectJWorldReadiness), false, Pawn);
        const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
        FCollisionResponseParams GroundResponses = Responses;
        GroundResponses.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
        FHitResult Ground;
        bGroundFound = World->LineTraceSingleByChannel(Ground, Origin, Origin - Up * (HalfHeight + Radius),
            Capsule->GetCollisionObjectType(), Params, GroundResponses) && Ground.bBlockingHit && !Ground.bStartPenetrating &&
            !Ground.ImpactPoint.ContainsNaN() && !Ground.ImpactNormal.ContainsNaN();
        bWalkable = bGroundFound && Project_J::Movement::IsWalkable(Pawn, Ground);
        bCapsuleClear = !World->OverlapBlockingTestByChannel(Origin, Capsule->GetComponentQuat(), Capsule->GetCollisionObjectType(),
            FCollisionShape::MakeCapsule(Radius, HalfHeight), Params, Responses);
        Report->SetNumberField(TEXT("capsule_radius_cm"), Radius);
        Report->SetNumberField(TEXT("capsule_half_height_cm"), HalfHeight);
        Report->SetArrayField(TEXT("capsule_position"), VectorValues(Origin));
        Report->SetBoolField(TEXT("movement_grounded"), Project_J::Movement::ReadPhysicalFrame(Pawn).bGrounded);
        if (bGroundFound)
        {
            Report->SetArrayField(TEXT("ground_point"), VectorValues(Ground.ImpactPoint));
            Report->SetNumberField(TEXT("ground_gap_cm"), FVector::DotProduct(Origin - Ground.ImpactPoint, Up) - HalfHeight);
        }
    }
    Report->SetBoolField(TEXT("blocking_ground_found_within_one_radius_below_capsule"), bGroundFound);
    Report->SetBoolField(TEXT("ground_walkable_by_current_movement_rules"), bWalkable);
    Report->SetBoolField(TEXT("current_capsule_clear"), bCapsuleClear);
    Report->SetStringField(TEXT("unmeasured"), TEXT("GPU/texture/Nanite/PSO readiness, Nav validity, traversal and HLOD visual transitions."));
    const bool bReady = bProviderComplete && bCellSnapshotComplete && bCapsuleValid && bGroundFound && bWalkable && bCapsuleClear;
    Report->SetBoolField(TEXT("snapshot_ready"), bReady);
    FString Json; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    IFileManager::Get().MakeDirectory(*OutputRoot(), true);
    const bool bSaved = FFileHelper::SaveStringToFile(Json, *(OutputRoot() / Filename));
    UE_LOG(LogProjectJDNetwork, Display, TEXT("WORLD_READINESS_SNAPSHOT ready=%d saved=%d source=%d consumed=%d wpComplete=%d cells=%d capped=%d ground=%d capsuleClear=%d"),
        bReady, bSaved, bProviderValid, bConsumed, bProviderComplete, Cells.Num(), bLimitReached, bGroundFound, bCapsuleClear);
    return bReady && bSaved;
}

void WriteResult(const TCHAR* Name, bool bSuccess, const FString& Reason, int32 Slot = INDEX_NONE)
{
	auto Object = MakeShared<FJsonObject>(); Object->SetBoolField(TEXT("success"), bSuccess);
	Object->SetStringField(TEXT("reason"), Reason); Object->SetNumberField(TEXT("slot"), Slot);
	FString Json; FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Json));
	IFileManager::Get().MakeDirectory(*OutputRoot(), true);
	FFileHelper::SaveStringToFile(Json, *(OutputRoot() / Name));
}
#include "Testing/Project_JNetworkLoadFixture.WorldArrival.inl"
}

void UProject_JNetworkFixtureLifetime::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const bool bAudit = !UE_BUILD_SHIPPING && FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditNetwork"));
    const bool bWorldSmoke = !UE_BUILD_SHIPPING && FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditWorldSmoke"));
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditWorldResidency")))
    {
        FString ArrivalRole;
        const bool bRole = FParse::Value(FCommandLine::Get(), TEXT("ProjectJAuditArrivalRole="), ArrivalRole);
        if (!bWorldSmoke || !FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditPreparedArrival")) || bAudit || bRole
            || FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditPreparedOwnerArrival"))
            || FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditPreparedOwnerOverlap"))
            || FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditPreparedOwnerBackpressure"))
            || FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditHLODRead"))
            || FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditHLODHandover"))
            || FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditHLODRHICapture"))
            || (GetWorld() && GetWorld()->GetNetMode() != NM_Standalone))
        {
            WriteResult(TEXT("world-residency.json"),false,TEXT("WorldResidency requires standalone WorldSmoke+PreparedArrival only; remote/HLOD/network variants are incompatible"));
            FPlatformMisc::RequestExitWithStatus(false,1); return;
        }
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditPreparedOwnerBackpressure"))
        && (!bWorldSmoke || !FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditPreparedOwnerArrival"))
            || !FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditPreparedOwnerOverlap"))))
    {
        WriteResult(TEXT("world-arrival-backpressure.json"),false,TEXT("Backpressure requires explicit WorldSmoke, PreparedOwnerArrival and PreparedOwnerOverlap flags"));
        FPlatformMisc::RequestExitWithStatus(false,1); return;
    }
#endif
    if (!Enabled() && !bAudit && !bWorldSmoke) { return; }
    if (bWorldSmoke)
    {
#if !UE_BUILD_SHIPPING
        if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditPreparedOwnerArrival")))
        {
            FString Role; FParse::Value(FCommandLine::Get(), TEXT("ProjectJAuditArrivalRole="), Role);
            if (Role != TEXT("Server") && Role != TEXT("Client"))
            {
                WriteResult(TEXT("world-arrival.json"), false, TEXT("Explicit Server/Client remote arrival role is required"));
                FPlatformMisc::RequestExitWithStatus(false, 1); return;
            }
            TWeakObjectPtr<UProject_JNetworkFixtureLifetime> WeakThis(this);
            if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditPreparedOwnerOverlap")))
            {
                const auto Pair = MakeShared<FProjectJPreparedOwnerOverlapProbe>(Role == TEXT("Server"));
                Timeout = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, Pair](float)
                {
                    auto* Self = WeakThis.Get(); return Pair->Tick(Self ? Self->GetWorld() : nullptr);
                }), 0.0f);
                return;
            }
            const auto Probe = MakeShared<FProjectJPreparedOwnerProbe>(Role == TEXT("Server"));
            Timeout = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, Probe](float)
            {
                auto* Self = WeakThis.Get(); return Probe->Tick(Self ? Self->GetWorld() : nullptr);
            }), 0.0f); // Diagnostic observations each engine frame; production cadence is unchanged.
            return;
        }
        if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditPreparedArrival")))
        {
            TWeakObjectPtr<UProject_JNetworkFixtureLifetime> WeakThis(this);
            const auto Probe = MakeShared<FProjectJPreparedWorldProbe>();
            Timeout = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, Probe](float)
            {
                auto* Self = WeakThis.Get();
                return Probe->Tick(Self ? Self->GetWorld() : nullptr);
            }), Probe->bObserveResidency ? 0.0f : .1f);
            return;
        }
#endif
        const double Started = FPlatformTime::Seconds();
        TWeakObjectPtr<UProject_JNetworkFixtureLifetime> WeakThis(this);
        Timeout = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, Started](float)
        {
            auto* Self = WeakThis.Get();
            UWorld* World = Self ? Self->GetWorld() : nullptr;
            const double Elapsed = FPlatformTime::Seconds() - Started;
            if (Elapsed < 3.0) { return true; }
            FString ExpectedMap, ExpectedDefinition;
            int32 ExpectedPlaces = 0;
            FParse::Value(FCommandLine::Get(), TEXT("ProjectJAuditWorldMap="), ExpectedMap);
            FParse::Value(FCommandLine::Get(), TEXT("ProjectJAuditWorldDefinition="), ExpectedDefinition);
            FParse::Value(FCommandLine::Get(), TEXT("ProjectJAuditWorldPlaces="), ExpectedPlaces);
            if (!World || !World->HasBegunPlay())
            {
                if (Elapsed < 120.0) { return true; }
                WriteResult(TEXT("world-smoke.json"), false, TEXT("No begun game world before deadline"));
                FPlatformMisc::RequestExitWithStatus(false, 1); return false;
            }
            int32 States = 0; UProject_JWorldDefinition* Definition = nullptr;
            for (TActorIterator<AProject_JWorldState> It(World); It; ++It) { ++States; Definition = It->Definition; }
            TArray<FPrimaryAssetId> Ids;
            UAssetManager::Get().GetPrimaryAssetIdList(FPrimaryAssetType(TEXT("Project_JWorldDefinition")), Ids);
            bool bRegistered = false;
            for (const auto& Id : Ids)
            { bRegistered |= UAssetManager::Get().GetPrimaryAssetPath(Id).GetLongPackageName() == ExpectedDefinition; }
            const FString ActualMap = World->GetOutermost()->GetName();
            const FString ActualDefinition = Definition ? Definition->GetOutermost()->GetName() : FString();
            const bool bPassed = !ExpectedMap.IsEmpty() && !ExpectedDefinition.IsEmpty() && ExpectedPlaces > 0 &&
                ActualMap == ExpectedMap && States == 1 && ActualDefinition == ExpectedDefinition &&
                Definition && Definition->Places.Num() == ExpectedPlaces && bRegistered && World->GetWorldPartition();
            WriteResult(TEXT("world-smoke.json"), bPassed, FString::Printf(
                TEXT("map=%s definition=%s states=%d places=%d registered=%d partition=%d netmode=%d"),
                *ActualMap, *ActualDefinition, States, Definition ? Definition->Places.Num() : 0,
                bRegistered, World->GetWorldPartition() != nullptr, int32(World->GetNetMode())));
            // This opt-in is consumed only inside WorldSmoke. Preserve its existing
            // startup payload and timing; write one independent readiness snapshot.
            const bool bReadinessPassed = !FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditWorldReadiness")) || WriteWorldReadinessSnapshot(World);
            FPlatformMisc::RequestExitWithStatus(false, bPassed && bReadinessPassed ? 0 : 1); return false;
        }));
        return;
    }
    if (bAudit)
    {
        // Possession/Restart and replicated movement can re-enable a character's
        // movement after BeginPlay. Hold only this explicit static-geometry audit
        // fixture before each actor tick; production CMC/SSR limits are unchanged.
        AnimationKinematicsTick = FWorldDelegates::OnWorldPreActorTick.AddLambda([](UWorld* World, ELevelTick, float)
        {
            if (!World || !World->IsGameWorld()) { return; }
            for (TActorIterator<AProject_JAuditNetworkPlayer> It(World); It; ++It)
            {
                if (It->HasAuthority()) ProjectJ::Tests::PublishMoverFrame(*It,MOVE_None);
            }
        });
    }
	if (AnimationEnabled())
	{
		FloorWorldInit = FWorldDelegates::OnPostWorldInitialization.AddLambda([](UWorld* World, const UWorld::InitializationValues)
		{ EnsureAnimationFloor(World); });
		EnsureAnimationFloor(GetWorld());

	}
	const double Started = FPlatformTime::Seconds();
	Timeout = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started, bAudit](float)
	{
		if (FPlatformTime::Seconds() - Started < 180) { return true; }
		WriteResult(bAudit ? *FString::Printf(TEXT("timeout_%u.json"), FPlatformProcess::GetCurrentProcessId()) : TEXT("timeout.json"), false, TEXT("Bounded fixture lifetime expired, including connection failure"));
		if (bAudit) { FPlatformMisc::RequestExitWithStatus(false, 1); }
		else { FPlatformMisc::RequestExit(false); } return false;
	}));
}
void UProject_JNetworkFixtureLifetime::Deinitialize()
{
	FWorldDelegates::OnPostWorldInitialization.Remove(FloorWorldInit);
	FWorldDelegates::OnWorldPreActorTick.Remove(AnimationKinematicsTick);
	FTSTicker::GetCoreTicker().RemoveTicker(Timeout);
	Super::Deinitialize();
}

AProject_JNetworkViewPawn::AProject_JNetworkViewPawn()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ViewRoot")));
	bReplicates = true; bOnlyRelevantToOwner = true; SetReplicateMovement(true);
}
AProject_JNetworkProbeNPC::AProject_JNetworkProbeNPC()
{
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ProbeRoot")));
 bReplicates=true; bAlwaysRelevant=false; PrimaryActorTick.bCanEverTick=false;
 SetReplicateMovement(true); SetNetCullDistanceSquared(FMath::Square(8000.f));
 SetNetUpdateFrequency(10.f); SetMinNetUpdateFrequency(2.f);
}
AProject_JAlwaysRelevantProbeNPC::AProject_JAlwaysRelevantProbeNPC() { bAlwaysRelevant = true; }
void AProject_JNetworkProbeNPC::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AProject_JNetworkProbeNPC, ProbeId); DOREPLIFETIME(AProject_JNetworkProbeNPC, Pulse); }

void AProject_JNetworkLoadController::ClientConfigure_Implementation(int32 InStage, int32 Slot, const TArray<int32>& Expected, FGuid OwnItem, bool bExpectItems, int32 MinimumPulse)
{
	if (!Enabled()) { return; }
	CurrentStage = InStage; LocalSlot = Slot; ExpectedIds = Expected; ItemId = OwnItem;
	CleaveStableSince = 0;
	if (CleaveThreatStage(InStage)) { bCleaveThreatObservedAttack = false; CleaveThreatObservedMontage.Reset(); }
	bItemsExpected = bExpectItems; RequiredPulse = MinimumPulse; NextObservation = 0;
	if (RenderedClientEnabled() && InStage >= 40 && InStage <= 45) ConfigureRenderedStage(InStage);
	// Stage 2 is reached only after every real client has observed the authored
	// actors, pose and ownership data for Stage 1. Capture that established stream,
	// rather than the temporary travel world and initial asset-load burst.
	if (InStage==2 && FParse::Param(FCommandLine::Get(),TEXT("ProjectJCaptureMoverBuffers")))
	{
		GEngine->Exec(GetWorld(),TEXT("ProjectJ.CaptureMoverBuffers 120"));
	}
}
void AProject_JNetworkLoadController::UpdateCameraManager(float DeltaSeconds)
{
	Project_J::Movement::RequestMoverCameraUpdate(this);
	Super::UpdateCameraManager(DeltaSeconds);
}

void AProject_JNetworkLoadController::PlayerTick(float DeltaSeconds)
{
	Super::PlayerTick(DeltaSeconds);
	// These clients prove the unchanged initial AOI stages. Fanout damage is authority-only evidence.
	if (CombatFanoutEnabled() && !HasAuthority() && CurrentStage>=50 && CurrentStage<=52) return;
	if (RenderedClientEnabled() && !HasAuthority() && CurrentStage >= 40 && CurrentStage <= 45) DriveRenderedOwner();
	if (EquipmentInterestEnabled() && !HasAuthority() && EquipmentInterestStage(CurrentStage)) { ObserveEquipmentInterest(); return; }
	if (CraftingJobsEnabled() && !HasAuthority() && CurrentStage >= 46 && CurrentStage <= 49) { ObserveCraftingJobs(); return; }
	if (PartyRosterEnabled() && !HasAuthority() && CurrentStage >= 20) { ObserveParty(); return; }
	if (CompanionAudienceEnabled() && !HasAuthority() && CurrentStage >= 8) { ObserveCompanion(); return; }
	if (CombatEnabled() && !HasAuthority() && CombatTrial != INDEX_NONE)
	{
		ObserveCombat(); return;
	}
	// Observe the real montage every client frame, independently of the existing
	// 4Hz readiness/control report. Phase32 first confirms the old montage stopped.
	if (!HasAuthority() && CleaveThreatStage(CurrentStage)) ObserveCleaveThreatMontage();
	if (!Enabled() || HasAuthority() || CurrentStage == INDEX_NONE || FPlatformTime::Seconds() < NextObservation) { return; }
	NextObservation = FPlatformTime::Seconds() + 0.25;
	TArray<int32> Seen; int32 MinPulse = MAX_int32;
	for (TActorIterator<AProject_JNetworkProbeNPC> It(GetWorld()); It; ++It)
	{ if (It->ProbeId > 0) { Seen.Add(It->ProbeId); MinPulse = FMath::Min(MinPulse, It->Pulse); } }
	Seen.Sort();
	bool bOwn = false, bForeignEmpty = true, bEquipment = true; int32 States = 0;
	for (TActorIterator<AProject_JPlayerState> It(GetWorld()); It; ++It)
	{
		if (!It->GetPublicClassId().ToString().StartsWith(TEXT("DClient"))) { continue; }
		++States;
		const bool bMine = *It == PlayerState;
		const FString Inventory = It->GetInventoryComponent()->GetReplicationDiagnosticSummary();
		if (bMine)
		{
			bOwn = It->GetInventoryComponent()->HasItemInstance(ItemId) == bItemsExpected;
			if (QuestsEnabled())
			{
				const auto* Quest = It->FindComponentByClass<UProject_JQuestComponent>();
				const auto* Progress = Quest ? Quest->States.FindByPredicate([](const auto& S) { return S.QuestId == FixtureQuestId; }) : nullptr;
				bOwn &= Progress && Progress->Count == CurrentStage && Progress->bTracked == (CurrentStage != 2);
                const auto* Compound = Quest ? Quest->States.FindByPredicate([](const auto& S) { return S.QuestId == FixtureCompoundQuestId; }) : nullptr;
                bOwn &= Compound && Compound->Count == 0 && Compound->Objectives.Num() == 2 && !Compound->bClaimed &&
                    Compound->bTracked == (CurrentStage != 2) && Compound->GetObjectiveCount(TEXT("First")) == CurrentStage &&
                    Compound->GetObjectiveCount(TEXT("Second")) == CurrentStage;
			}
		}
		else
		{
			bForeignEmpty &= Inventory.Contains(TEXT("Items=0")) && It->GetEquipmentManagerComponent()->GetPrivateEquipmentCount() == 0;
			if (QuestsEnabled())
			{
				const auto* Quest = It->FindComponentByClass<UProject_JQuestComponent>();
				bForeignEmpty &= Quest && Quest->States.IsEmpty();
			}
		}
		// The default ViewPawn is intentionally owner-only. Public equipment is
		// available only on a represented Avatar; private ownership stays on PS.
		const bool Represented = bMine || (It->GetPawn() && It->GetPawn()->FindComponentByClass<UProject_JEquipmentRuntimeComponent>());
		bEquipment &= It->GetEquipmentManagerComponent()->GetAllEquippedItems().Num() == (bItemsExpected && Represented ? 1 : 0);
	}
	ServerObserve(CurrentStage, Seen, bOwn, bForeignEmpty && States == FixtureClients(), bEquipment && States == FixtureClients() && ObserveAttributeAudience(), MinPulse, (RenderedClientEnabled() && CurrentStage >= 40 ? ObserveRenderedCrowd() : ObserveAnimation() && ObserveCleave()), ObserveWorldClock());
}
bool AProject_JNetworkLoadController::ObserveWorldClock() const
{
	if (!WorldClockEnabled()) return true;
	int32 Count = 0;
	for (TActorIterator<AProject_JWorldState> It(GetWorld()); It; ++It)
	{
		++Count;
		if (It->Definition || It->ClockSettings.DayLengthSeconds != 600 || It->ClockSettings.InitialHour != 19 ||
			!FMath::IsFinite(It->GetHour()) || It->GetHour() < 0 || It->GetHour() >= 24 || !FMath::IsNearlyEqual(It->GetRain(), .4f)) return false;
	}
	return Count == 1;
}
bool AProject_JNetworkLoadController::ObserveAttributeAudience()
{
	if (!AttributeAudienceEnabled()) { return true; }
	if (CurrentStage < 1 || CurrentStage > 5) { return false; }
	PendingAttributeAudienceRows.Reset();
	bool bReady = !bAttributeAudienceViolation;
	const auto* Defaults = GetDefault<UProject_JAttributeSet>();
	auto Observe = [this, Defaults, &bReady](const UProject_JAttributeSet* Stats, int32 Slot, const FString& Actor, const TCHAR* Audience, bool bOwner)
	{
		if (!Stats) { bReady = false; return; }
		const FGameplayAttributeData* Values[] = {&Stats->MaxHealth, &Stats->Health, &Stats->MaxMana, &Stats->Mana, &Stats->AttackPower, &Stats->Defense};
		const FGameplayAttributeData* DefaultValues[] = {&Defaults->MaxHealth, &Defaults->Health, &Defaults->MaxMana, &Defaults->Mana, &Defaults->AttackPower, &Defaults->Defense};
		for (int32 I = 0; I < UE_ARRAY_COUNT(Values); ++I)
		{
			const float Base = Values[I]->GetBaseValue(), Current = Values[I]->GetCurrentValue();
			const bool bPrivateObserver = I >= 4 && !bOwner;
			const float Expected = bPrivateObserver ? DefaultValues[I]->GetBaseValue() : AttributeBase(Slot, CurrentStage, I);
			const bool bPassed = FMath::IsNearlyEqual(Base, Expected) && FMath::IsFinite(Current) && Current >= 0 &&
				(!bPrivateObserver || FMath::IsNearlyEqual(Current, DefaultValues[I]->GetCurrentValue()));
			if (bPrivateObserver && !bPassed) { bAttributeAudienceViolation = true; }
			bReady &= bPassed;
			PendingAttributeAudienceRows += FString::Printf(TEXT("%d,%s,%s,%s,%.9g,%.9g,%.9g,%d\n"), CurrentStage, *Actor, Audience, AttributeNames[I], Base, Current, Expected, bPassed);
		}
		bReady &= Stats->GetHealth() <= Stats->GetMaxHealth() && Stats->GetMana() <= Stats->GetMaxMana();
	};
	int32 Players = 0, Owned = 0, NPCCount = 0;
	for (TActorIterator<AProject_JPlayerState> It(GetWorld()); It; ++It)
	{
		const FString Id = It->GetPublicClassId().ToString();
		if (!Id.StartsWith(TEXT("DClient"))) { continue; }
		const int32 Slot = FCString::Atoi(*Id.Mid(7));
		if (Slot < 0 || Slot >= FixtureClients()) { bReady = false; continue; }
		const bool bOwner = *It == PlayerState; ++Players; Owned += bOwner;
		Observe(It->GetProjectJAttributeSet(), Slot, Id, bOwner ? TEXT("owner") : TEXT("remote"), bOwner);
	}
	for (TActorIterator<AProject_JNPCCharacter> It(GetWorld()); It; ++It)
	{
		++NPCCount;
		Observe(It->GetAttributeSet(), AttributeNPCSlot, TEXT("NPC"), TEXT("npc"), false);
	}
	bReady &= Players == FixtureClients() && Owned == 1 && NPCCount == 1 && !bAttributeAudienceViolation;
	if (bReady && AttributeAudienceRecordedStage != CurrentStage)
	{
		// Five successful snapshots maximum; retain only the latest pending diagnostic.
		AttributeAudienceRows += PendingAttributeAudienceRows;
		AttributeAudienceRecordedStage = CurrentStage;
	}
	return bReady;
}
void AProject_JNetworkLoadController::ClientAnimationExpect_Implementation(int32 InStage, const TArray<AProject_JPlayerCharacter*>& Players, const TArray<FProject_JReplicatedAnimEventState>& States)
{
	if (!AnimationEnabled()) { return; }
	AnimationStage = InStage; AnimationPlayers.Reset(); AnimationStates = States;
	for (auto* ObservedCharacter : Players) { AnimationPlayers.Add(ObservedCharacter); }
}
bool AProject_JNetworkLoadController::ObserveAnimation()
{
	if (!AnimationEnabled()) { return true; }
	if (AnimationStage != CurrentStage || AnimationPlayers.Num() != FixtureClients() || AnimationStates.Num() != AnimationPlayers.Num())
	{
		AnimationRows += FString::Printf(TEXT("%d,NotReady_ExpectedStage%d_Players%d_States%d\n"), CurrentStage, AnimationStage, AnimationPlayers.Num(), AnimationStates.Num()); return false;
	}
	int32 RemoteCount = 0; bool bReady = !bAnimationViolation;
	for (int32 I = 0; I < AnimationPlayers.Num(); ++I)
	{
		auto* ObservedCharacter = AnimationPlayers[I].Get();
		if (!ObservedCharacter)
		{
			// The reliable controller RPC can arrive before a separate actor channel
			// resolves its reference. Re-resolve by stable PlayerState identity instead
			// of retaining that initial null as a permanent failed observation.
			const FName ClassId(*FString::Printf(TEXT("DClient%d"), I));
			for (TActorIterator<AProject_JPlayerCharacter> It(GetWorld()); It; ++It)
			{
				const auto* PS = It->GetPlayerState<AProject_JPlayerState>();
				if (PS && PS->GetPublicClassId() == ClassId) { ObservedCharacter = *It; AnimationPlayers[I] = *It; break; }
			}
		}
		if (!ObservedCharacter) { AnimationRows += FString::Printf(TEXT("%d,NotReady_NullActor%d\n"), CurrentStage, I); bReady = false; continue; }
		// Only this opt-in fixture supplies kinematics/visibility. No asset is saved.
		// Simulated proxies consume the authority Mover frame; no local writer is installed.
		TInlineComponentArray<USkeletalMeshComponent*> Visuals(ObservedCharacter);
		for (auto* Mesh : Visuals) { Mesh->SetLastRenderTime(CurrentStage == 4 ? -1000.0f : GetWorld()->GetTimeSeconds()); }
		if (ObservedCharacter->IsLocallyControlled()) { continue; } // SkipOwner is a production contract.
		++RemoteCount;
		auto* Anim = Cast<UProject_JPlayerAnimInstance>(ObservedCharacter->GetMesh()->GetAnimInstance());
		auto* Locomotion = ObservedCharacter->GetLocomotionAnimStateComponent();
		if (!Anim || !Locomotion || ObservedCharacter->GetLocalRole() != ROLE_SimulatedProxy)
		{
			AnimationRows += FString::Printf(TEXT("%d,NotReady_%s_Role%d_Anim%s_Class%s_State%s\n"), CurrentStage, *ObservedCharacter->GetName(), int32(ObservedCharacter->GetLocalRole()),
				*GetNameSafe(ObservedCharacter->GetMesh()->GetAnimInstance()), *GetNameSafe(ObservedCharacter->GetMesh()->GetAnimClass()), *GetNameSafe(Locomotion)); bReady = false; continue;
		}
		const auto State = ReadAnimationEvents(ObservedCharacter); const auto& Expected = AnimationStates[I];
		bReady &= State.MoveSequence == Expected.MoveSequence && State.LandingRevision == Expected.LandingRevision
			&& State.FallOffStartCounter == Expected.FallOffStartCounter && State.bLandingActive == Expected.bLandingActive;
		const auto Playback=Anim->GetPlaybackSnapshot();
		const bool bHasPose=!Playback.SelectedAnimation.IsNone() && Playback.CachedNodeWeight>ZERO_ANIMWEIGHT_THRESH;
		if (bHasPose) { ObservedPose.Add(ObservedCharacter); }
		const bool bFreshPose=bHasPose && Playback.CaptureFrame>0 && Playback.CaptureFrame<=GFrameCounter && GFrameCounter-Playback.CaptureFrame<=2;
		// This stage injects a replicated landing event, not a physical Mover landing.
		// Verify its lease alongside a fresh GASP body evaluation. Actual landing
		// clips and ground handoff are exercised by the physical PIE air fixture.
		if (CurrentStage == 3 && State.LandingRevision == Expected.LandingRevision && Locomotion->bIsLanding
			&& bFreshPose)
		{ ObservedLandingPose.Add(ObservedCharacter); }
		if (CurrentStage == 3) { bReady &= ObservedLandingPose.Contains(ObservedCharacter); }
		// Reliable RPC + unreliable multicast + replicated fallback must not manufacture
		// multiple physical landing epochs for one authoritative landing identity.
		if (State.LandingRevision == Expected.LandingRevision && Locomotion->LandingPresentationRevision > Expected.LandingSequence)
		{ bAnimationViolation = true; bReady = false; }
		if (CurrentStage >= 4) { bReady &= !Locomotion->bIsLanding && ObservedPose.Contains(ObservedCharacter); }
		bReady &= FMath::IsFinite(Playback.SelectedAnimationTime);
		AnimationRows += FString::Printf(TEXT("%d,%s,%d,%d,%d,%d,%d,%llu,%s,%s,ExpectedMove%d_Land%d_Fall%d_ActualFall%d_Ready%d\n"), CurrentStage, *ObservedCharacter->GetName(), int32(ObservedCharacter->GetLocalRole()),
			State.MoveSequence, State.LandingSequence, State.LandingRevision, Locomotion->LandingPresentationRevision,
			Playback.CaptureFrame, *Anim->GetFoundationStateName().ToString(), *Playback.SelectedAnimation.ToString(),
			Expected.MoveSequence, Expected.LandingRevision, Expected.FallOffStartCounter, State.FallOffStartCounter, bReady);
	}
	return bReady && RemoteCount == FixtureClients() - 1;
}
void AProject_JNetworkLoadController::ServerObserve_Implementation(int32 InStage, const TArray<int32>& Seen, bool bOwnInventory, bool bForeignInventoryEmpty, bool bEquipment, int32 MinPulse, bool bAnimation, bool bWorldClock)
{
	if (!Enabled() || Seen.Num() > FixtureNPCs()) { return; }
	if (auto* Mode = GetWorld()->GetAuthGameMode<AProject_JNetworkLoadGameMode>())
	{
		LastObservation = FString::Printf(TEXT("stage=%d seen=%d own=%d foreignEmpty=%d equipment=%d pulse=%d animation=%d worldClock=%d"), InStage, Seen.Num(), bOwnInventory, bForeignInventoryEmpty, bEquipment, MinPulse, bAnimation, bWorldClock);
		if (Mode->ValidateObservation(this, InStage, Seen, bOwnInventory, bForeignInventoryEmpty, bEquipment, MinPulse, bAnimation, bWorldClock)) { ConfirmedStage = InStage; }
	}
}
void AProject_JNetworkLoadController::ClientFinish_Implementation(bool bSuccess)
{
	if (!Enabled()) { return; }
	const TStrongObjectPtr<AProject_JNetworkLoadController> KeepSelf(this);
 if (CanonicalMeleeEnabled() && CombatTrial==4) RetireCanonicalMelee();
	if (PlayerStateAudienceLifetimeEnabled())
	{
		PartyOwnershipDiagnosticRows += PartyOwnershipSnapshot(this, CurrentStage, LocalSlot, TEXT("final"));
		FFileHelper::SaveStringToFile(PartyOwnershipDiagnosticRows, *(OutputRoot() / TEXT("party-ownership.jsonl")));
		if (PlayerStateAudienceOwnerViewEnabled()) FFileHelper::SaveStringToFile(OwnerViewRows, *(OutputRoot() / TEXT("player-state-owner-view.csv")));
	}
	if (RenderedClientEnabled())
	{
		// Observe the native client connection before presentation teardown.
		const auto* Driver = GetWorld() ? GetWorld()->GetNetDriver() : nullptr;
		const auto* Connection = Driver ? Driver->ServerConnection.Get() : nullptr;
		const bool bComplete = GetNetMode() == NM_Client && IsValid(Connection) && Connection->GetDriver() == Driver;
		const FString Policy = FString::Printf(TEXT("{\"complete\":%s,\"fixture_slot\":%d,\"current_net_speed_bytes_per_second\":%d,\"max_packet_bytes\":%d}"),
			bComplete ? TEXT("true") : TEXT("false"), LocalSlot, bComplete ? Connection->CurrentNetSpeed : -1, bComplete ? Connection->MaxPacket : -1);
		bSuccess &= FFileHelper::SaveStringToFile(Policy, *(OutputRoot() / TEXT("connection-policy.json")));
		if (auto* OwnedAvatar=Cast<AProject_JPlayerCharacter>(GetPawn()))
		{ OwnedAvatar->GetLocomotionAnimStateComponent()->SetMoveInput(FVector2D::ZeroVector); OwnedAvatar->StopJumping(); }
		if (auto* Observer=GetGameInstance()->GetSubsystem<UProject_JRenderedCrowdFixture>())
		{
			if (RenderedObserverProcess()) bSuccess &= Observer->IsNetworkPhaseComplete(6) && !Observer->HasNetworkFailed();
			Observer->StopNetworkObservation();
		}
	}
	if (CraftingJobsEnabled()) FFileHelper::SaveStringToFile(CraftingRows, *(OutputRoot() / TEXT("crafting-client.csv")));
	CraftingObservedSource = nullptr; CraftingRetiredSource = nullptr;
	RetireEquipmentOwnerCommands();
	WriteResult(TEXT("client.json"), bSuccess, TEXT("Server completed connection/AOI/FastArray assertions"), LocalSlot);
	if (EquipmentInterestEnabled() && QuestsEnabled()) FFileHelper::SaveStringToFile(EquipmentCommandRows, *(OutputRoot() / TEXT("owner-commands.csv")));
	if (EquipmentInterestEnabled()) FFileHelper::SaveStringToFile(EquipmentInterestRows, *(OutputRoot() / TEXT("equipment-interest.csv")));
	if (PartyRosterEnabled())
	{
		FFileHelper::SaveStringToFile(PartyRows, *(OutputRoot() / TEXT("party-summary.csv")));
		if (PlayerStateAudienceEnabled()) FFileHelper::SaveStringToFile(PlayerStateInterestRows, *(OutputRoot() / TEXT("player-state-interest.csv")));
		if (PlayerStateAudienceLifetimeEnabled()) FFileHelper::SaveStringToFile(PlayerStateLifetimeRows, *(OutputRoot() / TEXT("player-state-lifetime.csv")));
		FFileHelper::SaveStringToFile(PartyMemberRows, *(OutputRoot() / TEXT("party-members.csv")));
		FScreenshotRequest::OnScreenshotRequestProcessed().Remove(PartyScreenshotHandle); PartyScreenshotHandle.Reset();
		const TStrongObjectPtr<UProject_JPlayerUIComponent> RetiringUI(PartyUI.Get());
		PartyUI = nullptr; // Close ownership before input/gameplay cleanup can reenter.
		if (RetiringUI.IsValid()) RetiringUI->DestroyComponent();
	}
	if (AnimationEnabled()) { FFileHelper::SaveStringToFile(AnimationRows, *(OutputRoot() / TEXT("animation-observations.csv"))); }
	if (CleaveEnabled()) FFileHelper::SaveStringToFile(CleaveRows, *(OutputRoot() / TEXT("cleave-observations.csv")));
	if (CleaveThreatEnabled()) FFileHelper::SaveStringToFile(CleaveThreatRows, *(OutputRoot() / TEXT("cleave-threat-observations.csv")));
	if (CombatEnabled()) { FFileHelper::SaveStringToFile(CombatRows, *(OutputRoot() / TEXT("combat-observations.csv"))); }
	if (CompanionAudienceEnabled())
	{
		if (CompanionModel) CompanionModel->Unbind();
		FFileHelper::SaveStringToFile(CompanionRows, *(OutputRoot() / TEXT("companion-audience.csv")));
	}
	if (AttributeAudienceEnabled())
	{
		FFileHelper::SaveStringToFile(AttributeAudienceRows, *(OutputRoot() / TEXT("attribute-audience.csv")));
		FFileHelper::SaveStringToFile(PendingAttributeAudienceRows, *(OutputRoot() / TEXT("attribute-audience-last.csv")));
	}
	FPlatformMisc::RequestExit(false);
}

void AProject_JNetworkLoadController::ClientBeginCombatTrial_Implementation(int32 Trial,AProject_JNPCCharacter* CanonicalTarget)
{
	if (!CombatEnabled()) { return; }
	CombatTrial = Trial; bSentR = bSentMouse = bReportedCombat = false;
	CombatRSeen.Reset(); CombatTargetSeen.Reset(); CombatEndSeen.Reset();
 if(CanonicalMeleeEnabled() && Trial==4)
 {
  if(!IsValid(CanonicalTarget) || CanonicalTarget->IsActorBeingDestroyed() || CanonicalTarget->GetWorld()!=GetWorld())
  { UE_LOG(LogProjectJDNetwork,Error,TEXT("Canonical control recipient failed native object-reference mapping"));ClientFinish(false);return; }
  CanonicalMelee.Target=CanonicalTarget;
 }
}
void AProject_JNetworkLoadController::ObserveCombat()
{
 if (CanonicalMeleeEnabled() && CombatTrial==4) { ObserveCanonicalMelee(); return; }
	if (bReportedCombat) { return; }
	auto* PawnCharacter = Cast<AProject_JPlayerCharacter>(GetPawn());
	auto* ASC = PawnCharacter ? Cast<UProject_JAbilitySystemComponent>(PawnCharacter->GetAbilitySystemComponent()) : nullptr;
	const auto* RuntimeEquipment = PawnCharacter ? PawnCharacter->FindComponentByClass<UProject_JEquipmentRuntimeComponent>() : nullptr;
	const auto& InputTags = FProject_JGameplayTags::Get();
	if (!bSentR && RuntimeEquipment && RuntimeEquipment->GetWeaponRevision() > 0 && ASC && ASC->GetAvatarActor() == PawnCharacter
		&& ASC->AbilityActorInfo.IsValid() && ASC->AbilityActorInfo->PlayerController.Get() == this && ASC->AbilityActorInfo->IsLocallyControlled()
		&& ASC->HasMatchingGameplayTag(InputTags.State_CombatMode)
		&& ASC->GetActivatableAbilities().ContainsByPredicate([&](const FGameplayAbilitySpec& Spec)
		{ return !Spec.PendingRemove && Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTags.InputTag_Skill_R)
			&& Spec.Ability->CanActivateAbility(Spec.Handle, ASC->AbilityActorInfo.Get()); }))
	{
		UE_LOG(LogProjectJDNetwork, Display, TEXT("CombatReady trial=%d slot=%d pawn=%s state=%s ASC=%s avatar=%s weapon=%llu local=%d actorInfoPC=%s"),
			CombatTrial, LocalSlot, *GetNameSafe(PawnCharacter), *GetNameSafe(PawnCharacter->GetPlayerState()),
			*GetNameSafe(ASC), *GetNameSafe(ASC->GetAvatarActor()), RuntimeEquipment->GetWeaponRevision(),
			ASC->AbilityActorInfo->IsLocallyControlled(), *GetNameSafe(ASC->AbilityActorInfo->PlayerController.Get()));
		PawnCharacter->HandleSkillInputTagPressed(InputTags.InputTag_Skill_R); PawnCharacter->HandleSkillInputTagReleased(InputTags.InputTag_Skill_R);
		UE_LOG(LogProjectJDNetwork, Display, TEXT("CombatR dispatched trial=%d active=%d"), CombatTrial, ASC->HasMatchingGameplayTag(InputTags.State_Attacking));
		bSentR = true; CombatInputAt = FPlatformTime::Seconds();
	}
	if (bSentR && !bSentMouse && FPlatformTime::Seconds() - CombatInputAt >= 0.5)
	{
		if (auto* Router = PawnCharacter->FindComponentByClass<UProject_JSkillInputRouterComponent>())
		{
			Router->HandleButtonPressed(EProject_JSkillInputButton::RMB);
			Router->HandleButtonReleased(EProject_JSkillInputButton::RMB); bSentMouse = true;
		}
	}
	for (TActorIterator<AProject_JPlayerCharacter> It(GetWorld()); It; ++It)
	{
		const auto* PS = It->GetPlayerState<AProject_JPlayerState>();
		if (PS && PS->GetPublicClassId().ToString().StartsWith(TEXT("DClient")))
		{ RecordCombat(*It, CombatTrial, CombatRSeen, CombatTargetSeen, CombatEndSeen, &CombatRows); }
	}
	if (bSentMouse && CombatEndSeen.Num() == FixtureClients())
	{
		bReportedCombat = true; ServerCombatObserved(CombatTrial, true);
	}
}
void AProject_JNetworkLoadController::ServerCombatObserved_Implementation(int32 Trial, bool bComplete)
{
	if (auto* Mode = GetWorld()->GetAuthGameMode<AProject_JNetworkLoadGameMode>())
	{ Mode->AcceptCombatObservation(this, Trial, bComplete); }
}
void AProject_JNetworkLoadGameMode::AcceptCombatObservation(AProject_JNetworkLoadController* PC, int32 Trial, bool bComplete)
{
	if (CanonicalMeleeEnabled() && Stage==53 && Trial==4 && Clients.IsValidIndex(0) && PC==Clients[0] && bComplete) { PC->ConfirmedStage=53; return; }
	if (CombatEnabled() && Stage == 7 && Trial == CombatTrial && Clients.Contains(PC) && bComplete)
	{ PC->ConfirmedStage = 7; }
}
void AProject_JNetworkLoadGameMode::BeginCombatTrial()
{
	Stage = 7; StageStarted = FPlatformTime::Seconds(); ++CombatTrial;
	CombatRSeen.Reset(); CombatTargetSeen.Reset(); CombatEndSeen.Reset();
	for (AProject_JNetworkLoadController* PC : Clients)
	{
		PC->ConfirmedStage = INDEX_NONE;
		if (CombatTrial == 1)
		{
			auto* State = PC->GetPlayerState<AProject_JPlayerState>();
			auto* Definition = LoadObject<UProject_JEquipmentItemDefinition>(nullptr, TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.DA_Greatsword_Equip"));
			State->GetEquipmentManagerComponent()->EquipItem(Definition);
			CastChecked<UProject_JAbilitySystemComponent>(State->GetAbilitySystemComponent())->AddProjectJLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		}
		PC->ClientBeginCombatTrial(CombatTrial);
	}
	TRACE_BOOKMARK(TEXT("ProjectJ.Network CombatTrial=%d"), CombatTrial);
}
bool AProject_JNetworkLoadGameMode::ObserveServerCombat()
{
	for (AProject_JNetworkLoadController* PC : Clients) { RecordCombat(Cast<AProject_JPlayerCharacter>(PC->GetPawn()), CombatTrial, CombatRSeen, CombatTargetSeen, CombatEndSeen); }
	return CombatEndSeen.Num() == FixtureClients();
}

void AProject_JNetworkLoadController::ObserveCanonicalMelee()
{
 if(bCanonicalReported || bReportedCombat) return;
 const TStrongObjectPtr<ThisClass> KeepSelf(this);
 if(!CanonicalMelee.Source.IsValid())
 {
  auto* SourcePlayer=Cast<AProject_JPlayerCharacter>(GetPawn());
  if(!SourcePlayer || SourcePlayer->GetLocalRole()!=ROLE_AutonomousProxy || !SourcePlayer->IsLocallyControlled()) return;
  auto* Target=CanonicalMelee.Target.Get(); // Exact native-mapped actor reference from the existing control RPC.
  if(!Target || !InitCanonicalWitness(CanonicalMelee,SourcePlayer,Target)) return; // Native replication/equipment admission, bounded by existing timeout.
  if(!CanonicalMelee.ASC->AbilityActorInfo.IsValid() || CanonicalMelee.ASC->AbilityActorInfo->PlayerController.Get()!=this ||
     !CanonicalMelee.ASC->AbilityActorInfo->IsLocallyControlled()) { ClientFinish(false);return; }
  BindCanonicalWitness(this,CanonicalMelee);
 }
 if(!CanonicalCurrent(CanonicalMelee) || CanonicalMelee.bViolation) { ClientFinish(false);return; }
 auto* SourcePlayer=CanonicalMelee.Source.Get();
 if(!bCanonicalInputSent)
 {
  const auto Physical=Project_J::Movement::ReadPhysicalFrame(SourcePlayer);
  auto* Router=SourcePlayer->FindComponentByClass<UProject_JSkillInputRouterComponent>();
  if(!Physical.bValid || !Physical.bGrounded || !Router) return;
  bCanonicalInputSent=true;
  Router->HandleButtonPressed(EProject_JSkillInputButton::LMB);
  if(!CanonicalCurrent(CanonicalMelee)) { ClientFinish(false);return; }
  Router->HandleButtonReleased(EProject_JSkillInputButton::LMB);
  UE_LOG(LogProjectJDNetwork,Display,TEXT("CANONICAL147 input source=%s role=%d local=%d weapon=%llu"),*CanonicalMelee.SourceName,CanonicalMelee.Role,CanonicalMelee.bLocal,CanonicalMelee.WeaponRevision);
 }
 ObserveCanonicalActive(CanonicalMelee);
 if(CanonicalMelee.bViolation) { ClientFinish(false);return; }
 if(!CanonicalMelee.bEnded) return;
 if(CanonicalMelee.NaturalEnds!=1 || CanonicalMelee.RawContacts<1 || !CanonicalMelee.bMontage || CanonicalMelee.NativeFrame<0 || !CanonicalMelee.AttackFinalizations ||
    CanonicalMelee.Hit->GetActiveAttackDefinition() || CanonicalMelee.Anim->IsAnyMontagePlaying() ||
    CanonicalMelee.ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Attacking)) return;
 // A small ordinary post-end observation, no clock/animation advancement or forced update.
 if(FPlatformTime::Seconds()-CanonicalMelee.EndedAt<0.25) return;
 bCanonicalReported=bReportedCombat=true;
 RetireCanonicalWitness(CanonicalMelee);
 if(!WriteCanonicalWitness(CanonicalMelee,TEXT("canonical-melee-owner.json"),true,true)) { ClientFinish(false);return; }
 ServerCombatObserved(4,true);
}
void AProject_JNetworkLoadController::RetireCanonicalMelee()
{
 if(!CanonicalMelee.Source.IsExplicitlyNull())
 {
  RetireCanonicalWitness(CanonicalMelee);
  if(!bCanonicalReported) WriteCanonicalWitness(CanonicalMelee,TEXT("canonical-melee-owner.json"),false,true);
 }
}
void AProject_JNetworkLoadGameMode::BeginCanonicalMelee()
{
 const TStrongObjectPtr<ThisClass> KeepSelf(this);
 Stage=53;StageStarted=FPlatformTime::Seconds();bCanonicalSetup=true;
 if(Clients.Num()!=2 || !IsValid(Clients[0])) { Finish(false,TEXT("Canonical source connection unavailable"));return; }
 const TStrongObjectPtr<AProject_JPlayerCharacter> Player(Cast<AProject_JPlayerCharacter>(Clients[0]->GetPawn()));
 auto* State=Player?Player->GetPlayerState<AProject_JPlayerState>():nullptr;
 auto* Manager=State?State->GetEquipmentManagerComponent():nullptr;
 const TStrongObjectPtr<AProject_JPlayerState> StatePin(State);
 const TStrongObjectPtr<UProject_JEquipmentManagerComponent> ManagerPin(Manager);
 CanonicalMelee.Source=Player.Get();CanonicalMelee.ASC=Player?Cast<UProject_JAbilitySystemComponent>(Player->GetAbilitySystemComponent()):nullptr;
 if(CanonicalMelee.ASC.IsValid()) CanonicalMelee.SourceEpoch=CanonicalMelee.ASC->GetActorInfoRevision();
 if(!Player || !Manager || Player->GetController()!=Clients[0] || Player->GetLocalRole()!=ROLE_Authority)
 { Finish(false,TEXT("Canonical source is not the existing owned authority Avatar"));return; }
 CanonicalMeleeOriginalItem=Manager->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon);
 CanonicalMeleeItem=LoadObject<UProject_JEquipmentItemDefinition>(nullptr,CanonicalEquipPath);
 if(!CanonicalMeleeOriginalItem || !CanonicalMeleeItem || !CanonicalMeleeItem->CombatStyleDefinition || !CanonicalMeleeItem->CombatStyleDefinition->IsRuntimeReady())
 { Finish(false,TEXT("Selected canonical reference equipment unavailable"));return; }
 // These exact provider keys are absent before this fixture equips its reference. Never acquire/remove them directly.
 if(CanonicalMeleeItem->AbilitySet)
  CanonicalMeleeGrantSources.Add(FName(*FString::Printf(TEXT("Equipment.%d.%s"),int32(CanonicalMeleeItem->EquipmentSlot),*CanonicalMeleeItem->GetPathName())));
 for(const UProject_JAbilitySet* Set:CanonicalMeleeItem->CombatStyleDefinition->GetRuntimeAbilitySets())
  if(Set) CanonicalMeleeGrantSources.AddUnique(FName(*FString::Printf(TEXT("CombatStyle.%s.%s"),*CanonicalMeleeItem->CombatStyleDefinition->GetPathName(),*Set->GetPathName())));
 if(CanonicalMeleeGrantSources.IsEmpty() || !CanonicalMelee.ASC.IsValid()) { Finish(false,TEXT("Canonical owned ability source keys unavailable"));return; }
 for(const FName Key:CanonicalMeleeGrantSources)
 {
  CanonicalMelee.OwnedGrantSources.Add(Key.ToString());
  if(CanonicalMelee.ASC->HasAbilityGrantSource(Key)) { Finish(false,TEXT("Canonical reference source already has a foreign provider"));return; }
 }
 CanonicalMelee.bOwnedSourcesInitiallyAbsent=true;
 Manager->EquipItem(CanonicalMeleeItem);
 if(Player.IsValid()) { CanonicalMelee.Equipment=Player->FindComponentByClass<UProject_JEquipmentRuntimeComponent>();if(CanonicalMelee.Equipment.IsValid()) CanonicalMelee.WeaponRevision=CanonicalMelee.Equipment->GetWeaponRevision(); }
 if(bFinished || !IsValid(Player.Get()) || Player->IsActorBeingDestroyed() || Clients[0]->GetPawn()!=Player.Get() ||
    State!=Player->GetPlayerState<AProject_JPlayerState>() || Manager!=State->GetEquipmentManagerComponent() || Manager->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon)!=CanonicalMeleeItem)
 { Finish(false,TEXT("Canonical equipment admission changed source/manager"));return; }
 FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 // One fixed setup recipient, no source relocation, live target steering, or attack-time transform writer.
 // Fixed crossing scene: original forward120 anchor, right-600 to right+600; never steer during the attack.
 const FVector RecipientAnchor=Player->GetActorLocation()+Player->GetActorForwardVector()*120.f;
 if(CanonicalMovingRecipientEnabled()) CanonicalRecipientRouteAxis=Player->GetActorRightVector();
 const FVector RecipientStart=CanonicalMovingRecipientEnabled()?RecipientAnchor-CanonicalRecipientRouteAxis*600.f:RecipientAnchor;
 CanonicalMeleeTarget=GetWorld()->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(),RecipientStart,FRotator::ZeroRotator,Params);
 const TStrongObjectPtr<AProject_JNPCCharacter> Target(CanonicalMeleeTarget.Get());
 if(!Target || Target->IsActorBeingDestroyed()) { Finish(false,TEXT("Canonical native recipient spawn failed"));return; }
 const TStrongObjectPtr<UProject_JAbilitySystemComponent> TargetASC(Cast<UProject_JAbilitySystemComponent>(Target->GetAbilitySystemComponent()));
 if(!TargetASC || TargetASC->GetAvatarActor()!=Target.Get() || !TargetASC->IsRegistered()) { Finish(false,TEXT("Canonical recipient GAS unavailable"));return; }
 TargetASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(),1000.f);
 if(bFinished || !IsValid(Target.Get()) || Target->IsActorBeingDestroyed() || Target->GetAbilitySystemComponent()!=TargetASC.Get()) { Finish(false,TEXT("Canonical recipient attribute callback changed identity"));return; }
 TargetASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(),1000.f);
 if(bFinished || !IsValid(Target.Get()) || Target->IsActorBeingDestroyed() || Target->GetAbilitySystemComponent()!=TargetASC.Get()) { Finish(false,TEXT("Canonical recipient attribute callback changed identity"));return; }
 auto* Rewind=Target->FindComponentByClass<UProject_JServerSideRewindComponent>();
 if(bFinished || !IsValid(Target.Get()) || Target->IsActorBeingDestroyed() || !IsValid(Rewind) || !Rewind->IsRegistered() || !Rewind->HasBegunPlay())
 { CanonicalMelee.Error=FString::Printf(TEXT("assembly finished=%d target_valid=%d target_destroying=%d rewind_valid=%d registered=%d begun=%d"),
    bFinished,IsValid(Target.Get()),IsValid(Target.Get())?Target->IsActorBeingDestroyed():-1,IsValid(Rewind),IsValid(Rewind)?Rewind->IsRegistered():-1,IsValid(Rewind)?Rewind->HasBegunPlay():-1);
   Finish(false,TEXT("Canonical source/recipient/component assembly changed identity: ")+CanonicalMelee.Error);return; }
 // Possession may legitimately refresh the recipient ASC epoch. Capture the witness after command admission.
 if(CanonicalMovingRecipientEnabled() && !BeginCanonicalRecipientNavigation())
 { Finish(false,TEXT("Canonical native route admission failed: ")+CanonicalMelee.Error);return; }
 if(!InitCanonicalWitness(CanonicalMelee,Player.Get(),Target.Get()))
 { Finish(false,TEXT("Canonical source/recipient/component assembly changed identity: ")+CanonicalMelee.Error);return; }
 for(const FName Key:CanonicalMeleeGrantSources)
  if(!CanonicalMelee.ASC->HasAbilityGrantSource(Key)) { Finish(false,TEXT("Canonical equipment did not publish its owned grant source"));return; }
 BindCanonicalWitness(this,CanonicalMelee);
 const TWeakObjectPtr<ThisClass> WeakThis(this);const uint64 Revision=CanonicalMelee.Revision;
 CanonicalMelee.EffectHandle=TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda([WeakThis,Revision](UAbilitySystemComponent* EffectSource,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle)
 {
  const TStrongObjectPtr<ThisClass> Self(WeakThis.Get());if(!Self || Self->CanonicalMelee.Revision!=Revision || Self->CanonicalMelee.bCleaned) return;
  auto& W=Self->CanonicalMelee;
  if(!W.Attack.IsValid() || Spec.Def!=W.Attack->DamageEffect->GetDefaultObject<UGameplayEffect>()) return;
  if(!CanonicalCurrent(W) || W.bEnded || Spec.GetContext().GetOriginalInstigator()!=W.Source.Get() || EffectSource!=W.ASC.Get() || ++W.Effects!=1)
  { W.bViolation=true;W.Error=TEXT("Canonical damage outside exact source/target execution or duplicated");return; }
  W.HealthAfter=W.TargetASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
  if(!FMath::IsFinite(W.HealthAfter) || W.HealthAfter>=W.HealthBefore || W.HealthAfter<0) { W.bViolation=true;W.Error=TEXT("Canonical GE did not reduce actual recipient Health"); }
 });
 for(AProject_JNetworkLoadController* PC:Clients) PC->ConfirmedStage=INDEX_NONE;
 Target->ForceNetUpdate();Player->ForceNetUpdate();State->ForceNetUpdate();
 UE_LOG(LogProjectJDNetwork,Display,TEXT("CANONICAL147 prepared source=%s target=%s source_epoch=%llu weapon=%llu"),*CanonicalMelee.SourceName,*CanonicalMelee.TargetName,CanonicalMelee.SourceEpoch,CanonicalMelee.WeaponRevision);
}
bool AProject_JNetworkLoadGameMode::BeginCanonicalRecipientNavigation()
{
 const TStrongObjectPtr<AProject_JNPCCharacter> Target(CanonicalMeleeTarget.Get());
 const TStrongObjectPtr<AProject_JPlayerCharacter> Source(CanonicalMelee.Source.Get());
 const TStrongObjectPtr<UWorld> World(GetWorld());
 const auto Current=[&]() { return !bFinished && !bCanonicalCleaning && IsValid(Target.Get()) && !Target->IsActorBeingDestroyed() &&
  IsValid(Source.Get()) && !Source->IsActorBeingDestroyed() && World && !World->bIsTearingDown && GetWorld()==World.Get() &&
  Target->GetWorld()==World.Get() && Source->GetWorld()==World.Get() && CanonicalMeleeTarget==Target.Get() && CanonicalMelee.Source==Source.Get(); };
 if(!Current()) { CanonicalMelee.Error=TEXT("navigation.owner_lifetime");return false; }
 const TStrongObjectPtr<UNavigationSystemV1> Navigation(FNavigationSystem::GetCurrent<UNavigationSystemV1>(World.Get()));
 CanonicalRecipientStart=Target->GetNavAgentLocation();
 CanonicalRecipientGoal=CanonicalRecipientStart+CanonicalRecipientRouteAxis*1200.f;
 if(!Current() || !Navigation || Navigation->IsNavigationBuildInProgress() || CanonicalRecipientStart.ContainsNaN() || CanonicalRecipientGoal.ContainsNaN())
 { CanonicalMelee.Error=TEXT("navigation.system_or_build_not_ready");return false; }
 const TStrongObjectPtr<ANavigationData> Data(const_cast<ANavigationData*>(Navigation->GetNavDataForProps(Target->GetNavAgentPropertiesRef(),CanonicalRecipientStart)));
 if(!Data || Data->IsActorBeingDestroyed() || !Data->IsRegistered() || Data->GetWorld()!=World.Get())
 { CanonicalMelee.Error=TEXT("navigation.registered_agent_data_missing");return false; }
 const auto SourcePhysical=Project_J::Movement::ReadPhysicalFrame(Source.Get());
 const TStrongObjectPtr<UCapsuleComponent> Capsule(Project_J::Movement::FindCapsule(Target.Get()));
 const auto& Config=Data->GetConfig();const FVector QueryExtent=Data->GetDefaultQueryExtent();
 float Radius=0,HalfHeight=0;if(Capsule) Capsule->GetScaledCapsuleSize(Radius,HalfHeight);
 const float Step=SourcePhysical.MaxStepHeight;
 if(!Current() || !SourcePhysical.bValid || !SourcePhysical.bGrounded || !SourcePhysical.Floor.bBlockingHit || SourcePhysical.Floor.bStartPenetrating ||
    SourcePhysical.Floor.ImpactPoint.ContainsNaN() || SourcePhysical.Floor.ImpactNormal.ContainsNaN() || !Project_J::Movement::IsWalkable(Source.Get(),SourcePhysical.Floor) ||
    !FMath::IsFinite(Step) || Step<=0 || !Capsule || Capsule->IsBeingDestroyed() || !Capsule->IsRegistered() || !Capsule->IsQueryCollisionEnabled() ||
    Capsule->GetOwner()!=Target.Get() || Capsule->GetWorld()!=World.Get() || !Capsule->GetUpVector().Equals(FVector::UpVector,UE_KINDA_SMALL_NUMBER) ||
    !FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight) || Radius<=0 || HalfHeight<Radius ||
    !Config.IsValid() || !FMath::IsFinite(Config.AgentRadius) || !FMath::IsFinite(Config.AgentHeight) || QueryExtent.ContainsNaN() || QueryExtent.GetMin()<0)
 { CanonicalMelee.Error=TEXT("navigation.physical_floor_or_agent_contract_missing");return false; }
 FNavLocation Start,Goal;
 if(!Navigation->ProjectPointToNavigation(CanonicalRecipientStart,Start,QueryExtent,Data.Get()) ||
    !Navigation->ProjectPointToNavigation(CanonicalRecipientGoal,Goal,QueryExtent,Data.Get()) ||
    !Current() || !Data->IsRegistered() || Data->IsActorBeingDestroyed())
 { CanonicalMelee.Error=TEXT("navigation.configured_endpoints_not_projected");return false; }
 FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(ProjectJCanonicalRecipientRoute),false,Target.Get());GroundParams.AddIgnoredActor(Source.Get());
 auto Responses=FCollisionResponseParams(Capsule->GetCollisionResponseToChannels());Responses.CollisionResponse.SetResponse(ECC_Pawn,ECR_Ignore);
 const auto SamePhysicalLayer=[&](const FVector& Configured,const FVector& Projected)
 {
  if(Configured.ContainsNaN() || Projected.ContainsNaN() || !FVector2D(Configured.X,Configured.Y).Equals(FVector2D(Projected.X,Projected.Y),UE_KINDA_SMALL_NUMBER) ||
     FMath::Abs(Projected.Z-Configured.Z)>Step+UE_KINDA_SMALL_NUMBER) return false;
  FHitResult Ground;
  return World->LineTraceSingleByChannel(Ground,Configured+FVector::UpVector*Step,Configured-FVector::UpVector*Step,Capsule->GetCollisionObjectType(),GroundParams,Responses) &&
   Ground.bBlockingHit && !Ground.bStartPenetrating && !Ground.ImpactPoint.ContainsNaN() && !Ground.ImpactNormal.ContainsNaN() && !Cast<APawn>(Ground.GetActor()) &&
   Project_J::Movement::IsWalkable(Target.Get(),Ground) && FMath::Abs(Ground.ImpactPoint.Z-SourcePhysical.Floor.ImpactPoint.Z)<=Step+UE_KINDA_SMALL_NUMBER &&
   FMath::Abs(Projected.Z-Ground.ImpactPoint.Z)<=Step+UE_KINDA_SMALL_NUMBER;
 };
 if(!SamePhysicalLayer(CanonicalRecipientStart,Start.Location) || !Current() || !SamePhysicalLayer(CanonicalRecipientGoal,Goal.Location) ||
    !Current() || !Data->IsRegistered() || Data->IsActorBeingDestroyed())
 { CanonicalMelee.Error=TEXT("navigation.projection_changed_configured_route_or_floor_layer");return false; }
 CanonicalRecipientProjectedStart=Start.Location;CanonicalRecipientProjectedGoal=Goal.Location;
 CanonicalRecipientNavData=Data.Get();CanonicalRecipientNavDataPath=Data->GetPathName();
 bCanonicalRecipientOwnController=Target->GetController()==nullptr;
 const TWeakObjectPtr<ThisClass> WeakThis(this);const TWeakObjectPtr<AProject_JNPCCharacter> WeakTarget(Target.Get());
 const bool Admitted=Target->StartNavigationTo(Goal.Location,60,[WeakThis,WeakTarget](bool Arrived)
 {
  if(auto* Self=WeakThis.Get(); Self && WeakTarget.IsValid() && Self->CanonicalMeleeTarget.Get()==WeakTarget.Get())
  { ++Self->CanonicalRecipientStopCalls;Self->bCanonicalRecipientStopped=true;Self->bCanonicalRecipientArrived=Arrived; }
 });
 // Keep exact post-call observations even when admission/callback currentness failed.
 // The NPC helper retains its created controller until its normal EndPlay.
 CanonicalRecipientController=Cast<AAIController>(Target->GetController());
 CanonicalRecipientAction=Target->FindComponentByClass<UProject_JNPCActionComponent>();
 if(!Admitted || !Current()) { CanonicalMelee.Error=TEXT("navigation.native_command_not_admitted");return false; }
 bCanonicalRecipientAdmitted=true;
 const auto* Action=CanonicalRecipientAction.Get();const auto* Controller=CanonicalRecipientController.Get();
 CanonicalRecipientToken=Action?Action->GetOwnershipToken():0;
 if(!Action || !Action->IsRegistered() || !CanonicalRecipientToken || !Controller || Controller->GetPawn()!=Target.Get())
 { CanonicalMelee.Error=TEXT("navigation.owned_command_identity_missing");return false; }
 return true;
}
bool AProject_JNetworkLoadGameMode::ObserveCanonicalRecipientNavigation()
{
 const auto* Controller=CanonicalRecipientController.Get();const auto* Action=CanonicalRecipientAction.Get();
 auto* Target=CanonicalMeleeTarget.Get();const auto* Data=CanonicalRecipientNavData.Get();
 if(!Target || !Controller || Controller->IsActorBeingDestroyed() || Controller->GetPawn()!=Target || Target->GetController()!=Controller ||
    !Action || Action->IsBeingDestroyed() || !Action->IsRegistered() || Action->GetOwner()!=Target || Action->GetWorld()!=GetWorld() ||
    !Data || Data->IsActorBeingDestroyed() || !Data->IsRegistered() || Data->GetWorld()!=GetWorld() || CanonicalRecipientStopCalls>1 ||
    Action->GetOwnershipToken()!=(bCanonicalRecipientStopped?uint64(0):CanonicalRecipientToken))
 { Finish(false,TEXT("Canonical native route changed owner/action/NavData identity"));return false; }
 const auto Physical=Project_J::Movement::ReadPhysicalFrame(Target);
 const FVector Feet=Target->GetNavAgentLocation();
 if(!CanonicalCurrent(CanonicalMelee) || Feet.ContainsNaN() || !Physical.bValid || !Physical.bMover || Physical.Velocity.ContainsNaN())
 { Finish(false,TEXT("Canonical native recipient movement observation invalid"));return false; }
 CanonicalRecipientMaxDisplacement=FMath::Max(CanonicalRecipientMaxDisplacement,FVector::Dist(Feet,CanonicalRecipientStart));
 CanonicalRecipientMaxSpeed=FMath::Max(CanonicalRecipientMaxSpeed,Physical.Velocity.Size());
 if(!bCanonicalRecipientMoving)
 {
  if(bCanonicalRecipientStopped) { Finish(false,TEXT("Canonical native route stopped before actual motion/input"));return false; }
  const auto* Following=Controller->GetPathFollowingComponent();const auto Path=Following?Following->GetPath():FNavPathSharedPtr();
  const auto* Paths=GetWorld()->GetSubsystem<UProject_JNPCPathSubsystem>();
  if(!Following || Following->GetStatus()!=EPathFollowingStatus::Moving || !Following->GetCurrentRequestId().IsValid() ||
     !Paths || !Paths->IsPathUsable(Path) || Path->GetNavigationDataUsed()!=Data || Path->GetPathPoints().Num()<2 ||
     !Physical.bGrounded || FVector::DotProduct(Feet-CanonicalRecipientStart,CanonicalRecipientRouteAxis)<=UE_KINDA_SMALL_NUMBER ||
     FVector::DotProduct(Physical.Velocity,CanonicalRecipientRouteAxis)<=UE_KINDA_SMALL_NUMBER) return true;
  bCanonicalRecipientMoving=true;CanonicalRecipientRequestID=Following->GetCurrentRequestId().GetID();CanonicalRecipientPathPoints=Path->GetPathPoints().Num();
  UE_LOG(LogProjectJDNetwork,Display,TEXT("CANONICAL151 native route moving target=%s request=%u points=%d displacement=%.9g speed=%.9g"),
   *CanonicalMelee.TargetName,CanonicalRecipientRequestID,CanonicalRecipientPathPoints,CanonicalRecipientMaxDisplacement,CanonicalRecipientMaxSpeed);
 }
 return true;
}
bool AProject_JNetworkLoadGameMode::WriteCanonicalRecipientNavigation(bool bCleanupPassed)
{
 const auto Report=MakeShared<FJsonObject>();
 Report->SetNumberField(TEXT("schema"),1);Report->SetBoolField(TEXT("success"),bCanonicalRecipientAdmitted && bCanonicalRecipientMoving && bCleanupPassed);
 Report->SetStringField(TEXT("scope"),TEXT("One fixed native route; navigation observation is separate from positive combat and accepted SSR coverage"));
 Report->SetStringField(TEXT("map"),GetWorld()->GetPackage()->GetName());Report->SetStringField(TEXT("target"),CanonicalMelee.TargetName);
 Report->SetStringField(TEXT("nav_data"),CanonicalRecipientNavDataPath);Report->SetBoolField(TEXT("native_admitted"),bCanonicalRecipientAdmitted);
 Report->SetBoolField(TEXT("native_motion_observed"),bCanonicalRecipientMoving);Report->SetNumberField(TEXT("native_request_id"),CanonicalRecipientRequestID);
 Report->SetNumberField(TEXT("native_path_points"),CanonicalRecipientPathPoints);Report->SetNumberField(TEXT("max_displacement_cm"),CanonicalRecipientMaxDisplacement);
 Report->SetNumberField(TEXT("max_physical_speed_cm_s"),CanonicalRecipientMaxSpeed);Report->SetNumberField(TEXT("stopped_callbacks"),CanonicalRecipientStopCalls);
 Report->SetBoolField(TEXT("arrived"),bCanonicalRecipientArrived);Report->SetBoolField(TEXT("cleanup_passed"),bCleanupPassed);
 Report->SetBoolField(TEXT("controller_owned"),bCanonicalRecipientOwnController);Report->SetBoolField(TEXT("owned_controller_retired"),bCanonicalRecipientControllerRetired);
 const auto Vector=[&](const TCHAR* Name,const FVector& Value) { TArray<TSharedPtr<FJsonValue>> A;
  for(const double V:{Value.X,Value.Y,Value.Z}) A.Add(MakeShared<FJsonValueNumber>(V));Report->SetArrayField(Name,A); };
 Vector(TEXT("route_axis"),CanonicalRecipientRouteAxis);Vector(TEXT("configured_start_feet"),CanonicalRecipientStart);Vector(TEXT("configured_goal_feet"),CanonicalRecipientGoal);
 Vector(TEXT("projected_start_feet"),CanonicalRecipientProjectedStart);Vector(TEXT("projected_goal_feet"),CanonicalRecipientProjectedGoal);
 FString JSON;const auto Writer=TJsonWriterFactory<>::Create(&JSON);FJsonSerializer::Serialize(Report,Writer);
 return FFileHelper::SaveStringToFile(JSON,*(OutputRoot()/TEXT("canonical-recipient-navigation.json")));
}

void AProject_JNetworkLoadGameMode::UpdateCanonicalMelee()
{
 if(bCanonicalCleaning || bFinished) return;
 if(!CanonicalCurrent(CanonicalMelee) || CanonicalMelee.bViolation) { Finish(false,TEXT("Canonical source/target identity or callback violation"));return; }
 if(CanonicalMovingRecipientEnabled() && !ObserveCanonicalRecipientNavigation()) return;
 if(!bCanonicalControlSent)
 {
  if(CanonicalMovingRecipientEnabled() && !bCanonicalRecipientMoving) return;
  const auto SourcePhysical=Project_J::Movement::ReadPhysicalFrame(CanonicalMelee.Source.Get());
  const auto TargetPhysical=Project_J::Movement::ReadPhysicalFrame(CanonicalMelee.Target.Get());
  if(!SourcePhysical.bValid || !SourcePhysical.bGrounded || !TargetPhysical.bValid || !TargetPhysical.bGrounded || (!CanonicalMovingRecipientEnabled() && CanonicalMelee.Target->GetGameTimeSinceCreation()<0.1f)) return;
  bCanonicalControlSent=true;
  Clients[0]->ClientBeginCombatTrial(4,CanonicalMeleeTarget.Get()); // Existing control RPC maps the exact native recipient.
 }
 ObserveCanonicalActive(CanonicalMelee);
 if(CanonicalMelee.bViolation) { Finish(false,*CanonicalMelee.Error);return; }
 if(!CanonicalMelee.bEnded || Clients[0]->ConfirmedStage!=53) return;
 if(CanonicalMelee.NaturalEnds!=1 || (!CanonicalMovingRecipientEnabled() && CanonicalMelee.RawContacts<1) || CanonicalMelee.Effects!=1 || CanonicalMelee.OpenPackets<1 || CanonicalMelee.MaxPacketSegments<2 ||
    !CanonicalMelee.bMontage || CanonicalMelee.NativeFrame<0 || !CanonicalMelee.AttackFinalizations || CanonicalMelee.HealthAfter>=CanonicalMelee.HealthBefore)
 { Finish(false,TEXT("Canonical native contact/multi-piece packet/GE/natural end evidence incomplete"));return; }
 if(CanonicalMelee.Hit->GetActiveAttackDefinition() || CanonicalMelee.Anim->IsAnyMontagePlaying() ||
    CanonicalMelee.ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Attacking)) return;
 if(FPlatformTime::Seconds()-CanonicalMelee.EndedAt<0.25) return;
 if(CanonicalMelee.TargetASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute())!=CanonicalMelee.HealthAfter)
 { Finish(false,TEXT("Canonical target Health changed after natural end"));return; }
 if(!CleanupCanonicalMelee()) { Finish(false,TEXT("Canonical exact grant/recipient cleanup failed"));return; }
 Stage=6;StageStarted=FPlatformTime::Seconds();Clients[1]->ClientFinish(true);
}
bool AProject_JNetworkLoadGameMode::CleanupCanonicalMelee()
{
 if(!bCanonicalSetup) return true;if(bCanonicalCleaning) return bCanonicalCleanupPassed;
 bCanonicalCleaning=true;
 const bool Complete=CanonicalMelee.bEnded && CanonicalMelee.NaturalEnds==1 && CanonicalMelee.Effects==1 && !CanonicalMelee.bViolation;
 const TStrongObjectPtr<AProject_JPlayerCharacter> Source(CanonicalMelee.Source.Get());
 const TStrongObjectPtr<AProject_JNPCCharacter> Target(CanonicalMeleeTarget.Get());
 const TStrongObjectPtr<UProject_JAbilitySystemComponent> ASC(CanonicalMelee.ASC.Get());
 RetireCanonicalWitness(CanonicalMelee); // Detach exact delegates before equipment cancellation/replacement or actor destruction.
 bool Restored=false;
 if(Source && !Source->IsActorBeingDestroyed())
 {
  auto* State=Source->GetPlayerState<AProject_JPlayerState>();auto* Manager=State?State->GetEquipmentManagerComponent():nullptr;
  const TStrongObjectPtr<AProject_JPlayerState> StatePin(State);const TStrongObjectPtr<UProject_JEquipmentManagerComponent> ManagerPin(Manager);
  if(Manager && ASC && ASC->GetAvatarActor()==Source.Get() && ASC->GetActorInfoRevision()==CanonicalMelee.SourceEpoch &&
     CanonicalMelee.Equipment.IsValid() && CanonicalMelee.Equipment->GetWeaponRevision()==CanonicalMelee.WeaponRevision && Manager->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon)==CanonicalMeleeItem)
  {
   Manager->UnequipSlot(EProject_JEquipmentSlot::Weapon);
   if(IsValid(Source.Get()) && !Source->IsActorBeingDestroyed() && IsValid(State) && !State->IsActorBeingDestroyed() &&
      State==Source->GetPlayerState<AProject_JPlayerState>() && IsValid(Manager) && !Manager->IsBeingDestroyed() && Manager->IsRegistered() &&
      Manager==State->GetEquipmentManagerComponent() && IsValid(ASC.Get()) && !ASC->IsBeingDestroyed() && ASC->IsRegistered() &&
      Source->GetAbilitySystemComponent()==ASC.Get() && ASC->GetAvatarActor()==Source.Get() && ASC->GetActorInfoRevision()==CanonicalMelee.SourceEpoch &&
      !Manager->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon))
   { Manager->EquipItem(CanonicalMeleeOriginalItem);Restored=Manager->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon)==CanonicalMeleeOriginalItem; }
  }
 }
 bool Destroyed=!Target;
 bool NavigationRetired=!CanonicalMovingRecipientEnabled();
 if(Target)
 {
  if(CanonicalMovingRecipientEnabled())
  {
   Target->StopNavigation(); // Existing helper stops only its exact owned action and detaches before callback.
   const auto* OwnedAction=CanonicalRecipientAction.Get();
   NavigationRetired=bCanonicalRecipientAdmitted && CanonicalRecipientStopCalls==1 && bCanonicalRecipientStopped &&
    (!OwnedAction || (OwnedAction->GetOwnershipToken()==0 && OwnedAction->GetActionState()==EProjectJNPCActionState::Disabled));
  }
  Target->Destroy();Destroyed=Target->IsActorBeingDestroyed();
  TInlineComponentArray<UActorComponent*> Components(Target.Get());for(auto* Component:Components) if(Component->IsRegistered()) Destroyed=false;
 }
 if(CanonicalMovingRecipientEnabled())
 {
  const auto* Controller=CanonicalRecipientController.Get();
  bCanonicalRecipientControllerRetired=!bCanonicalRecipientOwnController || !Controller || Controller->IsActorBeingDestroyed();
 }
 CanonicalMeleeTarget=nullptr;
 CanonicalMelee.bOwnedSourcesRetired=ASC && CanonicalMelee.bOwnedSourcesInitiallyAbsent;
 if(ASC) for(const FName Key:CanonicalMeleeGrantSources) CanonicalMelee.bOwnedSourcesRetired&=!ASC->HasAbilityGrantSource(Key);
 const auto* ExecutionSpec=ASC?ASC->FindAbilitySpecFromHandle(CanonicalMelee.Grant):nullptr;
 const auto* ExecutionInstance=ExecutionSpec?ExecutionSpec->GetPrimaryInstance():nullptr;
 const bool ExecutionInactive=!ExecutionSpec || (!ExecutionSpec->IsActive() && (!ExecutionInstance || !ExecutionInstance->IsActive()));
 bCanonicalCleanupPassed=Restored && Destroyed && ASC && CanonicalMelee.bOwnedSourcesRetired && ExecutionInactive && NavigationRetired &&
  (!CanonicalMovingRecipientEnabled() || bCanonicalRecipientControllerRetired);
 if(CanonicalMovingRecipientEnabled() && !WriteCanonicalRecipientNavigation(NavigationRetired && Destroyed && bCanonicalRecipientControllerRetired)) bCanonicalCleanupPassed=false;
 return WriteCanonicalWitness(CanonicalMelee,TEXT("canonical-melee-authority.json"),Complete && bCanonicalCleanupPassed,bCanonicalCleanupPassed) && bCanonicalCleanupPassed;
}

AProject_JNetworkLoadGameMode::AProject_JNetworkLoadGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PlayerControllerClass = AProject_JNetworkLoadController::StaticClass();
	DefaultPawnClass = AProject_JNetworkViewPawn::StaticClass();
}
void AProject_JNetworkLoadGameMode::BeginPlay()
{
	Super::BeginPlay(); bFixture = Enabled(); Started = StageStarted = FPlatformTime::Seconds();
 if (FParse::Param(FCommandLine::Get(),TEXT("ProjectJCanonicalMeleeFixture")) && (!CanonicalMeleeEnabled() || FixtureClients()!=2 || CleaveEnabled() || RenderedClientEnabled() || CompanionAudienceEnabled() || EquipmentInterestEnabled() || PartyRosterEnabled() || CraftingJobsEnabled() || AttributeAudienceEnabled()))
 { Finish(false,TEXT("Canonical melee requires two Animation+CombatInput clients in its separate cohort")); return; }
 if(FParse::Param(FCommandLine::Get(),TEXT("ProjectJCanonicalMeleeMovingRecipient")) &&
    (!CanonicalMovingRecipientEnabled() || GetWorld()->GetPackage()->GetName()!=TEXT("/Game/Tests/Combat/L_CanonicalNetwork")))
 { Finish(false,TEXT("Moving canonical recipient requires the explicit canonical cohort on the selected canonical network test map"));return; }
	if (!bFixture) { SetActorTickEnabled(false); return; }
	if (CombatFanoutCount()!=0 && (!CombatFanoutEnabled() || GetNetMode()!=NM_DedicatedServer || FixtureClients()!=2 || CleaveThreatEnabled() || WorldClockEnabled()))
	{ Finish(false,TEXT("Fanout requires explicit32, dedicated authored Cleave and two baseline clients")); return; }
	if (CraftingJobsEnabled() && (FixtureClients()!=2 || AnimationEnabled() || CombatEnabled() || CleaveEnabled() || CompanionAudienceEnabled() || EquipmentInterestEnabled() || PartyRosterEnabled() || QuestsEnabled() || AttributeAudienceEnabled()))
	{ Finish(false, TEXT("Crafting transport requires exactly two separate non-provider clients")); return; }
	if (RenderedClientEnabled() && (FixtureClients()!=2 || CombatEnabled() || CleaveEnabled() || CompanionAudienceEnabled() ||
		EquipmentInterestEnabled() || PartyRosterEnabled() || QuestsEnabled() || AttributeAudienceEnabled()))
	{ Finish(false,TEXT("Rendered client requires exactly two authored owners in its separate cohort")); return; }
	if (CompanionAudienceEnabled() && (FixtureClients() != 2 || CombatEnabled())) { Finish(false, TEXT("Companion audience requires exactly2 authored clients and a separate combat cohort")); return; }
	if (PartyRosterEnabled() && (FixtureClients() != 2 || CombatEnabled() || CompanionAudienceEnabled())) { Finish(false, TEXT("Party roster needs exactly2 authored clients in its separate cohort")); return; }
	if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJNPCDamageThreatFixture")) && !CleaveEnabled())
	{ Finish(false, TEXT("DamageThreat fixture requires the separate authored Cleave cohort")); return; }
	if (CleaveEnabled() && (FixtureClients() != 2 || CombatEnabled() || CompanionAudienceEnabled() || EquipmentInterestEnabled() || PartyRosterEnabled() || QuestsEnabled() || AttributeAudienceEnabled()))
	{ Finish(false, TEXT("Cleave requires exactly2 authored clients in its separate cohort")); return; }
	if (AttributeAudienceEnabled() && !AnimationEnabled()) { Finish(false, TEXT("Attribute audience requires actual authored player avatars")); return; }
	if (WorldClockEnabled())
	{
		auto* State = GetWorld()->SpawnActorDeferred<AProject_JWorldState>(AProject_JWorldState::StaticClass(), FTransform::Identity);
		if (!State) { Finish(false, TEXT("World clock fixture spawn failed")); return; }
		auto* Definition = NewObject<UProject_JWorldDefinition>(State);
		Definition->RegionId = TEXT("NetworkFixture.WorldClock"); Definition->DayLengthSeconds = 600; Definition->InitialHour = 19;
		State->Definition = Definition; State->FinishSpawning(FTransform::Identity); State->SetRain(.4f, 0);
	}
	bAllRelevant = FParse::Param(FCommandLine::Get(), TEXT("ProjectJDAllRelevant"));
	auto* Driver = GetWorld()->GetNetDriver();
	if (!Driver || !Driver->GetReplicationSystem()) { Finish(false, TEXT("Iris replication system missing")); return; }
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 I = 0; I < FixtureNPCs(); ++I)
	{
		UClass* Class = bAllRelevant ? AProject_JAlwaysRelevantProbeNPC::StaticClass() : AProject_JNetworkProbeNPC::StaticClass();
		auto* NPC = GetWorld()->SpawnActor<AProject_JNetworkProbeNPC>(Class, FVector((I < FixtureNPCs() / 2 ? 0 : 20000) + (I % 32) * 20, ((I % (FixtureNPCs() / 2)) / 32) * 20, 100), FRotator::ZeroRotator, Params);
		NPC->ProbeId = I + 1;
		NPC->SetActorEnableCollision(false); NPCs.Add(NPC);
	}
	if (AttributeAudienceEnabled())
	{
		Params.Name = TEXT("AttributeAudienceNPC"); Params.bDeferConstruction = true; Params.ObjectFlags |= RF_Transient;
		AttributeAudienceNPC = GetWorld()->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(), FVector(0, 200, 100), FRotator::ZeroRotator, Params);
		if (!AttributeAudienceNPC) { Finish(false, TEXT("Actual GAS NPC spawn failed")); return; }
		// This native class keeps its Iris spatial filter. Retain floor collision
		// for its live Mover and cover both fixture clusters with an explicit range.
		AttributeAudienceNPC->SetActorEnableCollision(true);
		AttributeAudienceNPC->AutoPossessAI = EAutoPossessAI::Disabled;
		AttributeAudienceNPC->FinishSpawning(FTransform(FRotator::ZeroRotator, FVector(0, 200, 100)));
		// NPC BeginPlay applies its production 80m default, so override only afterward.
		AttributeAudienceNPC->SetNetCullDistanceSquared(FMath::Square(100000.0f));
	}
	const TCHAR* ServerNetMode = GetNetMode() == NM_ListenServer ? TEXT("ListenServer") : GetNetMode() == NM_DedicatedServer ? TEXT("DedicatedServer") : TEXT("Unexpected");
	UE_LOG(LogProjectJDNetwork, Display, TEXT("READY Iris=1 NetMode=%s AllRelevant=%d NPCs=%d Clients=%d"), ServerNetMode, bAllRelevant, FixtureNPCs(), FixtureClients());
	const auto* IrisConfig = GEngine->GetIrisNetDriverConfig(NAME_GameNetDriver, NAME_GameNetDriver);
	const auto* ParallelCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("net.iris.AllowParallelNetTick"));
	UE_LOG(LogProjectJDNetwork, Display, TEXT("ParallelSettings DriverAllowed=%d TasksAllowed=%d MinimumConnections=%d CVar=%d"),
		IrisConfig && IrisConfig->bCanUseParallelNetConnectionTick, Driver->ReplicationSystemConfigServer.bAllowParallelTasks,
		Driver->ReplicationSystemConfigServer.MinConnectionsForParallelTick, ParallelCVar ? ParallelCVar->GetInt() : -1);
}
void AProject_JNetworkLoadGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	if (!Enabled()) { return; }
	auto* PC = CastChecked<AProject_JNetworkLoadController>(NewPlayer);
	// A packaged Game target creates a normal local host. Only remote socket
	// clients enter the authored cohort; retain the host's normal pawn/PS lifecycle.
	if (GetNetMode() == NM_ListenServer && PC->IsLocalController() && !PC->GetNetConnection())
	{
		auto* ObserverState = PC->GetPlayerState<AProject_JPlayerState>();
		if (!IsValid(ObserverState) || !ObserverState->HasAuthority() || !Clients.IsEmpty())
		{ Finish(false, TEXT("Listen observer must be marked before remote admission")); return; }
		for (TActorIterator<AProject_JPlayerState> It(GetWorld()); It; ++It)
			if (*It != ObserverState && !It->IsActorBeingDestroyed() && It->GetPlayerId() == FixtureListenObserverPlayerId)
			{ Finish(false, TEXT("Duplicate reserved listen observer PlayerId")); return; }
		ObserverState->SetPlayerId(FixtureListenObserverPlayerId);
		UE_LOG(LogProjectJDNetwork, Display, TEXT("LISTEN_HOST excluded=1 local=1 connection=0 fixtureSlot=%d nativePlayerId=%d"), PC->FixtureSlot, ObserverState->GetPlayerId());
		return;
	}
	auto* PS = PC->GetPlayerState<AProject_JPlayerState>();
	if (!IsValid(PS) || PS->GetPlayerId() == FixtureListenObserverPlayerId)
	{ Finish(false, TEXT("Participant PlayerId collides with reserved listen observer")); return; }
	if (GetNetMode() == NM_ListenServer && PartyRosterEnabled())
	{
		int32 ObserverStates = 0;
		for (TActorIterator<AProject_JPlayerState> It(GetWorld()); It; ++It)
			if (!It->IsActorBeingDestroyed() && It->GetPlayerId() == FixtureListenObserverPlayerId) ++ObserverStates;
		if (ObserverStates != 1) { Finish(false, TEXT("Fresh listen party cohort needs exactly one marked observer")); return; }
	}
	if (Clients.Num() >= FixtureClients()) { Finish(false, TEXT("Unexpected extra connection")); return; }
	PC->FixtureSlot = Clients.Num(); Clients.Add(PC);
	PS->SetPublicCharacterSnapshot(FName(*FString::Printf(TEXT("DClient%d"), PC->FixtureSlot)), 1);
	auto* Definition = LoadObject<UProject_JEquipmentItemDefinition>(nullptr, TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.DA_Greatsword_Equip"));
	if (!Definition) { Finish(false, TEXT("Read-only equipment fixture asset unavailable")); return; }
	const auto Item = PS->GetInventoryComponent()->AddItemDefinition(Definition);
	Items.Add(Item.InstanceId);
	PS->GetEquipmentManagerComponent()->TryEquipItemInstanceById(Item.InstanceId);
	if (CompanionAudienceEnabled())
	{
		auto* Def = LoadObject<UProject_JCompanionItemDefinition>(nullptr, TEXT("/Game/DataAssetSets/Companion/DA_Companion_Follow_NPC_Test.DA_Companion_Follow_NPC_Test"));
		auto* Data = GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
		if (!Def || !Def->IsFollowDefinitionValid() || !Data || Data->GetItemDefinition(Def->ItemId, Def->SchemaVersion) != Def)
		{ Finish(false, TEXT("Canonical companion definition is unavailable")); return; }
		const auto Owned = PS->GetInventoryComponent()->AddItemDefinition(Def);
		if (!Owned.InstanceId.IsValid()) { Finish(false, TEXT("Companion inventory ownership grant failed")); return; }
		CompanionItems.Add(Owned.InstanceId);
	}
	if (QuestsEnabled())
	{
		auto* Quest = PS->FindComponentByClass<UProject_JQuestComponent>();
		auto* Def = NewObject<UProject_JQuestDefinition>(Quest);
		Def->QuestId = FixtureQuestId; Def->ObjectiveEvent = FixtureQuestEvent; Def->RequiredCount = 100;
		Quest->Definitions.Add(Def);
		if (!Quest->Accept(FixtureQuestId)) { Finish(false, TEXT("Quest fixture admission failed")); return; }
        auto* Compound = NewObject<UProject_JQuestDefinition>(Quest); Compound->QuestId = FixtureCompoundQuestId;
        FProject_JQuestObjectiveDefinition First; First.ObjectiveId = TEXT("First"); First.Event = FixtureQuestEvent; First.RequiredCount = 100;
        auto Second = First; Second.ObjectiveId = TEXT("Second"); Compound->Objectives = {First, Second};
        Quest->Definitions.Add(Compound);
        if (!Quest->Accept(FixtureCompoundQuestId)) { Finish(false, TEXT("Compound quest fixture admission failed")); return; }
	}
}
TArray<int32> AProject_JNetworkLoadGameMode::ExpectedIds(int32 Slot) const
{
	TArray<int32> Result;
	const int32 Cluster = ((CleaveEnabled() && Stage >= 30) || (RenderedClientEnabled() && Stage >= 39)) ? 0 : Stage == 2 ? 1 - Slot % 2 : Slot % 2;
	for (int32 Id = 1; Id <= FixtureNPCs(); ++Id)
	{
		if (Stage >= 4 && Id <= 10) { continue; }
		if (bAllRelevant || (Id <= FixtureNPCs() / 2 ? 0 : 1) == Cluster) { Result.Add(Id); }
	}
	return Result;
}
void AProject_JNetworkLoadGameMode::StartStage(int32 NewStage)
{
	if (RegionId) { TRACE_END_REGION_WITH_ID(RegionId); }
	const FString Region = FString::Printf(TEXT("ProjectJ.Network.Stage%d"), NewStage);
	RegionId = TRACE_BEGIN_REGION_WITH_ID(*Region);
	Stage = NewStage; StageStarted = FPlatformTime::Seconds(); RequiredPulse = Pulse + 1;
	auto* Driver = GetWorld()->GetNetDriver(); StageOutBytes = Driver ? Driver->OutTotalBytes : 0;
	if (Stage == 4) { for (int32 I = 0; I < 10; ++I) { NPCs[I]->Destroy(); NPCs[I] = nullptr; } }
	if (Stage == 5)
	{
		for (int32 I = 0; I < Clients.Num(); ++I)
		{
			auto* PS = Clients[I]->GetPlayerState<AProject_JPlayerState>();
			for (auto* Item : PS->GetEquipmentManagerComponent()->GetAllEquippedItems()) { PS->GetEquipmentManagerComponent()->UnequipItem(Item); }
			PS->GetInventoryComponent()->RemoveItemInstance(Items[I]);
		}
	}
	if (AttributeAudienceEnabled())
	{
		for (AProject_JNetworkLoadController* PC : Clients)
		{
			auto* PS = PC->GetPlayerState<AProject_JPlayerState>();
			if (!PS || !PublishAttributeBases(PS->GetAbilitySystemComponent(), PC->FixtureSlot, Stage, PS->GetPublicClassId().ToString(), AttributeAuthorityRows))
			{ Finish(false, TEXT("Player ASC attribute base publication failed")); return; }
			PS->ForceNetUpdate();
		}
		if (!IsValid(AttributeAudienceNPC) || !PublishAttributeBases(AttributeAudienceNPC->GetAbilitySystemComponent(), AttributeNPCSlot, Stage, TEXT("NPC"), AttributeAuthorityRows))
		{ Finish(false, TEXT("NPC ASC attribute base publication failed")); return; }
		AttributeAudienceNPC->ForceNetUpdate();
		UE_LOG(LogProjectJDNetwork, Display, TEXT("AttributeAudienceNPC stage=%d replicated=%d begun=%d destroying=%d collision=%d alwaysRelevant=%d cull=%.0f location=%s"),
			Stage, AttributeAudienceNPC->GetIsReplicated(), AttributeAudienceNPC->HasActorBegunPlay(), AttributeAudienceNPC->IsActorBeingDestroyed(),
			AttributeAudienceNPC->GetActorEnableCollision(), AttributeAudienceNPC->bAlwaysRelevant,
			FMath::Sqrt(AttributeAudienceNPC->GetNetCullDistanceSquared()), *AttributeAudienceNPC->GetActorLocation().ToCompactString());
	}
	for (AProject_JNetworkLoadController* PC : Clients)
	{
		const int32 Slot = PC->FixtureSlot;
		if (QuestsEnabled())
		{
			auto* Quest = PC->GetPlayerState<AProject_JPlayerState>()->FindComponentByClass<UProject_JQuestComponent>();
			Quest->RecordObjective(FixtureQuestEvent, 1);
			Quest->RequestTracking(FixtureQuestId, Stage != 2);
            Quest->RequestTracking(FixtureCompoundQuestId, Stage != 2);
		}
		if (APawn* Pawn = PC->GetPawn())
		{
			const FVector Target((Stage == 2 ? 1 - Slot % 2 : Slot % 2) * 20000,0,Pawn->GetActorLocation().Z);
            if(auto* Player=Cast<AProject_JPlayerCharacter>(Pawn))
            {
                auto Teleport=MakeShared<FTeleportEffect>();Teleport->TargetLocation=Target;
                Player->GetMover()->QueueInstantMovementEffect(Teleport);
            }
            else Pawn->SetActorLocation(Target);
            Pawn->ForceNetUpdate();
			PC->SetViewTarget(Pawn);
		}
		PC->ConfirmedStage = INDEX_NONE;
		PC->ClientConfigure(Stage, Slot, ExpectedIds(Slot), Items[Slot], Stage < 5, RequiredPulse);
	}
	if (AnimationEnabled())
	{
		TArray<AProject_JPlayerCharacter*> Players; TArray<FProject_JReplicatedAnimEventState> States;
		for (AProject_JNetworkLoadController* PC : Clients)
		{
			auto* Player = CastChecked<AProject_JPlayerCharacter>(PC->GetPawn());
			auto* Events = Player->FindComponentByClass<UProject_JReplicatedAnimEventComponent>();
			if (Stage == 1) { Events->DispatchMoveStarted(false); }
			if (Stage == 2) { Events->DispatchMoveStopped(false); }
			if (Stage == 3) { Events->DispatchLandingStarted(650, false, false, false); }
			if (Stage == 4) { Events->DispatchLandingCancelled(); }
			if (Stage == 5) { Events->DispatchMoveStarted(false); Events->DispatchLandingStarted(650, false, false, false); Events->DispatchLandingCancelled(); Events->DispatchMoveStopped(false); }
			Player->ForceNetUpdate(); Players.Add(Player); States.Add(ReadAnimationEvents(Player));
		}
		for (AProject_JNetworkLoadController* PC : Clients) { PC->ClientAnimationExpect(Stage, Players, States); }
	}
	TRACE_BOOKMARK(TEXT("ProjectJ.Network Stage=%d AllRelevant=%d Connections=%d"), Stage, bAllRelevant, Clients.Num());
}
bool AProject_JNetworkLoadGameMode::ValidateObservation(AProject_JNetworkLoadController* PC, int32 InStage, const TArray<int32>& Seen, bool bOwn, bool bForeignEmpty, bool bEquipment, int32 MinPulse, bool bAnimation, bool bWorldClock)
{
	return bFixture && !bFinished && Clients.Contains(PC) && InStage == Stage && Stage > 0
		&& Seen == ExpectedIds(PC->FixtureSlot) && bOwn && bForeignEmpty && bEquipment && MinPulse >= RequiredPulse && bAnimation && bWorldClock
		&& (!CleaveEnabled() || Stage < 30 || bCleaveStageReady);
}
void AProject_JNetworkLoadGameMode::Logout(AController* Exiting)
{
	auto* PC = Cast<AProject_JNetworkLoadController>(Exiting);
	if (PC && Clients.Contains(PC))
	{
		if (Stage == 6 && PC->FixtureSlot == 1) { bSawDisconnect = true; }
		if (CanonicalMeleeEnabled() && !bFinished && Stage==53) Finish(false,TEXT("Canonical melee owner disconnected"));
		if (RenderedClientEnabled() && !bFinished && Stage != 6) Finish(false,TEXT("Rendered cohort owner disconnected"));
		if (CleaveEnabled() && !bFinished && Stage != 6) Finish(false, TEXT("Cleave owner disconnected before completion"));
	}
	Super::Logout(Exiting);
}
void AProject_JNetworkLoadGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds); if (!bFixture || bFinished) { return; }
	const double Now = FPlatformTime::Seconds();
	if (Now - Started > (RenderedClientEnabled() ? 180 : 120) || (Stage > 0 && Now - StageStarted > 25))
	{
        // On a failed readiness boundary, reuse Iris's native readonly dump for
        // three representative PS roots. No policy or timing changes.
        if (RenderedClientEnabled() && Stage==40 && RenderedStates.Num()==198 && GEngine)
        {
            FString Ids;
            TSet<uint64> SeenIds;
            for (const int32 Index : {0,37,197})
                if (const auto* State=RenderedStates[Index].Get(); IsValid(State))
                    if (const auto* Bridge=UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(State))
                    {
                        const auto AppendObject=[&](const UObject* Object)
                        {
                            const uint64 Id=Object ? Bridge->GetReplicatedRefHandle(Object).GetId():0;
                            if (Id && !SeenIds.Contains(Id))
                            {
                                SeenIds.Add(Id);
                                if (!Ids.IsEmpty()) Ids+=TEXT(",");
                                Ids+=LexToString(Id);
                            }
                        };
                        AppendObject(State);
                        // Three PS bundles only: at most eight replicated components
                        // plus the existing AttributeSet. No new reflection/debug API.
                        TInlineComponentArray<UActorComponent*> Components(State);
                        int32 IrisReplicatedComponentCount=0;
                        for (const UActorComponent* Component:Components)
                            if (IsValid(Component) && Component->GetIsReplicated())
                            {
                                if (++IrisReplicatedComponentCount>8) break;
                                AppendObject(Component);
                            }
                        AppendObject(State->GetProjectJAttributeSet());
                    }
            if (auto* System=GetWorld()->GetNetDriver()->GetReplicationSystem(); System && !Ids.IsEmpty())
                GEngine->Exec(GetWorld(),*FString::Printf(TEXT("Net.Iris.PrintNetInfoOfObject FindById=%s RepSystemId=%u"),*Ids,System->GetId()));
        }
        FString Details;
        for (AProject_JNetworkLoadController* PC : Clients)
        {
            if (!IsValid(PC)) continue;
            if (CanonicalMovingRecipientEnabled() && Stage==0)
            {
                // One failed-boundary snapshot only: preserve all spawn, contact and landing gates.
                const TStrongObjectPtr<APawn> Pawn(PC->GetPawn());
                const bool Alive=IsValid(Pawn.Get()) && !Pawn->IsActorBeingDestroyed();
                const auto* Avatar=Alive?Cast<AProject_JPlayerCharacter>(Pawn.Get()):nullptr;
                const auto* Locomotion=Avatar?Avatar->GetLocomotionAnimStateComponent():nullptr;
                const auto Physical=Alive?Project_J::Movement::ReadPhysicalFrame(Pawn.Get()):Project_J::Movement::FPhysicalFrame();
                const auto* Mover=Alive?Project_J::Movement::FindMover(Pawn.Get()):nullptr;
                const auto* Capsule=Alive?Project_J::Movement::FindCapsule(Pawn.Get()):nullptr;
                const auto* Step=Mover?&Mover->GetLastTimeStep():nullptr;
                const AActor* Settings=GetWorld()->GetWorldSettings(); // Same public actor view used by spawn fallback.
                Details+=FString::Printf(TEXT("Stage0 slot=%d PC=%s Pawn=%s Class=%s alive=%d controller_match=%d position=%s world_settings=%s physical_valid=%d mover=%d grounded=%d in_air=%d mode=%d landing=%d velocity=%s floor=%s floor_blocking=%d floor_penetrating=%d floor_point=%s mover_registered=%d initialized=%d begun=%d async=%d frame=%d sim_ms=%.17g step_ms=%.9g capsule_registered=%d capsule_collision=%d; "),
                    PC->FixtureSlot,*GetNameSafe(PC),*GetNameSafe(Pawn.Get()),*GetNameSafe(Pawn?Pawn->GetClass():nullptr),Alive,
                    Alive && Pawn->GetController()==PC,*(Alive?Pawn->GetActorLocation():FVector::ZeroVector).ToString(),
                    *(Settings?Settings->GetActorLocation():FVector::ZeroVector).ToString(),Physical.bValid,Physical.bMover,Physical.bGrounded,Physical.bInAir,int32(Physical.Mode),
                    Locomotion?int32(Locomotion->bIsLanding):-1,*Physical.Velocity.ToString(),*GetNameSafe(Physical.Floor.GetActor()),Physical.Floor.bBlockingHit,Physical.Floor.bStartPenetrating,*Physical.Floor.ImpactPoint.ToString(),
                    Mover && Mover->IsRegistered(),Mover && Mover->HasBeenInitialized(),Mover && Mover->HasBegunPlay(),Mover && Mover->IsBackendAsync(),
                    Step?Step->ServerFrame:INDEX_NONE,Step?Step->BaseSimTimeMs:-1.0,Step?double(Step->StepMs):-1.0,
                    Capsule && Capsule->IsRegistered(),Capsule?int32(Capsule->GetCollisionEnabled()):-1);
            }
            else Details+=PC->LastObservation+TEXT("; ");
        }
		Finish(false, FString::Printf(TEXT("Timeout stage=%d %s"), Stage, *Details)); return;
	}
	if (Now >= NextPulse)
	{
		NextPulse = Now + 0.1; ++Pulse;
		for (AProject_JNetworkProbeNPC* NPC : NPCs) { if (IsValid(NPC)) { NPC->Pulse = Pulse; } }
	}
	if (Stage == 0 && Clients.Num() == FixtureClients() && !Clients.ContainsByPredicate([](const auto& PC) { return !IsValid(PC) || !PC->GetPawn(); }))
	{
		if (AnimationEnabled())
		{
            TArray<FVector> NativeSpawnCenters;
            if(CanonicalMovingRecipientEnabled() && Clients.ContainsByPredicate([](const auto& PC) { return !Cast<AProject_JPlayerCharacter>(PC->GetPawn()); }))
            {
                // Two fixed initial scene slots, projected once before either authored Pawn is created.
                auto* Class=LoadClass<AProject_JPlayerCharacter>(nullptr,TEXT("/Game/Character_BPs/Spellblade/BP_Spellblade.BP_Spellblade_C"));
                const auto* Template=Class?Class->GetDefaultObject<AProject_JPlayerCharacter>():nullptr;
                const auto* Capsule=Template?Cast<UCapsuleComponent>(Template->GetRootComponent()):nullptr;
                float Radius=0,HalfHeight=0;if(Capsule) Capsule->GetScaledCapsuleSize(Radius,HalfHeight);
                auto* Navigation=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
                const AActor* StartSettings=GetWorld()->GetWorldSettings();
                const FVector Anchor=StartSettings->GetActorLocation();
                const auto* Data=Navigation && Template?Navigation->GetNavDataForProps(Template->GetNavAgentPropertiesRef(),Anchor):nullptr;
                if(Clients.Num()!=2 || !Capsule || !Capsule->IsQueryCollisionEnabled() || !FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight) || Radius<=0 || HalfHeight<Radius ||
                   !Navigation || Navigation->IsNavigationBuildInProgress() || !IsValid(Data) || Data->IsActorBeingDestroyed() || !Data->IsRegistered() || Data->GetWorld()!=GetWorld())
                { Finish(false,TEXT("Authored-map initial slots require actual CDO capsule and registered compatible NavData"));return; }
                for(int32 Slot=0;Slot<2;++Slot)
                {
                    const FVector Seed=Anchor+FVector(0,double(Slot)*4.0*Radius,0);
                    FNavLocation Foot;
                    if(!Navigation->ProjectPointToNavigation(Seed,Foot,Data->GetDefaultQueryExtent(),Data) || Foot.Location.ContainsNaN() ||
                       !FVector2D(Seed.X,Seed.Y).Equals(FVector2D(Foot.Location.X,Foot.Location.Y),UE_KINDA_SMALL_NUMBER))
                    { Finish(false,FString::Printf(TEXT("Authored-map initial slot projection rejected slot=%d seed=%s"),Slot,*Seed.ToString()));return; }
                    NativeSpawnCenters.Add(Foot.Location+Capsule->GetUpVector()*HalfHeight);
                }
                if(FVector::DistSquared(NativeSpawnCenters[0],NativeSpawnCenters[1])<FMath::Square(2.0*Radius))
                { Finish(false,TEXT("Authored-map initial slots do not separate the actual capsules"));return; }
            }
            bool bSpawnedThisTick=false;
			for (AProject_JNetworkLoadController* PC : Clients)
			{
				APawn* OldPawn = PC->GetPawn();
                if(Cast<AProject_JPlayerCharacter>(OldPawn)) continue;
                auto* PS = PC->GetPlayerState<AProject_JPlayerState>();
                FString SpawnFailure;
                if(CanonicalMovingRecipientEnabled() && !NativeSpawnCenters.IsValidIndex(PC->FixtureSlot))
                { Finish(false,TEXT("Authored-map initial slot identity changed before Pawn creation"));return; }
                const FVector SpawnLocation=CanonicalMovingRecipientEnabled()?NativeSpawnCenters[PC->FixtureSlot]:OldPawn->GetActorLocation();
				auto* Player = ProjectJAuthoredAnimationFixture::Spawn(GetWorld(), PC, PS, SpawnLocation,false,CanonicalMovingRecipientEnabled(),&SpawnFailure);
				if (!Player) { Finish(false, CanonicalMovingRecipientEnabled()?TEXT("Authored-map native spawn admission failed: ")+SpawnFailure:TEXT("Authored animation fixture spawn failed")); return; }
				OldPawn->Destroy();
                bSpawnedThisTick=true;
				PS->SetPublicCharacterSnapshot(FName(*FString::Printf(TEXT("DClient%d"), PC->FixtureSlot)), 1);
			}
            if(bSpawnedThisTick) return; // Let the newly constructed Mover simulate before checking contact.
		}
        if(AnimationEnabled()) for(const auto& PC:Clients)
        {
            const auto* Player=CastChecked<AProject_JPlayerCharacter>(PC->GetPawn());
            const auto Physical=Project_J::Movement::ReadPhysicalFrame(Player);
            // Capture semantic expectations only after the real initial touchdown settles.
            if(!Physical.bValid || !Physical.bGrounded || Player->GetLocomotionAnimStateComponent()->bIsLanding) return;
        }
        StartStage(1); return;
	}
	if (CanonicalMeleeEnabled() && Stage==53) { UpdateCanonicalMelee(); return; }
	if (CombatFanoutEnabled() && Stage>=50 && Stage<=52) { UpdateCombatFanout(); return; }
	if (RenderedClientEnabled() && Stage >= 39 && Stage <= 45) { UpdateRenderedCrowd(); return; }
	if (CleaveEnabled() && ((Stage >= 30 && Stage <= 32) || CleaveThreatStage(Stage))) { UpdateCleave(); return; }
	if (Stage >= 1 && Stage <= 5 && Now - StageStarted >= 3
		&& !Clients.ContainsByPredicate([this](const auto& PC) { return !IsValid(PC) || PC->ConfirmedStage != Stage; }))
	{
		auto* Driver = GetWorld()->GetNetDriver();
		Rows += FString::Printf(TEXT("%d,%.3f,%d,%u\n"), Stage, Now - StageStarted, Driver->ClientConnections.Num(), Driver->OutTotalBytes - StageOutBytes);
		UE_LOG(LogProjectJDNetwork, Display, TEXT("PASS stage=%d connections=%d bytes=%u"), Stage, Driver->ClientConnections.Num(), Driver->OutTotalBytes - StageOutBytes);
		if (Stage < 5) { StartStage(Stage + 1); }
		else
		{
			TRACE_END_REGION_WITH_ID(RegionId); RegionId = 0;
			if (CombatFanoutEnabled()) { BeginCombatFanout(); }
			else if (CraftingJobsEnabled()) { StartCraftingStage(46); }
			else if (RenderedClientEnabled()) { BeginRenderedCrowd(); }
			else if (CleaveEnabled()) { StartCleaveStage(30); }
			else if (CombatEnabled()) { BeginCombatTrial(); }
			else if (EquipmentInterestEnabled()) { StartEquipmentStage(16); }
			else if (PartyRosterEnabled()) { StartPartyStage(20); }
			else if (CompanionAudienceEnabled()) { StartCompanionStage(8); }
			else { Stage = 6; StageStarted = Now; Clients[1]->ClientFinish(true); }
		}
	}
	if (CraftingJobsEnabled() && Stage >= 46 && Stage <= 49 && Now - StageStarted >= 3 &&
		!Clients.ContainsByPredicate([this](const auto& PC) { return !IsValid(PC) || PC->ConfirmedStage != Stage; }))
	{
		if (Stage < 49) StartCraftingStage(Stage + 1);
		else { Stage = 6; StageStarted = Now; Clients[1]->ClientFinish(true); }
	}
	if (Stage == 7 && ObserveServerCombat()
		&& !Clients.ContainsByPredicate([](const auto& PC) { return !IsValid(PC) || PC->ConfirmedStage != 7; }))
	{
		UE_LOG(LogProjectJDNetwork, Display, TEXT("PASS combat trial=%d authority + owner prediction + remote R/target/end"), CombatTrial);
		if (CombatTrial < 3) { BeginCombatTrial(); }
        else if (CanonicalMeleeEnabled()) { BeginCanonicalMelee(); }
		else { Stage = 6; StageStarted = Now; Clients[1]->ClientFinish(true); }
	}
	if (CompanionAudienceEnabled() && Stage >= 8 && Stage <= 14)
	{
		if (!bCompanionPublished) { PublishCompanionStage(); return; }
		if (Now - StageStarted >= 3 && !Clients.ContainsByPredicate([this](const auto& PC) { return !IsValid(PC) || PC->ConfirmedStage != Stage; }))
		{
			if (Stage < 14) StartCompanionStage(Stage + 1);
			else { Stage = 6; StageStarted = Now; Clients[1]->ClientFinish(true); }
		}
	}
	if (EquipmentInterestEnabled() && EquipmentInterestStage(Stage))
	{
		if (Now - StageStarted >= 3 && !Clients.ContainsByPredicate([this](const auto& PC) { return !IsValid(PC) || PC->ConfirmedStage != Stage; }))
		{
			if (Stage < 19) StartEquipmentStage(Stage + 1);
			else if (Stage == 19) StartEquipmentStage(28);
			else if (Stage == 28) StartEquipmentStage(29);
			else if (PartyRosterEnabled()) StartPartyStage(20);
			else { Stage = 6; StageStarted = Now; Clients[1]->ClientFinish(true); }
		}
	}
	if (PartyRosterEnabled() && Stage >= 20 && Stage <= PartyLastSupportedPhase())
	{
		const int32 Required = Stage == 27 ? 1 : 2;
		bool Ready = true;
		for (int32 Slot = 0; Slot < Required; ++Slot)
			Ready &= IsValid(Clients[Slot]) && Clients[Slot]->ConfirmedStage == Stage;
		if (PlayerStateAudienceOwnerViewEnabled() && Stage == 29)
		{
			const bool Detached = Clients.Num() == 2 && PartyOwnerViewDetached(Clients[1], RetiredAudiencePawn.Get());
			if (!Detached) OwnerViewDetachedSince = 0;
			else if (OwnerViewDetachedSince == 0) OwnerViewDetachedSince = Now;
			Ready &= Detached && Now - OwnerViewDetachedSince >= 8.0;
		}
		if (PlayerStateAudienceOwnerViewEnabled() && Stage == 31) Ready &= PartyOwnerViewRestored(Clients[1]);
		if (Ready && Now - StageStarted >= 3)
		{
			RecordPartyAuthority();
			if (bFinished) return;
			if (Stage < 26) StartPartyStage(Stage + 1);
			else if (Stage == 26 && PlayerStateAudienceEnabled()) StartPartyStage(28);
			else if (Stage == 28) StartPartyStage(PlayerStateAudienceLifetimeEnabled() ? 30 : 29);
			else if (Stage == 30) StartPartyStage(29);
			else if (Stage == 29 && PlayerStateAudienceLifetimeEnabled()) StartPartyStage(31);
			else if (Stage == 26 || Stage == 29 || Stage == 31) { Stage = 6; StageStarted = Now; Clients[1]->ClientFinish(true); }
			else Finish(true, TEXT("Party owner-only roster, actual HUD, distant Pawn absence and disconnect summaries passed"));
		}
	}
	if (Stage == 6 && bSawDisconnect && GetWorld()->GetNetDriver()->ClientConnections.Num() == FixtureClients() - 1)
	{
		if (PartyRosterEnabled()) StartPartyStage(27);
		else Finish(true, TEXT("Five AOI/FastArray stages and graceful disconnect passed"));
	}
}
void AProject_JNetworkLoadGameMode::Finish(bool bSuccess, const FString& Reason)
{
	if (bFinished) { return; } bFinished = true;
 if (CanonicalMeleeEnabled() && bCanonicalSetup) bSuccess &= CleanupCanonicalMelee();
	if (PlayerStateAudienceLifetimeEnabled())
	{
		for (AProject_JNetworkLoadController* PC : Clients) if (IsValid(PC)) PartyOwnershipAuthorityDiagnosticRows += PartyOwnershipSnapshot(PC, Stage, PC->FixtureSlot, TEXT("final"));
		FFileHelper::SaveStringToFile(PartyOwnershipAuthorityDiagnosticRows, *(OutputRoot() / TEXT("party-ownership.jsonl")));
		if (PlayerStateAudienceOwnerViewEnabled()) FFileHelper::SaveStringToFile(OwnerViewAuthorityRows, *(OutputRoot() / TEXT("player-state-owner-view.csv")));
	}
	// The runner starts only this explicitly opted-in server capture. STOP
	// queues end-frame finalization; default csv.BlockOnCaptureEnd=1 is retained.
	if (FParse::Param(FCommandLine::Get(),TEXT("ProjectJIrisCSVFixture")) && GEngine)
		GEngine->Exec(GetWorld(),TEXT("csvprofile STOP"));
	if (RenderedClientEnabled()) CleanupRenderedCrowd();
	if (CombatFanoutEnabled())
	{
		bSuccess &= CleanupCombatFanout();
		bSuccess &= FFileHelper::SaveStringToFile(FanoutTargets,*(OutputRoot()/TEXT("combat-fanout-targets.csv")));
		bSuccess &= FFileHelper::SaveStringToFile(FanoutSummary,*(OutputRoot()/TEXT("combat-fanout-summary.csv")));
	}
	else if (CleaveEnabled()) CleanupCleave();
	if (RegionId) { TRACE_END_REGION_WITH_ID(RegionId); RegionId = 0; }
	if (CraftingJobsEnabled()) FFileHelper::SaveStringToFile(CraftingAuthorityRows, *(OutputRoot() / TEXT("crafting-authority.csv")));
	WriteResult(TEXT("server.json"), bSuccess, Reason);
	FFileHelper::SaveStringToFile(Rows, *(OutputRoot() / TEXT("network-phases.csv")));
	if (EquipmentInterestEnabled() && QuestsEnabled()) FFileHelper::SaveStringToFile(EquipmentCommandAuthorityRows, *(OutputRoot() / TEXT("owner-commands.csv")));
	if (CleaveEnabled()) FFileHelper::SaveStringToFile(CleaveAuthorityRows, *(OutputRoot() / TEXT("cleave-authority.csv")));
	if (CleaveThreatEnabled()) FFileHelper::SaveStringToFile(CleaveThreatAuthorityRows, *(OutputRoot() / TEXT("cleave-threat-authority.csv")));
	if (PartyRosterEnabled()) { FFileHelper::SaveStringToFile(PartyAuthorityRows, *(OutputRoot() / TEXT("party-members.csv"))); }
	if (PlayerStateAudienceEnabled()) FFileHelper::SaveStringToFile(PlayerStateBindingRows, *(OutputRoot() / TEXT("player-state-bindings.csv")));
	if (PlayerStateAudienceLifetimeEnabled()) FFileHelper::SaveStringToFile(PlayerStateLifetimeAuthorityRows, *(OutputRoot() / TEXT("player-state-lifetime.csv")));
	if (CompanionAudienceEnabled()) { FFileHelper::SaveStringToFile(CompanionAuthorityRows, *(OutputRoot() / TEXT("companion-audience.csv"))); }
	if (AttributeAudienceEnabled()) { FFileHelper::SaveStringToFile(AttributeAuthorityRows, *(OutputRoot() / TEXT("attribute-audience.csv"))); }
	UE_LOG(LogProjectJDNetwork, Display, TEXT("FINISH success=%d %s"), bSuccess, *Reason);
	for (AProject_JNetworkLoadController* PC : Clients) { if (IsValid(PC) && PC->GetNetConnection()) { PC->ClientFinish(bSuccess); } }
	// Allow the reliable completion RPC to flush; all processes exit through the normal engine path.
	FTimerHandle Handle; GetWorldTimerManager().SetTimer(Handle, [] { FPlatformMisc::RequestExit(false); }, 1.0f, false);
}

#include "Testing/Project_JNetworkLoadFixture.Companion.inl"

#include "Testing/Project_JNetworkLoadFixture.Party.inl"

#include "Testing/Project_JNetworkLoadFixture.Equipment.inl"

void AProject_JNetworkLoadGameMode::EndPlay(EEndPlayReason::Type Reason)
{
 if (CanonicalMeleeEnabled()) CleanupCanonicalMelee();
	if (CombatFanoutEnabled()) CleanupCombatFanout();
	else if (CleaveEnabled()) CleanupCleave();
	if (RenderedClientEnabled()) CleanupRenderedCrowd();
	Super::EndPlay(Reason);
}

#include "Testing/Project_JNetworkLoadFixture.Cleave.inl"

#include "Testing/Project_JNetworkLoadFixture.Rendered.inl"

#include "Testing/Project_JNetworkLoadFixture.Crafting.inl"

#include "Testing/Project_JNetworkLoadFixture.CombatFanout.inl"
