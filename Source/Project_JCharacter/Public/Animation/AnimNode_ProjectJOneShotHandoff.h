#pragma once

#include "AnimNodes/AnimNode_BlendListByBool.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "AnimNode_ProjectJOneShotHandoff.generated.h"

/** True=MM, false=external one-shot. Keeps the outgoing command alive only
 * while the engine blend list still gives its branch a nonzero weight. */
USTRUCT(BlueprintInternalUseOnly)
struct PROJECT_JCHARACTER_API FAnimNode_ProjectJOneShotHandoff : public FAnimNode_BlendListByBool
{
	GENERATED_BODY()
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	bool IsLiveReturnActive() const { return bLiveReturn; }
	float GetOutgoingWeight() const { return PerBlendData.IsValidIndex(1) ? PerBlendData[1].Weight : 0.f; }
private:
	UPROPERTY(Transient)
	FProject_JAnimOneShotPresentationThreadSafeData OutgoingPresentation;
	bool bWasOverride = false;
	bool bLiveReturn = false;
	bool bRootTurnEpisode = false;
	bool bHasUpdated = false;
};
