#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpecHandle.h"
#include "Templates/Atomic.h"
#include "Game/Project_JGameMode.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Animation/Project_JReplicatedAnimEventTypes.h"
#include "Project_JNetworkLoadFixture.generated.h"

class UActorComponent;
class AProject_JNPCCharacter;

// One finite native-input witness; weak observations never own source actors or assets.
struct FProject_JCanonicalMeleeWitness
{
 TWeakObjectPtr<class AProject_JPlayerCharacter> Source;
 TWeakObjectPtr<class AProject_JNPCCharacter> Target;
 TWeakObjectPtr<class UProject_JAbilitySystemComponent> ASC, TargetASC;
 TWeakObjectPtr<class UProject_JCombatHitValidationComponent> Hit;
 TWeakObjectPtr<class UProject_JEquipmentRuntimeComponent> Equipment;
 TWeakObjectPtr<class UProject_JEquipmentItemDefinition> Item;
 TWeakObjectPtr<class UProject_JAttackDefinition> Attack;
 TWeakObjectPtr<class USkeletalMeshComponent> Mesh;
 TWeakObjectPtr<class USkeletalMesh> MeshAsset;
 TWeakObjectPtr<class UAnimInstance> Anim;
 FGameplayAbilitySpecHandle Grant; // Fixed only after observing the one native active execution.
 TArray<FGameplayAbilitySpecHandle> MatchingGrants;
 TArray<FString> MatchingSpecSnapshot, OwnedGrantSources;
 bool bOwnedSourcesInitiallyAbsent=false, bOwnedSourcesRetired=false;
 FGameplayTag HitTag;
 FGameplayTagContainer MatchingHitTags;
 FDelegateHandle ContactHandle, EndHandle, BoneHandle, EffectHandle;
 uint64 Revision=1, SourceEpoch=0, TargetEpoch=0, WeaponRevision=0, SpatialRevision=0;
 int32 RawContacts=0, Effects=0, NaturalEnds=0, Finalizations=0, AttackFinalizations=0;
 int32 PredictionKey=0, MaxPacketSegments=0, OpenPackets=0, Role=0, NativeFrame=INDEX_NONE, AuthoredKeys=0;
 float HealthBefore=0, HealthAfter=0;
 double EndedAt=0, ContactTimestamp=0;
 FVector TraceStart=FVector::ZeroVector, TraceEnd=FVector::ZeroVector;
 bool bLocal=false, bObservedActive=false, bMontage=false, bEnded=false, bViolation=false, bCleaned=false;
 FString SourceName, TargetName, MeshPath, SourceStamp, AttackTag, NodeTag, Error;
};


UCLASS()
class UProject_JNetworkFixtureLifetime : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
private:
	FTSTicker::FDelegateHandle Timeout;
	FDelegateHandle FloorWorldInit;
	FDelegateHandle AnimationKinematicsTick;
};

/** Native diagnostics only. Selected explicitly by CLI map URL and -ProjectJNetworkFixture. */
UCLASS(NotBlueprintable)
class AProject_JNetworkViewPawn : public APawn
{
	GENERATED_BODY()
public:
	AProject_JNetworkViewPawn();
};

UCLASS(NotBlueprintable)
// Static AOI/counter probes have no movement or animation contract.
class AProject_JNetworkProbeNPC : public APawn
{
	GENERATED_BODY()
public:
	AProject_JNetworkProbeNPC();
	UPROPERTY(Replicated) int32 ProbeId = 0;
	UPROPERTY(Replicated) int32 Pulse = 0;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
};

UCLASS(NotBlueprintable)
class AProject_JAlwaysRelevantProbeNPC : public AProject_JNetworkProbeNPC
{
	GENERATED_BODY()
public:
	AProject_JAlwaysRelevantProbeNPC();
};

