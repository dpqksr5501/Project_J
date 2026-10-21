// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JLocomotionAnimStateComponentBase.h"
#include "Movement/Project_JMovementQueries.h"

#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Project_JPlayerCharacter.h"
#include "Animation/Project_JPresentationMeshResolver.h"

UProject_JLocomotionAnimStateComponentBase::UProject_JLocomotionAnimStateComponentBase()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JLocomotionAnimStateComponentBase::BeginPlay()
{
	Super::BeginPlay();

	CacheOwnerReferences();
}

void UProject_JLocomotionAnimStateComponentBase::RefreshCachedReferences()
{
	CacheOwnerReferences();
}

void UProject_JLocomotionAnimStateComponentBase::CacheOwnerReferences()
{
	const auto PreviousASC = CachedAbilitySystemComponent;
	CachedPlayerOwner = Cast<AProject_JPlayerCharacter>(GetOwner());
	CachedCapsuleComponent = CachedPlayerOwner ? CachedPlayerOwner->GetCapsuleComponent() : nullptr;
	CachedAbilitySystemComponent = CachedPlayerOwner ? CachedPlayerOwner->GetAbilitySystemComponent() : nullptr;
	CachedMeshComponent = CachedPlayerOwner ? CachedPlayerOwner->GetMesh() : nullptr;
	if (PreviousASC != CachedAbilitySystemComponent) { OnCachedAbilitySystemChanged(); }
}

AProject_JPlayerCharacter* UProject_JLocomotionAnimStateComponentBase::GetPlayerOwner() const
{
	return CachedPlayerOwner ? CachedPlayerOwner.Get() : Cast<AProject_JPlayerCharacter>(GetOwner());
}

UAbilitySystemComponent* UProject_JLocomotionAnimStateComponentBase::GetCachedAbilitySystemComponent() const
{
	return CachedAbilitySystemComponent.Get();
}

UCapsuleComponent* UProject_JLocomotionAnimStateComponentBase::GetCachedCapsuleComponent() const
{
	return CachedCapsuleComponent.Get();
}

USkeletalMeshComponent* UProject_JLocomotionAnimStateComponentBase::GetCachedMeshComponent() const
{
	return CachedMeshComponent.Get();
}

bool UProject_JLocomotionAnimStateComponentBase::IsInAirForAnimation() const
{
	return Project_J::Movement::IsInAir(GetOwner());
}

bool UProject_JLocomotionAnimStateComponentBase::IsDedicatedServerContext() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->GetNetMode() == NM_DedicatedServer;
}

bool UProject_JLocomotionAnimStateComponentBase::WasRecentlyRendered(float RecentlyRenderedTolerance) const
{
	const AProject_JPlayerCharacter* Player = GetPlayerOwner();
	return Player && Project_J::Animation::WasCharacterVisualRecentlyRendered(*Player, RecentlyRenderedTolerance);
}
