#pragma once

#include "Project_JLocomotionAnimTypes.h"

/** Value-only lifetime for a forward-running 180-degree Turn search window.
 * Does not own movement, animation time, a frozen snapshot, or a new tick. */
class FProject_JMovingTurnPolicy
{
public:
	struct FInput
	{
		bool bEligible = false;
		// Speed/old velocity qualify a new event, never the same event's braking.
		bool bEntryQualified = true;
		EProject_JLocomotionRotationMode RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
		float ActorYaw = 0.0f;
		float TargetFacingYaw = 0.0f;
		float VelocityYaw = 0.0f;
		float MoveYaw = 0.0f;
		float EntryAngle = 150.0f;
		float ExitAngle = 15.0f;
		double NowSeconds = 0.0;
	};

	void Reset() { *this = FProject_JMovingTurnPolicy(); }

	bool Update(const FInput& Input)
	{
		const bool bModeChanged = bHasMode && RotationMode != Input.RotationMode;
		RotationMode = Input.RotationMode;
		bHasMode = true;
		if (bModeChanged || !Input.bEligible || !FMath::IsFinite(Input.NowSeconds) ||
			!FMath::IsFinite(Input.ActorYaw) || !FMath::IsFinite(Input.TargetFacingYaw) ||
			!FMath::IsFinite(Input.VelocityYaw) || !FMath::IsFinite(Input.MoveYaw))
		{
			// A mode/one-shot handoff is not a fresh running turn. Re-arm only
			// after observing a coherent aligned running context in that owner.
			bActive = false;
			bArmed = false;
			bAllowContinuation = false;
			return false;
		}

		const float FacingAngle = Angle(Input.ActorYaw, Input.TargetFacingYaw);
		const float ExitAngle = FMath::IsFinite(Input.ExitAngle) ? FMath::Clamp(Input.ExitAngle, 0.0f, 45.0f) : 15.0f;
		if (bAllowContinuation && (FacingAngle > RearmAngle ||
			Angle(EntryTargetYaw, Input.TargetFacingYaw) > 60.0f ||
			Angle(Input.MoveYaw, Input.TargetFacingYaw) > 45.0f)) bAllowContinuation = false;
		if (bActive)
		{
			if (Input.NowSeconds < StartedAt || Input.NowSeconds - StartedAt >= MaxDurationSeconds ||
				FacingAngle <= ExitAngle || Angle(EntryTargetYaw, Input.TargetFacingYaw) > 60.0f ||
				Angle(Input.MoveYaw, Input.TargetFacingYaw) > 45.0f)
			{
				bAllowContinuation = Input.NowSeconds >= StartedAt && Input.NowSeconds - StartedAt < MaxDurationSeconds &&
					FacingAngle <= ExitAngle && Angle(EntryTargetYaw, Input.TargetFacingYaw) <= 60.0f &&
					Angle(Input.MoveYaw, Input.TargetFacingYaw) <= 45.0f;
				bActive = false;
				bArmed = false;
				NextEntryTime = Input.NowSeconds + CooldownSeconds;
			}
			else
			{
				// Reversal of velocity is an entry fact, not a condition that must
				// stay true while the character finishes the same moving turn.
				return true;
			}
		}

		if (Input.bEntryQualified && FacingAngle <= RearmAngle)
		{
			bArmed = true;
		}
		const float EntryAngle = FMath::IsFinite(Input.EntryAngle) ? FMath::Clamp(Input.EntryAngle, 150.0f, 180.0f) : 150.0f;
		if (!Input.bEntryQualified || !bArmed || Input.NowSeconds < NextEntryTime || FacingAngle < EntryAngle ||
			Angle(Input.VelocityYaw, Input.MoveYaw) < 135.0f ||
			Angle(Input.VelocityYaw, Input.ActorYaw) > 45.0f ||
			Angle(Input.MoveYaw, Input.TargetFacingYaw) > 45.0f)
		{
			return false;
		}
		bActive = true;
		bAllowContinuation = false;
		bArmed = false;
		StartedAt = Input.NowSeconds;
		EntryTargetYaw = Input.TargetFacingYaw;
		return true;
	}

	bool IsActive() const { return bActive; }
	bool IsArmed() const { return bArmed; }
	bool AllowsCompletedTurnContinuation() const { return bAllowContinuation; }

	/** Read-only explanation, evaluated before Update only while tracing. */
	const TCHAR* DescribeNextUpdate(const FInput& I) const
	{
		if (bHasMode && RotationMode != I.RotationMode) return TEXT("ModeChanged");
		if (!I.bEligible) return TEXT("Ineligible");
		if (!FMath::IsFinite(I.NowSeconds) || !FMath::IsFinite(I.ActorYaw) || !FMath::IsFinite(I.TargetFacingYaw) ||
			!FMath::IsFinite(I.VelocityYaw) || !FMath::IsFinite(I.MoveYaw)) return TEXT("InvalidSample");
		const float Facing = Angle(I.ActorYaw, I.TargetFacingYaw);
		if (bActive)
		{
			if (I.NowSeconds < StartedAt) return TEXT("ClockReversed");
			if (I.NowSeconds - StartedAt >= MaxDurationSeconds) return TEXT("Timeout");
			if (Facing <= (FMath::IsFinite(I.ExitAngle) ? FMath::Clamp(I.ExitAngle, 0.0f, 45.0f) : 15.0f)) return TEXT("Aligned");
			if (Angle(EntryTargetYaw, I.TargetFacingYaw) > 60.0f) return TEXT("TargetChanged");
			if (Angle(I.MoveYaw, I.TargetFacingYaw) > 45.0f) return TEXT("MoveFacingChanged");
			return TEXT("Active");
		}
		if (!I.bEntryQualified) return TEXT("EntryUnqualified");
		if (!bArmed && Facing > RearmAngle) return TEXT("NotArmed");
		if (I.NowSeconds < NextEntryTime) return TEXT("Cooldown");
		if (Facing < (FMath::IsFinite(I.EntryAngle) ? FMath::Clamp(I.EntryAngle, 150.0f, 180.0f) : 150.0f)) return TEXT("FacingBelowEntry");
		if (Angle(I.VelocityYaw, I.MoveYaw) < 135.0f) return TEXT("PathBelow135");
		if (Angle(I.VelocityYaw, I.ActorYaw) > 45.0f) return TEXT("OldTravelNotForward");
		if (Angle(I.MoveYaw, I.TargetFacingYaw) > 45.0f) return TEXT("NewTravelNotForward");
		return TEXT("Enter");
	}

private:
	static float Angle(float From, float To) { return FMath::Abs(FMath::FindDeltaAngleDegrees(From, To)); }
	static constexpr float RearmAngle = 90.0f;
	static constexpr double MaxDurationSeconds = 0.75;
	static constexpr double CooldownSeconds = 0.10;
	EProject_JLocomotionRotationMode RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
	double StartedAt = 0.0;
	double NextEntryTime = 0.0;
	float EntryTargetYaw = 0.0f;
	bool bHasMode = false;
	bool bArmed = false;
	bool bActive = false;
	bool bAllowContinuation = false;
};
