// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JLocomotionAnimStateComponentBase.h"

#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Project_JPlayerCharacter.h"

UProject_JLocomotionAnimStateComponentBase::UProject_JLocomotionAnimStateComponentBase()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JLocomotionAnimStateComponentBase::BeginPlay()
{
	Super::BeginPlay();

	CacheOwnerReferences();
}

void UProject_JLocomotionAnimStateComponentBase::CacheOwnerReferences()
{
	CachedPlayerOwner = Cast<AProject_JPlayerCharacter>(GetOwner());
	CachedMovementComponent = CachedPlayerOwner ? CachedPlayerOwner->GetCharacterMovement() : nullptr;
	CachedCapsuleComponent = CachedPlayerOwner ? CachedPlayerOwner->GetCapsuleComponent() : nullptr;
	CachedAbilitySystemComponent = CachedPlayerOwner ? CachedPlayerOwner->GetAbilitySystemComponent() : nullptr;
	CachedMeshComponent = CachedPlayerOwner ? CachedPlayerOwner->GetMesh() : nullptr;
}

AProject_JPlayerCharacter* UProject_JLocomotionAnimStateComponentBase::GetPlayerOwner() const
{
	return CachedPlayerOwner ? CachedPlayerOwner.Get() : Cast<AProject_JPlayerCharacter>(GetOwner());
}

UCharacterMovementComponent* UProject_JLocomotionAnimStateComponentBase::GetCachedMovementComponent() const
{
	return CachedMovementComponent.Get();
}

UAbilitySystemComponent* UProject_JLocomotionAnimStateComponentBase::GetCachedAbilitySystemComponent() const
{
	return CachedAbilitySystemComponent.Get();
}