UCLASS(NotBlueprintable)
class AProject_JNetworkLoadController : public APlayerController
{
	GENERATED_BODY()
public:
	virtual void PlayerTick(float DeltaSeconds) override;
	virtual void UpdateCameraManager(float DeltaSeconds) override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	UFUNCTION(Client, Reliable) void ClientConfigure(int32 Stage, int32 Slot, const TArray<int32>& Expected, FGuid OwnItem, bool bExpectItems, int32 MinimumPulse);
	UFUNCTION(Server, Reliable) void ServerObserve(int32 Stage, const TArray<int32>& Seen, bool bOwnInventory, bool bForeignInventoryEmpty, bool bEquipment, int32 MinimumPulse, bool bAnimation, bool bWorldClock);
	UFUNCTION(Client, Reliable) void ClientFinish(bool bSuccess);
	UFUNCTION(Client, Reliable) void ClientAnimationExpect(int32 Stage, const TArray<AProject_JPlayerCharacter*>& Players, const TArray<FProject_JReplicatedAnimEventState>& States);
	UFUNCTION(Client, Reliable) void ClientBeginCombatTrial(int32 Trial, AProject_JNPCCharacter* CanonicalTarget = nullptr);
	UFUNCTION(Server, Reliable) void ServerCombatObserved(int32 Trial, bool bComplete);
	UFUNCTION(Client, Reliable) void ClientCompanionStage(int32 Phase, FGuid OwnItem, int32 Action, bool bExpectationReady, const TArray<uint64>& PublicIds, const TArray<float>& PublicHealth);
	UFUNCTION(Server, Reliable) void ServerCompanionObserved(int32 Phase, bool bReady, bool bViolation, const FString& Detail);
	UFUNCTION(Client, Reliable) void ClientEquipmentStage(int32 Phase);
	UFUNCTION(Server, Reliable) void ServerEquipmentObserved(int32 Phase, bool bReady, bool bViolation, const FString& Detail);
	UFUNCTION(Client, Reliable) void ClientPartyStage(int32 Phase);
	UFUNCTION(Server, Reliable) void ServerPartyObserved(int32 Phase, bool bReady, bool bViolation, const FString& Detail);
	UFUNCTION(Client, Reliable) void ClientCraftingStage(int32 Phase, FGuid JobId, int32 Batches, bool bReplaceSource);
	UFUNCTION(Server, Reliable) void ServerCraftingObserved(int32 Phase, bool bReady, bool bViolation, FGuid Correlation, const FString& Detail);
	int32 FixtureSlot = INDEX_NONE;
	int32 ConfirmedStage = INDEX_NONE;
	FString LastObservation;
private:
	void ObserveCraftingJobs();
	UPROPERTY(Transient) TObjectPtr<class UProject_JCraftingJobsComponent> CraftingObservedSource;
	UPROPERTY(Transient) TObjectPtr<class UProject_JCraftingJobsComponent> CraftingRetiredSource;
	FGuid CraftingExpectedJob, CraftingCorrelation;
	int32 CraftingExpectedBatches = 0, CraftingRecordedPhase = INDEX_NONE;
	bool bCraftingReplaceSource = false, bCraftingActionSent = false, bCraftingSawPending = false, bCraftingViolation = false, bCraftingLateSourceSafe = false;
	double CraftingStableSince = 0;
	FString CraftingRows = TEXT("phase,slot,owner_rows,owner_wire_rows,foreign_rows,foreign_wire_rows,foreign_states,batches,pending_observed,result_stage,correlation,source_replaced,late_source_safe,passed,scope\n");
	void ObserveEquipmentInterest();
	bool ObserveEquipmentOwnerCommands(int32 ForeignBag, int32 ForeignQuests);
	void RetireEquipmentOwnerCommands();
	UPROPERTY(Transient) TObjectPtr<class UProject_JInventoryViewModel> EquipmentCommandModel;
	TWeakObjectPtr<class AProject_JPlayerState> EquipmentCommandState;
	FGuid EquipmentCommandSource, EquipmentCommandTarget, EquipmentCommandWeapon;
	int32 EquipmentSourceOrder = INDEX_NONE, EquipmentTargetOrder = INDEX_NONE;
	int32 EquipmentCommandSentPhase = INDEX_NONE, EquipmentCommandRecordedPhase = INDEX_NONE;
	bool bEquipmentCommandsEnding = false;
	FString EquipmentCommandRows = TEXT("phase,observer_slot,character_id,source_id,target_id,weapon_id,source_order_before,target_order_before,source_order_after,target_order_after,equipped,tracked,foreign_bag,foreign_quests,response,passed\n");
	int32 EquipmentRecordedPhase = INDEX_NONE;
	double EquipmentStableSince = 0;
	bool bEquipmentInterestViolation = false;
	FString EquipmentInterestRows = TEXT("phase,observer_slot,owner_private,owner_public,foreign_private,remote_pawns,remote_rows,remote_presented,manager_public,remote_visuals,passed\n");
	void ObserveParty();
	UPROPERTY(Transient) TObjectPtr<class UProject_JPlayerUIComponent> PartyUI;
	int32 PartyRecordedPhase = INDEX_NONE;
	double PartyStableSince = 0;
	bool bPartyViolation = false, bPartyScreenshotRequested = false, bPartyScreenshotProcessed = false;
	FDelegateHandle PartyScreenshotHandle;
	bool bLatePlayerStateCallbackProbed = false, bLatePlayerStateCallbackInvariant = false;
	FString OwnerViewRows = TEXT("phase,observer_slot,subject_slot,pc_pawn_id,state_pawn_id,asc_avatar_id,retired_id,camera_target_id,pawn_role,controller_matches,detached,cache_distance_uu,cull_radius_uu,stable_seconds,retired_expected,passed\n");
	FString PartyOwnershipDiagnosticRows;
	FString PlayerStateLifetimeRows = TEXT("phase,observer_slot,retired_pawns,retired_cleared,own_linked,own_bag,own_private,own_public,remote_public,weapon_id,late_ps_probe,late_ps_invariant,passed\n");
	FString PlayerStateInterestRows = TEXT("phase,observer_slot,foreign_states,remote_pawns,foreign_private,linked,health,passed\n");
	FString PartyRows = TEXT("phase,observer_slot,owner_rows,foreign_rows,remote_pawns,panel_rows,passed\n");
	FString PartyMemberRows = TEXT("phase,observer_slot,order,character_id,party_id,leader,available,name,level,health_known,permille,passed\n");
	void ObserveCompanion();
	UPROPERTY(Transient) TObjectPtr<class UProject_JInventoryViewModel> CompanionModel;
	TArray<uint64> CompanionExpectedIds;
	TArray<float> CompanionExpectedHealth;
	FGuid CompanionItem;
	int32 CompanionAction = 0, CompanionRecordedStage = INDEX_NONE;
	bool bCompanionExpectReady = false, bCompanionViolation = false;
	double CompanionStableSince = 0;
	FString CompanionRows = TEXT("phase,observer_slot,subject_slot,actor_id,own_pointer_id,foreign_pointer_null,max_health_base,max_health_current,health_base,health_current,attack_base,attack_current,defense_base,defense_current,no_player_connection,passed\n");
	void ConfigureRenderedStage(int32 InStage);
	void DriveRenderedOwner();
	bool ObserveRenderedCrowd();
	TArray<TWeakObjectPtr<AProject_JPlayerCharacter>> RenderedObserved;
	double RenderedOwnerPhaseAt = 0;
	bool bRenderedOwnerJump = false, bRenderedOwnerAttack = false, bRenderedRosterRecorded = false;
	bool ObserveCleave();
	void ObserveCleaveThreatMontage();
	TWeakObjectPtr<class AProject_JNPCCharacter> CleaveObservedCaster;
	bool bCleaveThreatObservedAttack = false;
	FString CleaveThreatObservedMontage;
	FString CleaveThreatRows = TEXT("phase,observer_slot,subject_slot,actor_id,caster_id,health,observed_attack,montage,passed\n");
	int32 CleaveRecordedStage = INDEX_NONE;
	double CleaveStableSince = 0;
	TMap<uint64, float> CleaveObservedHealth;
	FString CleaveRows = TEXT("phase,observer_slot,subject_slot,actor_id,caster_id,health,passed\n");
	bool ObserveAnimation();
	bool ObserveWorldClock() const;
	bool ObserveAttributeAudience();
	void ObserveCombat();
 void ObserveCanonicalMelee();
 void RetireCanonicalMelee();
 FProject_JCanonicalMeleeWitness CanonicalMelee;
 bool bCanonicalInputSent=false, bCanonicalReported=false;

