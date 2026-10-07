#pragma once

#include "Animation/AnimInstanceProxy.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"

class UPoseSearchDatabase;

struct FProject_JMotionMatchingBlendStackPlayerDebug
{
	FName Animation;
	float AssetTime = 0.0f;
	float AssetLength = 0.0f;
	float PlayRate = 1.0f;
	float BlendWeight = 0.0f;
	float BlendTime = 0.0f;
	float BlendElapsed = 0.0f;
	bool bActive = false;
	bool bLooping = false;
	bool bMirrored = false;
};

struct FProject_JMotionMatchingPivotTraceEntry
{
	uint64 FrameNumber = 0;
	EProject_JLocomotionPhaseFamily PhaseFamily = EProject_JLocomotionPhaseFamily::Idle;
	FName RequestedDatabase;
	FName NativeSelectedDatabase;
	FName SelectedAnimation;
	float SelectedAnimationTime = 0.0f;
	float SearchCost = 0.0f;
	float WantedPlayRate = 1.0f;
	float ElapsedPoseSearchTime = 0.0f;
	EPoseSearchInterruptMode AppliedInterruptMode = EPoseSearchInterruptMode::DoNotInterrupt;
	bool bContinuingPoseSearch = false;
	bool bNewBlendThisFrame = false;
	TArray<FProject_JMotionMatchingBlendStackPlayerDebug, TInlineAllocator<4>> BlendPlayers;
};

struct FProject_JCharacterAnimInstanceProxy : public FAnimInstanceProxy
{
	FProject_JCharacterAnimInstanceProxy();
	explicit FProject_JCharacterAnimInstanceProxy(UAnimInstance* InAnimInstance);

	void QueueGameThreadData(
		const FProject_JAnimThreadSafeData& InData,
		UPoseSearchDatabase* InSelectedDatabase,
		bool bInMotionMatchingEnabled,
		bool bInUpdateMotionMatchingThisFrame,
		bool bInForceMotionMatchingReselect,
		bool bInFreshSnapshot = true,
		UPoseSearchDatabase* InTurnCycleCompanion = nullptr);

	const FProject_JAnimThreadSafeData& GetThreadSafeData() const { return ThreadSafeData; }
	UPoseSearchDatabase* GetCurrentActiveDatabase() const { return CurrentActiveDatabase.Get(); }
	UPoseSearchDatabase* GetTurnCycleCompanion() const { return CurrentTurnCycleCompanion.Get(); }
	int32 GetThreadSafeCandidateCount() const { return ThreadSafeCandidateCount; }
	const FProject_JAnimMotionMatchingPostSelectionData& GetLatestPostSelection() const { return LatestPostSelection; }
	FString GetPivotTraceSummary() const;
	void SetFlowTraceEnabled(bool bEnabled);
	void SetMovingTurnTraceEnabled(bool bEnabled) { bMovingTurnTraceEnabled = bEnabled; }
	const FProject_JAnimationFlowWork& GetFlowTraceWork() const { return FlowTraceWork; }
	uint64 GetReselectRequestForTrace() const { return PendingReselectRevision; }
	bool IsReselectPendingForTrace() const { return bForceMotionMatchingReselect; }
	bool IsReturnQueryForTrace() const { return bReselectFromPoseHistory; }
	/** Read a captured representative only after its graph update has completed. */
	const FAnimNode_MotionMatching* GetCapturedMotionMatchingNode() const;

protected:
	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void UpdateAnimationNode_WithRoot(
		const FAnimationUpdateContext& InContext,
		FAnimNode_Base* InRootNode,
		FName InLayerName) override;
	virtual FAnimNode_Base* GetCustomRootNode() override;
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

private:
	friend class FProjectJAnimationSnapshotBoundaryTest;
	friend class FProjectJAnimationClockTest;
	friend class FProjectJStopIdleInterruptTest;
	friend class FProjectJStrafeFacingSearchTest;
	friend class FProjectJMotionMatchingReturnRequestTest;
	friend class FProjectJMotionMatchingSearchExecutionTest;
	friend class FProjectJMotionMatchingCrowdPolicyTest;
	friend class FProjectJMotionMatchingNestedGraphTest;
	friend class FProjectJLocomotionCandidateContinuityTest;
	void LinkNativeGraph();
	void ConsumeQueuedGameThreadData();
	void ApplySelectedDatabaseToNativeNode();
	void ApplyMotionMatchingSearchPolicy();
	void ForceReselectMotionMatchingNodes();
	void CompleteMotionMatchingReselects();
	void ClearMotionMatchingReselects();
	EPoseSearchInterruptMode ResolveReselectInterruptMode() const;
	struct FNodeReselectState
	{
		uint64 HandledRevision = 0;
		uint64 ArmedRevision = 0;
		float SavedElapsedSearchTime = 0.0f;
		float SavedSearchThrottleTime = 0.0f;
	};
	void ArmMotionMatchingReselect(FAnimNode_MotionMatching& Node, FNodeReselectState& State);
	bool CompleteMotionMatchingReselect(FAnimNode_MotionMatching& Node, FNodeReselectState& State);
	void CapturePostSelection();
	void CapturePivotDebugTrace();
	/**
	 * Generated AnimBP graphs commonly contain far more nodes than Motion Matching
	 * nodes. Cache only the latter's indices and rebuild when the generated class
	 * interface changes (for example after an AnimBP reinstance in the editor).
	 */
	const TArray<int32>& GetGeneratedMotionMatchingNodeIndices();
	EPoseSearchInterruptMode ResolveDatabaseChangeInterruptMode() const;
	void CacheMotionMatchingPolicyState();

