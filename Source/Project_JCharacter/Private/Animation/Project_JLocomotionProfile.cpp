// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JLocomotionProfile.h"

bool FProject_JMotionMatchingSearchPolicy::ShouldSearchEveryUpdate(
	EProject_JLocomotionPhaseFamily PhaseFamily,
	bool bIsFallOffStart) const
{
	switch (PhaseFamily)
	{
	case EProject_JLocomotionPhaseFamily::JumpStart:
		return bSearchJumpStartEveryUpdate;

	case EProject_JLocomotionPhaseFamily::Fall:
		return bIsFallOffStart
			? bSearchFallOffEveryUpdate
			: bSearchAirborneLoopEveryUpdate;

	case EProject_JLocomotionPhaseFamily::Landing:
		return bSearchLandingEveryUpdate;

	default:
		return true;
	}
}

float FProject_JMotionMatchingSearchPolicy::ResolveSearchThrottleTime(
	EProject_JLocomotionPhaseFamily PhaseFamily,
	bool bIsFallOffStart,
	float DefaultSearchThrottleTime,
	bool bDatabaseChanged) const
{
	return ShouldSearchEveryUpdate(PhaseFamily, bIsFallOffStart) || bDatabaseChanged
		? FMath::Max(0.0f, DefaultSearchThrottleTime)
		: FMath::Max(0.0f, SuppressedSearchThrottleTime);
}

UProject_JLocomotionProfile::UProject_JLocomotionProfile()
{
	FootPlacementPlantSettingsStops.SpeedThreshold = 80.0f;
	FootPlacementPlantSettingsStops.UnplantRadius = 25.0f;
	FootPlacementPlantSettingsStops.UnplantAngle = 35.0f;
	FootPlacementPlantSettingsStops.ReplantRadiusRatio = 0.5f;
	FootPlacementPlantSettingsStops.ReplantAngleRatio = 0.65f;

	FootPlacementInterpolationSettingsStops.UnplantLinearStiffness = 500.0f;
	FootPlacementInterpolationSettingsStops.UnplantAngularStiffness = 700.0f;
	FootPlacementInterpolationSettingsStops.FloorLinearStiffness = 1200.0f;
	FootPlacementInterpolationSettingsStops.FloorAngularStiffness = 650.0f;
}

FProject_JAnimationBudgetSettings UProject_JLocomotionProfile::GetResolvedAnimationBudgetSettings() const
{
	FProject_JAnimationBudgetSettings ResolvedSettings = AnimationBudget;
	ResolvedSettings.NearDistance = NearMotionMatchingDistance;
	ResolvedSettings.MidDistance = MidMotionMatchingDistance;
	ResolvedSettings.FarDistance = FarMotionMatchingDistance;
	ResolvedSettings.MidUpdateInterval = MidMotionMatchingUpdateInterval;
	ResolvedSettings.FarUpdateInterval = FarMotionMatchingUpdateInterval;
	ResolvedSettings.HiddenUpdateInterval = AnimInstanceHiddenRemoteUpdateInterval;
	ResolvedSettings.bDisableMotionMatchingBeyondFarDistance = bDisableMotionMatchingBeyondFarDistance;
	return ResolvedSettings;
}
