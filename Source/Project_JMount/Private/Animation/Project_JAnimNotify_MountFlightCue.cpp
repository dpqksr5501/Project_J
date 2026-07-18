#include "Animation/Project_JAnimNotify_MountFlightCue.h"

#include "Components/SkeletalMeshComponent.h"
#include "Mount/Project_JFlyingMountCharacter.h"

void UProject_JAnimNotify_MountFlightCue::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (MeshComp)
	{
		if (AProject_JFlyingMountCharacter* Mount = Cast<AProject_JFlyingMountCharacter>(MeshComp->GetOwner()))
		{
			Mount->HandleFlightAnimationCue(Cue);
		}
	}
}
