#pragma once
#include "CoreMinimal.h"

/** Input compatibility for a direct Start/Land, independent of clip time or CMC ownership. */
struct FProject_JOneShotInputResponse
{
	struct FInput
	{
		bool bCanSteer = false, bHasMoveReference = false, bHasMoveInput = false;
		float EntryControlYaw = 0, EntryMoveYaw = 0, ControlYaw = 0, MoveYaw = 0;
		float FacingError = 0, PathError = 0;
		float MouseCancelAngle = 15, MoveCancelAngle = 30, SteeringLimit = 45;
	};
	struct FResult { bool bRelease = false; const TCHAR* Reason = TEXT("CompatibleInput"); };
	static FResult Resolve(const FInput& I)
	{
		if (!FMath::IsFinite(I.ControlYaw) || !FMath::IsFinite(I.MoveYaw) || !FMath::IsFinite(I.FacingError) ||
			!FMath::IsFinite(I.PathError) || !FMath::IsFinite(I.SteeringLimit)) return {true, TEXT("InvalidInputSample")};
		const float CameraDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(I.EntryControlYaw, I.ControlYaw));
		if (!I.bCanSteer)
		{
			const bool bMoveChanged = I.bHasMoveReference && I.bHasMoveInput &&
				FMath::Abs(FMath::FindDeltaAngleDegrees(I.EntryMoveYaw, I.MoveYaw)) >= I.MoveCancelAngle;
			return {CameraDelta >= I.MouseCancelAngle || bMoveChanged, TEXT("LegacyUnsteerable")};
		}
		// Compare keys in the camera frame: W while turning the mouse is the
		// same command. W->A/S is a new directional command, in either rotation mode.
		const float KeyDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(
			FMath::FindDeltaAngleDegrees(I.EntryControlYaw, I.EntryMoveYaw),
			FMath::FindDeltaAngleDegrees(I.ControlYaw, I.MoveYaw)));
		if (I.bHasMoveReference && I.bHasMoveInput && KeyDelta >= I.MoveCancelAngle)
			return {true, TEXT("DirectionalInputChanged")};
		const float Limit = FMath::Clamp(I.SteeringLimit, 0.f, 90.f);
		if (FMath::Abs(I.FacingError) > Limit || FMath::Abs(I.PathError) > Limit)
			return {true, TEXT("BeyondSteeringRange")};
		return {};
	}
};
