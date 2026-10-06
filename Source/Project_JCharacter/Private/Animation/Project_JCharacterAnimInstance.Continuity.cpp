#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Project_JPlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"

namespace
{
TAutoConsoleVariable<int32> CVarLocomotionSteering(TEXT("p.ProjectJ.LocomotionSteering"), 1,
	TEXT("Local grounded locomotion visual Steering. 0=legacy release, 1=profile-controlled. No movement/RPC changes."));
float SafeSetting(float Value, float Fallback, float Min, float Max)
{
	return FMath::IsFinite(Value) ? FMath::Clamp(Value, Min, Max) : Fallback;
}
}

void UProject_JCharacterAnimInstance::UpdateLocomotionSteeringData(FProject_JAnimThreadSafeData& Data) const
{
	Data.bLocomotionSteeringEnabled = false;
	const auto* Profile = GetLocomotionProfile();
	const auto* Movement = OwningCharacter ? OwningCharacter->GetCharacterMovement() : nullptr;
	const auto Phase = Data.LocomotionContext.PhaseFamily;
	const auto Presentation = Data.OneShotPresentation.PresentationState;
	const bool bContinuous = !Data.OneShotPresentation.bShouldOverrideMotionMatching &&
		(Phase == EProject_JLocomotionPhaseFamily::Cycle || Phase == EProject_JLocomotionPhaseFamily::Turn);
	const bool bMovingOneShot = Data.OneShotPresentation.bShouldOverrideMotionMatching &&
		(Phase == EProject_JLocomotionPhaseFamily::Start || Phase == EProject_JLocomotionPhaseFamily::Landing) &&
		(Presentation == EProject_JStateControllerPresentationState::TransitionToLocomotion ||
			Presentation == EProject_JStateControllerPresentationState::TransitionToLand);
	using Gate = EProject_JLocomotionSteeringGate;
	// Preserve the existing guard order while publishing its exact rejection.
	if (!Profile) Data.LocomotionSteeringGate = Gate::MissingProfile;
	else if (!Profile->bEnableLocomotionSteering) Data.LocomotionSteeringGate = Gate::ProfileDisabled;
	else if (CVarLocomotionSteering.GetValueOnGameThread() == 0) Data.LocomotionSteeringGate = Gate::ConsoleDisabled;
	else if (!Data.bIsLocallyControlled || !OwningPlayerCharacter || !OwningPlayerCharacter->IsPlayerControlled() || !IsPrimaryMeshAnimInstance()) Data.LocomotionSteeringGate = Gate::Ownership;
	else if (Data.LocomotionMode != EProject_JAnimationLocomotionMode::OnFoot || !Movement || !Movement->IsMovingOnGround() || Data.Air.bIsInAir) Data.LocomotionSteeringGate = Gate::NotGroundedOnFoot;
	else if (!Data.Input.bHasMoveInput) Data.LocomotionSteeringGate = Gate::NoInput;
	else if (!bContinuous && !bMovingOneShot) Data.LocomotionSteeringGate = Gate::PhaseOwner;
	else if (Data.Combat.bIsAttacking || Data.Combat.bIsDodging || Data.Combat.bIsHitReacting) Data.LocomotionSteeringGate = Gate::Action;
	else if (IsAnyMontagePlaying()) Data.LocomotionSteeringGate = Gate::Montage;
	else if (Movement->HasAnimRootMotion() || Movement->CurrentRootMotion.HasActiveRootMotionSources()) Data.LocomotionSteeringGate = Gate::RootMotion;
	else if (!Data.Movement.bHasTrajectory) Data.LocomotionSteeringGate = Gate::MissingTrajectory;
	else if (!Data.Movement.bTrajectoryPredictionUsable) Data.LocomotionSteeringGate = Gate::PredictionUnusable;
	else if (!FMath::IsFinite(Data.Movement.TrajectoryAgeSeconds) || Data.Movement.TrajectoryAgeSeconds < 0 || Data.Movement.TrajectoryAgeSeconds > 0.1f) Data.LocomotionSteeringGate = Gate::StaleTrajectory;
	else Data.LocomotionSteeringGate = Gate::IncompletePrediction;
	if (Data.LocomotionSteeringGate != Gate::IncompletePrediction) return;

	const float LookAhead = SafeSetting(Profile->SteeringFacingLookAhead, 0.5f, 0.1f, 1.0f);
	Data.LocomotionSteeringLookAhead = LookAhead;
	const FTransformTrajectorySample* Before = nullptr;
	const FTransformTrajectorySample* After = nullptr;
	for (const auto& Sample : Data.Movement.Trajectory.Samples)
	{
		if (!FMath::IsFinite(Sample.TimeInSeconds) || Sample.TimeInSeconds < 0 || Sample.Facing.ContainsNaN() ||
			!Sample.Facing.IsNormalized()) continue;
		if (Sample.TimeInSeconds <= LookAhead && (!Before || Sample.TimeInSeconds > Before->TimeInSeconds)) Before = &Sample;
		if (Sample.TimeInSeconds >= LookAhead && (!After || Sample.TimeInSeconds < After->TimeInSeconds)) After = &Sample;
	}
	// Do not extrapolate an incomplete or stale trajectory into an invented yaw.
	if (!Before || !After) return;
	const float Span = After->TimeInSeconds - Before->TimeInSeconds;
	const float Alpha = Span > UE_SMALL_NUMBER ? (LookAhead - Before->TimeInSeconds) / Span : 0.0f;
	Data.LocomotionSteeringTarget = FQuat::Slerp(Before->Facing, After->Facing, Alpha).GetNormalized();
	Data.LocomotionSteeringProceduralTime = SafeSetting(Profile->SteeringProceduralTime, 0.4f, 0.1f, 2.0f);
	Data.LocomotionSteeringAnimatedTime = SafeSetting(Profile->SteeringAnimatedTime, 2.0f, 0.0f, 3.0f);
	Data.LocomotionSteeringMaxYawError = SafeSetting(Profile->SteeringMaxVisualYawError, 45.0f, 0.0f, 90.0f);
	Data.bLocomotionSteeringEnabled = true;
	Data.LocomotionSteeringGate = Gate::Enabled;
}

float UProject_JCharacterAnimInstance::GetThreadSafeLocomotionSteeringAlpha() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().bLocomotionSteeringEnabled ? 1.0f : 0.0f;
}
int32 UProject_JCharacterAnimInstance::GetThreadSafeMotionMatchingCandidateCount() const
{
	const auto& Proxy = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>();
	return Proxy.GetThreadSafeCandidateCount();
}
FQuat UProject_JCharacterAnimInstance::GetThreadSafeLocomotionSteeringTarget() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().LocomotionSteeringTarget;
}
float UProject_JCharacterAnimInstance::GetThreadSafeLocomotionSteeringProceduralTime() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().LocomotionSteeringProceduralTime;
}
float UProject_JCharacterAnimInstance::GetThreadSafeLocomotionSteeringAnimatedTime() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().LocomotionSteeringAnimatedTime;
}
float UProject_JCharacterAnimInstance::GetThreadSafeLocomotionSteeringMaxYawError() const
{
	// Existing TIP and legacy paths retain their authored unlimited error bound.
	const auto& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return Data.bLocomotionSteeringEnabled ? Data.LocomotionSteeringMaxYawError : -1.0f;
}
