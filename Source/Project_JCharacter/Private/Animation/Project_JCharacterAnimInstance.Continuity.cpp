#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Project_JPlayerCharacter.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "Animation/Project_JMotionMatchingCVars.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"

namespace
{
TAutoConsoleVariable<int32> CVarLocomotionSteering(TEXT("p.ProjectJ.LocomotionSteering"), 1,
	TEXT("Local grounded locomotion visual Steering. 0=legacy release, 1=profile-controlled. No movement/RPC changes."));
TAutoConsoleVariable<int32> CVarGeneralTurnCandidates(TEXT("p.ProjectJ.GeneralTurnCandidates"), 1,
	TEXT("Local forward Run/Sprint Cycle/general-turn candidates. 0=disable the candidate window, 1=profile-controlled."));
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

void UProject_JCharacterAnimInstance::UpdateGeneralTurnData(FProject_JAnimThreadSafeData& Data)
{
	const auto* Profile = GetLocomotionProfile();
	auto& Context = Data.MotionMatching.SelectionContext;
	FProject_JGeneralTurnPolicy::FInput Input;
	FProject_JGeneralTurnPolicy::FSettings Settings;
	const FVector Move = Data.LocomotionContext.RequestedMoveWorldDirection;
	const bool bSprint = Data.LocomotionContext.GaitIntent == EProject_JLocomotionGaitIntent::Sprint;
	const bool bSupportedMode = Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::OrientToMovement ||
		(Data.Combat.bIsCombatMode && Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe);
	Input.bEligible = Profile && Profile->bEnableGeneralTurnCandidates && CVarGeneralTurnCandidates.GetValueOnGameThread() != 0 &&
		Project_J::MotionMatchingCVars::ShouldUseTurnCycleCandidates() &&
		Data.bLocomotionSteeringEnabled && bSupportedMode && !Context.bMovingTurn180 &&
		(Data.LocomotionContext.GaitIntent == EProject_JLocomotionGaitIntent::Run || bSprint) &&
		(!bSprint || (OwningPlayerCharacter && OwningPlayerCharacter->IsSprintInputDirectionAllowed())) &&
		Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Cycle &&
		!Data.OneShotPresentation.bShouldOverrideMotionMatching && !Move.ContainsNaN() && !Move.IsNearlyZero();
	bool bCandidateDataUnavailable = false;
	const UProject_JMotionMatchingAssetSet* CandidateSet = nullptr;
	if (Input.bEligible)
	{
		const bool bCombat = Data.Combat.bIsCombatMode && Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe;
		const auto* Set = bCombat ? OwningPlayerCharacter->GetCombatStrafeMotionMatchingAssetSet() : OwningPlayerCharacter->GetMotionMatchingAssetSet();
		CandidateSet = Set;
		auto CandidateContext = Context;
		CandidateContext.bGeneralTurnCandidates = true;
		CandidateContext.bUseGenericFamiliesForNonOrientToMovement = bCombat;
		bCandidateDataUnavailable = !Set || !Set->FindTurnCycleCompanion(CandidateContext,
			Set->GetDatabaseFamily(Context.GaitIntent).Cycle);
		Input.bEligible = !bCandidateDataUnavailable;
	}
	const auto& Request = Data.TurnRequest;
	Input.bEligible &= Request.IsUsable(GFrameCounter, GetWorld() ? GetWorld()->GetTimeSeconds() : 0,
		Data.LocomotionContext.RotationMode, Data.LocomotionContext.GaitIntent) && !Request.bAcuteActive;
	Input.Mode = Request.Mode;
	Input.Gait = Request.Gait;
	Input.Now = Request.Seconds;
	Input.MoveYaw = Request.MoveYaw;
	Input.FacingYaw = Request.FacingYaw;
	Input.ActorYaw = Request.ActorYaw;
	Input.VelocityYaw = Request.VelocityYaw;
	Input.Speed = Request.Speed;
	Input.bAcuteApproach = Request.bAcuteApproach;
	// During braking there is no meaningful zero-velocity heading to reject.
	if (Input.Speed <= 50) Input.VelocityYaw = Input.MoveYaw;
	const auto& Result = GetCompletedTurnFeedback();
	if (Input.bEligible && CandidateSet && Result.bRelevant && Result.IsFresh(GFrameCounter, Input.Now, Input.Mode, Input.Gait))
	{
		const auto& Family = CandidateSet->GetDatabaseFamily(Input.Gait);
		Input.SelectionFrame = Result.Frame;
		Input.bSelectedGeneralTurn = Family.GeneralTurn && Result.SelectedDatabase == Family.GeneralTurn->GetFName();
		Input.bSelectedCycle = (Family.Cycle && Result.SelectedDatabase == Family.Cycle->GetFName()) ||
			(Family.SettledCycle && Result.SelectedDatabase == Family.SettledCycle->GetFName());
	}
	if (Profile)
	{
		Settings.EntrySpeed = bSprint ? Profile->SprintGeneralTurnEntrySpeed : Profile->GeneralTurnEntrySpeed;
		if (bSprint) Settings.ForwardRequestCone = Profile->GetForwardTurnSettings(Input.Mode, Input.Gait).Resolved().ForwardConeAngle;
		Settings.CommittedHeadingAngle = Profile->GeneralTurnCommittedHeadingAngle;
		Settings.StrafeEntryYawRate = Profile->GeneralTurnStrafeEntryYawRate;
		Settings.AlignmentEntryAngle = Profile->GeneralTurnAlignmentEntryAngle;
		Settings.AlignmentConfirmation = Profile->GeneralTurnAlignmentConfirmation;
		Settings.MinimumWindow = Profile->GeneralTurnMinimumWindow;
		Settings.QuietGrace = Profile->GeneralTurnQuietGrace;
		Settings.CompletionGrace = Profile->GeneralTurnCompletionGrace;
	}
	Context.bGeneralTurnCandidates = GeneralTurnPolicy.Update(Input, Settings);
	Context.bAllowGeneralTurnContinuation = GeneralTurnPolicy.AllowsContinuation();
	if (Context.bGeneralTurnCandidates) Context.bAllowTurnContinuation = false;
	const auto* CombatSet = OwningPlayerCharacter ? OwningPlayerCharacter->GetCombatStrafeMotionMatchingAssetSet() : nullptr;
	if (Input.Mode == EProject_JLocomotionRotationMode::Strafe &&
		(GeneralTurnPolicy.RequiresDynamicCycle() || Context.bGeneralTurnCandidates || (Context.bAllowGeneralTurnContinuation && CombatSet &&
			CombatSet->GetDatabaseFamily(Input.Gait).GeneralTurn && Data.MotionMatching.PostSelection.SelectedDatabase ==
			CombatSet->GetDatabaseFamily(Input.Gait).GeneralTurn->GetFName()))) Context.bUseSettledCycle = false;
	Data.GeneralTurnRecentHeading = GeneralTurnPolicy.GetRecentHeading();
	Data.GeneralTurnWindowElapsed = GeneralTurnPolicy.GetElapsed();
	Data.GeneralTurnMoveYawRate = GeneralTurnPolicy.GetMoveYawRate();
	Data.GeneralTurnFacingYawRate = GeneralTurnPolicy.GetFacingYawRate();
	Data.GeneralTurnPathError = GeneralTurnPolicy.GetPathError();
	Data.GeneralTurnFacingError = GeneralTurnPolicy.GetFacingError();
	Data.bGeneralTurnDynamicCycle = GeneralTurnPolicy.RequiresDynamicCycle();
	Data.bGeneralTurnCycleHandoff = GeneralTurnPolicy.IsCycleHandoffHeld();
	Data.GeneralTurnDemand = FName(GeneralTurnPolicy.GetDemand());
	Data.GeneralTurnReason = FName(bCandidateDataUnavailable ? TEXT("CandidateDataUnavailable") : GeneralTurnPolicy.GetReason());
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
