// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/Project_JRetargetAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Project_JPlayerCharacter.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Engine/World.h"

UProject_JRetargetAnimInstance::UProject_JRetargetAnimInstance()
{
	RightGripLocation = FVector::ZeroVector;
	GripIKAlpha = 1.0f;
	PrimaryGripSocketName = TEXT("WeaponGrip_R");
	GripInterpSpeed = 12.0f;
	bIsCombatMode = false;
	bAutoDetectWeaponIfNull = true;

	bHasValidSocketSnapshot = false;
	TargetAlphaSnapshot = 1.0f;
}

void UProject_JRetargetAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	// Clear transient snapshots
	bHasValidSocketSnapshot = false;
	CachedWeaponComponent.Reset();
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
			bHasValidSocketSnapshot = false;
			TargetAlphaSnapshot = 0.0f;
			return;
		}
	}

	APawn* OwnerPawn = TryGetPawnOwner();
	if (!OwnerPawn)
	{
		bHasValidSocketSnapshot = false;
		TargetAlphaSnapshot = 0.0f;
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

	// 2. Fallback: Auto-detect weapon component on owner actor if not explicitly set
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

		// Also check child attached actors (e.g. WeaponPresentationActor)
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

	// 3. Capture thread-safe spatial snapshots for worker-thread evaluation
	USkeletalMeshComponent* OwningComp = GetOwningComponent();
	USceneComponent* WeaponComp = CachedWeaponComponent.Get();

	if (OwningComp && WeaponComp && WeaponComp->DoesSocketExist(PrimaryGripSocketName))
	{
		SnapshotWeaponSocketWorldTransform = WeaponComp->GetSocketTransform(PrimaryGripSocketName, RTS_World);
		SnapshotOwningCompWorldTransform = OwningComp->GetComponentTransform();
		bHasValidSocketSnapshot = true;
	}
	else
	{
		bHasValidSocketSnapshot = false;
	}

	// Combat stance: weapon is drawn in hand -> turn off back-grip IK.
	// Sheathed stance: weapon is on back -> turn on back-grip IK.
	TargetAlphaSnapshot = bIsCombatMode ? 0.0f : (bHasValidSocketSnapshot ? 1.0f : 0.0f);
}

void UProject_JRetargetAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);

	// Worker-thread evaluation: no UObject or Actor queries.
	if (bHasValidSocketSnapshot)
	{
		RightGripLocation = SnapshotOwningCompWorldTransform.InverseTransformPosition(SnapshotWeaponSocketWorldTransform.GetLocation());
	}

	// Smoothly interpolate IK alpha to eliminate snapping between sheathed and drawn states
	GripIKAlpha = FMath::FInterpTo(GripIKAlpha, TargetAlphaSnapshot, DeltaSeconds, GripInterpSpeed);
}
