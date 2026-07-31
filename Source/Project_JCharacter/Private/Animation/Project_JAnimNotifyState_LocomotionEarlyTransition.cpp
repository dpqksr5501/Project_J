// Copyright Project_J. All Rights Reserved.

#include "Animation/Project_JAnimNotifyState_LocomotionEarlyTransition.h"

#include "Animation/Project_JCharacterAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

void UProject_JAnimNotifyState_LocomotionEarlyTransition::NotifyBegin(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (MeshComp)
	{
		if (UProject_JCharacterAnimInstance* AnimInstance = Cast<UProject_JCharacterAnimInstance>(MeshComp->GetAnimInstance()))
		{
			AnimInstance->BeginOneShotEarlyTransitionWindow();
		}
	}
}

void UProject_JAnimNotifyState_LocomotionEarlyTransition::NotifyEnd(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (MeshComp)
	{
		if (UProject_JCharacterAnimInstance* AnimInstance = Cast<UProject_JCharacterAnimInstance>(MeshComp->GetAnimInstance()))
		{
			AnimInstance->EndOneShotEarlyTransitionWindow();
		}
	}
}
