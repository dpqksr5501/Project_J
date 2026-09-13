#pragma once

#include "Animation/TrajectoryTypes.h"

namespace Project_J::Animation
{
// Value-only query: no UObject access, mutable cache, allocation, or thread ownership.
// History sample times can change without a change in array length.
inline bool TryGetFuturePlanarVelocity(const FTransformTrajectory& Trajectory, float Horizon,
	const FVector& CurrentVelocity, FVector& OutVelocity, float& OutTurnAngle)
{
	OutVelocity = FVector::ZeroVector;
	OutTurnAngle = 0.0f;
	if (!FMath::IsFinite(Horizon) || CurrentVelocity.ContainsNaN()) return false;
	const float SafeHorizon = FMath::Max(0.0f, Horizon);
	int32 Present = INDEX_NONE, Future = INDEX_NONE;
	float NearestPresent = MAX_flt, NearestFuture = MAX_flt;
	for (int32 Index = 0; Index < Trajectory.Samples.Num(); ++Index)
	{
		const float Time = Trajectory.Samples[Index].TimeInSeconds;
		if (!FMath::IsFinite(Time)) return false;
		if (FMath::Abs(Time) < NearestPresent)
		{
			Present = Index;
			NearestPresent = FMath::Abs(Time);
		}
		if (Time > UE_KINDA_SMALL_NUMBER && FMath::Abs(Time - SafeHorizon) < NearestFuture)
		{
			Future = Index;
			NearestFuture = FMath::Abs(Time - SafeHorizon);
		}
	}
	if (Present == INDEX_NONE || Future == INDEX_NONE) return false;
	const float Delta = Trajectory.Samples[Future].TimeInSeconds - Trajectory.Samples[Present].TimeInSeconds;
	if (!FMath::IsFinite(Delta) || Delta <= UE_KINDA_SMALL_NUMBER) return false;
	FVector Velocity = (Trajectory.Samples[Future].GetTransform().GetLocation() -
		Trajectory.Samples[Present].GetTransform().GetLocation()) / Delta;
	Velocity.Z = 0.0;
	if (Velocity.ContainsNaN()) return false;
	const FVector CurrentDirection = CurrentVelocity.GetSafeNormal2D();
	if (!CurrentDirection.IsNearlyZero() && !Velocity.IsNearlyZero())
	{
		OutTurnAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(CurrentDirection, Velocity.GetSafeNormal2D()), -1.0, 1.0)));
	}
	OutVelocity = Velocity;
	return true;
}
}
