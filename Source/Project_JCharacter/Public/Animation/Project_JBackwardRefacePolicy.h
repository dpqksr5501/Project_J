#pragma once

#include "CoreMinimal.h"

/** Opens authored backward refacing poses beside Cycle; never owns CMC or clip time. */
class FProject_JBackwardRefacePolicy
{
public:
	struct FInput
	{
		bool bEligible = false, bVisualFresh = false;
		double Now = 0;
		float Speed = 0, FacingYaw = 0, VisualYaw = 0, MoveYaw = 0, VelocityYaw = 0;
		uint64 SelectionFrame = 0;
		bool bSelectedReface = false, bSelectedCycle = false;
	};
	void Reset() { *this = FProject_JBackwardRefacePolicy(); }
	bool Update(const FInput& I)
	{
		if (!I.bEligible || !I.bVisualFresh || !FMath::IsFinite(I.Now) || !FMath::IsFinite(I.Speed) ||
			!FMath::IsFinite(I.FacingYaw) || !FMath::IsFinite(I.VisualYaw) || !FMath::IsFinite(I.MoveYaw) ||
			!FMath::IsFinite(I.VelocityYaw) || I.Speed < 0 ||
			(bSample && (I.Now < LastNow || I.Now - LastNow > .1)))
		{
			Reset(); Reason = TEXT("OwnerOrSampleChanged"); return false;
		}
		FacingError = Angle(I.VisualYaw, I.FacingYaw);
		PathError = I.Speed > 50 ? Angle(I.VelocityYaw, I.MoveYaw) : 0;
		const float Step = bSample ? FMath::FindDeltaAngleDegrees(LastFacing, I.FacingYaw) : 0;
		const double Dt = I.Now - LastNow;
		const float Rate = bSample && Dt > UE_SMALL_NUMBER ? FMath::Abs(Step) / float(Dt) : 0;
		bSample = true; LastNow = I.Now; LastFacing = I.FacingYaw;
		const bool bQuiet = FacingError < 35 && PathError < 35 && Rate < 90;
		if (!bQuiet) QuietSince = -1;
		else if (QuietSince < 0) QuietSince = I.Now;
		const bool bFreshResult = I.SelectionFrame > LastSelection;
		if (bFreshResult) LastSelection = I.SelectionFrame;
		if (bActive && bFreshResult && I.bSelectedReface) bUsed = true;
		if ((bUsed || bContinuation) && bFreshResult && I.bSelectedCycle)
		{
			bActive = bContinuation = bUsed = false; Reason = TEXT("CycleHandoff"); return false;
		}
		if (bContinuation)
		{
			if (I.Now >= ContinueUntil || Angle(CompletedFacing, I.FacingYaw) > 25)
				bContinuation = false;
			Reason = bContinuation ? TEXT("CompletionGrace") : TEXT("ContinuationEnded"); return false;
		}
		if (bActive)
		{
			// A reverse camera correction is a new query, not permission to keep the old pose.
			if (FMath::Abs(Step) > 2 && FMath::Sign(Step) != Direction && Direction != 0)
			{
				bActive = false; Reason = TEXT("DirectionReversed"); return false;
			}
			if (I.Now - Started >= 1.2)
			{
				bActive = false; Reason = TEXT("Timeout"); return false;
			}
			if (I.Now - Started >= .25 && QuietSince >= 0 && I.Now - QuietSince >= .12)
			{
				bActive = false; bContinuation = bUsed; ContinueUntil = I.Now + .35;
				CompletedFacing = I.FacingYaw; Reason = TEXT("Aligned"); return false;
			}
			Reason = TEXT("Active"); return true;
		}
		if (!bArmed)
		{
			if (QuietSince >= 0 && I.Now - QuietSince >= .15) bArmed = true;
			else { Reason = TEXT("AwaitCoherentRun"); return false; }
		}
		if (I.Speed < 150) { Reason = TEXT("EntrySpeed"); return false; }
		// B_180 starts facing against the new backward target and ends in backward Run.
		// Physical path reversal also counts when a bounded visual root cannot lag 130 degrees.
		if (FacingError < 100 && PathError < 120) { Reason = TEXT("BelowEntry"); return false; }
		bActive = true; bArmed = false; bUsed = false; Started = I.Now;
		Direction = FMath::Abs(Step) > .01 ? FMath::Sign(Step) : 0;
		Reason = TEXT("Enter"); return true;
	}
	bool AllowsContinuation() const { return bContinuation; }
	float GetFacingError() const { return FacingError; }
	float GetPathError() const { return PathError; }
	const TCHAR* GetReason() const { return Reason; }
private:
	static float Angle(float A, float B) { return FMath::Abs(FMath::FindDeltaAngleDegrees(A, B)); }
	double LastNow = 0, Started = 0, QuietSince = -1, ContinueUntil = 0;
	float LastFacing = 0, CompletedFacing = 0, Direction = 0, FacingError = 0, PathError = 0;
	uint64 LastSelection = 0;
	bool bSample = false, bActive = false, bArmed = true, bUsed = false, bContinuation = false;
	const TCHAR* Reason = TEXT("Inactive");
};
