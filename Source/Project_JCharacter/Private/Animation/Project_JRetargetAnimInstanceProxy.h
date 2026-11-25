#pragma once

#include "Animation/AnimInstanceProxy.h"
#include "Animation/Project_JHandGripProfile.h"

class UIKRetargeter;

/** Installs GT-resolved settings before the engine retarget node captures its source pose. */
struct FProject_JRetargetAnimInstanceProxy : FAnimInstanceProxy
{
	struct FHandContactUpdate
	{
		FProject_JInputPoseContact PoseContacts[2];
		FTransform WristTargets[2] = {FTransform::Identity, FTransform::Identity};
		float Alphas[2] = {};
		FName PrimaryHandBone;
		FTransform LeftWristInPrimaryHand = FTransform::Identity;
		bool bUsePrimaryHandSpace = false;
		bool bUseAuthoredCarryRotation = false;
		bool bLimitCarryReach = false;
	};
	FHandContactUpdate HandContacts;

	explicit FProject_JRetargetAnimInstanceProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
	virtual void Initialize(UAnimInstance* Instance) override;
	virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override;
	virtual void PreEvaluateAnimation(UAnimInstance* Instance) override;
	virtual void AddReferencedObjects(UAnimInstance* Instance, FReferenceCollector& Collector) override;
private:
	struct FNodeDefaults
	{
		int32 Index = INDEX_NONE;
		TObjectPtr<UIKRetargeter> Asset = nullptr;
		bool bOverrideSets = false;
		TArray<FName> Sets;
		bool bApplied = false;
	};
	TWeakObjectPtr<UClass> CachedClass;
	TArray<FNodeDefaults> Nodes;
	TArray<int32> FootContactNodes;
};