	int32 CombatTrial = INDEX_NONE;
	bool bSentR = false, bSentMouse = false, bReportedCombat = false;
	double CombatInputAt = 0;
	TSet<FName> CombatRSeen, CombatTargetSeen, CombatEndSeen;
	FString CombatRows = TEXT("trial,actor,role,attack,r_seen,target_seen,end_seen\n");
	TArray<TWeakObjectPtr<AProject_JPlayerCharacter>> AnimationPlayers;
	TArray<FProject_JReplicatedAnimEventState> AnimationStates;
	TSet<TWeakObjectPtr<AProject_JPlayerCharacter>> ObservedPose;
	TSet<TWeakObjectPtr<AProject_JPlayerCharacter>> ObservedLandingPose;
	int32 AnimationStage = INDEX_NONE;
	bool bAnimationViolation = false;
	FString AnimationRows = TEXT("stage,actor,role,move_sequence,landing_sequence,landing_revision,presentation_epoch,capture_frame,gasp_state,playback_asset,comparison\n");
	bool bAttributeAudienceViolation = false;
	int32 AttributeAudienceRecordedStage = INDEX_NONE;
	FString AttributeAudienceRows = TEXT("stage,actor,audience,attribute,base,current,expected_base,passed\n");
	FString PendingAttributeAudienceRows;
	int32 CurrentStage = INDEX_NONE, LocalSlot = INDEX_NONE, RequiredPulse = 0;
	FGuid ItemId;
	bool bItemsExpected = true;
	TArray<int32> ExpectedIds;
	double NextObservation = 0;
};

