#pragma once
#include "CoreMinimal.h"

/** Prediction-only smoothing before yaw-rate clamping. Physical input is untouched. */
class FProject_JPredictionYawRateFilter
{
public:
	void Reset() { *this = {}; }
	float Update(float RawRate, double Now, uint64 Frame, float HalfLife, bool bCanSmooth)
	{
		if (!FMath::IsFinite(RawRate) || !FMath::IsFinite(Now)) { Reset(); return 0; }
		if (!bCanSmooth || !FMath::IsFinite(HalfLife) || HalfLife <= 0 || !bHasSample ||
			Now < LastTime || Now - LastTime > .1)
		{
			bHasSample = true; LastTime = Now; LastFrame = Frame; FilteredRate = RawRate;
			return RawRate;
		}
		// A same-frame trajectory rebuild must not consume another zero-delta
		// controller sample and erase the prediction's first input.
		if (Frame == LastFrame) return FilteredRate;
		const double Dt = Now - LastTime;
		if (Dt <= 0) { Reset(); return RawRate; }
		const float Alpha = 1 - FMath::Exp2(-float(Dt) / FMath::Clamp(HalfLife, .001f, .1f));
		FilteredRate = FMath::Lerp(FilteredRate, RawRate, Alpha);
		LastTime = Now; LastFrame = Frame;
		return FilteredRate;
	}
private:
	bool bHasSample = false;
	float FilteredRate = 0;
	double LastTime = 0;
	uint64 LastFrame = TNumericLimits<uint64>::Max();
};
