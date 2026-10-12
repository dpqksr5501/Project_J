#pragma once
#include "Animation/Project_JContinuousCurvePolicy.h"

/** Compatible authored exits use the same evidence rules as regular Strafe routing. */
class FProject_JOneShotCurvePolicy
{
public:
	struct FSettings
	{
		float TravelDistance = 100, HeadingSweep = 20, DirectionConsistency = .85f;
		FProject_JContinuousCurveSettings Sustained;
	};
	struct FInput
	{
		bool bEligible = false, bCanExit = false, bHasRequestedHeading = false;
		int32 OwnerRevision = 0, Mode = 0, Gait = 0, TrajectoryRevision = 0;
		double Now = 0;
		FVector Position = FVector::ZeroVector, Velocity = FVector::ZeroVector, FutureVelocity = FVector::ZeroVector;
		float RequestedHeading = 0;
	};
	struct FResult { bool bYield = false; float Distance = 0, Sweep = 0, Consistency = 0; };
	void Reset() { Evidence.Reset(); }
	FResult Update(const FInput& I, FSettings S)
	{
		if (I.FutureVelocity.ContainsNaN() || I.FutureVelocity.SizeSquared2D() <= UE_SMALL_NUMBER)
		{ Reset(); return {}; }
		FProject_JContinuousCurvePolicy::FInput Sample;
		Sample.bEligible = I.bEligible; Sample.bHasRequestedHeading = I.bHasRequestedHeading;
		Sample.OwnerRevision = I.OwnerRevision; Sample.HistoryRevision = I.TrajectoryRevision;
		Sample.Mode = I.Mode; Sample.Gait = I.Gait; Sample.Now = I.Now;
		Sample.Position = I.Position; Sample.Velocity = I.Velocity; Sample.RequestedHeading = I.RequestedHeading;
		const auto R = Evidence.Update(Sample, S.Sustained);
		const float FutureStep = FMath::FindDeltaAngleDegrees(I.Velocity.Rotation().Yaw, I.FutureVelocity.Rotation().Yaw);
		if (FMath::Abs(R.Sweep) > UE_SMALL_NUMBER && FMath::Abs(FutureStep) > 1 && FutureStep * R.Sweep < 0) { Reset(); return {}; }
		const auto Safe = [](float V, float F, float Lo, float Hi) { return FMath::IsFinite(V) ? FMath::Clamp(V, Lo, Hi) : F; };
		// Preserve the authored fast-curve path as well as the new sustained path.
		const bool bFast = R.bRecent && R.Distance >= Safe(S.TravelDistance, 100, 10, 1000) &&
			FMath::Abs(R.Sweep) >= Safe(S.HeadingSweep, 20, 5, 90) &&
			R.Consistency >= Safe(S.DirectionConsistency, .85f, .5f, 1);
		return {I.bCanExit && (R.bActive || bFast), R.Distance, R.Sweep, R.Consistency};
	}
private:
	FProject_JContinuousCurvePolicy Evidence;
};
