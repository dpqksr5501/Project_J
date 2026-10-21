// Copyright Project_J. All Rights Reserved.

#pragma once

#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Project_JLocomotionAnimTypes.h"
#include "Project_JAnimNotifyState_LocomotionEarlyTransition.generated.h"

/**
 * Authored GASP transition request. It does not change physical movement,
 * gameplay state, or replication. Existing clip payloads retain their meaning.
 */
UCLASS(meta = (DisplayName = "Project J Locomotion Early Transition"))
class PROJECT_JCHARACTER_API UProject_JAnimNotifyState_LocomotionEarlyTransition : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	/** The original six Run clips request Re-Transition / Gait != Run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transition")
	bool bRequireGaitChange = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transition")
	EProject_JLocomotionGaitIntent ExcludedGait = EProject_JLocomotionGaitIntent::Run;
	/** GASP BP adapter: destination 0=Re-Transition, 1=ToLoop; condition 0=Always, 1=GaitNotEqual. */
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion")
	static void RequestGASPTransition(USkeletalMeshComponent* MeshComp, const FAnimNotifyEventReference& EventReference,
		uint8 Destination, uint8 Condition, uint8 GaitNotEqual);
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
