#pragma once

#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"
#include "Animation/Project_JStrafeLowerBodyFacing.h"
#include "AnimNode_ProjectJStrafeLowerBodyFacing.generated.h"

/** After Pose History, before Upper Body Facing. Never changes search or root motion. */
USTRUCT(BlueprintInternalUseOnly)
struct PROJECT_JCHARACTER_API FAnimNode_ProjectJStrafeLowerBodyFacing : public FAnimNode_Base
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Links")
	FPoseLink Source;
	UPROPERTY(EditAnywhere, Category = "Performance", meta = (ClampMin = "-1"))
	int32 LODThreshold = 1;
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void ResetDynamics(ETeleportType InTeleportType) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	float GetAppliedYaw() const { return Correction; }
	float GetInputPelvisYaw() const { return InputYaw; }
	float GetDesiredYaw() const { return Desired; }
	float GetReachScale() const { return ReachScale; }
	bool WasEnabled() const { return Input.Facing.bEnabled; }
#if WITH_DEV_AUTOMATION_TESTS
	bool GetValidationSourceFeet(FTransform& Left, FTransform& Right) const
	{ Left = ValidationSourceFeet[0]; Right = ValidationSourceFeet[1]; return bValidationSourceFeetValid; }
#endif
private:
	void ResolveBones(const FBoneContainer& Bones);
	FProject_JStrafeLowerBodyFacingInput Input;
	TArray<FName, TInlineAllocator<9>> CachedNames;
	TArray<FCompactPoseBoneIndex, TInlineAllocator<32>> PelvisPath;
	TArray<FCompactPoseBoneIndex, TInlineAllocator<9>> Indices;
	FQuat ReferencePelvis = FQuat::Identity, ReferenceRoot = FQuat::Identity;
	int32 RootPosition = INDEX_NONE;
	FGraphTraversalCounter LastUpdate;
	float PendingDeltaTime = 0.f, Correction = 0.f, InputYaw = 0.f, Desired = 0.f, ReachScale = 1.f;
#if WITH_DEV_AUTOMATION_TESTS
	FTransform ValidationSourceFeet[2];
	bool bValidationSourceFeetValid = false;
#endif
};
