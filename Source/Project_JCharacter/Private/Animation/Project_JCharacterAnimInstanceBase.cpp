// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JCharacterAnimInstanceBase.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Project_JPlayerCharacter.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"

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
	CachedTrajectoryComponent = OwningCharacter ? OwningCharacter->FindComponentByClass<UProject_JMotionMatchingTrajectoryComponent>() : nullptr;
}

bool UProject_JCharacterAnimInstanceBase::NeedsOwnerReferenceRefresh() const
{
	return !OwningPawn || OwningPawn != TryGetPawnOwner();
}

bool UProject_JCharacterAnimInstanceBase::IsDedicatedServerAnimationContext() const
{
	return OwningCharacter && OwningCharacter->GetNetMode() == NM_DedicatedServer;
}

bool UProject_JCharacterAnimInstanceBase::IsLocallyControlledCharacter() const
{
	return OwningCharacter && OwningCharacter->IsLocallyControlled();
}

bool UProject_JCharacterAnimInstanceBase::WasOwnerRecentlyRendered(float RecentlyRenderedTolerance) const
{
	if (!OwningCharacter)
	{
		return false;
	}

	const USkeletalMeshComponent* MeshComponent = OwningCharacter->GetMesh();
	return !MeshComponent || MeshComponent->WasRecentlyRendered(RecentlyRenderedTolerance);
}


