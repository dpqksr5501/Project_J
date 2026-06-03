// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JMotionMatchingAssetSet.h"

#include "PoseSearch/PoseSearchDatabase.h"

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
	EProject_JLocomotionPhaseFamily PhaseFamily) const
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
