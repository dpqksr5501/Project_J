#pragma once
#include "CoreMinimal.h"

/** Spatial evidence for handing a direct Start/Land to a curved locomotion Cycle.
 * Samples actual travel, never accumulated camera yaw or a new expiry timer. */
class FProject_JOneShotCurvePolicy
{
public:
	struct FSettings { float TravelDistance = 100, HeadingSweep = 20, DirectionConsistency = .85f; };
	struct FInput
	{
		bool bEligible = false, bCanExit = false;
		int32 OwnerRevision = 0, Mode = 0, Gait = 0, TrajectoryRevision = 0;
		double Now = 0;
		FVector Position = FVector::ZeroVector, Velocity = FVector::ZeroVector, FutureVelocity = FVector::ZeroVector;
	};
	struct FResult { bool bYield = false; float Distance = 0, Sweep = 0, Consistency = 0; };
	void Reset() { *this = FProject_JOneShotCurvePolicy(); }
	FResult Update(const FInput& I, FSettings S)
	{
		if (!I.bEligible || !FMath::IsFinite(I.Now) || I.Position.ContainsNaN() || I.Velocity.ContainsNaN() ||
			I.FutureVelocity.ContainsNaN() || I.Velocity.SizeSquared2D() <= UE_SMALL_NUMBER || I.FutureVelocity.SizeSquared2D() <= UE_SMALL_NUMBER)
		{ Reset(); return {}; }
		if (!bHasSample || I.OwnerRevision != Last.OwnerRevision || I.Mode != Last.Mode || I.Gait != Last.Gait ||
			I.TrajectoryRevision != Last.TrajectoryRevision || I.Now < Last.Now || I.Now - Last.Now > .1)
		{ Reset(); bHasSample = true; Last = I; return {}; }
		const double Dt = I.Now - Last.Now;
		if (Dt <= UE_SMALL_NUMBER) return {}; // At most one spatial sample per update.
		const float Distance = FVector::Dist2D(Last.Position, I.Position);
		const float Step = FMath::FindDeltaAngleDegrees(Last.Velocity.Rotation().Yaw, I.Velocity.Rotation().Yaw);
		const float ExpectedTravel = FMath::Max(Last.Velocity.Size2D(), I.Velocity.Size2D()) * Dt;
		Last = I;
		// A blocked character, correction/teleport, abrupt redirect, or straight
		// travel cannot contribute old distance to a later curve.
		if (Distance <= UE_SMALL_NUMBER || Distance > ExpectedTravel * 2 + 2 || FMath::Abs(Step) > 30 || FMath::Abs(Step) < .01f)
		{ Travel = Sweep = AbsoluteSweep = 0; return {}; }
		Travel += Distance; Sweep += Step; AbsoluteSweep += FMath::Abs(Step);
		const float Consistency = AbsoluteSweep > UE_SMALL_NUMBER ? FMath::Abs(Sweep) / AbsoluteSweep : 0;
		const float FutureStep = FMath::FindDeltaAngleDegrees(I.Velocity.Rotation().Yaw, I.FutureVelocity.Rotation().Yaw);
		// CMC prediction follows current input; it need not invent future mouse
		// input. A straight forecast is compatible, an opposing forecast is not.
		const bool bForecastCompatible = FMath::Abs(FutureStep) <= 1 || FutureStep * Sweep > 0;
		if (!bForecastCompatible) { Travel = Sweep = AbsoluteSweep = 0; return {}; }
		const auto Safe = [](float V, float Fallback, float Min, float Max)
		{ return FMath::IsFinite(V) ? FMath::Clamp(V, Min, Max) : Fallback; };
		return {I.bCanExit && Travel >= Safe(S.TravelDistance, 100, 10, 1000) &&
			FMath::Abs(Sweep) >= Safe(S.HeadingSweep, 20, 5, 90) &&
			Consistency >= Safe(S.DirectionConsistency, .85f, .5f, 1), Travel, Sweep, Consistency};
	}
private:
	bool bHasSample = false;
	FInput Last;
	float Travel = 0, Sweep = 0, AbsoluteSweep = 0;
};
