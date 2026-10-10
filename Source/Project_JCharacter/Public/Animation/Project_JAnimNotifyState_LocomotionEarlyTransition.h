// Copyright Project_J. All Rights Reserved.

#pragma once

#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Project_JLocomotionAnimTypes.h"
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
	/** Original six GASP Run clips use Re-Transition / Gait != Run.
	 * This bridge permits the current held one-shot to be re-evaluated; it does
	 * not implement GASP's separate Transition-to-Loop destination. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transition")
	bool bRequireGaitChange = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transition")
	EProject_JLocomotionGaitIntent ExcludedGait = EProject_JLocomotionGaitIntent::Run;
	virtual void NotifyTick(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		float FrameDeltaTime,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override
	{
		return TEXT("Project_J Locomotion Early Transition");
	}
};
