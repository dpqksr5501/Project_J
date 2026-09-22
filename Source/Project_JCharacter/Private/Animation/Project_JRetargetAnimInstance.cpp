// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/Project_JRetargetAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "GameFramework/Character.h"
#include "Project_JPlayerCharacter.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Engine/World.h"

UProject_JRetargetAnimInstance::UProject_JRetargetAnimInstance()
{
	RightGripLocation = FVector::ZeroVector;
	LeftGripLocation = FVector::ZeroVector;
	RightGripAlpha = 1.0f;
	LeftGripAlpha = 0.0f;
	GripIKAlpha = 1.0f;
	PrimaryGripSocketName = TEXT("WeaponGrip_R");
	SecondaryGripSocketName = TEXT("WeaponGrip_L");
	GripInterpSpeed = 12.0f;
	bIsCombatMode = false;
	bEnableCombatGripIK = true;
	bAutoDetectWeaponIfNull = false; // Production default: event-driven via UpdateWeaponTarget

	bHasValidRightSnapshot = false;
	bHasValidLeftSnapshot = false;
	TargetRightAlphaSnapshot = 1.0f;
	TargetLeftAlphaSnapshot = 0.0f;
}

void UProject_JRetargetAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	// Dedicated server: completely disable tick on follower visual mesh to avoid any wasted evaluation
	if (const UWorld* World = GetWorld())
	{
		if (World->GetNetMode() == NM_DedicatedServer)
		{
			if (USkeletalMeshComponent* OwningComp = GetOwningComponent())
			{
				OwningComp->SetComponentTickEnabled(false);
			}
			return;
		}
	}

	// Clear transient snapshots
	bHasValidRightSnapshot = false;
	bHasValidLeftSnapshot = false;
	CachedWeaponComponent.Reset();
	CachedPresentationComp.Reset();

	if (APawn* OwnerPawn = TryGetPawnOwner())
	{
		CachedPresentationComp = OwnerPawn->FindComponentByClass<UProject_JWeaponPresentationComponent>();
	}
}

void UProject_JRetargetAnimInstance::UpdateWeaponTarget(USceneComponent* InWeaponComponent, FName InSocketName)
{
	CachedWeaponComponent = InWeaponComponent;
	if (!InSocketName.IsNone())
	{
		PrimaryGripSocketName = InSocketName;
	}
}

void UProject_JRetargetAnimInstance::SetCombatMode(bool bInCombatMode)
{
	bIsCombatMode = bInCombatMode;
}

