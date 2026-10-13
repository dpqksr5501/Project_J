#include "Animation/Project_JAnimationQualityPolicy.h"

namespace ProjectJ::Animation
{
FProject_JAnimOptimizationPolicy ResolveSourceQuality(const FQualityObservation& I,
	const FProject_JAnimationBudgetSettings& S)
{
	FProject_JAnimOptimizationPolicy P;
	if (!I.bHasOwner || (!I.bLocal && !I.bGameplayPose && !I.bRecentlyRendered))
	{
		P.Tier = EProject_JAnimBudgetTier::Hidden;
		P.bUpdateAnimationData = P.bUseFullChooserRows = P.bUseFarChooserRowsOnly = false;
		P.bEnableHandIK = P.bEnableFootIK = P.bEnableRetargetIK = P.bEnableFollowerRetarget = false;
		if (I.bHasOwner)
		{
			P.RetargetIKLODThreshold = P.RetargetLODThreshold = 0;
			P.MotionMatchingUpdateInterval = S.HiddenUpdateInterval;
		}
		return P;
	}
	if (I.bLocal) { P.RetargetLODThreshold = -1; return P; }
	P.Tier = EProject_JAnimBudgetTier::Near;
	if (I.bGameplayPose) return P;
	if (!I.bHasSignificance || I.Significance <= 0.f)
	{
		P.RetargetIKLODThreshold = 1; P.RetargetLODThreshold = -1;
		return P;
	}
	P.bEnableHandIK = P.bEnableRetargetIK = false;
	if (I.Significance <= 1.f)
	{
		P.Tier = EProject_JAnimBudgetTier::Mid;
		P.MotionMatchingUpdateInterval = S.MidUpdateInterval;
		P.RetargetLODThreshold = -1;
		return P;
	}
	P.Tier = EProject_JAnimBudgetTier::Far;
	P.bUseFullChooserRows = P.bEnableFootIK = false;
	P.bUseFarChooserRowsOnly = true;
	P.MotionMatchingUpdateInterval = S.FarUpdateInterval;
	P.RetargetLODThreshold = 2;
	return P;
}
float ResolveVisualInterval(bool bProtected, bool bHidden, float Significance, bool bSourceThrottled)
{
	if (bProtected || bHidden) return 0.f;
	return Significance > 1.f ? 1.f / 15 : Significance > 0.f ? 1.f / 30 : bSourceThrottled ? 1.f / 30 : 0.f;
}
}
