// Copyright Project_J. All Rights Reserved.

#include "Animation/Project_JAnimNotifyState_LocomotionEarlyTransition.h"

#include "Animation/Project_JPlayerAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimNotifyLibrary.h"

void UProject_JAnimNotifyState_LocomotionEarlyTransition::NotifyTick(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float FrameDeltaTime,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);

	RequestGASPTransition(MeshComp, EventReference, 0, bRequireGaitChange ? 1 : 0, uint8(ExcludedGait));
}

void UProject_JAnimNotifyState_LocomotionEarlyTransition::RequestGASPTransition(USkeletalMeshComponent* MeshComp,
	const FAnimNotifyEventReference& EventReference, uint8 Destination, uint8 Condition, uint8 GaitNotEqual)
{
	if (!MeshComp || Destination > 1 || Condition > 1 || GaitNotEqual > uint8(EProject_JLocomotionGaitIntent::Sprint) ||
		UAnimNotifyLibrary::IsBlendingOut(EventReference)) return;
	if (auto* Anim = Cast<UProject_JPlayerAnimInstance>(MeshComp->GetAnimInstance()))
		Anim->RequestLocomotionTransition(Destination == 1, Condition == 1, EProject_JLocomotionGaitIntent(GaitNotEqual));
}
