#pragma once
#include "Animation/Project_JAnimationBudgetTypes.h"

namespace ProjectJ::Animation
{
// Gather on GT using the existing role/visibility owners. Pure decisions below
// never look up the worker proxy, tick a mesh, or alter ABA registration.
struct FQualityObservation
{
	bool bHasOwner = false, bLocal = false, bGameplayPose = false, bRecentlyRendered = false, bHasSignificance = false;
	float Significance = 0.f;
};
FProject_JAnimOptimizationPolicy ResolveSourceQuality(const FQualityObservation& Input,
	const FProject_JAnimationBudgetSettings& Settings);
float ResolveVisualInterval(bool bProtected, bool bHidden, float Significance, bool bSourceThrottled);
}
