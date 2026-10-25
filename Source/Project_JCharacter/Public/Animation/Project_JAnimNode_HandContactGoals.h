#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Animation/Project_JHandGripProfile.h"
#include "Project_JAnimNode_HandContactGoals.generated.h"

/** Publish calibrated, alpha-blended wrist goals from this input pose. Does not solve or move the body. */
USTRUCT(BlueprintInternalUseOnly)
struct PROJECT_JCHARACTER_API FProject_JAnimNode_HandContactGoals : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Goals")
	FBoneReference PrimaryGoalBone;
	UPROPERTY(EditAnywhere, Category="Goals")
	FBoneReference SecondaryGoalBone;
	virtual bool HasPreUpdate() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* Instance) override;
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void InitializeBoneReferences(const FBoneContainer& Bones) override;
	virtual bool IsValidToEvaluate(const USkeleton*, const FBoneContainer& Bones) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& Transforms) override;
private:
	struct FTarget
	{
		FProject_JInputPoseContact Relative;
		FTransform Component = FTransform::Identity;
		float Alpha = 0.f;
	};
	FTarget Targets[2];
	FName Hands[2], CachedHands[2], CachedGoals[2], CachedReferences[2];
	FCompactPoseBoneIndex HandIndices[2] = {FCompactPoseBoneIndex(INDEX_NONE),FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex GoalIndices[2] = {FCompactPoseBoneIndex(INDEX_NONE),FCompactPoseBoneIndex(INDEX_NONE)};
	FCompactPoseBoneIndex ReferenceIndices[2] = {FCompactPoseBoneIndex(INDEX_NONE),FCompactPoseBoneIndex(INDEX_NONE)};
	bool bFollowPublishedPrimary = false;
	bool bBonesValid = false;
	FName ReferenceName(int32 Side) const;
};
