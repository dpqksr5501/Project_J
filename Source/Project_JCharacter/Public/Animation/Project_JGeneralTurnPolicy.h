#pragma once

#include "Project_JLocomotionAnimTypes.h"

/** Local candidate lifetime. Never owns CMC yaw, one-shots, or animation time. */
class FProject_JGeneralTurnPolicy
{
public:
	struct FSettings
	{
		float EntrySpeed = 120.0f;
		float CommittedHeadingAngle = 60.0f;
		float StrafeEntryYawRate = 120.0f;
		float AlignmentEntryAngle = 45.0f;
		float AlignmentConfirmation = 0.04f;
		float MinimumWindow = 0.20f;
		float QuietGrace = 0.18f;
		float CompletionGrace = 0.25f;
	};
	struct FInput
	{
		bool bEligible = false;
		EProject_JLocomotionRotationMode Mode = EProject_JLocomotionRotationMode::OrientToMovement;
		double Now = 0;
		float MoveYaw = 0, FacingYaw = 0, ActorYaw = 0, VelocityYaw = 0, Speed = 0;
		// A fresh, relevant MM result from this same owner/family. Initial Cycle
		// competition must not close a window until GeneralTurn actually won.
		uint64 SelectionFrame = 0;
		bool bSelectedGeneralTurn = false, bSelectedCycle = false;
	};
	void Reset() { *this = FProject_JGeneralTurnPolicy(); }
	bool Update(const FInput& I, FSettings S)
	{
		S.EntrySpeed = Safe(S.EntrySpeed, 120, 50, 500);
		S.CommittedHeadingAngle = Safe(S.CommittedHeadingAngle, 60, 45, 90);
		S.StrafeEntryYawRate = Safe(S.StrafeEntryYawRate, 120, 60, 360);
		S.AlignmentEntryAngle = Safe(S.AlignmentEntryAngle, 45, 30, 75);
		S.AlignmentConfirmation = Safe(S.AlignmentConfirmation, .04f, .02f, .15f);
		S.MinimumWindow = Safe(S.MinimumWindow, .20f, .1f, .5f);
		S.QuietGrace = Safe(S.QuietGrace, .18f, .1f, .4f);
		S.CompletionGrace = Safe(S.CompletionGrace, .25f, .1f, .5f);
		const bool bValid = I.bEligible && FMath::IsFinite(I.Now) && FMath::IsFinite(I.MoveYaw) &&
			FMath::IsFinite(I.FacingYaw) && FMath::IsFinite(I.ActorYaw) && FMath::IsFinite(I.VelocityYaw) &&
			FMath::IsFinite(I.Speed) && I.Speed >= 0;
		if (!bValid || (bHasSample && (I.Mode != Mode || I.Now < LastNow || I.Now - LastNow > .1)))
		{
			Reset(); Reason = TEXT("OwnerOrSampleChanged"); return false;
		}
		PathError = Angle(I.VelocityYaw, I.MoveYaw);
		FacingError = Angle(I.ActorYaw, I.FacingYaw);
		if (!bHasSample)
		{
			bHasSample = true; LastYaw = I.MoveYaw; LastFacingYaw = I.FacingYaw; LastNow = I.Now; Mode = I.Mode;
			Add(I.Now, 0, 0);
		}
		const float Step = FMath::FindDeltaAngleDegrees(LastYaw, I.MoveYaw);
		// Large jumps belong to the existing reversal/pivot policy; do not build a
		// long-lived ordinary-turn event from a teleport or a fresh reverse target.
		if (FMath::Abs(Step) > 75)
		{
			Reset(); Reason = TEXT("AbruptTarget"); return false;
		}
		UnwrappedYaw += Step; LastYaw = I.MoveYaw;
		UnwrappedFacing += FMath::FindDeltaAngleDegrees(LastFacingYaw, I.FacingYaw);
		LastFacingYaw = I.FacingYaw; LastNow = I.Now;
		if (I.Now - Samples[LastIndex].Time >= .02) Add(I.Now, UnwrappedYaw, UnwrappedFacing);
		const FSample Long = HistoryAt(I.Now - .35);
		const FSample Short = HistoryAt(I.Now - .12);
		RecentHeading = UnwrappedYaw - Long.Yaw;
		const double Span = I.Now - Short.Time;
		MoveYawRate = Span > UE_SMALL_NUMBER ? float((UnwrappedYaw - Short.Yaw) / Span) : 0;
		FacingYawRate = Span > UE_SMALL_NUMBER ? float((UnwrappedFacing - Short.Facing) / Span) : 0;
		const bool bStrafe = I.Mode == EProject_JLocomotionRotationMode::Strafe;
		const bool bRateReady = Span >= .08;
		// Camera-only strafe rotation changes travel relative to the body. It must
		// search directional Cycles, not borrow a forward authored root turn.
		const bool bCoupled = bRateReady && FMath::Abs(FacingYawRate) >= S.StrafeEntryYawRate && MoveYawRate * FacingYawRate > 0;
		const bool bAlignmentDemand = PathError >= S.AlignmentEntryAngle || (!bStrafe && FacingError >= S.AlignmentEntryAngle);
		// A swept world heading is a curve, regardless of its accumulated angle
		// or mouse rate. Turn competition requires real physical correction.
		// Strafe additionally requires coordinated travel/camera rotation.
		const bool bDemand = bAlignmentDemand && (!bStrafe || bCoupled);
		Demand = bDemand ? (PathError >= S.AlignmentEntryAngle ? TEXT("PathAlignment") : TEXT("FacingAlignment")) : TEXT("None");
		// Keep the rich Cycle searchable during even gentle curves. Settling is
		// independent of forward-turn eligibility and has its own quiet hysteresis.
		if ((bRateReady && (FMath::Abs(MoveYawRate) >= 8 || (bStrafe && FMath::Abs(FacingYawRate) >= 8))) ||
			PathError > 15 || FacingError > 12) DynamicUntil = I.Now + S.QuietGrace;
		bDynamicCycle = I.Now < DynamicUntil;
		// The general data contains forward turns only; retain directional Cycles
		// for lateral/backward Strafe, including while the camera rotates.
		if ((bStrafe && (Angle(I.MoveYaw, I.FacingYaw) > 45 ||
			(I.Speed > 50 && Angle(I.VelocityYaw, I.ActorYaw) > 75))) || PathError >= 135)
		{
			if (bActive) Close(I.Now, false);
			DemandSince = -1; bContinuation = false; Reason = TEXT("TravelNotForward"); return false;
		}
		// Recovery is physical alignment, not a requirement to stop rotating.
		// Entry/exit hysteresis and quiet grace keep brief fluctuations stable.
		const bool bQuiet = PathError <= 15 && FacingError <= 12;
		const bool bFreshSelection = I.SelectionFrame > LastSelectionFrame;
		if (bFreshSelection) LastSelectionFrame = I.SelectionFrame;
		if (bActive && bFreshSelection && I.bSelectedGeneralTurn) bTurnUsed = true;
		if (((bActive && bTurnUsed) || bContinuation) && bFreshSelection && I.bSelectedCycle)
		{
			Close(I.Now, false); LatchCycleHandoff(I);
			Reason = TEXT("CycleHandoff"); return false;
		}
		if (bContinuation && (I.Now >= ContinueUntil || bDemand || Angle(CompletedYaw, I.MoveYaw) > 20 ||
			Angle(CompletedFacingYaw, I.FacingYaw) > 20)) bContinuation = false;
		if (bCycleHandoff)
		{
			if (!bQuiet) HandoffQuietSince = -1;
			else if (HandoffQuietSince < 0) HandoffQuietSince = I.Now;
			// Use an event-relative new target; the previous 0.35 s history alone
			// cannot reopen the old event after its Cycle has already won.
			const float NewHeading = Angle(HandoffYaw, I.MoveYaw);
			const bool bReversed = HandoffDirection != 0 && MoveYawRate * HandoffDirection < 0;
			const bool bNewIntent = bDemand && bReversed && NewHeading >= S.CommittedHeadingAngle;
			if (bNewIntent || (HandoffQuietSince >= 0 && I.Now - HandoffQuietSince >= S.QuietGrace)) bCycleHandoff = false;
			else { DemandSince = -1; Reason = TEXT("CycleHandoffHold"); return false; }
		}
		if (bActive)
		{
			if (FMath::Abs(RecentHeading) >= 10 && Sign != 0 && FMath::Sign(RecentHeading) != Sign)
			{
				Close(I.Now, false); Reason = TEXT("DirectionReversed"); return false;
			}
			if (I.Now - Started >= 1.5)
			{
				Close(I.Now, false); LatchCycleHandoff(I); Reason = TEXT("Timeout"); return false;
			}
			if (!bQuiet) QuietSince = -1;
			else if (QuietSince < 0) QuietSince = I.Now;
			if (I.Now - Started >= S.MinimumWindow && QuietSince >= 0 && I.Now - QuietSince >= S.QuietGrace)
			{
				CompletedYaw = I.MoveYaw; CompletedFacingYaw = I.FacingYaw;
				ContinueUntil = I.Now + S.CompletionGrace;
				Close(I.Now, true); LatchCycleHandoff(I); Reason = TEXT("Settled"); return false;
			}
			Reason = bQuiet ? TEXT("QuietGrace") : TEXT("Active"); return true;
		}
		if (I.Now < NextEntry) { DemandSince = -1; Reason = TEXT("Cooldown"); return false; }
		if (I.Speed < S.EntrySpeed) { DemandSince = -1; Reason = TEXT("EntrySpeed"); return false; }
		if (!bDemand) { DemandSince = -1; Reason = bDynamicCycle ? TEXT("ContinuousCurve") : TEXT("BelowEntry"); return false; }
		const float CorrectionDirection = FMath::Sign(PathError >= S.AlignmentEntryAngle ?
			FMath::FindDeltaAngleDegrees(I.VelocityYaw, I.MoveYaw) : FMath::FindDeltaAngleDegrees(I.ActorYaw, I.FacingYaw));
		if (DemandSince < 0 || CorrectionDirection != PendingDirection)
		{
			DemandSince = I.Now; PendingDirection = CorrectionDirection;
		}
		if (I.Now - DemandSince + UE_SMALL_NUMBER < S.AlignmentConfirmation)
		{
			Reason = TEXT("AlignmentPending"); return false;
		}
		bActive = true; bContinuation = false; bTurnUsed = false; Started = I.Now; QuietSince = -1;
		Sign = FMath::Abs(RecentHeading) >= 5 ? FMath::Sign(RecentHeading) :
			FMath::Sign(FMath::FindDeltaAngleDegrees(I.VelocityYaw, I.MoveYaw));
		Reason = TEXT("Enter"); return true;
	}
	bool AllowsContinuation() const { return bContinuation; }
	float GetRecentHeading() const { return RecentHeading; }
	float GetMoveYawRate() const { return MoveYawRate; }
	float GetFacingYawRate() const { return FacingYawRate; }
	float GetPathError() const { return PathError; }
	float GetFacingError() const { return FacingError; }
	bool RequiresDynamicCycle() const { return bDynamicCycle; }
	bool IsCycleHandoffHeld() const { return bCycleHandoff; }
	const TCHAR* GetDemand() const { return Demand; }
	float GetElapsed() const { return bActive ? float(LastNow - Started) : 0; }
	const TCHAR* GetReason() const { return Reason; }
private:
	static float Safe(float V, float F, float Lo, float Hi) { return FMath::IsFinite(V) ? FMath::Clamp(V, Lo, Hi) : F; }
	static float Angle(float A, float B) { return FMath::Abs(FMath::FindDeltaAngleDegrees(A, B)); }
	void Close(double Now, bool bCompleted) { bActive = false; bContinuation = bCompleted; DemandSince = -1; NextEntry = Now + .1; }
	void LatchCycleHandoff(const FInput& I) { bCycleHandoff = true; HandoffYaw = I.MoveYaw; HandoffDirection = Sign; HandoffQuietSince = -1; }
	struct FSample { double Time = 0; float Yaw = 0, Facing = 0; };
	FSample HistoryAt(double Cutoff) const
	{
		const FSample* Before = nullptr; const FSample* After = nullptr;
		for (int32 N = 0; N < Count; ++N)
		{
			const auto& Sample = Samples[N];
			if (Sample.Time <= Cutoff && (!Before || Sample.Time > Before->Time)) Before = &Sample;
			if (Sample.Time >= Cutoff && (!After || Sample.Time < After->Time)) After = &Sample;
		}
		if (Before && After && After->Time > Before->Time)
		{
			const float Alpha = float((Cutoff - Before->Time) / (After->Time - Before->Time));
			return {Cutoff, FMath::Lerp(Before->Yaw, After->Yaw, Alpha), FMath::Lerp(Before->Facing, After->Facing, Alpha)};
		}
		return Before ? *Before : After ? *After : FSample{};
	}
	void Add(double T, float Y, float F)
	{
		LastIndex = NextIndex; Samples[NextIndex] = {T, Y, F}; NextIndex = (NextIndex + 1) % UE_ARRAY_COUNT(Samples);
		Count = FMath::Min(Count + 1, int32(UE_ARRAY_COUNT(Samples)));
	}
	FSample Samples[32];
	int32 Count = 0, NextIndex = 0, LastIndex = 0;
	double LastNow = 0, Started = 0, QuietSince = -1, NextEntry = 0, ContinueUntil = 0, DynamicUntil = 0;
	double HandoffQuietSince = -1;
	double DemandSince = -1;
	uint64 LastSelectionFrame = 0;
	float LastYaw = 0, LastFacingYaw = 0, UnwrappedYaw = 0, UnwrappedFacing = 0, RecentHeading = 0;
	float MoveYawRate = 0, FacingYawRate = 0, PathError = 0, FacingError = 0, Sign = 0, CompletedYaw = 0, CompletedFacingYaw = 0;
	float HandoffYaw = 0, HandoffDirection = 0;
	float PendingDirection = 0;
	EProject_JLocomotionRotationMode Mode = EProject_JLocomotionRotationMode::OrientToMovement;
	bool bHasSample = false, bActive = false, bContinuation = false, bDynamicCycle = false;
	bool bTurnUsed = false, bCycleHandoff = false;
	const TCHAR* Reason = TEXT("Inactive");
	const TCHAR* Demand = TEXT("None");
};
