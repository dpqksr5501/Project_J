#pragma once
#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "UObject/ObjectKey.h"
#include "Async/Future.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"
#include "Project_JRenderedCrowdFixture.generated.h"
class AProject_JPlayerCharacter;
class USkeletalMesh;
class AActor;
class APlayerController;

/** Explicit non-shipping diagnostic; actual viewport/RHI, authored Blueprint bodies. */
UCLASS()
class UProject_JRenderedCrowdFixture : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// Diagnostic-only C++ adapter. Observes real replicated actors; never creates
	// Avatars, injects proxy input or changes a role. Existing CSV/quality gates run.
	bool PrepareNetworkView(class APlayerController* Observer);
	bool BindNetworkPlayers(const TArray<AProject_JPlayerCharacter*>& Actors);
	void SetNetworkPhase(int32 InPhase);
	bool IsNetworkPhaseComplete(int32 InPhase) const
	{
		return CompletedNetworkPhase >= InPhase && !bNetworkFailed &&
			(InPhase < 6 || !NetworkCSVCompletion.IsValid() || NetworkCSVCompletion.IsReady());
	}
	bool HasNetworkFailed() const { return bNetworkFailed; }
	void StopNetworkObservation();
	// Pure diagnostic accumulation; server use never starts this fixture's ticker/view.
	void ObserveNetworkMotion(int32 InPhase, int32 ActorIndex, const AProject_JPlayerCharacter* ObservedAvatar,
		const FVector& Position, const FVector& Velocity, double PhaseAge);
	void WriteNetworkMotionWitness(int32 InPhase, const FString& Filename) const;