void UProject_JRetargetAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	// Dedicated server early-out: visual IK and retargeting evaluation are client-only concerns.
	if (const UWorld* World = GetWorld())
	{
		if (World->GetNetMode() == NM_DedicatedServer)
		{
			bHasValidRightSnapshot = false;
			bHasValidLeftSnapshot = false;
			TargetRightAlphaSnapshot = 0.0f;
			TargetLeftAlphaSnapshot = 0.0f;
			return;
		}
	}

	APawn* OwnerPawn = TryGetPawnOwner();
	if (!OwnerPawn)
	{
		bHasValidRightSnapshot = false;
		bHasValidLeftSnapshot = false;
		TargetRightAlphaSnapshot = 0.0f;
		TargetLeftAlphaSnapshot = 0.0f;
		return;
	}

	// 1. Synchronize combat state deterministically from local gameplay authority / ASC
	if (const AProject_JPlayerCharacter* PlayerChar = Cast<AProject_JPlayerCharacter>(OwnerPawn))
	{
		bIsCombatMode = PlayerChar->IsCombatModeActive();
	}
	else if (const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwnerPawn))
	{
		bIsCombatMode = ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
	}

	USkeletalMeshComponent* OwningComp = GetOwningComponent();
	if (!OwningComp)
	{
		bHasValidRightSnapshot = false;
		bHasValidLeftSnapshot = false;
		return;
	}
	SnapshotOwningCompWorldTransform = OwningComp->GetComponentTransform();

	// 2. Try consuming from UProject_JWeaponPresentationComponent if present
	if (!CachedPresentationComp.IsValid())
	{
		CachedPresentationComp = OwnerPawn->FindComponentByClass<UProject_JWeaponPresentationComponent>();
	}

	bool bResolvedFromPresentation = false;
	if (CachedPresentationComp.IsValid())
	{
		const FProject_JWeaponGripTargets GripTargets = CachedPresentationComp->GetWeaponGripTargets();
		if (GripTargets.bHasPrimaryGrip)
		{
			SnapshotRightGripWorldTransform = GripTargets.PrimaryGripWorldTransform;
			bHasValidRightSnapshot = true;
			TargetRightAlphaSnapshot = bIsCombatMode ? (bEnableCombatGripIK ? 1.0f : 0.0f) : 1.0f;
			bResolvedFromPresentation = true;
		}
		if (GripTargets.bHasSecondaryGrip)
		{
			SnapshotLeftGripWorldTransform = GripTargets.SecondaryGripWorldTransform;
			bHasValidLeftSnapshot = true;
			TargetLeftAlphaSnapshot = bIsCombatMode ? (bEnableCombatGripIK ? 1.0f : 0.0f) : 0.0f;
			bResolvedFromPresentation = true;
		}
	}

	// 3. Fallback: Component tracking if not resolved via WeaponPresentationComponent
	if (!bResolvedFromPresentation)
	{
		// Auto-detect fallback only when enabled explicitly
		if (bAutoDetectWeaponIfNull && !CachedWeaponComponent.IsValid())
		{
			TArray<USceneComponent*> Components;
			OwnerPawn->GetComponents(Components);
			for (USceneComponent* Comp : Components)
			{
				if (Comp && Comp->DoesSocketExist(PrimaryGripSocketName))
				{
					CachedWeaponComponent = Comp;
					break;
				}
			}

			if (!CachedWeaponComponent.IsValid())
			{
				TArray<AActor*> AttachedActors;
				OwnerPawn->GetAttachedActors(AttachedActors);
				for (AActor* Attached : AttachedActors)
				{
					if (Attached)
					{
						TArray<USceneComponent*> AttachedComponents;
						Attached->GetComponents(AttachedComponents);
						for (USceneComponent* AttachedComp : AttachedComponents)
						{
							if (AttachedComp && AttachedComp->DoesSocketExist(PrimaryGripSocketName))
							{
								CachedWeaponComponent = AttachedComp;
								break;
							}
						}
						if (CachedWeaponComponent.IsValid())
						{
							break;
						}
					}
				}
			}
		}

		USceneComponent* WeaponComp = CachedWeaponComponent.Get();
		if (WeaponComp && WeaponComp->DoesSocketExist(PrimaryGripSocketName))
		{
			SnapshotRightGripWorldTransform = WeaponComp->GetSocketTransform(PrimaryGripSocketName, RTS_World);
			bHasValidRightSnapshot = true;
			// In sheathed state (weapon on back): Right hand grips hilt (alpha 1.0)
			// In combat state (weapon drawn): Right hand aligns to grip if enabled
			TargetRightAlphaSnapshot = bIsCombatMode ? (bEnableCombatGripIK ? 1.0f : 0.0f) : 1.0f;
		}
		else
		{
			bHasValidRightSnapshot = false;
			TargetRightAlphaSnapshot = 0.0f;
		}

		if (WeaponComp && WeaponComp->DoesSocketExist(SecondaryGripSocketName))
		{
			SnapshotLeftGripWorldTransform = WeaponComp->GetSocketTransform(SecondaryGripSocketName, RTS_World);
			bHasValidLeftSnapshot = true;
			TargetLeftAlphaSnapshot = bIsCombatMode ? (bEnableCombatGripIK ? 1.0f : 0.0f) : 0.0f;
		}
		else
		{
			bHasValidLeftSnapshot = false;
			TargetLeftAlphaSnapshot = 0.0f;
		}
	}
}

void UProject_JRetargetAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);

	// Worker-thread evaluation: purely math on captured snapshots
	if (bHasValidRightSnapshot)
	{
		RightGripLocation = SnapshotOwningCompWorldTransform.InverseTransformPosition(SnapshotRightGripWorldTransform.GetLocation());
	}

	if (bHasValidLeftSnapshot)
	{
		LeftGripLocation = SnapshotOwningCompWorldTransform.InverseTransformPosition(SnapshotLeftGripWorldTransform.GetLocation());
	}

	// Smoothly interpolate IK alphas to eliminate snapping between states
	RightGripAlpha = FMath::FInterpTo(RightGripAlpha, TargetRightAlphaSnapshot, DeltaSeconds, GripInterpSpeed);
	LeftGripAlpha = FMath::FInterpTo(LeftGripAlpha, TargetLeftAlphaSnapshot, DeltaSeconds, GripInterpSpeed);
	GripIKAlpha = RightGripAlpha;
}
