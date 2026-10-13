#pragma once

#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"
#include "Animation/Project_JStrafeUpperBodyFacing.h"
#include "AnimNode_ProjectJStrafeUpperBodyFacing.generated.h"

/** Local-pose presentation AFTER Pose History. Does not modify search, root or legs. */
USTRUCT(BlueprintInternalUseOnly)
struct PROJECT_JCHARACTER_API FAnimNode_ProjectJStrafeUpperBodyFacing : public FAnimNode_Base
{
	GENERATED_BODY()
	FAnimNode_ProjectJStrafeUpperBodyFacing();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Links")
	FPoseLink Source;
	UPROPERTY(EditAnywhere, Category = "Rig")
	bool bUseProfileBones = true;
	UPROPERTY(EditAnywhere, Category = "Rig", meta = (EditCondition = "!bUseProfileBones"))
	FBoneReference SpineRoot;
	UPROPERTY(EditAnywhere, Category = "Rig", meta = (EditCondition = "!bUseProfileBones"))
	FBoneReference Chest;
	/** Highest mesh LOD to correct; -1 means all. Excluded LODs pass through. */
	UPROPERTY(EditAnywhere, Category = "Performance", meta = (ClampMin = "-1"))
	int32 LODThreshold = 1;
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void ResetDynamics(ETeleportType InTeleportType) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	float GetAppliedYaw() const { return Correction; }
	float GetInputChestYaw() const { return InputChestYaw; }
	float GetFacingError() const { return FacingError; }
	float GetDesiredYaw() const { return DesiredYaw; }
	float GetRootWeight() const { return RootWeight; }
	float GetUprightWeight() const { return UprightWeight; }
	bool WasEnabled() const { return Input.bEnabled; }
private:
	void ResolveBones(const FBoneContainer& Bones);
	FProject_JStrafeUpperBodyFacingInput Input;
	TArray<FCompactPoseBoneIndex> Ancestors;
	int32 SpineStart = INDEX_NONE;
	int32 RootPosition = INDEX_NONE;
	FName CachedRoot, CachedSpineRoot, CachedChest;
	FQuat ReferenceChest = FQuat::Identity;
	FQuat ReferenceRoot = FQuat::Identity;
	FGraphTraversalCounter LastUpdate;
	float PendingDeltaTime = 0.f;
	float Correction = 0.f;
	float FacingError = 0.f;
	float DesiredYaw = 0.f;
	float InputChestYaw = 0.f;
	float RootWeight = 0.f;
	float UprightWeight = 0.f;
};
