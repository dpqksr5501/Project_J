#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Animation/Project_JGuidedArmSolver.h"
#include "Animation/Project_JGuidedArmChain.h"
#include "Animation/Project_JHandGripProfile.h"
#include "Project_JAnimNode_GuidedHandIK.generated.h"

struct FProject_JGuidedHandIKTraceState;

UENUM()
enum class EProject_JGuidedArmDefinitionSource : uint8
{
	NodeSettings,
	PrimaryBodyProfile,
	SecondaryBodyProfile
};

/** Three-joint contact correction guided by the current input pose, with calibrated wrist rotation. */
USTRUCT(BlueprintInternalUseOnly)
struct PROJECT_JCHARACTER_API FProject_JAnimNode_GuidedHandIK : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	/** Existing graphs retain their static bone/settings. Profile modes snapshot body data on the game thread. */
	UPROPERTY(EditAnywhere, Category = "Arm")
	EProject_JGuidedArmDefinitionSource ArmDefinitionSource = EProject_JGuidedArmDefinitionSource::NodeSettings;

	/** End bone in NodeSettings mode. A body profile may infer its hand from the palm socket. */
	UPROPERTY(EditAnywhere, Category = "Arm")
	FBoneReference HandBone;
	/** Set BOTH to support intervening helper bones. Leave both empty for the legacy parent chain. */
	UPROPERTY(EditAnywhere, Category = "Arm")
	FBoneReference ForearmBone;
	UPROPERTY(EditAnywhere, Category = "Arm")
	FBoneReference UpperArmBone;

	/** Calibrated WRIST transform, in the visible mesh's component space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact", meta = (PinShownByDefault))
	FTransform EffectorTransform = FTransform::Identity;

	/** Use the current INPUT pose's reference bone for hand-driven contact. False retains the component-space target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact|Target Space", meta = (PinHiddenByDefault))
	bool bUseBoneSpaceEffector = false;

	/** Already calibrated wrist target relative to EffectorSpaceBoneName. No Palm conversion is repeated here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact|Target Space", meta = (PinHiddenByDefault))
	FTransform EffectorBoneSpaceTransform = FTransform::Identity;

	/** An independent pose bone, normally the weapon's primary hand; never a bone in this solved arm's subtree. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contact|Target Space", meta = (PinHiddenByDefault))
	FName EffectorSpaceBoneName = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Contact")
	bool bMatchWristRotation = true;

	/** Normally the input pose provides the elbow guide. Enable only for a body-authored guide. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elbow")
	bool bUseExplicitElbowGuide = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elbow", meta = (PinHiddenByDefault, EditCondition = "bUseExplicitElbowGuide"))
	FVector ElbowGuideLocation = FVector::ZeroVector;

	/** Local to the INPUT upper-arm orientation; used only when its bend plane is degenerate. */
	UPROPERTY(EditAnywhere, Category = "Elbow")
	FVector FallbackPoleAxis = FVector::YAxisVector;

	/** Retain a stable INPUT bend direction near a straight arm, then smoothly reacquire it. */
	UPROPERTY(EditAnywhere, Category = "Elbow Stability")
	bool bStabilizeBendPlane = true;

	/** Input elbow height / shorter segment length below which its bend direction is held. */
	UPROPERTY(EditAnywhere, Category = "Elbow Stability", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bStabilizeBendPlane"))
	float HoldBendRatio = 0.15f;

	UPROPERTY(EditAnywhere, Category = "Elbow Stability", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bStabilizeBendPlane"))
	float ReliableBendRatio = 0.25f;

	UPROPERTY(EditAnywhere, Category = "Elbow Stability", meta = (ClampMin = "1.0", Units = "deg/s", EditCondition = "bStabilizeBendPlane"))
	float ReacquireDegreesPerSecond = 720.0f;

	/** Discard guide history after an evaluation gap; never catch up an old final elbow. */
	UPROPERTY(EditAnywhere, Category = "Elbow Stability", meta = (ClampMin = "0.01", Units = "s", EditCondition = "bStabilizeBendPlane"))
	float MaxGuideHistorySeconds = 0.25f;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void EvaluateComponentPose_AnyThread(FComponentSpacePoseContext& Output) override;
	virtual bool NeedsDynamicReset() const override { return true; }
	virtual void ResetDynamics(ETeleportType InTeleportType) override;

	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;

	virtual bool HasPreUpdate() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;

private:
	Project_J::Animation::FGuidedArmChain Chain;
	FProject_JGripArmBones ProfileArmSnapshot;
	FProject_JArmBendStability ProfileStabilitySnapshot;
	bool bProfileArmValid = false;
	FProject_JGripArmBones CachedArmNames;
	EProject_JGuidedArmDefinitionSource CachedArmSource = EProject_JGuidedArmDefinitionSource::NodeSettings;
	FName CachedEffectorSpaceBoneName = NAME_None;
	bool bCachedUseBoneSpaceEffector = false;
	bool bEffectorSpaceValid = false;
	FCompactPoseBoneIndex EffectorSpaceIndex = FCompactPoseBoneIndex(INDEX_NONE);
	bool ResolveComponentSpaceEffector(FComponentSpacePoseContext& Output, FTransform& OutTarget) const;
	FProject_JGripArmBones GetRequestedArm() const;
	Project_J::Animation::FGuidedArmBendState BendState;
	FGraphTraversalCounter LastActiveUpdate;
	double PendingGuideDeltaTime = 0.0;
#if !UE_BUILD_SHIPPING
	TSharedPtr<FProject_JGuidedHandIKTraceState> TraceState;
#endif
};
