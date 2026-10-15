#pragma once

#include "Animation/Project_JCharacterAnimInstance.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JAnimationContinuationFixture.generated.h"

/** Exposes the native GT continuity entry point without duplicating its policy. */
UCLASS(Transient, NotBlueprintable)
class UProject_JAnimationContinuationFixture : public UProject_JCharacterAnimInstance
{
	GENERATED_BODY()
public:
	void EvaluateBackward(AProject_JPlayerCharacter* Player, const FProject_JTurnSelectionFeedback& Feedback,
		FProject_JAnimThreadSafeData& Data)
	{
		OwningCharacter = Player;
		OwningPlayerCharacter = Player;
		CompletedTurnFeedback = Feedback;
		UpdateBackwardRefaceData(Data);
	}
};
