// Copyright Project_J. All Rights Reserved.

#include "Animation/Project_JAnimNotifyState_LocomotionEarlyTransition.h"

#include "Animation/Project_JCharacterAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimNotifyLibrary.h"

void UProject_JAnimNotifyState_LocomotionEarlyTransition::NotifyTick(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float FrameDeltaTime,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);

	if (MeshComp && !UAnimNotifyLibrary::IsBlendingOut(EventReference))
	{
		if (UProject_JCharacterAnimInstance* AnimInstance = Cast<UProject_JCharacterAnimInstance>(MeshComp->GetAnimInstance()))
		{
			AnimInstance->RequestAuthoredEarlyTransition(Animation, bRequireGaitChange, ExcludedGait);
		}
	}
}
