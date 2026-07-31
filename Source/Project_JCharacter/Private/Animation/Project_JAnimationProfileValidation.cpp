// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JAnimationProfileValidation.h"

#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JPlayerCharacter.h"

namespace
{
void AddWarning(const UObject* Context, TArray<FString>& OutWarnings, const TCHAR* Format)
{
	OutWarnings.Add(FString::Printf(TEXT("%s %s"), *GetNameSafe(Context), Format));
}

void AddWarning(const UObject* Context, TArray<FString>& OutWarnings, const FString& Message)
{
	OutWarnings.Add(FString::Printf(TEXT("%s %s"), *GetNameSafe(Context), *Message));
}

void ValidatePositiveValue(const UObject* Context, TArray<FString>& OutWarnings, const TCHAR* FieldName, float Value)
{
	if (Value <= 0.0f)
	{
		AddWarning(Context, OutWarnings, FString::Printf(TEXT("has non-positive %s %.3f."), FieldName, Value));
	}
}

void ValidateNonNegativeValue(const UObject* Context, TArray<FString>& OutWarnings, const TCHAR* FieldName, float Value)
{
	if (Value < 0.0f)
	{
		AddWarning(Context, OutWarnings, FString::Printf(TEXT("has negative %s %.3f."), FieldName, Value));
	}
}

void ValidateLocomotionProfile(
	const UObject* Context,
	const UProject_JLocomotionProfile* LocomotionProfile,
	TArray<FString>& OutWarnings)
{
	if (!LocomotionProfile)
	{
		return;
	}

	ValidatePositiveValue(Context, OutWarnings, TEXT("WalkSpeed"), LocomotionProfile->WalkSpeed);
	ValidatePositiveValue(Context, OutWarnings, TEXT("SprintSpeed"), LocomotionProfile->SprintSpeed);
	ValidatePositiveValue(Context, OutWarnings, TEXT("WalkRotationRateYaw"), LocomotionProfile->WalkRotationRateYaw);
	ValidatePositiveValue(Context, OutWarnings, TEXT("SprintRotationRateYaw"), LocomotionProfile->SprintRotationRateYaw);
	ValidateNonNegativeValue(Context, OutWarnings, TEXT("GenericMoveInputSpeedThreshold"), LocomotionProfile->GenericMoveInputSpeedThreshold);
	ValidateNonNegativeValue(Context, OutWarnings, TEXT("SprintLocomotionSpeedThreshold"), LocomotionProfile->SprintLocomotionSpeedThreshold);

	if (LocomotionProfile->SprintSpeed < LocomotionProfile->WalkSpeed)
	{
		AddWarning(Context, OutWarnings, TEXT("has SprintSpeed lower than WalkSpeed."));
	}

	if (LocomotionProfile->SprintLocomotionSpeedThreshold > LocomotionProfile->SprintSpeed)
	{
		AddWarning(Context, OutWarnings, TEXT("has SprintLocomotionSpeedThreshold above SprintSpeed."));
	}

	if (LocomotionProfile->MidMotionMatchingDistance < LocomotionProfile->NearMotionMatchingDistance)
	{
		AddWarning(Context, OutWarnings, TEXT("has MidMotionMatchingDistance lower than NearMotionMatchingDistance."));
	}

	if (LocomotionProfile->FarMotionMatchingDistance < LocomotionProfile->MidMotionMatchingDistance)
	{
		AddWarning(Context, OutWarnings, TEXT("has FarMotionMatchingDistance lower than MidMotionMatchingDistance."));
	}
}

void ValidateLocomotionAnimState(
	const UObject* Context,
	const UProject_JLocomotionAnimStateComponent* AnimState,
	TArray<FString>& OutWarnings)
{
	if (!AnimState)
	{
		AddWarning(Context, OutWarnings, TEXT("has no LocomotionAnimStateComponent."));
		return;
	}

}

void ValidateWeaponProfile(
	const UObject* Context,
	const UProject_JWeaponAnimProfile* WeaponProfile,
	TArray<FString>& OutWarnings)
{
	if (!WeaponProfile)
	{
		return;
	}

	if (WeaponProfile->CombatIntroMontage && WeaponProfile->CombatIntroMontagePlayRate <= 0.0f)
	{
		AddWarning(Context, OutWarnings, TEXT("has CombatIntroMontage but non-positive CombatIntroMontagePlayRate."));
	}
	if (WeaponProfile->CombatOutroMontage && WeaponProfile->CombatOutroMontagePlayRate <= 0.0f)
	{
		AddWarning(Context, OutWarnings, TEXT("has CombatOutroMontage but non-positive CombatOutroMontagePlayRate."));
	}

}

void ValidateCombatProfile(
	const UObject* Context,
	const UProject_JCombatAnimProfile* CombatProfile,
	const UProject_JWeaponAnimProfile* WeaponProfile,
	TArray<FString>& OutWarnings)
{
	if (!CombatProfile)
	{
		return;
	}

	if (CombatProfile->bPlayIntroMontageWhenEnteringCombat && (!WeaponProfile || !WeaponProfile->CombatIntroMontage))
	{
		AddWarning(Context, OutWarnings, TEXT("plays combat intro on enter but no effective CombatIntroMontage is assigned."));
	}

	if (CombatProfile->bAllowSprintInCombat &&
		CombatProfile->bRequireForwardInputForSprintInCombat &&
		CombatProfile->CombatSprintForwardInputThreshold <= 0.0f)
	{
		AddWarning(Context, OutWarnings, TEXT("allows combat sprint with a non-positive forward-input threshold; use a small positive value such as 0.1."));
	}
}
}

void Project_J::AnimationProfileValidation::ValidatePlayerAnimationConfiguration(
	const AProject_JPlayerCharacter& PlayerCharacter,
	const UProject_JLocomotionProfile* LocomotionProfile,
	const UProject_JMotionMatchingAssetSet* MotionMatchingAssetSet,
	const UProject_JWeaponAnimProfile* WeaponAnimProfile,
	const UProject_JCombatAnimProfile* CombatAnimProfile,
	const UProject_JLocomotionAnimStateComponent* LocomotionAnimStateComponent,
	TArray<FString>& OutWarnings)
{
	ValidateLocomotionProfile(&PlayerCharacter, LocomotionProfile, OutWarnings);
	ValidateLocomotionAnimState(&PlayerCharacter, LocomotionAnimStateComponent, OutWarnings);
	ValidateWeaponProfile(&PlayerCharacter, WeaponAnimProfile, OutWarnings);
	ValidateCombatProfile(&PlayerCharacter, CombatAnimProfile, WeaponAnimProfile, OutWarnings);

	if (MotionMatchingAssetSet)
	{
		MotionMatchingAssetSet->ValidateForProjectJLocomotion(&PlayerCharacter, OutWarnings);
		if (CombatAnimProfile && CombatAnimProfile->bUseCombatRotationMode)
		{
			const UProject_JMotionMatchingAssetSet* CombatStrafeAssetSet =
				CombatAnimProfile->CombatStrafeMotionMatchingAssetSet
					? CombatAnimProfile->CombatStrafeMotionMatchingAssetSet.Get()
					: MotionMatchingAssetSet;
			CombatStrafeAssetSet->ValidateCombatStrafeForProjectJLocomotion(&PlayerCharacter, OutWarnings);
		}
	}
}
