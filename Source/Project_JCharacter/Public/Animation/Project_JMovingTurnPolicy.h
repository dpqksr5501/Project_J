#pragma once

#include "Project_JLocomotionAnimTypes.h"
#include "Animation/Project_JTurnEventSettings.h"

/** A forward-running correction event, shared by OTM and Strafe.
 * Keeps a qualified running origin through braking. Never owns CMC yaw or clip time. */
class FProject_JMovingTurnPolicy
{
public:
	struct FInput
	{
		bool bEligible = false, bEntryQualified = true;
		EProject_JLocomotionRotationMode RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
		float ActorYaw = 0, TargetFacingYaw = 0, VelocityYaw = 0, MoveYaw = 0;
		float EntryAngle = 150, ExitAngle = 15;
		double NowSeconds = 0;
		bool bHasVisualFacing = false;
		float VisualFacingYaw = 0;
		uint64 SelectionFrame = 0;
		bool bSelectedTurn = false, bSelectedCycle = false;
	};
	enum class EStage : uint8 { Observing, Tracking, Preparing, Active };
	enum class EDemand : uint8 { None, TravelRedirect, FacingRecovery };
	void Reset() { *this = FProject_JMovingTurnPolicy(); }

	bool Update(const FInput& I, const FProject_JTurnEventSettings& Settings = {})
	{
		const auto S = Settings.Resolved();
		const bool bModeChanged = bHasMode && Mode != I.RotationMode;
		Mode = I.RotationMode; bHasMode = true;
		if (bModeChanged) return Cancel(TEXT("ModeChanged"));
		if (!I.bEligible) return Cancel(TEXT("Ineligible"));
		if (!FMath::IsFinite(I.NowSeconds) || !FMath::IsFinite(I.ActorYaw) || !FMath::IsFinite(I.TargetFacingYaw) ||
			!FMath::IsFinite(I.VelocityYaw) || !FMath::IsFinite(I.MoveYaw) ||
			(I.bHasVisualFacing && !FMath::IsFinite(I.VisualFacingYaw))) return Cancel(TEXT("InvalidSample"));
		if (bHasTime && I.NowSeconds < LastNow)
		{
			bHasTime = false; LastSelectionFrame = 0;
			return Cancel(TEXT("ClockReversed"));
		}
		LastNow = I.NowSeconds; bHasTime = true;
		const float Facing = Angle(I.ActorYaw, I.TargetFacingYaw);
		RemainingFacing = I.bHasVisualFacing ? Angle(I.VisualFacingYaw, I.TargetFacingYaw) : Facing;
		PathError = Angle(I.VelocityYaw, I.MoveYaw);
		const float Entry = SafeEntry(I.EntryAngle);
		const float Exit = FMath::Clamp(FMath::IsFinite(I.ExitAngle) ? I.ExitAngle : S.CompletionFacingAngle,
			0.f, FMath::Min(S.ForwardConeAngle, S.MinimumRemainingFacingAngle - 1.f));
		const bool bForwardRequest = Angle(I.MoveYaw, I.TargetFacingYaw) <= S.ForwardConeAngle;
		const bool bForwardTravel = Angle(I.VelocityYaw, I.ActorYaw) <= S.ForwardConeAngle;
		// Small residual visual offsets are steering's normal recovery range.
		// Do not retain a restricted turn pool until root yaw is exactly zero.
		const bool bCorrectionResolved = PathError <= S.SettledPathAngle &&
			(RemainingFacing <= Exit || (Facing <= Exit && RemainingFacing < S.MinimumRemainingFacingAngle));
		const bool bFreshSelection = I.SelectionFrame > LastSelectionFrame;
		if (bFreshSelection) LastSelectionFrame = I.SelectionFrame;
		if (bAllowContinuation && (Facing > S.RearmFacingAngle || !bForwardRequest ||
			Angle(CompletedTargetYaw, I.TargetFacingYaw) > S.ContinuationTargetAngle ||
			(bFreshSelection && I.bSelectedCycle))) bAllowContinuation = false;
		if (!bForwardRequest) return Cancel(TEXT("NewTravelNotForward"));

		if (Stage != EStage::Observing)
		{
			// An unwrapped request records the same correction across +/-180.
			// Small mouse jitter is allowed; a meaningful retreat starts another event.
			RequestedSweep += FMath::FindDeltaAngleDegrees(LastMoveYaw, I.MoveYaw);
			LastMoveYaw = I.MoveYaw;
			if (Direction == 0 && FMath::Abs(RequestedSweep) > UE_SMALL_NUMBER) Direction = FMath::Sign(RequestedSweep);
			const float Progress = RequestedSweep * Direction;
			PeakProgress = FMath::Max(PeakProgress, Progress);
			if (Progress < -S.DirectionRetreatAngle || PeakProgress - Progress > S.DirectionRetreatAngle)
				return Cancel(TEXT("DirectionReversed"));
			if (PeakProgress > S.MaximumRequestedSweep) return Cancel(TEXT("OverRotation"));
			if (Stage == EStage::Active)
			{
				if (I.NowSeconds - StartedAt >= S.ActiveWatchdogSeconds) return Cancel(TEXT("Timeout"));
				if (bFreshSelection && I.bSelectedTurn) bTurnUsed = true;
				if (bTurnUsed && bFreshSelection && I.bSelectedCycle) return Complete(I, false, TEXT("CycleHandoff"));
				if (bCorrectionResolved) return Complete(I, true, TEXT("Aligned"));
				Reason = TEXT("Active"); return true;
			}
			if (I.NowSeconds - PreparedAt >= S.PreparationWatchdogSeconds) return Cancel(TEXT("PreparationTimeout"));
			if (Stage == EStage::Tracking)
			{
				if (PathError >= S.PreparationPathAngle || RemainingFacing >= S.PreparationFacingAngle)
				{
					Stage = EStage::Preparing; PreparedAt = I.NowSeconds; bAllowContinuation = false;
					Demand = PathError >= S.PreparationPathAngle ? EDemand::TravelRedirect : EDemand::FacingRecovery;
				}
				else
				{
					if (!I.bEntryQualified) return Cancel(TEXT("EntryUnqualified"));
					if (PathError <= S.SettledPathAngle && RemainingFacing <= Exit)
						return Complete(I, false, TEXT("CorrectionResolved"));
					Reason = TEXT("Tracking"); return false;
				}
			}
			if (bCorrectionResolved) return Complete(I, false, TEXT("CorrectionResolved"));
			if (Progress >= Entry && RemainingFacing >= S.MinimumRemainingFacingAngle)
				return Enter(I, TEXT("PreparedEnter"));
			Reason = TEXT("Preparing"); return false;
		}

		// Re-arm only from a coherent physical run. Side/backward Strafe never
		// lends its origin to a forward authored turn or a facing recovery.
		if (I.bEntryQualified && Facing <= S.RearmFacingAngle && bForwardTravel && bForwardRequest)
		{
			bArmed = true;
			// A transient velocity excursion opposite to the new request must not
			// move the origin backward and inflate an ordinary 135 into a 180 event.
			const bool bOriginFollowingRequest = !bHasOrigin ||
				FMath::FindDeltaAngleDegrees(OriginTravelYaw, I.MoveYaw) *
				FMath::FindDeltaAngleDegrees(OriginTravelYaw, I.VelocityYaw) >= 0;
			if (bOriginFollowingRequest && (!bHasOrigin || (PathError <= S.SettledPathAngle && RemainingFacing <= Exit)))
			{
				bHasOrigin = true; OriginTravelYaw = I.VelocityYaw;
			}
		}
		// Retain the proven immediate-reversal path; preparation adds admission
		// after its original speed and instantaneous angular conditions diverge.
		if (I.bEntryQualified && bArmed && Facing >= Entry && PathError >= S.ImmediatePathAngle &&
			bForwardTravel && bForwardRequest)
		{
			OriginTravelYaw = I.VelocityYaw; bHasOrigin = true;
			BeginEvent(I, EDemand::TravelRedirect);
			return Enter(I, TEXT("Enter"));
		}
		if (bArmed && bHasOrigin && bForwardRequest && (PathError > S.SettledPathAngle || RemainingFacing > Exit))
		{
			const bool bStrongDemand = PathError >= S.PreparationPathAngle || RemainingFacing >= S.PreparationFacingAngle;
			BeginEvent(I, PathError >= S.PreparationPathAngle ? EDemand::TravelRedirect : EDemand::FacingRecovery, !bStrongDemand);
			if (!bStrongDemand)
			{
				if (!I.bEntryQualified) return Cancel(TEXT("EntryUnqualified"));
				Reason = TEXT("Tracking"); return false;
			}
			if (PeakProgress >= Entry && RemainingFacing >= S.MinimumRemainingFacingAngle) return Enter(I, TEXT("PreparedEnter"));
			Reason = TEXT("Preparing"); return false;
		}
		// Braking can carry an event already being prepared, but an ordinary
		// low-speed/blocked run cannot retain a stale running origin indefinitely.
		if (!I.bEntryQualified) return Cancel(TEXT("EntryUnqualified"));
		else if (!bArmed && Facing > S.RearmFacingAngle) Reason = TEXT("NotArmed");
		else if (Facing < Entry) Reason = TEXT("FacingBelowEntry");
		else if (PathError < S.ImmediatePathAngle) Reason = TEXT("PathBelowEntry");
		else if (!bForwardTravel) Reason = TEXT("OldTravelNotForward");
		else Reason = TEXT("NewTravelNotForward");
		return false;
	}

