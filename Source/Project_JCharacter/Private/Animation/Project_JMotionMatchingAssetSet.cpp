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
	bool bUseRemoteStart) const
{
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

	if (RotationMode == EProject_JLocomotionRotationMode::OrientToMovement)
	{
		const FProject_JMotionMatchingGaitDatabaseFamily& GaitFamily =
			GaitIntent == EProject_JLocomotionGaitIntent::Sprint ? SprintDatabases : RunDatabases;
		if (UPoseSearchDatabase* GaitDatabase = SelectGaitDatabase(GaitFamily, PhaseFamily, bUseRemoteStart))
		{
			return GaitDatabase;
		}
	}

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