private:
	struct FNetworkMotionWitnessEntry
	{
		FObjectKey ObjectKey;
		uint64 ActorId=0, FirstQPC=0, LastQPC=0;
		int32 Samples=0, Role=INDEX_NONE, PositiveVx=0, NegativeVx=0, PositiveDX=0, NegativeDX=0, InvalidSamples=0;
		int32 FirstSimFrame=INDEX_NONE, LastSimFrame=INDEX_NONE;
		double FirstPhaseAge=0, LastPhaseAge=0, FirstWorldTime=0, LastWorldTime=0, FirstSimMs=0, LastSimMs=0;
		double FirstX=0, LastX=0, MinVx=0, MaxVx=0, MinDX=0, MaxDX=0, SumPositiveDX=0, SumNegativeDX=0;
		FName FirstMode, LastMode;
		bool bIdentityStable=true;
	};
	// At most2 phases*200 values. No actor ownership, ticker, timer or per-frame IO.
	TArray<FNetworkMotionWitnessEntry> NetworkMotionWitness;
	void RegisterObservedPlayer(AProject_JPlayerCharacter* Player, int32 Index);
    // Explicit Count1 qualification only; values/weak identities, never a new scheduler.
    struct FLeaderBoundsProofPart
    {
        TWeakObjectPtr<class UProject_JModularMeshComponent> Part;
        TWeakObjectPtr<class USkeletalMeshComponent> Leader;
        TWeakObjectPtr<USkeletalMesh> Asset, LeaderAsset;
        TWeakObjectPtr<class UPhysicsAsset> LeaderPA;
        FString StaticFlags;
        int32 PhasePairs[7] = {};
    };
    bool bLeaderBoundsProof = false, bSharedPhysicsBoundsProof = false, bBoundsProofSelected = false, bBoundsProofWritten = false, bBoundsProofQualified = false;
    bool bBoundsProofSharedCacheReady = false;
    int32 BoundsProofSharedCacheOriginal = 0;
    uint32 BoundsProofSharedCacheSetBy = 0;
    int32 BoundsProofCacheHitPairs = 0, BoundsProofCacheMissPairs = 0;
    FDelegateHandle BoundsProofPostTick, BoundsProofCleanup;
    TWeakObjectPtr<class UWorld> BoundsProofWorld;
    TWeakObjectPtr<AProject_JPlayerCharacter> BoundsProofPlayer;
    TArray<FLeaderBoundsProofPart> BoundsProofParts;
    TArray<FString> BoundsProofExcluded;
    FString BoundsProofRows, BoundsProofUnknown;
    double BoundsProofLastTime = -1.;
    int32 BoundsProofBatches = 0, BoundsProofPairs = 0, BoundsProofSkipped = 0;
    int32 BoundsProofIdentityInvalid = 0, BoundsProofViolations = 0, BoundsProofRestoreFailures = 0, BoundsProofEquivalentPairs = 0;
    double BoundsProofMaxBoxDelta = 0, BoundsProofMaxRadiusDelta = 0;
    void StartLeaderBoundsProof();
    void ObserveLeaderBoundsProof(UWorld* World, ELevelTick TickType, float DeltaSeconds);
    void CleanupLeaderBoundsProof(UWorld* World, bool bSessionEnded, bool bCleanupResources);
    bool EndLeaderBoundsProof(bool RunCompleted, const FString& Reason);

	void RestoreClothExecutionOverrides();
	struct FClothExecutionOriginal
	{
		TWeakObjectPtr<class UProject_JModularMeshComponent> Mesh;
		float Blend = 0;
		bool bAllowed = false, bSuspended = false, bLocal = false;
	};
	TArray<FClothExecutionOriginal> ClothExecutionOriginals;
	bool bClothExecutionExperiment = false;
	bool Tick(float Delta);
	bool Setup();
	void Finish(bool bSuccess, const TCHAR* Reason);
	void ReleaseCanonicalProofDemands();
	// The opt-in NPC cohort keeps screenshot timing but moves PNG encoding off
	// the GT. One immutable pixel buffer/write; completion is polled, never waited.
	bool PrepareAsyncScreenshots(FString& Failure);
	bool RequestFixtureScreenshot(const FString& Filename, FString& Failure);
	bool PollAsyncScreenshot(bool& Ready, FString& Failure);
	void ReceiveScreenshot(int32 Width, int32 Height, const TArray<FColor>& Pixels);
	void ReleaseAsyncScreenshotCapture();
	class IImageWriteQueue* AsyncScreenshotQueue=nullptr;
	TWeakObjectPtr<class UGameViewportClient> ScreenshotViewport;
	TFuture<bool> ScreenshotWrite;
	FDelegateHandle ScreenshotCaptureHandle;
	FString ScreenshotFilename, ScreenshotFailure;
	int32 ScreenshotPhase=0, ScreenshotWritesStarted=0, ScreenshotWritesCompleted=0;
	uint32 ScreenshotCompletedMask=0;
	bool bAwaitingScreenshotPixels=false;
	UPROPERTY() TArray<TObjectPtr<AProject_JPlayerCharacter>> Players;
	UPROPERTY() TObjectPtr<class ACameraActor> ReviewCamera;
	FTSTicker::FDelegateHandle Handle;
	bool bNetworkObserver = false, bNetworkFailed = false, bNetworkViewRetired = false;
	int32 RequestedNetworkPhase = 0, CompletedNetworkPhase = 0;
	// No callback/owner capture: only phase6 ACK polls this shared completion.
	TSharedFuture<FString> NetworkCSVCompletion;
	TArray<bool> NetworkSawJump;
	double NetworkWaitStarted = 0;
	TWeakObjectPtr<class APlayerController> NetworkObserver;
	TWeakObjectPtr<AActor> PreviousViewTarget, NetworkFloor, NetworkLight;
	FString Output, Rows =
		TEXT("phase,elapsed_s,frame_delta_ms,gpu_ms,actors,rendered_actors,montages,capture_requested,body_lod0,body_lod1,body_lod2,local_body_lod,gt_ms,render_ms,rhi_ms,prediction30,nanite_candidates,nanite_parts,cloth_parts,cloth_tick_parts,modular_parts,phase_game_s,weapon_meshes,weapon_visible,weapon_rendered,weapon_auto_lod,weapon_mesh_matches,direct_bodies,retarget_bodies,cloth_managed_parts,")
		TEXT("cloth_reduced_parts,local_cloth_reduced_parts,render_lod0_parts,render_lod1_parts,render_lod2_parts,local_max_render_lod,cloth_simulations,cloth_substeps1,cloth_substeps3,remote_cloth_max_iterations,local_cloth_substeps,local_cloth_iterations,source_gameplay_pose_demands,secondary_budget_opted_in,secondary_policy_enabled,secondary_policy_disabled,local_secondary_policy_disabled,")
		TEXT("trajectory_metrics_enabled,trajectory_interval_start_frame,trajectory_interval_end_frame,trajectory_gt_calls_interval,trajectory_gt_ms_interval,trajectory_local_calls_interval,trajectory_local_ms_interval,trajectory_oldest_last_update_frame,trajectory_newest_last_update_frame,network_observer,autonomous_actors,simulated_actors,authority_actors,local_actors,cloth_max_lod_physics_vertex_cost,cloth_unknown_cost_parts,cloth_qualified_native_frame_parts,cloth_execution_opted_in_parts,cloth_execution_suspended_parts,local_cloth_execution_authoring_mismatches,cloth_execution_eligible_vertices,cloth_execution_reserved_vertices,cloth_execution_protected_vertices,cloth_execution_unknown_parts,cloth_execution_active_parts,cloth_execution_fadeout_parts,cloth_execution_parked_parts,cloth_execution_warming_parts,cloth_execution_fadein_parts,cloth_execution_unmanaged_parts,cloth_execution_draining_parts,cloth_execution_desired_sources,cloth_execution_may_resume_sources\n");
	FString PoseRoutes=TEXT("actor,component,asset,main_class,post_class,leader,bones,modular,")
		TEXT("child_class,bounds_scale,use_leader_bounds,use_attach_parent_bounds,fixed_skel_bounds,own_physics_asset,leader_asset,leader_physics_asset,leader_class,leader_bounds_scale,leader_fixed_skel_bounds,relative_transform_identity,cloth_metadata_all_lod\n");
	int32 ExpectedModularParts=0;
	// Wall time bounds the run; world/game time controls the prescribed animation workload.
	double Started = 0, PhaseAt = 0;
	int32 Phase = 0, Samples = 0, VisibleSamples = 0, CrowdCount = 200;
	bool bSimulateMovement = false;
 bool bTrajectoryMetrics = false;
	bool bRequirePlayerPoseReadiness = false, bPlayerPoseReceiptWritten = false;
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> PlayerPoseReadinessPins;
	bool WritePlayerPoseReadiness(bool RunSuccess,const FString& Reason);
