// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JMotionMatchingAssetSet.h"

#include "PoseSearch/PoseSearchDatabase.h"

namespace
{
UPoseSearchDatabase* SelectGaitDatabase(
	const FProject_JMotionMatchingGaitDatabaseFamily& DatabaseFamily,
	EProject_JLocomotionPhaseFamily PhaseFamily,
	bool bUseRemoteStart)
{
	switch (PhaseFamily)
	{
	case EProject_JLocomotionPhaseFamily::Start:
		if (bUseRemoteStart && DatabaseFamily.RemoteStart)
		{
			return DatabaseFamily.RemoteStart.Get();
		}
		return DatabaseFamily.Start.Get();
	case EProject_JLocomotionPhaseFamily::Stop:
		return DatabaseFamily.Stop.Get();
	case EProject_JLocomotionPhaseFamily::Pivot:
	case EProject_JLocomotionPhaseFamily::Turn:
	case EProject_JLocomotionPhaseFamily::TurnInPlace:
		return DatabaseFamily.TurnRedirect.Get();
	case EProject_JLocomotionPhaseFamily::Cycle:
		return DatabaseFamily.Cycle.Get();
	default:
		return nullptr;
	}
}

UPoseSearchDatabase* SelectLandingDatabase(
	const FProject_JMotionMatchingLandDatabaseFamily& DatabaseFamily,
	EProject_JLocomotionGaitIntent GaitIntent,
	bool bLandWasMoving,
	bool bLandWasSprinting,
	bool bUseHeavyLand)
{
	if (!bLandWasMoving)
	{
		return bUseHeavyLand
			? (DatabaseFamily.StandHeavy.Get() ? DatabaseFamily.StandHeavy.Get() : DatabaseFamily.Stand.Get())
			: DatabaseFamily.Stand.Get();
	}

	const EProject_JLocomotionGaitIntent LandingGaitIntent = bLandWasSprinting
		? EProject_JLocomotionGaitIntent::Sprint
		: (GaitIntent == EProject_JLocomotionGaitIntent::Walk ? EProject_JLocomotionGaitIntent::Walk : EProject_JLocomotionGaitIntent::Run);

	switch (LandingGaitIntent)
	{
	case EProject_JLocomotionGaitIntent::Sprint:
		if (bUseHeavyLand)
		{
			return DatabaseFamily.SprintHeavy.Get()
				? DatabaseFamily.SprintHeavy.Get()
				: (DatabaseFamily.RunHeavy.Get() ? DatabaseFamily.RunHeavy.Get() : (DatabaseFamily.Sprint.Get() ? DatabaseFamily.Sprint.Get() : DatabaseFamily.Run.Get()));
		}
		return DatabaseFamily.Sprint.Get() ? DatabaseFamily.Sprint.Get() : DatabaseFamily.Run.Get();
	case EProject_JLocomotionGaitIntent::Run:
		if (bUseHeavyLand)
		{
			return DatabaseFamily.RunHeavy.Get() ? DatabaseFamily.RunHeavy.Get() : DatabaseFamily.Run.Get();
		}
		return DatabaseFamily.Run.Get();
	case EProject_JLocomotionGaitIntent::Walk:
	default:
		if (bUseHeavyLand)
		{
			return DatabaseFamily.StandHeavy.Get() ? DatabaseFamily.StandHeavy.Get() : DatabaseFamily.Stand.Get();
		}
		return DatabaseFamily.Stand.Get();
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
	ValidateDatabaseSlot(AssetSet, ValidationContext, OutWarnings, *FString::Printf(TEXT("%s.Start"), FamilyName), DatabaseFamily.Start.Get());
	ValidateDatabaseSlot(AssetSet, ValidationContext, OutWarnings, *FString::Printf(TEXT("%s.RemoteStart"), FamilyName), DatabaseFamily.RemoteStart.Get());
	ValidateDatabaseSlot(AssetSet, ValidationContext, OutWarnings, *FString::Printf(TEXT("%s.Stop"), FamilyName), DatabaseFamily.Stop.Get());
	ValidateDatabaseSlot(AssetSet, ValidationContext, OutWarnings, *FString::Printf(TEXT("%s.TurnRedirect"), FamilyName), DatabaseFamily.TurnRedirect.Get());
}

UPoseSearchDatabase* FindBestDatabaseEntry(
	const TArray<FProject_JMotionMatchingDatabaseEntry>& DatabaseEntries,
	EProject_JLocomotionGaitIntent GaitIntent,
	EProject_JLocomotionRotationMode RotationMode,
	EProject_JLocomotionPhaseFamily PhaseFamily)
{
	const FProject_JMotionMatchingDatabaseEntry* BestEntry = nullptr;
	int32 BestSpecificity = INDEX_NONE;

	for (const FProject_JMotionMatchingDatabaseEntry& Entry : DatabaseEntries)
	{
		if (!Entry.Matches(GaitIntent, RotationMode, PhaseFamily))
		{
			continue;
		}

		const int32 Specificity = Entry.GetSpecificity();
		if (!BestEntry || Specificity > BestSpecificity)
		{
			BestEntry = &Entry;
			BestSpecificity = Specificity;
		}
	}

	return BestEntry ? BestEntry->PoseSearchDatabase.Get() : nullptr;
}
}

bool FProject_JMotionMatchingDatabaseEntry::Matches(
	EProject_JLocomotionGaitIntent InGaitIntent,
	EProject_JLocomotionRotationMode InRotationMode,
	EProject_JLocomotionPhaseFamily InPhaseFamily) const
{
	if (!PoseSearchDatabase)
	{
		return false;
	}

	return
		(!bMatchGaitIntent || GaitIntent == InGaitIntent) &&
		(!bMatchRotationMode || RotationMode == InRotationMode) &&
		(!bMatchPhaseFamily || PhaseFamily == InPhaseFamily);
}

int32 FProject_JMotionMatchingDatabaseEntry::GetSpecificity() const
{
	int32 Specificity = 0;
	Specificity += bMatchGaitIntent ? 1 : 0;
	Specificity += bMatchRotationMode ? 1 : 0;
	Specificity += bMatchPhaseFamily ? 1 : 0;
	return Specificity;
}

UPoseSearchDatabase* UProject_JMotionMatchingAssetSet::FindDatabaseForContext(
	EProject_JLocomotionGaitIntent GaitIntent,
	EProject_JLocomotionRotationMode RotationMode,
	EProject_JLocomotionPhaseFamily PhaseFamily,
	bool bUseHeavyLand,
	bool bLandWasMoving,
	bool bLandWasSprinting,
	bool bUseFallOffStart,
	bool bUseRemoteStart,
	bool bUseGenericFamiliesForNonOrientToMovement) const
{
	// Camera-facing combat must be able to override every phase, including idle.
	// The generic idle database is intentionally evaluated after a Strafe entry.
	if (RotationMode != EProject_JLocomotionRotationMode::OrientToMovement)
	{
		if (UPoseSearchDatabase* RotationModeDatabase = FindBestDatabaseEntry(DatabaseEntries, GaitIntent, RotationMode, PhaseFamily))
		{
			return RotationModeDatabase;
		}
	}

	if (PhaseFamily == EProject_JLocomotionPhaseFamily::Idle)
	{
		if (IdlePoseSearchDatabase)
		{
			return IdlePoseSearchDatabase.Get();
		}
	}

	if (PhaseFamily == EProject_JLocomotionPhaseFamily::JumpStart)
	{
		if (JumpStartDatabase)
		{
			return JumpStartDatabase.Get();
		}
	}

	if (PhaseFamily == EProject_JLocomotionPhaseFamily::JumpStart ||
		PhaseFamily == EProject_JLocomotionPhaseFamily::Fall)
	{
		if (PhaseFamily == EProject_JLocomotionPhaseFamily::Fall && bUseFallOffStart && FallOffStartDatabase)
		{
			return FallOffStartDatabase.Get();
		}

		if (JumpAirDatabase)
		{
			return JumpAirDatabase.Get();
		}
	}

	if (PhaseFamily == EProject_JLocomotionPhaseFamily::Landing)
	{
		if (UPoseSearchDatabase* LandingDatabase = SelectLandingDatabase(LandDatabases, GaitIntent, bLandWasMoving, bLandWasSprinting, bUseHeavyLand))
		{
			return LandingDatabase;
		}
	}

	if (RotationMode == EProject_JLocomotionRotationMode::OrientToMovement ||
		bUseGenericFamiliesForNonOrientToMovement)
	{
		const FProject_JMotionMatchingGaitDatabaseFamily& GaitFamily =
			GaitIntent == EProject_JLocomotionGaitIntent::Sprint ? SprintDatabases : RunDatabases;
		if (UPoseSearchDatabase* GaitDatabase = SelectGaitDatabase(GaitFamily, PhaseFamily, bUseRemoteStart))
		{
			return GaitDatabase;
		}
	}

	return FindBestDatabaseEntry(DatabaseEntries, GaitIntent, RotationMode, PhaseFamily);
}

bool UProject_JMotionMatchingAssetSet::ValidateForProjectJLocomotion(
	const UObject* ValidationContext,
	TArray<FString>& OutWarnings) const
{
	const int32 InitialWarningCount = OutWarnings.Num();

	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("IdlePoseSearchDatabase"), IdlePoseSearchDatabase.Get());
	ValidateGaitDatabaseFamily(this, ValidationContext, OutWarnings, TEXT("RunDatabases"), RunDatabases);
	ValidateGaitDatabaseFamily(this, ValidationContext, OutWarnings, TEXT("SprintDatabases"), SprintDatabases);
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("JumpStartDatabase"), JumpStartDatabase.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("FallOffStartDatabase"), FallOffStartDatabase.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("JumpAirDatabase"), JumpAirDatabase.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("LandDatabases.Stand"), LandDatabases.Stand.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("LandDatabases.Run"), LandDatabases.Run.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("LandDatabases.Sprint"), LandDatabases.Sprint.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("LandDatabases.StandHeavy"), LandDatabases.StandHeavy.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("LandDatabases.RunHeavy"), LandDatabases.RunHeavy.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("LandDatabases.SprintHeavy"), LandDatabases.SprintHeavy.Get());

	return OutWarnings.Num() == InitialWarningCount;
}

bool UProject_JMotionMatchingAssetSet::ValidateCombatStrafeForProjectJLocomotion(
	const UObject* ValidationContext,
	TArray<FString>& OutWarnings) const
{
	const int32 InitialWarningCount = OutWarnings.Num();
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("Combat.IdlePoseSearchDatabase"), IdlePoseSearchDatabase.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("Combat.RunDatabases.Cycle"), RunDatabases.Cycle.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("Combat.RunDatabases.Start"), RunDatabases.Start.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("Combat.RunDatabases.Stop"), RunDatabases.Stop.Get());
	ValidateDatabaseSlot(this, ValidationContext, OutWarnings, TEXT("Combat.RunDatabases.TurnRedirect"), RunDatabases.TurnRedirect.Get());
	return OutWarnings.Num() == InitialWarningCount;
}
