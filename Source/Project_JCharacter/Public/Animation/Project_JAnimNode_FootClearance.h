#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Animation/Project_JFootContactData.h"
#include "Project_JAnimNode_FootClearance.generated.h"

/** Minimal sole-plane depenetration before hand/body contact. Uses the existing foot quality policy. */
USTRUCT(BlueprintInternalUseOnly)
struct PROJECT_JCHARACTER_API FProject_JAnimNode_FootClearance : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()
public:
	// The body's proxy prepares this immediately before evaluation, after movement.
	// Ordinary PreUpdate can run early to extract animation for Mover/root motion.
	virtual bool HasPreUpdate() const override { return false; }
	void PrepareContact(const UAnimInstance* Instance);
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void InitializeBoneReferences(const FBoneContainer&) override {}
	virtual bool IsValidToEvaluate(const USkeleton*, const FBoneContainer& Bones) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& Transforms) override;
	static float RequiredLift(TConstArrayView<FVector> Positions, const FPlane& Plane, float Clearance, float MaxLift);
#if WITH_EDITOR
	struct FContactDebug
	{
		FName Foot;
		bool bPrepared = false;
		float RequiredLift = -1.f, AppliedLift = -1.f;
	};
	TArray<FContactDebug> GetContactDebugData() const;
#endif
private:
	struct FPreparedLeg
	{
		FProject_JFootSupportSole Data;
		FPlane Plane;
		FCompactPoseBoneIndex Thigh{INDEX_NONE},Calf{INDEX_NONE},Foot{INDEX_NONE};
		TArray<TArray<FCompactPoseBoneIndex>> Influences;
		int32 PointCount = 0;
		bool bActive = false;
		bool bValid = false;
#if WITH_EDITOR
		float DebugRequiredLift = -1.f, DebugAppliedLift = -1.f;
#endif
	};
	TArray<FPreparedLeg> Legs;
};
