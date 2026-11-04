#pragma once
#include "Animation/Project_JAnimationBudgetTypes.h"
class APawn;

namespace ProjectJ::Animation
{
// Physical control moves to a mount; its local rider remains a protected avatar.
bool IsLocalPlayerAvatar(const APawn* Pawn);
/** Local/mounted/viewed and unresolved transition identities retain cosmetic cloth execution. */
bool IsClothExecutionProtected(const APawn* Pawn);
// Gather on GT using the existing role/visibility owners. Pure decisions below
// never look up the worker proxy, tick a mesh, or alter ABA registration.
struct FQualityObservation
{
	bool bHasOwner = false, bLocal = false, bGameplayPose = false, bRecentlyRendered = false, bHasSignificance = false;
	float Significance = 0.f;
};
FProject_JAnimOptimizationPolicy ResolveSourceQuality(const FQualityObservation& Input,
	const FProject_JAnimationBudgetSettings& Settings);
struct FSecondaryMotionObservation
{
	bool bRouteAllows = true, bEnabled = true, bAssetOptIn = false, bLocal = false, bOptOut = false;
	TOptional<float> DistanceTier;
};
/** Accessory-only RigidBody admission. Never changes source pose, contact controls, visibility or cadence. */
bool ResolveSecondaryMotionQuality(const FSecondaryMotionObservation& Input);
float ResolveVisualInterval(bool bProtected, bool bHidden, float Significance, bool bSourceThrottled);
float ResolveAnimationDistanceTier(double DistanceSquared, const FProject_JAnimationBudgetSettings& Settings);
}
