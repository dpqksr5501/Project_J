#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Mount/Project_JMountTypes.h"
#include "Project_JAnimNotify_MountFlightCue.generated.h"

/** Reusable animation cue for every flying mount. */
UCLASS(meta = (DisplayName = "Project J Mount Flight Cue"))
class PROJECT_JMOUNT_API UProject_JAnimNotify_MountFlightCue : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	EProject_JMountFlightAnimationCue GetCue() const { return Cue; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mount|Flight")
	EProject_JMountFlightAnimationCue Cue = EProject_JMountFlightAnimationCue::TakeOffImpulse;
};
