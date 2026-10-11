#include "Animation/Project_JStrafeUpperBodyFacing.h"
#include "Animation/Project_JCharacterAnimInstance.h"

namespace
{
float Safe(float Value, float Fallback, float Min, float Max)
{
	return FMath::IsFinite(Value) ? FMath::Clamp(Value, Min, Max) : Fallback;
}
}

bool Project_J::Animation::IsStrafeFacingTurnOwned(const FProject_JAnimThreadSafeData& Data)
{
	return Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Turn ||
		Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::TurnInPlace ||
		Data.TurnRequest.bAcuteActive || Data.TurnRequest.bAcuteApproach || Data.TurnRequest.bPreparing ||
		Data.MotionMatching.SelectionContext.bMovingTurn180 || Data.MotionMatching.SelectionContext.bGeneralTurnCandidates ||
		Data.MotionMatching.SelectionContext.bAllowGeneralTurnContinuation || Data.MotionMatching.SelectionContext.bAllowTurnContinuation;
}

FProject_JStrafeUpperBodyFacingInput Project_J::Animation::BuildStrafeUpperBodyFacingInput(
	const FProject_JAnimThreadSafeData& Data, const FProject_JStrafeUpperBodyFacingPolicy& Policy,
	bool bPrimaryLocalPlayer, bool bActionOwner, bool bConsoleEnabled, float ActorYaw)
{
	FProject_JStrafeUpperBodyFacingInput Result;
	Result.Policy = Policy;
	if (!Policy.bEnabled || !bConsoleEnabled || !bPrimaryLocalPlayer || bActionOwner || !FMath::IsFinite(ActorYaw) ||
		!FMath::IsFinite(Data.Aim.AimYaw) || !FMath::IsFinite(Data.Aim.AimOffsetAlpha) ||
		!Data.Combat.bIsCombatMode || Data.LocomotionMode != EProject_JAnimationLocomotionMode::OnFoot || Data.Air.bIsInAir ||
		Data.LocomotionContext.RotationMode != EProject_JLocomotionRotationMode::Strafe ||
		(Data.LocomotionContext.PhaseFamily != EProject_JLocomotionPhaseFamily::Cycle &&
		 Data.LocomotionContext.PhaseFamily != EProject_JLocomotionPhaseFamily::Turn) ||
		Data.OneShotPresentation.bShouldOverrideMotionMatching || Data.Combat.bIsAttacking || Data.Combat.bIsDodging ||
		Data.Combat.bIsHitReacting || Data.Aim.AimOffsetAlpha <= UE_SMALL_NUMBER) return Result;
	// Release smoothly on W -> A/S/D and into Turn. Never request a new
	// correction during Turn; this short decay avoids a presentation-layer pop.
	// One-shots/actions, OTM, followers and remote owners still bypass exactly.
	Result.bCanRelease = true;
	Result.ActorYaw = FRotator::NormalizeAxis(ActorYaw);
	Result.TargetYaw = FRotator::NormalizeAxis(ActorYaw + Data.Aim.AimYaw);
	Result.Strength = FMath::Clamp(Data.Aim.AimOffsetAlpha, 0.f, 1.f);
	const FVector Move = Data.LocomotionContext.RequestedMoveWorldDirection;
	if (IsStrafeFacingTurnOwned(Data)) return Result;
	Result.bEnabled = Data.Input.bHasMoveInput && FMath::IsFinite(Data.Movement.GroundSpeed) &&
		Data.Movement.GroundSpeed >= Safe(Policy.MinimumSpeed, 150.f, 10.f, 2000.f) && !Move.ContainsNaN() && !Move.IsNearlyZero() &&
		FMath::Abs(FMath::FindDeltaAngleDegrees(Result.TargetYaw, Move.Rotation().Yaw)) <= Safe(Policy.ForwardCone, 25.f, 0.f, 45.f);
	return Result;
}

float Project_J::Animation::UpperBodyFacingDemand(float Error, float Strength, const FProject_JStrafeUpperBodyFacingPolicy& Policy)
{
	if (!FMath::IsFinite(Error) || !FMath::IsFinite(Strength)) return 0.f;
	const float Normalized = FRotator::NormalizeAxis(Error);
	const float Excess = FMath::Max(0.f, FMath::Abs(Normalized) - Safe(Policy.Tolerance, 8.f, 0.f, 30.f));
	return FMath::Sign(Normalized) * FMath::Min(Excess, Safe(Policy.MaxCorrection, 40.f, 0.f, 60.f)) * FMath::Clamp(Strength, 0.f, 1.f);
}

float Project_J::Animation::StepUpperBodyFacing(float Current, float Target, float DeltaTime, const FProject_JStrafeUpperBodyFacingPolicy& Policy)
{
	if (!FMath::IsFinite(Current) || !FMath::IsFinite(Target) || !FMath::IsFinite(DeltaTime) || DeltaTime < 0.f) return 0.f;
	const float Dt = FMath::Min(DeltaTime, .1f);
	const float Limit = Safe(Policy.MaxCorrection, 40.f, 0.f, 60.f);
	Current = FMath::Clamp(Current, -Limit, Limit);
	Target = FMath::Clamp(Target, -Limit, Limit);
	const float Step = (Target - Current) * (1.f - FMath::Exp2(-Dt / Safe(Policy.HalfLife, .04f, .01f, .5f)));
	const float MaxStep = Safe(Policy.MaxCorrectionRate, 360.f, 30.f, 720.f) * Dt;
	return Current + FMath::Clamp(Step, -MaxStep, MaxStep);
}

float Project_J::Animation::UpperBodyRootFacingWeight(float Error, const FProject_JStrafeUpperBodyFacingPolicy& Policy)
{
	if (!FMath::IsFinite(Error)) return 0.f;
	const float Start = Safe(Policy.RootFadeStart, 25.f, 0.f, 60.f);
	const float End = FMath::Max(Start + 1.f, Safe(Policy.RootFadeEnd, 45.f, 1.f, 90.f));
	return 1.f - FMath::SmoothStep(Start, End, static_cast<float>(FMath::Abs(FRotator::NormalizeAxis(Error))));
}
