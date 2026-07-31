// Copyright Project_J. All Rights Reserved.

#pragma once

#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Project_JAnimNotifyState_LocomotionEarlyTransition.generated.h"

/**
 * Opens an authored, game-thread-safe permission window for the experimental
 * locomotion Blend Stack State Controller to leave a non-looping transition
 * early. It does not change CharacterMovement, gameplay state, or replication.
 */
UCLASS(meta = (DisplayName = "Project J Locomotion Early Transition"))
class PROJECT_JCHARACTER_API UProject_JAnimNotifyState_LocomotionEarlyTransition : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override
	{
		return TEXT("Project_J Locomotion Early Transition");
	}
};
