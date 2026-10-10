#pragma once

#include "Project_JLocomotionAnimTypes.h"

/** Completed primary-mesh evaluation, read on GT without touching a worker proxy. */
struct FProject_JTurnSelectionFeedback
{
	uint64 Frame = 0;
	double Seconds = 0;
	EProject_JLocomotionRotationMode Mode = EProject_JLocomotionRotationMode::OrientToMovement;
	EProject_JLocomotionGaitIntent Gait = EProject_JLocomotionGaitIntent::Run;
	FName SelectedDatabase;
	bool bRelevant = false, bVisualFacingValid = false;
	float VisualFacingYaw = 0;
	bool IsFresh(uint64 CurrentFrame, double Now, EProject_JLocomotionRotationMode CurrentMode,
		EProject_JLocomotionGaitIntent CurrentGait = EProject_JLocomotionGaitIntent::Run) const
	{
		return Frame > 0 && Frame <= CurrentFrame && CurrentFrame - Frame <= 2 && Mode == CurrentMode && Gait == CurrentGait &&
			FMath::IsFinite(Now) && FMath::IsFinite(Seconds) && Now >= Seconds && Now - Seconds <= .1;
	}
};

/** One GT physical sample shared by ordinary and acute turn admission.
 * This is presentation data, never a movement target or replicated request. */
struct FProject_JTurnRequestSample
{
	bool bValid = false, bAcuteActive = false, bAcuteApproach = false;
	uint64 Frame = 0;
	double Seconds = 0;
	EProject_JLocomotionRotationMode Mode = EProject_JLocomotionRotationMode::OrientToMovement;
	EProject_JLocomotionGaitIntent Gait = EProject_JLocomotionGaitIntent::Run;
	float ActorYaw = 0, FacingYaw = 0, MoveYaw = 0, VelocityYaw = 0, Speed = 0;
	const TCHAR* AcuteReason = TEXT("NoSample");
	const TCHAR* EligibilityGuard = TEXT("NoSample");
	bool bPreparing = false, bVisualFacingValid = false;
	uint8 Demand = 0;
	float RequestedSweep = 0, RemainingFacing = 0, VisualFacingYaw = 0;

	bool IsUsable(uint64 CurrentFrame, double Now, EProject_JLocomotionRotationMode CurrentMode,
		EProject_JLocomotionGaitIntent CurrentGait = EProject_JLocomotionGaitIntent::Run) const
	{
		return bValid && Mode == CurrentMode && Gait == CurrentGait && Frame <= CurrentFrame && CurrentFrame - Frame <= 2 &&
			FMath::IsFinite(Now) && FMath::IsFinite(Seconds) && Now >= Seconds && Now - Seconds <= .1 &&
			FMath::IsFinite(ActorYaw) && FMath::IsFinite(FacingYaw) && FMath::IsFinite(MoveYaw) &&
			FMath::IsFinite(VelocityYaw) && FMath::IsFinite(Speed) && Speed >= 0;
	}
};