	FProject_JAnimThreadSafeData PendingGameThreadData;
	FProject_JAnimThreadSafeData ThreadSafeData;
	uint64 PublishedSnapshotRevision = 0;
	uint64 ConsumedSnapshotRevision = 0;
	int32 ThreadSafeCandidateCount = 0;
	bool bFlowTraceEnabled = false;
	bool bMovingTurnTraceEnabled = false;
#if !UE_BUILD_SHIPPING
	FName LastMovingTurnTraceDatabase;
	FName LastMovingTurnTraceAnimation;
#endif
	/** A linked layer may re-enter this proxy while its outer graph is still updating. */
	bool bUpdatingMotionMatchingGraph = false;
	FProject_JAnimationFlowWork FlowTraceWork;
	bool bMotionMatchingEnabled = true;
	bool bUpdateMotionMatchingThisFrame = true;
	bool bForceMotionMatchingReselect = false;
	// Only request lifetime is latched; PendingGameThreadData is always replaced.
	uint64 ReselectSerial = 0;
	uint64 PendingReselectRevision = 0;
	bool bReselectFromPoseHistory = false;
	bool bRetireTurnContinuingPose = false;
	bool bLastPublishedForceReselect = false;
	FNodeReselectState NativeReselectState;
	TMap<int32, FNodeReselectState> GeneratedReselectStates;

	TObjectPtr<UPoseSearchDatabase> CurrentActiveDatabase = nullptr;
	TObjectPtr<UPoseSearchDatabase> AppliedDatabase = nullptr;
	TObjectPtr<UPoseSearchDatabase> CurrentTurnCycleCompanion = nullptr;
	TObjectPtr<UPoseSearchDatabase> AppliedTurnCycleCompanion = nullptr;
	/** Database last pushed directly into each generated AnimBP Motion Matching node. */
	TMap<int32, TObjectPtr<UPoseSearchDatabase>> AppliedGeneratedDatabases;
	TMap<int32, TObjectPtr<UPoseSearchDatabase>> AppliedGeneratedCompanions;
	const IAnimClassInterface* CachedGeneratedMotionMatchingAnimClass = nullptr;
	TArray<int32> CachedGeneratedMotionMatchingNodeIndices;
	TMap<int32, float> DefaultSearchThrottleTimes;
	float NativeDefaultSearchThrottleTime = 0.0f;
	bool bHasNativeDefaultSearchThrottleTime = false;
	bool bWasPivotPhaseForDebug = false;
	bool bHasMotionMatchingPolicyState = false;
	bool bLastPolicyWasInAir = false;
	bool bLastPolicyWasMoving = false;
	bool bLastPolicyWasCombat = false;
	/** Tracks the local Combat-Strafe Dynamic/Settled PSD boundary. */
	bool bLastPolicyUsedSettledCycle = false;
	EProject_JLocomotionGaitIntent LastPolicyGaitIntent = EProject_JLocomotionGaitIntent::Run;
	EProject_JLocomotionRotationMode LastPolicyRotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
	EProject_JLocomotionPhaseFamily LastPolicyPhaseFamily = EProject_JLocomotionPhaseFamily::Idle;
	EPoseSearchInterruptMode LastResolvedDatabaseChangeInterruptMode = EPoseSearchInterruptMode::DoNotInterrupt;
	FProject_JAnimMotionMatchingPostSelectionData LatestPostSelection;
	TArray<FProject_JMotionMatchingPivotTraceEntry> PivotDebugTrace;

	FAnimNode_PoseSearchHistoryCollector NativePoseHistoryNode;
	FAnimNode_MotionMatching NativeMotionMatchingNode;
};
