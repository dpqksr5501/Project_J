#include "Animation/Project_JStrafeLowerBodyFacing.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "TwoBoneIK.h"

FProject_JStrafeLowerBodyFacingInput Project_J::Animation::BuildStrafeLowerBodyFacingInput(
	const FProject_JAnimThreadSafeData& Data, const FProject_JStrafeLowerBodyFacingPolicy& Policy,
	bool bPrimaryLocalPlayer, bool bActionOwner, bool bConsoleEnabled, float ActorYaw, const FVector2D& LocalMoveInput)
{
	FProject_JStrafeLowerBodyFacingInput Result;
	Result.Policy = Policy;
	// Actions and every Turn owner are exact bypasses, including residual correction.
	// Walk/Sprint keep their existing presentation until separately validated.
	if (Data.LocomotionContext.GaitIntent != EProject_JLocomotionGaitIntent::Run ||
		Data.LocomotionContext.PhaseFamily != EProject_JLocomotionPhaseFamily::Cycle ||
		LocalMoveInput.ContainsNaN() || LocalMoveInput.IsNearlyZero() ||
		(Data.OneShotPresentation.bEnabled && Data.OneShotPresentation.PresentationState != EProject_JStateControllerPresentationState::LocomotionLoop) ||
		!Data.Input.bHasMoveInput || !FMath::IsFinite(Data.Movement.GroundSpeed) ||
		Data.Movement.GroundSpeed < 10.f || Data.Movement.Velocity.ContainsNaN() || Data.Movement.Velocity.IsNearlyZero() ||
		!FMath::IsFinite(Data.ProceduralIK.FootPlacementAlpha) || Data.ProceduralIK.FootPlacementAlpha < .99f ||
		!FMath::IsFinite(Data.ProceduralIK.LegIKAlpha) || Data.ProceduralIK.LegIKAlpha < .99f) return Result;
	FProject_JStrafeUpperBodyFacingPolicy Limits;
	Limits.bEnabled = Policy.bEnabled;
	auto Safe = [](float V, float Fallback, float Min, float Max) { return FMath::IsFinite(V) ? FMath::Clamp(V, Min, Max) : Fallback; };
	Limits.Tolerance = Safe(Policy.Tolerance, 10.f, 0.f, 30.f);
	Limits.MaxCorrection = Safe(Policy.MaxCorrection, 25.f, 0.f, 30.f);
	Limits.HalfLife = Safe(Policy.HalfLife, .08f, .01f, .5f);
	Limits.MaxCorrectionRate = Safe(Policy.MaxCorrectionRate, 180.f, 30.f, 360.f);
	Limits.ForwardCone = Safe(Policy.ForwardCone, 25.f, 0.f, 45.f);
	Limits.MinimumSpeed = Safe(Policy.MinimumSpeed, 150.f, 10.f, 2000.f);
	Result.Facing = BuildStrafeUpperBodyFacingInput(Data, Limits, bPrimaryLocalPlayer, bActionOwner, bConsoleEnabled, ActorYaw);
	// Aim yaw can be clamped while the camera outruns capsule yaw. It cannot
	// determine which input key is forward; use held camera-relative input too.
	const float InputAngle = FMath::RadiansToDegrees(FMath::Atan2(LocalMoveInput.X, LocalMoveInput.Y));
	Result.Facing.bEnabled &= FMath::Abs(InputAngle) <= Limits.ForwardCone;
	if (IsStrafeFacingTurnOwned(Data)) Result.Facing = {};
	if (Result.Facing.bCanRelease) Result.Facing.TargetYaw = Data.Movement.Velocity.Rotation().Yaw;
	return Result;
}

bool Project_J::Animation::SolveFacingLeg(const FTransform& Pelvis, const FTransform& ThighLocal,
	const FTransform& CalfLocal, const FTransform& FootLocal, const FTransform& FootTarget,
	FTransform& OutThigh, FTransform& OutCalf, FTransform& OutFoot)
{
	OutThigh = ThighLocal * Pelvis;
	OutCalf = CalfLocal * OutThigh;
	OutFoot = FootLocal * OutCalf;
	const double Upper = FVector::Distance(OutThigh.GetLocation(), OutCalf.GetLocation());
	const double Lower = FVector::Distance(OutCalf.GetLocation(), OutFoot.GetLocation());
	const double Distance = FVector::Distance(OutThigh.GetLocation(), FootTarget.GetLocation());
	if (!FMath::IsFinite(Distance) || Upper < UE_SMALL_NUMBER || Lower < UE_SMALL_NUMBER ||
		Distance > Upper + Lower + .0001 || Distance < FMath::Abs(Upper - Lower) + .0001) return false;
	// Preserve the authored knee bend plane under the new pelvis orientation.
	AnimationCore::SolveTwoBoneIK(OutThigh, OutCalf, OutFoot, OutCalf.GetLocation(), FootTarget.GetLocation(),
		Upper, Lower, false, 1., 1.);
	if (OutThigh.ContainsNaN() || OutCalf.ContainsNaN() || OutFoot.ContainsNaN() ||
		!OutFoot.GetLocation().Equals(FootTarget.GetLocation(), .001)) return false;
	// Existing Foot Placement / Leg IK owns contact position, yaw, scale and toes.
	OutFoot = FootTarget;
	return true;
}
