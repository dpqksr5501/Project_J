#pragma once

#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Project_JAnimNode_WingInheritance.generated.h"

/** Restores partial torso rotation inheritance to Winger's root-space wing pose. */
USTRUCT(BlueprintInternalUseOnly)
struct PROJECT_JCHARACTER_API FProject_JAnimNode_WingInheritance : public FAnimNode_SkeletalControlBase
{
    GENERATED_BODY()

    FProject_JAnimNode_WingInheritance() { LODThreshold = 1; }

    UPROPERTY(EditAnywhere, Category="Inheritance", meta=(PinHiddenByDefault))
    FRotator MaxTorsoRotation = FRotator(5.0, 12.0, 10.0);

    UPROPERTY(EditAnywhere, Category="Inheritance", meta=(PinHiddenByDefault, ClampMin="0.001", Units="s"))
    float MidResponseTime = 0.055f;

    UPROPERTY(EditAnywhere, Category="Inheritance", meta=(PinHiddenByDefault, ClampMin="0.001", Units="s"))
    float TipResponseTime = 0.115f;

    virtual void InitializeBoneReferences(const FBoneContainer& Bones) override;
    virtual bool IsValidToEvaluate(const USkeleton*, const FBoneContainer&) override { return bValidChain; }
    virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
    virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
        TArray<FBoneTransform>& OutBoneTransforms) override;

private:
    static constexpr int32 NumStages = 5;
    FCompactPoseBoneIndex Root{INDEX_NONE};
    FCompactPoseBoneIndex Torso{INDEX_NONE};
    FCompactPoseBoneIndex WingChains[2][NumStages];
    FQuat InverseReferenceTorso = FQuat::Identity;
    FQuat MidRotation = FQuat::Identity;
    FQuat TipRotation = FQuat::Identity;
    float PendingSeconds = 0.f;
    bool bValidChain = false;
    bool bHasHistory = false;
};