UCLASS(NotBlueprintable)
class AProject_JNetworkLoadGameMode : public AProject_JGameMode
{
	GENERATED_BODY()
public:
	AProject_JNetworkLoadGameMode();
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void Tick(float DeltaSeconds) override;
	bool ValidateObservation(AProject_JNetworkLoadController* PC, int32 Stage, const TArray<int32>& Seen, bool bOwn, bool bForeignEmpty, bool bEquipment, int32 MinPulse, bool bAnimation, bool bWorldClock);
	void ObserveEquipmentClient(AProject_JNetworkLoadController* PC, int32 Phase, bool bReady, bool bViolation, const FString& Detail);
	void AcceptCompanionObservation(AProject_JNetworkLoadController* PC, int32 Phase, bool bReady, bool bViolation, const FString& Detail);
	void AcceptPartyObservation(AProject_JNetworkLoadController* PC, int32 Phase, bool bReady, bool bViolation, const FString& Detail);
	void AcceptCombatObservation(AProject_JNetworkLoadController* PC, int32 Trial, bool bComplete);
	void AcceptCraftingObservation(AProject_JNetworkLoadController* PC, int32 Phase, bool bReady, bool bViolation, FGuid Correlation, const FString& Detail);
private:
	void StartCraftingStage(int32 Phase);
	FGuid CraftingJobIds[2], CraftingEscrowIds[2];
	FString CraftingAuthorityRows = TEXT("phase,slot,owner_rows,job_id,profile_ready,passed,scope\n");
	void BeginRenderedCrowd();
	bool AreRenderedOwnersCurrent() const;
	void UpdateRenderedCrowd();
	void StartRenderedStage(int32 InStage);
	void CleanupRenderedCrowd();
	UPROPERTY(Transient) TArray<TObjectPtr<AProject_JPlayerCharacter>> RenderedPlayers;
	UPROPERTY(Transient) TArray<TObjectPtr<class AAIController>> RenderedControllers;
	UPROPERTY(Transient) TArray<TObjectPtr<class AProject_JPlayerState>> RenderedStates;
	TWeakObjectPtr<UActorComponent> RenderedPreparationOwner;
	uint64 RenderedPreparationToken = 0, RenderedRevision = 1;
	double RenderedPhaseAt = 0, RenderedCreationStarted = 0;
	bool bRenderedJump = false, bRenderedAttack = false, bRenderedRetiring = false;
	bool PrepareCleave();
	bool InjectCleaveThreat();
	bool CleaveThreatSourcesCurrent() const;
	UPROPERTY(Transient) TObjectPtr<class UProject_JNPCActivationDefinition> CleaveThreatDefinition;
	TWeakObjectPtr<AActor> CleaveThreatSourceOwners[2];
	uint64 CleaveThreatSourceEpochs[2] = {};
	double CleaveThreatTotals[2] = {}, CleaveThreatDistances[3] = {};
	float CleaveThreatCasterBefore = 0, CleaveThreatCasterAfter = 0;
	uint64 CleaveThreatPrimaryId = 0, CleaveThreatStockId = 0;
	FString CleaveThreatMontage;
	FString CleaveThreatAuthorityRows = TEXT("phase,subject_slot,actor_id,caster_id,expected_primary_id,actual_primary_id,stock_target_id,source_epoch,source_total,distance,health,effects,contact,attack_completed,cancelled,peak_sources,accepted_losses,source_current,threat_released,caster_before,caster_after,montage,passed\n");

