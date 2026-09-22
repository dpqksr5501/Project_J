// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JCharacterAnimInstanceBase.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Animation/Project_JRetargetAnimInstance.h"
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
	CachedRuntimeRetargetFollowerMesh.Reset();
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
	if (CachedRuntimeRetargetFollowerMesh.IsValid())
	{
		return CachedRuntimeRetargetFollowerMesh.Get();
	}

	if (!OwningCharacter)
	{
		return nullptr;
	}

	const USkeletalMeshComponent* LeaderMesh = OwningCharacter->GetMesh();

	// 1. Check direct children attached to LeaderMesh
	if (LeaderMesh)
	{
		for (USceneComponent* Child : LeaderMesh->GetAttachChildren())
		{
			if (USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(Child))
			{
				if (SkelMesh->IsA<UProject_JModularMeshComponent>())
				{
					continue;
				}

				if (SkelMesh->GetAnimInstance() && SkelMesh->GetAnimInstance()->IsA<UProject_JRetargetAnimInstance>())
				{
					CachedRuntimeRetargetFollowerMesh = SkelMesh;
					return SkelMesh;
				}

				if (SkelMesh->GetAnimClass() && SkelMesh->GetAnimClass()->IsChildOf(UProject_JRetargetAnimInstance::StaticClass()))
				{
					CachedRuntimeRetargetFollowerMesh = SkelMesh;
					return SkelMesh;
				}
			}
		}
	}

	// 2. Search components across OwningCharacter
	TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes;
	OwningCharacter->GetComponents(SkeletalMeshes);
	for (USkeletalMeshComponent* SkelMesh : SkeletalMeshes)
	{
		if (!SkelMesh || SkelMesh == LeaderMesh || SkelMesh->IsA<UProject_JModularMeshComponent>())
		{
			continue;
		}

		if (SkelMesh->GetAnimInstance() && SkelMesh->GetAnimInstance()->IsA<UProject_JRetargetAnimInstance>())
		{
			CachedRuntimeRetargetFollowerMesh = SkelMesh;
			return SkelMesh;
		}

		if (SkelMesh->GetAnimClass() && SkelMesh->GetAnimClass()->IsChildOf(UProject_JRetargetAnimInstance::StaticClass()))
		{
			CachedRuntimeRetargetFollowerMesh = SkelMesh;
			return SkelMesh;
		}
	}

	// 3. Fallback: match tags or name on attached non-modular children
	if (LeaderMesh)
	{
		for (USceneComponent* Child : LeaderMesh->GetAttachChildren())
		{
			if (USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(Child))
			{
				if (SkelMesh->IsA<UProject_JModularMeshComponent>())
				{
					continue;
				}

				if (SkelMesh->ComponentTags.Contains(TEXT("VisualFollower")) ||
					SkelMesh->ComponentTags.Contains(TEXT("RetargetFollower")) ||
					SkelMesh->GetName().Contains(TEXT("VisualMesh")) ||
					SkelMesh->GetName().Contains(TEXT("Follower")))
				{
					CachedRuntimeRetargetFollowerMesh = SkelMesh;
					return SkelMesh;
				}
			}
		}
	}

	return nullptr;
}

bool UProject_JCharacterAnimInstanceBase::WasOwnerVisualRecentlyRendered(float RecentlyRenderedTolerance) const
{
	if (!OwningCharacter)
	{
		return false;
	}

	// 1. Follower Mesh precedence
	if (const USkeletalMeshComponent* FollowerMesh = GetRuntimeRetargetFollowerMesh())
	{
		if (FollowerMesh->IsRegistered() && FollowerMesh->WasRecentlyRendered(RecentlyRenderedTolerance))
		{
			return true;
		}
	}

	// 2. Any other non-leader skeletal visual component (e.g. modular equipment)
	const USkeletalMeshComponent* LeaderMesh = OwningCharacter->GetMesh();
	TInlineComponentArray<USkeletalMeshComponent*> SkeletalMeshes;
	OwningCharacter->GetComponents(SkeletalMeshes);
	for (const USkeletalMeshComponent* Mesh : SkeletalMeshes)
	{
		if (!Mesh || !Mesh->IsRegistered() || Mesh == LeaderMesh)
		{
			continue;
		}

		if (Mesh->WasRecentlyRendered(RecentlyRenderedTolerance))
		{
			return true;
		}
	}

	// 3. Leader mesh itself only if it is an active visible renderer (not hidden in game)
	if (LeaderMesh && LeaderMesh->IsRegistered() && !LeaderMesh->bHiddenInGame && LeaderMesh->GetVisibleFlag())
	{
		if (LeaderMesh->WasRecentlyRendered(RecentlyRenderedTolerance))
		{
			return true;
		}
	}

	return false;
}

bool UProject_JCharacterAnimInstanceBase::WasOwnerRecentlyRendered(float RecentlyRenderedTolerance) const
{
	return WasOwnerVisualRecentlyRendered(RecentlyRenderedTolerance);
}


