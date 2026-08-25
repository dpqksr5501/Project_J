// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JMotionMatchingAssetSet.h"

#include "PoseSearch/PoseSearchDatabase.h"

namespace
{
UPoseSearchDatabase* SelectGaitDatabase(
	const FProject_JMotionMatchingGaitDatabaseFamily& DatabaseFamily,
	EProject_JLocomotionPhaseFamily PhaseFamily)
{
	switch (PhaseFamily)
	{
	case EProject_JLocomotionPhaseFamily::Turn:
		return DatabaseFamily.TurnRedirect.Get();
	case EProject_JLocomotionPhaseFamily::Cycle:
		return DatabaseFamily.Cycle.Get();
	default:
		return nullptr;
	}
}

void ValidateDatabaseSlot(
	const UProject_JMotionMatchingAssetSet* AssetSet,
	const UObject* ValidationContext,
	TArray<FString>& OutWarnings,
	const TCHAR* SlotName,
	const UPoseSearchDatabase* Database)
{
	if (Database)
	{
		return;
	}

	OutWarnings.Add(FString::Printf(
		TEXT("%s uses MotionMatchingAssetSet %s, but %s is missing."),
		*GetNameSafe(ValidationContext),
		*GetNameSafe(AssetSet),
		SlotName));
}

void ValidateGaitDatabaseFamily(
	const UProject_JMotionMatchingAssetSet* AssetSet,
	const UObject* ValidationContext,
	TArray<FString>& OutWarnings,
	const TCHAR* FamilyName,
	const FProject_JMotionMatchingGaitDatabaseFamily& DatabaseFamily)
{
	ValidateDatabaseSlot(AssetSet, ValidationContext, OutWarnings, *FString::Printf(TEXT("%s.Cycle"), FamilyName), DatabaseFamily.Cycle.Get());
	ValidateDatabaseSlot(AssetSet, ValidationContext, OutWarnings, *FString::Printf(TEXT("%s.TurnRedirect"), FamilyName), DatabaseFamily.TurnRedirect.Get());
}
}

UPoseSearchDatabase* UProject_JMotionMatchingAssetSet::FindDatabaseForContext(const FProject_JMotionMatchingSelectionContext& Context) const
{
	const EProject_JLocomotionGaitIntent GaitIntent = Context.GaitIntent;
	const EProject_JLocomotionRotationMode RotationMode = Context.RotationMode;
	const EProject_JLocomotionPhaseFamily PhaseFamily = Context.PhaseFamily;
	if (PhaseFamily == EProject_JLocomotionPhaseFamily::Idle)
	{
		if (IdlePoseSearchDatabase)
		{
			return IdlePoseSearchDatabase.Get();
		}
	}

	if (RotationMode == EProject_JLocomotionRotationMode::OrientToMovement ||
		Context.bUseGenericFamiliesForNonOrientToMovement)
	{
		const FProject_JMotionMatchingGaitDatabaseFamily& GaitFamily =
			GaitIntent == EProject_JLocomotionGaitIntent::Sprint ? SprintDatabases : RunDatabases;
		if (UPoseSearchDatabase* GaitDatabase = SelectGaitDatabase(GaitFamily, PhaseFamily))
		{
			return GaitDatabase;
		}
	}

	return nullptr;
}

bool UProject_JMotionMatchingAssetSet::ValidateForProjectJLocomotion(
	const UObject* ValidationContext,
	TArray<FString>& OutWarnings) const
{
	const int32 InitialWarningCount = OutWarnings.Num();

	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("DefaultPoseSearchDatabase"), DefaultPoseSearchDatabase.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("IdlePoseSearchDatabase"), IdlePoseSearchDatabase.Get());
	ValidateGaitDatabaseFamily(this, ValidationContext, OutWarnings, TEXT("RunDatabases"), RunDatabases);
	ValidateGaitDatabaseFamily(this, ValidationContext, OutWarnings, TEXT("SprintDatabases"), SprintDatabases);
	return OutWarnings.Num() == InitialWarningCount;
}

bool UProject_JMotionMatchingAssetSet::ValidateCombatStrafeForProjectJLocomotion(
	const UObject* ValidationContext,
	TArray<FString>& OutWarnings) const
{
	const int32 InitialWarningCount = OutWarnings.Num();
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("Combat.DefaultPoseSearchDatabase"), DefaultPoseSearchDatabase.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("Combat.IdlePoseSearchDatabase"), IdlePoseSearchDatabase.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("Combat.RunDatabases.Cycle"), RunDatabases.Cycle.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("Combat.SprintDatabases.Cycle"), SprintDatabases.Cycle.Get());
	return OutWarnings.Num() == InitialWarningCount;
}
