// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JLocomotionAnimNotify.h"

#include "Animation/Project_JCharacterAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

void UProject_JLocomotionAnimNotify::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase*, const FAnimNotifyEventReference&)
{
	if (!MeshComp)
	{
		return;
	}

	if (UProject_JCharacterAnimInstance* AnimInstance = Cast<UProject_JCharacterAnimInstance>(MeshComp->GetAnimInstance()))
	{
		AnimInstance->HandleLocomotionAnimEvent(EventType);
	}
}

FString UProject_JLocomotionAnimNotify::GetNotifyName_Implementation() const
{
	switch (EventType)
	{
	case EProject_JLocomotionAnimEvent::GroundStartFinished:
		return TEXT("Locomotion: Ground Start Finished");
	case EProject_JLocomotionAnimEvent::StopFinished:
		return TEXT("Locomotion: Stop Finished");
	case EProject_JLocomotionAnimEvent::JumpStartFinished:
		return TEXT("Locomotion: Jump Start Finished");
	case EProject_JLocomotionAnimEvent::FallOffStartFinished:
		return TEXT("Locomotion: Fall Off Start Finished");
	case EProject_JLocomotionAnimEvent::LandingFinished:
		return TEXT("Locomotion: Landing Finished");
	case EProject_JLocomotionAnimEvent::HitReactFinished:
		return TEXT("Locomotion: Hit React Finished");
	case EProject_JLocomotionAnimEvent::AttackFinished:
		return TEXT("Locomotion: Attack Finished");
	default:
		return TEXT("Locomotion Event");
	}
}
