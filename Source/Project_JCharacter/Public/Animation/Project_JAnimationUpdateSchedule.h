#pragma once
#include "CoreMinimal.h"

/** Value-only deadline schedule. Critical state changes run immediately; periodic work keeps a per-owner phase. */
struct FProjectJAnimationUpdateSchedule
{
	double Elapsed = 0, NextDue = 0;
	float PreviousInterval = 0;
	uint32 PreviousOwner = 0;
	bool bInitialized = false;
	bool Advance(float Delta, float Interval, uint32 Owner, bool bForce)
	{
		Elapsed += FMath::IsFinite(Delta) ? FMath::Max(0.f, Delta) : 0.f;
		if (!FMath::IsFinite(Interval) || Interval <= 0) { bInitialized = false; return true; }
		if (!bInitialized || PreviousOwner != Owner || PreviousInterval != Interval || bForce || Elapsed >= NextDue)
		{
			const double Phase = FMath::Frac(double(Owner) * 0.6180339887498949);
			NextDue = (FMath::FloorToDouble(Elapsed / Interval - Phase) + 1 + Phase) * Interval;
			PreviousInterval = Interval; PreviousOwner = Owner; bInitialized = true; return true;
		}
		return false;
	}
};
