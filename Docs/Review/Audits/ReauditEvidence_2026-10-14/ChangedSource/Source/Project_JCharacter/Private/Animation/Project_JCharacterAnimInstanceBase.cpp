// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JCharacterAnimInstanceBase.h"

#include "GameFramework/Character.h"
#include "Project_JPlayerCharacter.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "Animation/Project_JPresentationMeshResolver.h"

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

USkeletalMeshComponent* UProject_JCharacterAnimInstanceBase::GetRuntimeRetargetFollowerMesh() const
{
	return OwningCharacter ? Project_J::Animation::FindVisualFollower(*OwningCharacter, false) : nullptr;
}

bool UProject_JCharacterAnimInstanceBase::WasOwnerVisualRecentlyRendered(float RecentlyRenderedTolerance) const
{
	return OwningCharacter && Project_J::Animation::WasCharacterVisualRecentlyRendered(*OwningCharacter, RecentlyRenderedTolerance);
}

bool UProject_JCharacterAnimInstanceBase::WasOwnerRecentlyRendered(float RecentlyRenderedTolerance) const
{
	return WasOwnerVisualRecentlyRendered(RecentlyRenderedTolerance);
}


