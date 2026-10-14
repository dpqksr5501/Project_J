#pragma once

#include "BoneControllers/AnimNode_FootPlacement.h"

namespace ProjectJ::Animation
{
/** Shared native fallback and DA constructor defaults; authored overrides stay intact. */
inline void InitializeFootPlacementDefaults(FFootPlacementPlantSettings& Default,
	FFootPlacementPlantSettings& Stops, FFootPlacementInterpolationSettings& StopInterpolation)
{
	Default.DistanceToGround = 10.0f;
	Default.MaxExtensionRatio = 0.95f;
	Default.MinExtensionRatio = 0.1f;
	Default.AnkleTwistReduction = 0.75f;
	Stops.DistanceToGround = 10.0f;
	Stops.MaxExtensionRatio = 0.95f;
	Stops.MinExtensionRatio = 0.1f;
	Stops.AnkleTwistReduction = 0.75f;
	Stops.SpeedThreshold = 80.0f;
	Stops.UnplantRadius = 25.0f;
	Stops.UnplantAngle = 35.0f;
	Stops.ReplantRadiusRatio = 0.5f;
	Stops.ReplantAngleRatio = 0.65f;
	StopInterpolation.UnplantLinearStiffness = 500.0f;
	StopInterpolation.UnplantAngularStiffness = 700.0f;
	StopInterpolation.FloorLinearStiffness = 1200.0f;
	StopInterpolation.FloorAngularStiffness = 650.0f;
}
}
