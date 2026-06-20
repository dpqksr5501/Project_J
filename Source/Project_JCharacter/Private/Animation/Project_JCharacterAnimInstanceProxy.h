#pragma once

#include "Animation/AnimInstanceProxy.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"

class UPoseSearchDatabase;

struct FProject_JCharacterAnimInstanceProxy : public FAnimInstanceProxy
{
	FProject_JCharacterAnimInstanceProxy();
	explicit FProject_JCharacterAnimInstanceProxy(UAnimInstance* InAnimInstance);

	void QueueGameThreadData(
		const FProject_JAnimThreadSafeData& InData,
		UPoseSearchDatabase* InSelectedDatabase,
		bool bInMotionMatchingEnabled,
		bool bInUpdateMotionMatchingThisFrame);

	const FProject_JAnimThreadSafeData& GetThreadSafeData() const { return ThreadSafeData; }
	UPoseSearchDatabase* GetCurrentActiveDatabase() const { return CurrentActiveDatabase.Get(); }

protected:
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void UpdateAnimationNode_WithRoot(
		const FAnimationUpdateContext& InContext,
		FAnimNode_Base* InRootNode,
		FName InLayerName) override;
	virtual FAnimNode_Base* GetCustomRootNode() override;
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

private:
	struct FInAirBlendStackDebugState
	{
		int32 StackCount = INDEX_NONE;
		FString StackSignature;
		bool bWasInAir = false;
	};

	void LinkNativeGraph();
	void ApplySelectedDatabaseToNativeNode();
	void ApplyMotionMatchingSearchPolicy();
	void LogInAirAnimBlueprintBlendStacks();

	FProject_JAnimThreadSafeData PendingGameThreadData;
	FProject_JAnimThreadSafeData ThreadSafeData;
	bool bMotionMatchingEnabled = true;
	bool bUpdateMotionMatchingThisFrame = true;

	TObjectPtr<UPoseSearchDatabase> CurrentActiveDatabase = nullptr;
	TObjectPtr<UPoseSearchDatabase> AppliedDatabase = nullptr;
	TMap<int32, FInAirBlendStackDebugState> InAirBlendStackDebugStates;
	TMap<int32, float> DefaultSearchThrottleTimes;
	float NativeDefaultSearchThrottleTime = 0.0f;
	bool bHasNativeDefaultSearchThrottleTime = false;

	FAnimNode_PoseSearchHistoryCollector NativePoseHistoryNode;
	FAnimNode_MotionMatching NativeMotionMatchingNode;
};
