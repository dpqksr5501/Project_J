#pragma once

#include "PoseSearch/AnimNode_MotionMatching.h"
#include "Animation/Project_JPlaybackReturnPolicy.h"
#include "AnimNode_ProjectJLocomotion.generated.h"

/** State-controller clips and regular MM share the same players, sample graph and clock. */
USTRUCT(BlueprintInternalUseOnly)
struct PROJECT_JCHARACTER_API FAnimNode_ProjectJLocomotion : public FAnimNode_MotionMatching
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category=Settings, meta=(ClampMin="0"))
	float OneShotEntryBlendTime = 0.2f;
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	bool IsPlayingOneShot() const { return bPlayingOneShot; }
	bool IsReturning() const;
	float GetOutgoingWeight() const;
	float GetOneShotSecondsRemaining() const;
	int32 GetOneShotSelectionRevision() const { return Presentation.SelectionRevision; }
	UAnimationAsset* GetOneShotAsset() const { return Presentation.SelectedAnimation; }
	float GetInertialReturnSecondsRemaining() const { return InertialReturn.GetRemaining(); }
private:
	UPROPERTY(Transient)
	FProject_JAnimOneShotPresentationThreadSafeData Presentation;
	FGraphTraversalCounter PlaybackUpdateCounter;
	int32 AuthoredPoseLinkIndex = INDEX_NONE;
	bool bPlayingOneShot = false;
	bool bRootTurnEpisode = false;
	ProjectJ::Animation::FInertialReturnRuntime InertialReturn;
};
