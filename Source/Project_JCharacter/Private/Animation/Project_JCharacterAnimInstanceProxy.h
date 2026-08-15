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
		bool bInForceMotionMatchingReselect);

	const FProject_JAnimThreadSafeData& GetThreadSafeData() const { return ThreadSafeData; }
	UPoseSearchDatabase* GetCurrentActiveDatabase() const { return CurrentActiveDatabase.Get(); }
	const FProject_JAnimMotionMatchingPostSelectionData& GetLatestPostSelection() const { return LatestPostSelection; }
	const FProject_JAnimOneShotPlaybackFeedback& GetLatestOneShotPlaybackFeedback() const { return LatestOneShotPlaybackFeedback; }
	FString GetPivotTraceSummary() const;

protected:
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void UpdateAnimationNode_WithRoot(
		const FAnimationUpdateContext& InContext,
		FAnimNode_Base* InRootNode,
		FName InLayerName) override;
	virtual FAnimNode_Base* GetCustomRootNode() override;
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

private:
	void LinkNativeGraph();
	void ApplySelectedDatabaseToNativeNode();
	void ApplyMotionMatchingSearchPolicy();
	void ForceReselectMotionMatchingNodes();
	void CapturePostSelection();
	void CaptureOneShotPlaybackFeedback();
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
	bool bMotionMatchingEnabled = true;
	bool bUpdateMotionMatchingThisFrame = true;
	bool bForceMotionMatchingReselect = false;

	TObjectPtr<UPoseSearchDatabase> CurrentActiveDatabase = nullptr;
	TObjectPtr<UPoseSearchDatabase> AppliedDatabase = nullptr;
	/** Database last pushed directly into each generated AnimBP Motion Matching node. */
	TMap<int32, TObjectPtr<UPoseSearchDatabase>> AppliedGeneratedDatabases;
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
	EProject_JLocomotionGaitIntent LastPolicyGaitIntent = EProject_JLocomotionGaitIntent::Run;
	EProject_JLocomotionRotationMode LastPolicyRotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
	EPoseSearchInterruptMode LastResolvedDatabaseChangeInterruptMode = EPoseSearchInterruptMode::DoNotInterrupt;
	FProject_JAnimMotionMatchingPostSelectionData LatestPostSelection;
	/** Updated after the generated AnimGraph has advanced its Blend Stack players. */
	FProject_JAnimOneShotPlaybackFeedback LatestOneShotPlaybackFeedback;
	const IAnimClassInterface* CachedOneShotBlendStackAnimClass = nullptr;
	int32 CachedOneShotBlendStackNodeIndex = INDEX_NONE;
	TArray<FProject_JMotionMatchingPivotTraceEntry> PivotDebugTrace;

	FAnimNode_PoseSearchHistoryCollector NativePoseHistoryNode;
	FAnimNode_MotionMatching NativeMotionMatchingNode;
};