#if WITH_EDITOR
	struct FPlayerPoseDatabaseReadiness
	{
		TWeakObjectPtr<class UPoseSearchDatabase> Database;
		int32 Status = -1, NewRequests = 0, Continues = 0;
		bool bRequested = false;
		TArray<FString> Transitions;
	};
	TArray<FPlayerPoseDatabaseReadiness> PlayerPoseDatabases;
	TArray<FObjectKey> PlayerPoseScopeKeys;
	TArray<bool> PlayerPoseOriginalDirty;
	FString PlayerPoseScope, PlayerPoseFailureScope, PlayerPoseSourceIdentity, PlayerPoseLatestScope;
	bool bPlayerPosePreparationStarted = false, bPlayerPoseReadinessSuccess = false, bPlayerPoseMeasurementAdmitted = false, bPlayerPoseMembershipUnchanged = true;
	double PlayerPosePreparationAt = 0, PlayerPoseAdmissionAt = 0;
	uint64 PlayerPoseLastPollFrame = MAX_uint64, PlayerPoseAdmissionFrame = 0;
	bool CollectPlayerPoseInputs(FString& Signature,FString& SourceIdentity,TArray<FObjectKey>& Keys,TArray<UObject*>& Pins,TArray<class UPoseSearchDatabase*>& Databases,FString& Failure) const;
	bool AdvancePlayerPoseReadiness(bool& Ready,FString& Failure);
