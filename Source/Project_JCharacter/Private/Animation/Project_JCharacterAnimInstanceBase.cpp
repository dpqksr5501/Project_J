// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JCharacterAnimInstanceBase.h"

#include "GameFramework/Character.h"
#include "Project_JPlayerCharacter.h"

void UProject_JCharacterAnimInstanceBase::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	CacheOwnerReferences();
}

void UProject_JCharacterAnimInstanceBase::CacheOwnerReferences()
{
	OwningPawn = TryGetPawnOwner();
	OwningCharacter = Cast<ACharacter>(OwningPawn);
	OwningPlayerCharacter = Cast<AProject_JPlayerCharacter>(OwningCharacter);
	LocomotionAnimStateComponent = OwningPlayerCharacter ? OwningPlayerCharacter->GetLocomotionAnimStateComponent() : nullptr;
}

bool UProject_JCharacterAnimInstanceBase::NeedsOwnerReferenceRefresh() const
{
	return !OwningPawn || OwningPawn != TryGetPawnOwner();
}
