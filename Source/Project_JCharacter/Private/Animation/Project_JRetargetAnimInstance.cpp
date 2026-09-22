// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstance.h"
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

void UProject_JRetargetAnimInstance::BeginDestroy()
{
	if (CachedPresentationComp.IsValid())
	{
		CachedPresentationComp->UnregisterRetargetAnimInstance(this);
	}
	Super::BeginDestroy();
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

	if (USkeletalMeshComponent* OwningComp = GetOwningComponent())
	{
		OwningComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	}

	// Clear transient snapshots
	bHasValidRightSnapshot = false;
	bHasValidLeftSnapshot = false;
	CachedWeaponComponent.Reset();
	CachedPresentationComp.Reset();

	if (APawn* OwnerPawn = TryGetPawnOwner())
	{
		CachedPresentationComp = OwnerPawn->FindComponentByClass<UProject_JWeaponPresentationComponent>();
		if (CachedPresentationComp.IsValid())
		{
			CachedPresentationComp->RegisterRetargetAnimInstance(this);
		}
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

	// Per-frame snapshot initialization to eliminate stale transforms
	bHasValidRightSnapshot = false;
	bHasValidLeftSnapshot = false;
	TargetRightAlphaSnapshot = 0.0f;
	TargetLeftAlphaSnapshot = 0.0f;

	// Dedicated server early-out: visual IK and retargeting evaluation are client-only concerns.
	if (const UWorld* World = GetWorld())
	{
		if (World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}
	}

	APawn* OwnerPawn = TryGetPawnOwner();
	if (!OwnerPawn)
	{
		return;
	}

	// 1. Synchronize animation quality tier from Leader Mesh policy
	if (const ACharacter* OwnerChar = Cast<ACharacter>(OwnerPawn))
	{
		if (const USkeletalMeshComponent* LeaderMesh = OwnerChar->GetMesh())
		{
			if (const UProject_JCharacterAnimInstance* LeaderAnim = Cast<UProject_JCharacterAnimInstance>(LeaderMesh->GetAnimInstance()))
			{
				const FProject_JAnimOptimizationPolicy& Policy = LeaderAnim->GetCurrentOptimizationPolicy();
				CurrentQualityTier = Policy.Tier;
				bEnableFollowerRetarget = Policy.bEnableFollowerRetarget;
				bTierAllowsHandIK = Policy.bEnableHandIK;
				bTierAllowsFootIK = Policy.bEnableFootIK;
				bTierAllowsRetargetIK = Policy.bEnableRetargetIK;
				RetargetIKLODThreshold = Policy.RetargetIKLODThreshold;
				RetargetLODThreshold = Policy.RetargetLODThreshold;
			}
		}
	}

	// Early out if tier forbids Follower Retarget / Hand IK or if character is off-screen/hidden
	if (!bEnableFollowerRetarget || !bTierAllowsHandIK || CurrentQualityTier == EProject_JAnimBudgetTier::Hidden)
	{
		return;
	}

	// 2. Synchronize combat state deterministically from local gameplay authority / ASC
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
		return;
	}
	SnapshotOwningCompWorldTransform = OwningComp->GetComponentTransform();

	// 3. Check Leader AnimInstance curves (supports authored high-precision Float Curves on attack montages)
	float LeaderLeftCurveValue = -1.0f;
	float LeaderRightCurveValue = -1.0f;
	if (const ACharacter* OwnerChar = Cast<ACharacter>(OwnerPawn))
	{
		if (const USkeletalMeshComponent* LeaderMesh = OwnerChar->GetMesh())
		{
			if (const UAnimInstance* LeaderAnim = LeaderMesh->GetAnimInstance())
			{
				if (!LeftHandIKCurveName.IsNone())
				{
					const float Val = LeaderAnim->GetCurveValue(LeftHandIKCurveName);
					if (Val > UE_KINDA_SMALL_NUMBER)
					{
						LeaderLeftCurveValue = FMath::Clamp(Val, 0.0f, 1.0f);
					}
				}
				if (!RightHandIKCurveName.IsNone())
				{
					const float Val = LeaderAnim->GetCurveValue(RightHandIKCurveName);
					if (Val > UE_KINDA_SMALL_NUMBER)
					{
						LeaderRightCurveValue = FMath::Clamp(Val, 0.0f, 1.0f);
					}
				}
			}
		}
	}

	// 4. Try consuming from UProject_JWeaponPresentationComponent if present
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
			const float PrimaryBaseAlpha = (LeaderRightCurveValue >= 0.0f) ? LeaderRightCurveValue : GripTargets.PrimaryIKAlpha;
			TargetRightAlphaSnapshot = bIsCombatMode
				? (bEnableCombatGripIK ? PrimaryBaseAlpha : 0.0f)
				: PrimaryBaseAlpha;
			bResolvedFromPresentation = true;
		}
		if (GripTargets.bHasSecondaryGrip)
		{
			SnapshotLeftGripWorldTransform = GripTargets.SecondaryGripWorldTransform;
			bHasValidLeftSnapshot = true;
			const float SecondaryBaseAlpha = (LeaderLeftCurveValue >= 0.0f) ? LeaderLeftCurveValue : GripTargets.SecondaryIKAlpha;
			TargetLeftAlphaSnapshot = bIsCombatMode
				? (bEnableCombatGripIK ? SecondaryBaseAlpha : 0.0f)
				: SecondaryBaseAlpha;
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
			// In sheathed state (weapon on back): Right hand alpha is 0.0 unless combat mode is active
			// In combat state (weapon drawn): Right hand aligns to grip if enabled (or driven by curve)
			const float PrimaryFallbackAlpha = (LeaderRightCurveValue >= 0.0f) ? LeaderRightCurveValue : 1.0f;
			TargetRightAlphaSnapshot = bIsCombatMode ? (bEnableCombatGripIK ? PrimaryFallbackAlpha : 0.0f) : 0.0f;
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
			// Secondary hand only follows if driven by curve; otherwise 0.0 in idle/run
			const float SecondaryFallbackAlpha = (LeaderLeftCurveValue >= 0.0f) ? LeaderLeftCurveValue : 0.0f;
			TargetLeftAlphaSnapshot = bIsCombatMode ? (bEnableCombatGripIK ? SecondaryFallbackAlpha : 0.0f) : 0.0f;
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