	bool IsActive() const { return Stage == EStage::Active; }
	bool IsArmed() const { return bArmed; }
	bool IsPreparing() const { return Stage == EStage::Preparing; }
	EStage GetStage() const { return Stage; }
	EDemand GetDemand() const { return Demand; }
	float GetRequestedSweep() const { return RequestedSweep; }
	float GetRemainingFacing() const { return RemainingFacing; }
	const TCHAR* GetLastUpdateReason() const { return Reason; }
	bool AllowsCompletedTurnContinuation() const { return bAllowContinuation; }

	/** Existing general data can hand over while this physical event is preparing.
	 * No fixed grace interval and no permission for fresh general admission. */
	bool AllowsGeneralTurnHandoff(const FInput& I, const FProject_JTurnEventSettings& Settings = {}) const
	{
		const auto S = Settings.Resolved();
		return Stage == EStage::Preparing && I.bEligible && bHasOrigin &&
			Mode == I.RotationMode && FMath::IsFinite(I.MoveYaw) && FMath::IsFinite(I.TargetFacingYaw) &&
			Angle(I.MoveYaw, I.TargetFacingYaw) <= S.ForwardConeAngle &&
			RemainingFacing >= S.MinimumRemainingFacingAngle &&
			PeakProgress >= SafeEntry(I.EntryAngle) - S.SettledPathAngle;
	}
	/** Simulate on a value copy: diagnostics and the producer use identical rules. */
	const TCHAR* DescribeNextUpdate(const FInput& I, const FProject_JTurnEventSettings& S = {}) const
	{
		auto Copy = *this; Copy.Update(I, S); return Copy.Reason;
	}
private:
	static float Angle(float A, float B) { return FMath::Abs(FMath::FindDeltaAngleDegrees(A, B)); }
	static float SafeEntry(float A) { return FMath::IsFinite(A) ? FMath::Clamp(A, 90.f, 180.f) : 150.f; }
	void BeginEvent(const FInput& I, EDemand InDemand, bool bTracking = false)
	{
		Stage = bTracking ? EStage::Tracking : EStage::Preparing;
		Demand = bTracking ? EDemand::None : InDemand; PreparedAt = I.NowSeconds;
		RequestedSweep = FMath::FindDeltaAngleDegrees(OriginTravelYaw, I.MoveYaw);
		Direction = FMath::Sign(RequestedSweep); LastMoveYaw = I.MoveYaw;
		PeakProgress = FMath::Abs(RequestedSweep);
		if (!bTracking) bAllowContinuation = false;
	}
	bool Enter(const FInput& I, const TCHAR* InReason)
	{
		Stage = EStage::Active; StartedAt = I.NowSeconds;
		bArmed = false; bTurnUsed = false; Reason = InReason; return true;
	}
	bool Cancel(const TCHAR* InReason)
	{
		Stage = EStage::Observing; Demand = EDemand::None;
		bArmed = bHasOrigin = bAllowContinuation = bTurnUsed = false;
		RequestedSweep = PeakProgress = 0; Reason = InReason; return false;
	}
	bool Complete(const FInput& I, bool bContinue, const TCHAR* InReason)
	{
		Cancel(InReason); bAllowContinuation = bContinue;
		CompletedTargetYaw = I.TargetFacingYaw;
		return false;
	}
	EStage Stage = EStage::Observing;
	EDemand Demand = EDemand::None;
	EProject_JLocomotionRotationMode Mode = EProject_JLocomotionRotationMode::OrientToMovement;
	bool bHasMode = false, bHasTime = false, bArmed = false, bHasOrigin = false;
	bool bAllowContinuation = false, bTurnUsed = false;
	double LastNow = 0, PreparedAt = 0, StartedAt = 0;
	uint64 LastSelectionFrame = 0;
	float OriginTravelYaw = 0, LastMoveYaw = 0, RequestedSweep = 0, Direction = 0, PeakProgress = 0;
	float RemainingFacing = 0, PathError = 0, CompletedTargetYaw = 0;
	const TCHAR* Reason = TEXT("NoSample");
};