#endif
    // Optional mixed resource owners are cosmetic-only, never full NPC population.
    bool SetupFXResourceCohort(FString& Failure);
    bool ObserveFXResourceCohort(FString& Failure);
    bool AdvanceFXResourceRetirement(bool& Ready, FString& Failure);
    void CleanupFXResourceCohort();
    bool WriteFXResourceCohort(bool bSuccess, const FString& Reason);
    bool BeginFXResourceCohort(FString& Failure);
    void CaptureFXResourceView(uint64 Epoch);
    void RecordFXResourceCheckpoint(const TCHAR* Label);
    bool IsFXResourceCurrent(uint64 Epoch) const;
    struct FFXResourceViewTick : FTickFunction
    {
        TWeakObjectPtr<UProject_JRenderedCrowdFixture> Owner;
        uint64 Epoch=0;
        virtual void ExecuteTick(float, ELevelTick, ENamedThreads::Type, const FGraphEventRef&) override;
        virtual FString DiagnosticMessage() override;
    };
    struct FFXResourceRow
    {
        TWeakObjectPtr<class AProject_JNPCCharacter> Owner;
        TWeakObjectPtr<class USkeletalMeshComponent> Mesh;
        TWeakObjectPtr<class UProject_JCombatPresentationComponent> Cue;
        TWeakObjectPtr<class UNiagaraComponent> Component;
        FVector Expected=FVector::ZeroVector;
        uint64 Lease=0;
        uint32 OwnerId=0, ComponentId=0;
        bool bSeen=false;
        int32 Retirement=0; //0=pending,1=invalid/destroying,2=pool,3=other-owner reuse.
    };
    struct FFXResourceNativeSlot
    {
        uint32 ComponentId=0;
        FVector Location=FVector::ZeroVector;
        double Distance=-1;
        bool bActive=false,bComplete=false,bRegistered=false,bLocal=false,bRecentlyRendered=false;
        int32 Culled=-1,CountCull=-1,OtherCull=-1,Pool=-1;
    };
    struct FFXResourceView
    {
        uint64 Frame=MAX_uint64;
        double WorldTime=-1;
        FVector Camera=FVector::ZeroVector;
        FQuat Rotation=FQuat::Identity;
        FMatrix Projection=FMatrix::Identity;
        TWeakObjectPtr<AActor> Target;
        int32 ViewCount=0;
        bool bProjection=false,bCameraMatch=false;
        TArray<FFXResourceNativeSlot,TInlineAllocator<132>> Slots;
    };
    struct FFXResourceCounts
    {
        int32 Current=0,Components=0,Distinct=0,Active=0,Culled=0,CountCull=0,OtherCull=0,RecentlyRendered=0;
        int32 Invalid=0,Pooled=0,Reused=0,PendingRetirement=0;
    };
    int32 FXResourceCount=0;
    uint64 FXResourceEpoch=0,FXResourceLastObservedFrame=MAX_uint64;
    TWeakObjectPtr<UWorld> FXResourceWorld;
    TWeakObjectPtr<APlayerController> FXResourceViewer;
    TWeakObjectPtr<class ACameraActor> FXResourceCamera;
    UPROPERTY(Transient) TObjectPtr<class UNiagaraSystem> FXResourceSystem;
    UPROPERTY(Transient) TObjectPtr<class UNiagaraEffectType> FXResourceType;
    UPROPERTY(Transient) TObjectPtr<class UProject_JCombatPresentationSet> FXResourceSet;
    UPROPERTY(Transient) TObjectPtr<class UProject_JAttackPresentationProfile> FXResourceProfile;
    FGameplayTag FXResourceAttackTag,FXResourceCueTag;
    FFXResourceViewTick FXResourceViewTick;
    FFXResourceView FXResourceView;
    FFXResourceCounts FXResourceCounts;
    TArray<FFXResourceRow> FXResourceRows;
    TArray<TSharedPtr<class FJsonObject>> FXResourceCheckpoints;
    FString FXResourceCSV,FXResourceFailure;
    FVector FXResourceCameraOrigin=FVector::ZeroVector;
    FQuat FXResourceCameraRotation=FQuat::Identity;
    int32 FXResourceSamples=0,FXResourceLastPhase=0,FXResourceQuietFrames=0,FXResourceDestroyedOwners=0,FXResourcePeakDistinct=0;
    bool bFXResourceSetup=false,bFXResourceStarted=false,bFXResourceStopping=false,bFXResourceCleaning=false,bFXResourceCleaned=false;
    bool bFXResourceRetired=false,bFXResourceCleanupPassed=false,bFXResourceViolation=false,bFXResourceWritten=false,bFXResourceWriteOK=false;
    bool bFXResourceAllSeen=false,bFXResourceSawActive=false,bFXResourceSawCountCull=false,bFXResourceOverlap=false;
    bool bFXResourceSystemDirty=false,bFXResourceTypeDirty=false;
    double FXResourceStopAt=0,FXResourceRetireSeconds=0;

    // Explicit fixture-only actual NPC cohort. Never joins Players/canonical pose demands.
    bool SetupNPCCombatCohort(FString& Failure);
    bool BeginNPCCombatCohort(FString& Failure);
    bool ObserveNPCCombatCohort(FString& Failure);
    bool AdvanceNPCCombatRetirement(bool& Ready, FString& Failure);
    void CleanupNPCCombatCohort();
    bool WriteNPCCombatCohort(bool bSuccess, const FString& Reason);
    bool IsNPCCombatRowCurrent(int32 Index) const;
    int32 NPCCombatCount = 0;
    bool bNPCAuthoredPath=false;
    struct FNPCCombatRow
    {
        TWeakObjectPtr<class AProject_JNPCCharacter> NPC, Recipient;
        TWeakObjectPtr<class AAIController> Controller;
        TWeakObjectPtr<class UProject_JBudgetedSkeletalMeshComponent> Mesh;
        TWeakObjectPtr<class USkeletalMeshComponent> RecipientMesh;
        TWeakObjectPtr<class UAnimInstance> Anim, Post;
        TWeakObjectPtr<USkeletalMesh> MeshAsset;
        TWeakObjectPtr<UClass> AnimClass;
        TWeakObjectPtr<class UProject_JAbilitySystemComponent> ASC, RecipientASC;
        TWeakObjectPtr<class UProject_JNPCActionComponent> Action;
        TWeakObjectPtr<class UProject_JTargetScoringComponent> Scoring;
        TWeakObjectPtr<class UProject_JCombatHitValidationComponent> Hit;
        FGameplayAbilitySpecHandle Grant;
        FDelegateHandle ContactHandle, EndHandle, EffectHandle, BoneHandle;
        uint64 SourceEpoch=0, RecipientEpoch=0, ActionToken=0;
        int32 Contacts=0, ContactsAtEnd=0, Effects=0, EffectsAtEnd=0, RecipientEffects=0;
        int32 Finalizations=0, AttackFinalizations=0, NaturalEnds=0;
        // Public GT history readbacks, not a count of distinct native ring records.
        uint64 TraceObservedFrame=MAX_uint64;
        int32 HistoryAttempts=0, HistoryFound=0, OpenHistoryObservations=0;
        int32 LastMatchedTraceSegments=0;
        int32 NotifySuppressedObservations=0, NotifyUnsuppressedObservations=0, MontageWeightObservations=0;
        double MontageWeightMin=TNumericLimits<double>::Max(), MontageWeightMax=-TNumericLimits<double>::Max();
        int32 PredictedLODMin=MAX_int32, PredictedLODMax=INDEX_NONE, MontagePositionObservations=0;
        double MontageFirstPosition=0, MontageLastPosition=0, MontagePreviousPosition=0, MontageMaxForwardAdvance=0;
        double LastHistoryQueryTime=0, LastOpenQueryTime=0, LastOpenMontagePosition=0;
        double LastMatchedQueryTime=0, LastMatchedMontagePosition=0;
        bool bLastMatchedWindowOpen=false;
        double MinSweepCapsuleAxisDistance=TNumericLimits<double>::Max(), MinSweepCapsuleClearance=TNumericLimits<double>::Max();
        float ObservedCapsuleRadius=0, ObservedCapsuleHalfHeight=0;
        FVector LastMatchedTraceStart=FVector::ZeroVector, LastMatchedTraceEnd=FVector::ZeroVector;
        FVector LastMatchedActorPosition=FVector::ZeroVector, LastMatchedRecipientPosition=FVector::ZeroVector;
        FString LastGeometryTarget;
        TArray<FObjectKey> EffectTargets;
        FString Name, RecipientName, AnimationPath;
        float InitialHealth=1000.f, LastHealth=1000.f, HealthLoss=0.f;
        uint8 QuietVisibility=0;
        bool bQuietURO=false, bQuietSuppress=false, bQuietDemand=false;
        bool bQuietManaged=false, bProtected=false, bMontage=false, bReturned=false;
        bool bEnded=false, bAdmitted=false, bTargetRegistered=false, bHelperController=false;
        bool bDestroyed=false, bComponentsUnregistered=false, bPresentationComponent=false;
    };
    TArray<FNPCCombatRow> NPCCombatRows;
    UPROPERTY(Transient) TArray<TObjectPtr<UObject>> NPCCombatPins;
    UPROPERTY(Transient) TObjectPtr<class UProject_JNPCActivationDefinition> NPCCombatDefinition;
    UPROPERTY(Transient) TObjectPtr<class UProject_JAttackDefinition> NPCCombatAttack;
    FGameplayTag NPCCombatHitTag;
    struct FNPCCombatNotifyObservation
    {
        FString ClassPath, Socket;
        float Start=0, Duration=0;
        bool bSocketResolved=false;
        int32 MontageTickType=INDEX_NONE;
        float TriggerWeightThreshold=0, NotifyTriggerChance=0;
        int32 NotifyFilterType=INDEX_NONE, NotifyFilterLOD=INDEX_NONE;
        bool bCanBeFilteredViaRequest=false, bTriggerOnDedicatedServer=false, bTriggerOnFollower=false;
        bool bNativeBranchingPoint=false, bEffectiveBranchingPoint=false;
    };
    TArray<FNPCCombatNotifyObservation> NPCCombatNotifyObservations;
    bool bNPCCombatNotifyMetadataTruncated=false;
    float NPCCombatObservedTraceRadius=0, NPCCombatObservedTraceDistance=0, NPCCombatObservedPlayRate=0;
    FVector NPCCombatObservedTipOffset=FVector::ZeroVector;
    bool bNPCCombatObservedUseTipOffset=false;
    TArray<bool> NPCCombatOriginalDirty;
    FString NPCCombatCSV, NPCCombatFailure;
    uint64 NPCCombatEpoch=0;
    int32 NPCCombatBaseAgents=0, NPCCombatBaseActions=0, NPCCombatBaseTargets=0, NPCCombatBaseTracked=0;
    int32 NPCCombatPeakMontages=0, NPCCombatSamples=0, NPCCombatDestroyed=0, NPCCombatUnregistered=0;
    bool bNPCCombatSetup=false, bNPCCombatStarted=false, bNPCCombatCleaning=false;
    bool bNPCCombatCleaned=false, bNPCCombatCleanupPassed=false, bNPCCombatRetired=false, bNPCCombatViolation=false;
    double NPCCombatRetireTailAt=0, NPCCombatRetireTailSeconds=0;

	bool bNPCMetrics = false;
	FString NPCDecisionRows; // Opt-in only; same sample frame as the existing frames.csv row.
 bool bHasTrajectoryCounters = false;
 uint64 PreviousTrajectoryCalls = 0, PreviousTrajectoryCycles = 0;
 uint64 PreviousLocalTrajectoryCalls = 0, PreviousLocalTrajectoryCycles = 0;
 uint64 PreviousTrajectorySampleFrame = 0;
	TArray<FVector> PreviousPositions;
	TArray<double> TravelDistances;
	TArray<int32> Reversals, Landings;
	TArray<bool> WasInAir;
	TArray<float> LastDirection;
	bool bJumpRequested = false;
	// Diagnostic only: one phase4 observation window, no timer or retained per-actor samples.
	int32 JumpWitnessRows = 0, LastJumpWitnessAirCount = INDEX_NONE;
	bool bShot = false, bFinished = false;
	bool bWithLocalPlayer = false;
	bool bCloseView = false;
	/** Explicit transient fixture opt-in; never changes an asset/CDO or normal gameplay actor. */
	bool bSecondaryMotionBudgetExperiment = false;
	/** Separate fixed-step quality cohort only; excluded from performance runs. */
	bool bCanonicalProof = false;
	int64 LastCanonicalKey = -1;
	int32 CanonicalRowsCount = 0;
	FString CanonicalRows = TEXT("phase,game_step,actor,montage_s,kind,valid,x,y,z,qx,qy,qz,qw\n");
	int32 LocalQualitySamples = 0;
	// Optional assertion for profiling the same saved assets through both render paths.
	int32 ExpectedNaniteMode = -1;
	// Optional visual-only comparison: equipment, abilities and weapon motion keep running.
	int32 WeaponVisibility = -1;
	FString ExpectedWeaponMesh;
};