	// Diagnostic-only32 independent ownership records; UObject ownership is in FanoutOwnedActors.
	struct FCombatFanoutGroup
	{
		TWeakObjectPtr<class AProject_JNPCCharacter> Caster, Victims[3];
		TWeakObjectPtr<class AAIController> Controller;
		TWeakObjectPtr<class UAbilitySystemComponent> Source, Recipients[3];
		TWeakObjectPtr<class UProject_JNPCActivationComponent> Activation;
		TWeakObjectPtr<class UProject_JTargetScoringComponent> Scoring;
		TWeakObjectPtr<class UProject_JNPCActionComponent> Action;
		TWeakObjectPtr<class UProject_JCombatHitValidationComponent> Hit;
		FDelegateHandle ContactHandle, EndHandle, EffectHandles[3];
		uint64 SourceEpoch=0, TargetEpoch[3]={};
		int32 Effects[3]={}, ContactMask=0, PreparationFrame=INDEX_NONE;
		float NaturalHealth[3]={1000,1000,1000};
		FGameplayAbilitySpecHandle Grant;
		bool bStarted=false, bEnded=false, bCancelRequested=false;
	};
	void BeginCombatFanout();
	void UpdateCombatFanout();
	bool IsCombatFanoutCurrent(int32 Index, uint64 Epoch) const;
	bool StartCombatFanoutBurst(int32 Phase);
	bool CleanupCombatFanout();
	UPROPERTY(Transient) TArray<TObjectPtr<AActor>> FanoutOwnedActors;
	TArray<FCombatFanoutGroup> FanoutGroups;
	TArray<TWeakObjectPtr<AActor>> FanoutRegisteredTargets;
	uint64 FanoutEpoch=1;
	int32 FanoutBaseAgents=0, FanoutBaseActions=0, FanoutBaseTargets=0;
	int32 FanoutPeakMontages=0, FanoutPeakAttacks=0, FanoutPeakConcurrent=0;
	int32 FanoutNaturalStarted=0, FanoutNaturalEnded=0, FanoutNaturalContacts=0, FanoutNaturalEffects=0, FanoutNaturalLosses=0;
	bool bFanoutBurstActivated=false;
	TSharedPtr<TAtomic<bool>,ESPMode::ThreadSafe> FanoutTraceReady;
	FDelegateHandle FanoutTraceConnectionHandle;
	bool bFanoutViolation=false, bFanoutTraceStarted=false, bFanoutTraceStopped=false, bFanoutCleanupPassed=false;
	FString FanoutTargets=TEXT("phase,group,subject,caster_id,actor_id,before_health,health,effects,contact,started,ended,cancelled,passed,scope\n");
	FString FanoutSummary;
	void StartCleaveStage(int32 Phase);
	void UpdateCleave();
	void CleanupCleave();
	UPROPERTY(Transient) TObjectPtr<class UProject_JNPCActivationDefinition> CleaveDefinition;
	UPROPERTY(Transient) TObjectPtr<class UProject_JAttackDefinition> CleaveAttack;
	UPROPERTY(Transient) TObjectPtr<class AProject_JNPCCharacter> CleaveCaster;
	UPROPERTY(Transient) TObjectPtr<class AProject_JNPCCharacter> CleaveNPCVictim;
	TWeakObjectPtr<class AAIController> CleaveController;
	TArray<TWeakObjectPtr<AActor>> CleaveVictims;
	TArray<TWeakObjectPtr<class UAbilitySystemComponent>> CleaveObservedASCs;
	TArray<FDelegateHandle> CleaveEffectHandles;
	FDelegateHandle CleaveEventHandle, CleaveEndHandle;
	FGameplayTag CleaveHitTag;
	TSet<uint64> CleaveContactIds;
	TArray<int32> CleaveEffects;
	TArray<float> CleaveHealthAfterAttack;
	TArray<int32> CleavePreparationFrames;
	uint64 CleaveRevision = 1;
	int32 CleaveRegisteredTargets = 0;
	float CleaveFirstImpact = 0;
	bool bCleavePublished = false, bCleaveStageReady = false, bCleaveStartedAttack = false;
	bool bCleaveNaturalEnd = false, bCleaveCancelling = false, bCleaveCancelled = false, bCleaveViolation = false;
	FString CleaveAuthorityRows = TEXT("phase,subject_slot,actor_id,caster_id,health,effects,contact,attack_completed,cancelled,passed\n");
	void StartEquipmentStage(int32 Phase);
	TArray<FGuid> EquipmentInterestItems;
	bool ValidateEquipmentOwnerCommands(AProject_JNetworkLoadController* PC, int32 Phase);
	TArray<FGuid> EquipmentOwnerBagItems;
	TArray<int32> EquipmentOwnerBagOrders;
	FString EquipmentCommandAuthorityRows = TEXT("phase,observer_slot,character_id,source_id,target_id,weapon_id,source_order_before,target_order_before,source_order_after,target_order_after,equipped,tracked,foreign_bag,foreign_quests,response,passed\n");
	void StartPartyStage(int32 Phase);
	void RecordPartyAuthority();
	FName PartyFixtureGroup;
	TWeakObjectPtr<class AProject_JPlayerCharacter> RetiredAudiencePawn;
	UE::Net::FNetRefHandle RetiredAudienceHandle, RetiredAudienceStateHandle;
	bool bRetiredAudienceDestroyed = false;
	FGuid ReplacementAudienceWeapon;
	double OwnerViewDetachedSince = 0;
	FString OwnerViewAuthorityRows = TEXT("phase,observer_slot,subject_slot,pc_pawn_id,state_pawn_id,asc_avatar_id,retired_id,camera_target_id,pawn_role,controller_matches,detached,cache_distance_uu,cull_radius_uu,stable_seconds,retired_expected,passed\n");
	FString PartyOwnershipAuthorityDiagnosticRows;
	FString PlayerStateLifetimeAuthorityRows = TEXT("phase,subject_slot,old_net_id,old_system,old_alive,old_registered,old_cleared,current_net_id,state_net_id,retired_fallback,reason,bag_count,private_equipment,public_equipment,weapon_id,passed\n");
	FString PlayerStateBindingRows = TEXT("phase,subject_slot,assigned,spatial,fallback,object_index,pawn_index,avatar_index,owner_connection,radius,observed_spawns,consumed_spawns,pending_witnesses,active_spatial,fallback_bindings,passed,observer_count,observer_spatial,observer_fallback,observer_player_id,observer_object_index,observer_net_id,observer_net_serial,observer_net_system,observer_binding_valid\n");
	FString PartyAuthorityRows = TEXT("phase,observer_slot,order,character_id,party_id,leader,available,name,level,health_known,permille,passed\n");
	void StartCompanionStage(int32 Phase);
	bool PublishCompanionStage();
	TArray<FGuid> CompanionItems;
	TArray<uint64> CompanionPreviousIds;
	bool bCompanionPublished = false;
	FString CompanionAuthorityRows = TEXT("phase,subject_slot,actor_id,max_health_base,max_health_current,health_base,health_current,attack_base,attack_current,defense_base,defense_current,target_character_id,no_player_connection,passed\n");
	void StartStage(int32 NewStage);
	void Finish(bool bSuccess, const FString& Reason);
	void BeginCombatTrial();
 void BeginCanonicalMelee();
 void UpdateCanonicalMelee();
 bool CleanupCanonicalMelee();
 bool BeginCanonicalRecipientNavigation();
 bool ObserveCanonicalRecipientNavigation();
 bool WriteCanonicalRecipientNavigation(bool bCleanupPassed);
 TWeakObjectPtr<class AAIController> CanonicalRecipientController;
 TWeakObjectPtr<class UProject_JNPCActionComponent> CanonicalRecipientAction;
 TWeakObjectPtr<class ANavigationData> CanonicalRecipientNavData;
 FVector CanonicalRecipientStart=FVector::ZeroVector, CanonicalRecipientGoal=FVector::ZeroVector, CanonicalRecipientRouteAxis=FVector::ZeroVector;
 FVector CanonicalRecipientProjectedStart=FVector::ZeroVector, CanonicalRecipientProjectedGoal=FVector::ZeroVector;
 uint64 CanonicalRecipientToken=0;
 uint32 CanonicalRecipientRequestID=0;
 int32 CanonicalRecipientPathPoints=0, CanonicalRecipientStopCalls=0;
 double CanonicalRecipientMaxDisplacement=0, CanonicalRecipientMaxSpeed=0;
 bool bCanonicalRecipientAdmitted=false, bCanonicalRecipientMoving=false, bCanonicalRecipientStopped=false;
 bool bCanonicalRecipientArrived=false, bCanonicalRecipientOwnController=false, bCanonicalRecipientControllerRetired=false;
 FString CanonicalRecipientNavDataPath;
 FProject_JCanonicalMeleeWitness CanonicalMelee;
 UPROPERTY(Transient) TObjectPtr<class UProject_JEquipmentItemDefinition> CanonicalMeleeItem;
 UPROPERTY(Transient) TObjectPtr<class UProject_JEquipmentItemDefinition> CanonicalMeleeOriginalItem;
 UPROPERTY(Transient) TObjectPtr<class AProject_JNPCCharacter> CanonicalMeleeTarget;
 TArray<FName> CanonicalMeleeGrantSources;
 bool bCanonicalSetup=false, bCanonicalCleaning=false, bCanonicalCleanupPassed=false, bCanonicalControlSent=false;

	bool ObserveServerCombat();
	int32 CombatTrial = 0;
	TSet<FName> CombatRSeen, CombatTargetSeen, CombatEndSeen;
	TArray<int32> ExpectedIds(int32 Slot) const;
	UPROPERTY() TArray<TObjectPtr<AProject_JNetworkLoadController>> Clients;
	UPROPERTY() TArray<TObjectPtr<AProject_JNetworkProbeNPC>> NPCs;
	UPROPERTY() TObjectPtr<class AProject_JNPCCharacter> AttributeAudienceNPC;
	FString AttributeAuthorityRows = TEXT("stage,actor,audience,attribute,base,current,expected_base,passed\n");
	TArray<FGuid> Items;
	int32 Stage = 0, Pulse = 0, RequiredPulse = 0;
	double Started = 0, StageStarted = 0, NextPulse = 0;
	uint32 StageOutBytes = 0;
	uint64 RegionId = 0;
	bool bFixture = false, bAllRelevant = false, bFinished = false, bSawDisconnect = false;
	FString Rows = TEXT("stage,seconds,connections,out_bytes\n");
};
