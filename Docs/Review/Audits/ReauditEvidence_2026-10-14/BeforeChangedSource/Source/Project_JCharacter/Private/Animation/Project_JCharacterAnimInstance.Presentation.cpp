// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JCharacterAnimInstance.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"

#include "ChooserFunctionLibrary.h"
#include "ChooserTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "IObjectChooser.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JLocomotionAnimStateComponentBase.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/Project_JMotionMatchingCVars.h"
#include "Animation/Project_JOneShotInputResponse.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "Animation/Project_JRiderAnimationProfile.h"
#include "Animation/AnimationAsset.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JBaseCharacter.h"
#include "Mount/Project_JMountComponent.h"
#include "Mount/Project_JMountCharacter.h"
#include "Mount/Project_JFlyingMountCharacter.h"
#include "Project_JLocomotionDebugUtils.h"
#include "StructUtils/InstancedStruct.h"

#include "Animation/Project_JAnimationPresentationHelpers.h"
using namespace ProjectJ::Animation::Presentation;

void UProject_JCharacterAnimInstance::UpdateReleasedLandingMotionContext(
	FProject_JAnimThreadSafeData& Data, bool bReleasingNow) const
{
	if (Data.LocomotionMode != EProject_JAnimationLocomotionMode::OnFoot || Data.Air.bIsInAir || !Data.Landing.bIsLanding)
	{
		return;
	}
	const bool bConsumedLanding = Data.Landing.PresentationRevision != INDEX_NONE &&
		Data.Landing.PresentationRevision == StateControllerRuntime.GetHeldLandingRevision() &&
		StateControllerRuntime.GetHeldState() != EProject_JStateControllerPresentationState::TransitionToLand;
	if (!bReleasingNow && !bConsumedLanding) { return; }
	// Keep the physical landing event and its revision intact. Only its released
	// presentation returns to the current ground intent, including subsequent snapshots.
	Data.LocomotionContext.bIsMotionMatchingMoving = Data.LocomotionContext.bHasGroundMovementIntent;
	Data.LocomotionContext.PhaseFamily = Data.LocomotionContext.bIsMotionMatchingMoving
		? EProject_JLocomotionPhaseFamily::Cycle : EProject_JLocomotionPhaseFamily::Idle;
	Data.MotionMatching.SelectionContext.PhaseFamily = Data.LocomotionContext.PhaseFamily;
	if (IsPrimaryMeshAnimInstance() && LocomotionAnimStateComponent &&
		LocomotionAnimStateComponent->NotifyLandingPresentationReleased(Data.Landing.PresentationRevision))
	{
		// Candidate ownership follows the released presentation, not the physical
		// impact flag. Reuse the component's normal direction/action/turn guards.
		const auto& Selection = LocomotionAnimStateComponent->GetMotionMatchingSelectionContext();
		Data.MotionMatching.SelectionContext.bUseForwardCycle = Selection.bUseForwardCycle;
		Data.MotionMatching.SelectionContext.bUseSettledCycle = Selection.bUseSettledCycle;
	}
}

void UProject_JCharacterAnimInstance::ResolveStateControllerPresentationStateWithPlaybackHold(
	const FProject_JAnimThreadSafeData& Data,
	FProject_JAnimOneShotPresentationThreadSafeData& InOutOneShot) const
{
	if (StateControllerRuntime.GetPivot().RequestRevision != 0 &&
		(Data.Air.bIsInAir || Data.Landing.bIsLanding))
	{
		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("CombatStrafeRunPivotCancelled Actor=%s PivotRev=%d Reason=%s"),
				*GetNameSafe(OwningCharacter), StateControllerRuntime.GetPivot().RequestRevision,
				Data.Air.bIsInAir ? TEXT("Air") : TEXT("Land"));
		}
		StateControllerRuntime.CancelPivot();
	}
	EProject_JStateControllerPresentationState DesiredState =
		ResolveStateControllerPresentationState(Data, InOutOneShot);
	// The semantic FallOff timer can outlive the authored entry. Once this
	// airborne episode reached its loop, that legacy flag is not a new command.
	// A genuine new JumpStart still retains TransitionToInAir ownership.
	if (Data.Air.bIsInAir && !Data.Air.bIsJumping &&
		StateControllerRuntime.GetHeldState() == EProject_JStateControllerPresentationState::InAirLoop &&
		DesiredState == EProject_JStateControllerPresentationState::TransitionToInAir)
	{
		DesiredState = EProject_JStateControllerPresentationState::InAirLoop;
	}
	const double NowSeconds = AnimationClock.Seconds;
	StateControllerRuntime.ClearTurnReselect();
	const bool bIsLocallyControlled = IsLocallyControlledCharacter();
	const bool bInFullBodyActionMontageOrRecentExit =
		Data.ProceduralIK.FullBodyMontageWeight > 0.05f ||
		Data.Combat.bIsAttacking ||
		Data.Combat.bIsDodging ||
		Data.Combat.bIsHitReacting ||
		(NowSeconds - LastFullBodyMontageEndedAtSeconds < 0.25);
	FProject_JStateControllerRuntime::FIntent Intent;
	Intent.bHasMoveInput = Data.Input.bHasMoveInput;
	Intent.bIsLocallyControlled = bIsLocallyControlled;
	Intent.bIsMotionMatchingMoving = Data.LocomotionContext.bIsMotionMatchingMoving;
	Intent.bIsInAir = Data.Air.bIsInAir;
	Intent.bFullBodyActionOrRecentExit = bInFullBodyActionMontageOrRecentExit;
	Intent.GroundMotionMode = Data.Ground.GroundMotionMode;
	Intent.TurnInPlaceSequence = Data.LocomotionContext.TurnInPlaceSequence;
	Intent.LandingPresentationRevision = Data.Landing.PresentationRevision;
	Intent.PhaseFamily = InOutOneShot.PhaseFamily;
	const FProject_JStateControllerRuntime::FHoldUpdate HoldUpdate =
		StateControllerRuntime.BeginDesiredHold(DesiredState, Intent, NowSeconds);
	DesiredState = HoldUpdate.DesiredState;
	const EProject_JStateControllerPresentationState RequestedState = DesiredState;
	const EProject_JStateControllerPresentationState PreviousHeldState = HoldUpdate.PreviousState;
	const bool bNaturalContinuation = HoldUpdate.bNaturalContinuation;
	const bool bNewLandingPresentation = HoldUpdate.bNewLanding;
	const bool bStartedNewPlaybackHold = HoldUpdate.bStartedTransition;

	InOutOneShot.TransitionElapsedTime = 0.0f;
	InOutOneShot.TransitionTimeRemaining = 0.0f;
	InOutOneShot.bTransitionAnimationAlmostComplete = false;

	if (!IsTransitionState(StateControllerRuntime.GetHeldState()))
	{
		InOutOneShot.PresentationState = DesiredState;
		return;
	}

	// The chooser is evaluated after this snapshot is built.  On the frame that
	// enters Start/Stop/Air, CachedStateControllerSelectedAnimation still refers
	// to the old state (usually Idle or Cycle).  Do not inspect that stale asset
	// or clear the new clock; publish the transition once so the chooser can
	// select its authored one-shot on this frame.
	if (bStartedNewPlaybackHold)
	{
		Project_J::AnimationFlowDebug::Decision(OwningCharacter, TEXT("Presentation"), TEXT("BeginHold"));
		InOutOneShot.PresentationState = StateControllerRuntime.GetHeldState();
		if (Project_J::MotionMatchingCVars::GetTurnInPlaceTraceMode() > 0 &&
			InOutOneShot.PresentationState == EProject_JStateControllerPresentationState::TurnInPlace)
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("TIPTrace Stage=Hold Event=Begin T=%.3f Actor=%s Seq=%d Bucket=%d Previous=%d Requested=%d"),
				GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f, *GetNameSafe(OwningCharacter),
				Data.LocomotionContext.TurnInPlaceSequence,
				static_cast<int32>(Data.LocomotionContext.TurnInPlaceDirectionBucket),
				static_cast<int32>(PreviousHeldState), static_cast<int32>(RequestedState));
		}
		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("StateControllerTransition Actor=%s Requested=%d PreviousHeld=%d NewHeld=%d LandEpoch=%d NewLand=%s Reason=GameplayIntentReplacement"),
				*GetNameSafe(OwningCharacter),
				static_cast<int32>(RequestedState),
				static_cast<int32>(PreviousHeldState),
				static_cast<int32>(StateControllerRuntime.GetHeldState()),
				Data.Landing.PresentationRevision,
				bNewLandingPresentation ? TEXT("true") : TEXT("false"));
		}
		return;
	}

	const UAnimationAsset* HeldAsset = CachedStateControllerSelectedAnimation;
	const float AssetLength = HeldAsset ? HeldAsset->GetPlayLength() : 0.0f;
	const float StartTime = FMath::Clamp(CachedStateControllerSelectedAnimationOutput.StartTime, 0.0f, AssetLength);
	const float AuthoredPlayableLength = FMath::Max(AssetLength - StartTime, 0.0f);
	const bool bHeldFallOff =
		StateControllerRuntime.GetHeldState() == EProject_JStateControllerPresentationState::TransitionToInAir &&
		(Data.Air.bIsFallOffStart || InOutOneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::Fall);
	const float FallOffMaxHoldTime = FMath::Max(
		Data.MotionMatchingSearchPolicy.ExperimentalFallOffMaxHoldTime,
		0.0f);

	float EffectivePlayableLength = AuthoredPlayableLength;
	if (bHeldFallOff && FallOffMaxHoldTime > KINDA_SMALL_NUMBER)
	{
		EffectivePlayableLength = FMath::Min(AuthoredPlayableLength, FallOffMaxHoldTime);
	}
	// TIP is a direct authored one-shot. Do not truncate it at a generic fixed
	// duration: the Blend Stack must retain it until its own completion (or an
	// authored early-transition window) so the root turn and planted feet finish
	// on the same animation timeline.
	const float Elapsed = StateControllerRuntime.GetHoldElapsed(NowSeconds);
	float LeadTime = FMath::Max(InOutOneShot.FallbackLeadTime, 0.0f);
	float Remaining = FMath::Max(EffectivePlayableLength - Elapsed, 0.0f);
	// A shared player can report its actual clock. Start -> Cycle must begin
	// while that player still has enough motion left for the return overlap.
	// Semantic readiness protects acceleration/first step; Pivot and Land retain
	// their existing authored completion/impact contracts.
	const auto& Playback = GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>().GetUnifiedPlaybackObservation();
	if (StateControllerRuntime.GetHeldState() == EProject_JStateControllerPresentationState::TransitionToLocomotion &&
		StateControllerRuntime.GetPivot().RequestRevision == 0 &&
		InOutOneShot.PhaseFamily != EProject_JLocomotionPhaseFamily::Pivot &&
		DesiredState == EProject_JStateControllerPresentationState::LocomotionLoop &&
		HeldAsset && Playback.Animation == HeldAsset->GetFName() &&
		Playback.SelectionRevision == StateControllerChooserSelectionRevision &&
		Playback.CaptureFrame > 0 && Playback.CaptureFrame <= GFrameCounter && GFrameCounter - Playback.CaptureFrame <= 2 &&
		FMath::IsFinite(Playback.SecondsRemaining) && Playback.SecondsRemaining >= 0.f)
	{
		Remaining = Playback.SecondsRemaining;
		const float AuthoredReturn = CachedStateControllerSelectedAnimationOutput.ReturnBlendTime;
		const float ReturnTime = FMath::IsFinite(AuthoredReturn) && AuthoredReturn >= 0.f ? AuthoredReturn :
			Data.MotionMatchingSearchPolicy.TransitionBlendTime;
		LeadTime = FMath::Max(LeadTime, FMath::Max(0.f, ReturnTime) + FMath::Max(0.f, Data.DeltaTime));
	}
	const bool bHasPlayableTransitionAsset = HeldAsset &&
		EffectivePlayableLength > KINDA_SMALL_NUMBER;
	const bool bReachedCompletion = bHasPlayableTransitionAsset && Remaining <= LeadTime;

	InOutOneShot.TransitionElapsedTime = Elapsed;
	InOutOneShot.TransitionTimeRemaining = Remaining;
	InOutOneShot.bTransitionAnimationAlmostComplete =
		InOutOneShot.bEarlyTransitionWindowOpen || bReachedCompletion;

	const bool bHoldingCommittedPivot =
		StateControllerRuntime.GetPivot().RequestRevision != 0 &&
		InOutOneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot;
	if (bHoldingCommittedPivot && InOutOneShot.bTransitionAnimationAlmostComplete)
	{
		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("CombatStrafeRunPivotReleased Actor=%s PivotRev=%d Reason=AuthoredExit Asset=%s Elapsed=%.3f Remaining=%.3f"),
				*GetNameSafe(OwningCharacter), StateControllerRuntime.GetPivot().RequestRevision,
				HeldAsset ? *HeldAsset->GetName() : TEXT("None"), Elapsed, Remaining);
		}
		StateControllerRuntime.CancelPivot();
		// A Pivot that reaches its authored exit returns to the normal Stop/Cycle
		// policy. Explicit Stop input is handled earlier and preempts immediately.
		const bool bQueueStopAfterPivot =
			!Data.Input.bHasMoveInput &&
			Data.Movement.GroundSpeed > GetEffectiveGenericMoveInputSpeedThreshold();
		const EProject_JStateControllerPresentationState ExitState = bQueueStopAfterPivot
			? EProject_JStateControllerPresentationState::TransitionToIdle
			: (Data.LocomotionContext.bIsMotionMatchingMoving
				? EProject_JStateControllerPresentationState::LocomotionLoop
				: EProject_JStateControllerPresentationState::IdleLoop);
		StateControllerRuntime.BeginHold(ExitState, NowSeconds);
		InOutOneShot.PresentationState = ExitState;
		InOutOneShot.bRequested = false;
		return;
	}

	if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace() && bStartedNewPlaybackHold)
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("StateControllerHold: RequestedState=%d PreviousHeld=%d DesiredState=%d Elapsed=%.3f Remaining=%.3f PlayableLen=%.3f EffectiveLen=%.3f FallOff=%s LeadTime=%.3f ReachedComp=%s EarlyOpen=%s AlmostComp=%s Asset=%s"),
			static_cast<int32>(RequestedState),
			static_cast<int32>(PreviousHeldState),
			static_cast<int32>(DesiredState),
			Elapsed,
			Remaining,
			AuthoredPlayableLength,
			EffectivePlayableLength,
			bHeldFallOff ? TEXT("true") : TEXT("false"),
			LeadTime,
			bReachedCompletion ? TEXT("true") : TEXT("false"),
			InOutOneShot.bEarlyTransitionWindowOpen ? TEXT("true") : TEXT("false"),
			InOutOneShot.bTransitionAnimationAlmostComplete ? TEXT("true") : TEXT("false"),
			HeldAsset ? *HeldAsset->GetName() : TEXT("None"));
	}

	// A missing/looping entry must use the GASP "No Valid Anim" escape route.
	// Otherwise keep the actual Start/Stop/air asset until its authored end (or
	// an authored early-transition notify), never until a locomotion phase flips.
	const bool bStillRequestingHeldTransition = DesiredState == StateControllerRuntime.GetHeldState();
	// Local and remote paths both expose a monotonic TIP edge. The local edge
	// is advanced only when the live camera has moved beyond the fixed target;
	// it is what safely restarts an identical 90/180 Blend Stack asset.
	if (StateControllerRuntime.HasNewTurnSequence(
		bIsLocallyControlled, Data.LocomotionContext.TurnInPlaceSequence))
	{
		StateControllerRuntime.RestartTurnSequence(
			bIsLocallyControlled, Data.LocomotionContext.TurnInPlaceSequence, NowSeconds);
		InOutOneShot.PresentationState = EProject_JStateControllerPresentationState::TurnInPlace;
		InOutOneShot.TransitionElapsedTime = 0.0f;
		InOutOneShot.TransitionTimeRemaining = EffectivePlayableLength;
		InOutOneShot.bTransitionAnimationAlmostComplete = false;
		if (Project_J::MotionMatchingCVars::GetTurnInPlaceTraceMode() > 0)
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("TIPTrace Stage=Hold Event=Restart T=%.3f Actor=%s Seq=%d Bucket=%d PreviousAsset=%s PreviousElapsed=%.3f"),
				GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f, *GetNameSafe(OwningCharacter),
				Data.LocomotionContext.TurnInPlaceSequence,
				static_cast<int32>(Data.LocomotionContext.TurnInPlaceDirectionBucket),
				*GetNameSafe(HeldAsset), Elapsed);
		}
		return;
	}

	// TransitionToInAir 상태 중 마우스 회전 또는 이동 입력으로 인한 점프 방향 재평가 감지
	bool bJumpAirInputReselectDue = false;
	const UProject_JLocomotionProfile* CurrentProfile = GetLocomotionProfile();
	const FProject_JMotionMatchingSearchPolicy& SearchPolicy = CurrentProfile
		? CurrentProfile->MotionMatchingSearchPolicy
		: FProject_JMotionMatchingSearchPolicy();

	if (SearchPolicy.bEnableAirJumpReselection &&
		StateControllerRuntime.GetHeldState() == EProject_JStateControllerPresentationState::TransitionToInAir &&
		DesiredState == EProject_JStateControllerPresentationState::TransitionToInAir &&
		!Data.Air.bIsFallOffStart &&
		EffectivePlayableLength > 0.05f)
	{
		// 1. 진행률 가드: 모션의 AirJumpMaxProgressRatio(기본 85%) 이전까지만 재선택 허용
		const bool bWithinReselectProgressWindow =
			Elapsed < (EffectivePlayableLength * FMath::Clamp(SearchPolicy.AirJumpMaxProgressRatio, 0.1f, 1.0f));

		// 2. 블렌드 진행률 동기화 가드: 이전 재선택 블렌드가 MinBlendProgressRatio(기본 70%) 이상 진행되었는지 확인 (블렌드 중첩 및 팝핑 원천 방지)
		const float ActiveBlendDuration = LastJumpAirBlendDuration > 0.0f
			? LastJumpAirBlendDuration
			: (SearchPolicy.AirJumpBlendTime > 0.0f ? SearchPolicy.AirJumpBlendTime : 0.15f);
		const float RequiredBlendWaitTime = ActiveBlendDuration * FMath::Clamp(SearchPolicy.AirJumpMinBlendProgressRatio, 0.3f, 1.0f);
		const bool bCooldownPassed = (LastJumpAirReselectElapsed <= 0.0f)
			? (Elapsed >= 0.05f)
			: ((Elapsed - LastJumpAirReselectElapsed) >= RequiredBlendWaitTime);

		if (bWithinReselectProgressWindow && bCooldownPassed && OwningCharacter)
		{
			FVector HorizontalVelocity = Data.Movement.Velocity;
			HorizontalVelocity.Z = 0.0f;
			const float IdleSpeed = 10.0f;
			float AirDirectionDegrees = 0.0f;
			bool bHasAirDirection = false;

			if (HorizontalVelocity.SizeSquared() > FMath::Square(IdleSpeed))
			{
				const FVector LocalDir = OwningCharacter->GetActorTransform().InverseTransformVectorNoScale(HorizontalVelocity.GetSafeNormal());
				AirDirectionDegrees = FMath::RadiansToDegrees(FMath::Atan2(LocalDir.Y, LocalDir.X));
				bHasAirDirection = true;
			}
			else if (Data.Input.bHasMoveInput)
			{
				AirDirectionDegrees = Data.Input.MovementDirection;
				bHasAirDirection = true;
			}

			if (bHasAirDirection)
			{
				const EProject_JStateControllerStrafeDirection LiveAirDirection = ResolveStateControllerStrafeDirection(
					AirDirectionDegrees,
					CachedStateControllerStrafeDirection,
					10.0f); // 10도 히스테리시스

				// 3. Gait-Aware 필터링: Sprint 점프는 정면(Forward, FL, FR)만 지원
				const bool bIsSprintJump = (GaitIntentForChooser == EProject_JLocomotionGaitIntent::Sprint) ||
					Data.Ground.bUseSprintLocomotion;
				const bool bIsDirectionSupportedBySprint =
					(LiveAirDirection == EProject_JStateControllerStrafeDirection::Forward ||
					 LiveAirDirection == EProject_JStateControllerStrafeDirection::ForwardLeft ||
					 LiveAirDirection == EProject_JStateControllerStrafeDirection::ForwardRight);

				if (bIsSprintJump && !bIsDirectionSupportedBySprint)
				{
					bJumpAirInputReselectDue = false;
				}
				else if (LiveAirDirection != CachedStateControllerStrafeDirection)
				{
					PendingJumpAirDirection = LiveAirDirection;
					bJumpAirInputReselectDue = true;
				}
			}
		}
	}

	if (bJumpAirInputReselectDue)
	{
		SavedJumpAirElapsed = Elapsed;
		LastJumpAirReselectElapsed = Elapsed;
		bIsJumpAirReselecting = true;
		InOutOneShot.PresentationState = EProject_JStateControllerPresentationState::TransitionToInAir;
		InOutOneShot.TransitionElapsedTime = Elapsed;
		InOutOneShot.TransitionTimeRemaining = Remaining;
		InOutOneShot.bTransitionAnimationAlmostComplete = false;
		return;
	}
	if (bHeldFallOff && !Data.Air.bIsJumping && Data.Air.bIsInAir &&
		InOutOneShot.bTransitionAnimationAlmostComplete)
	{
		// Release at the existing completion/early-window/maximum-hold contract,
		// even while the component retains its broader FallOff presentation flag.
		// Do not restart that same non-loop entry or park on its final frame.
		DesiredState = EProject_JStateControllerPresentationState::InAirLoop;
	}

	// When sprint_land is held, if the landing component phase has finished (!bIsLanding)
	// or if the user released sprint input (!bWantsSprint), interrupt the land one-shot
	// early so Motion Matching (PSD_Run / PSD_Sprint_Cycle / PSD_Idle) takes over immediately.
	bool bInterruptLandForMotionMatching = false;
	if (StateControllerRuntime.GetHeldState() == EProject_JStateControllerPresentationState::TransitionToLand &&
		DesiredState == EProject_JStateControllerPresentationState::LocomotionLoop)
	{
		if (!Data.Landing.bIsLanding || (Data.Landing.bLandWasSprinting && !Data.Ground.bWantsSprint))
		{
			bInterruptLandForMotionMatching = true;
		}
	}

	if (!bInterruptLandForMotionMatching && (bNaturalContinuation || bStillRequestingHeldTransition) && bHasPlayableTransitionAsset &&
		!InOutOneShot.bTransitionAnimationAlmostComplete)
	{
		InOutOneShot.PresentationState = StateControllerRuntime.GetHeldState();
		return;
	}

	// Every proxy plays one authored TIP per sequence edge. Remaining camera yaw
	// is handled by a new sequence, never by restarting the completed asset.
	if (StateControllerRuntime.GetHeldState() == EProject_JStateControllerPresentationState::TurnInPlace &&
		DesiredState == EProject_JStateControllerPresentationState::TurnInPlace)
	{
		DesiredState = EProject_JStateControllerPresentationState::IdleLoop;
	}
	if (Project_J::MotionMatchingCVars::GetTurnInPlaceTraceMode() > 0 &&
		StateControllerRuntime.GetHeldState() == EProject_JStateControllerPresentationState::TurnInPlace)
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("TIPTrace Stage=Hold Event=Exit T=%.3f Actor=%s Seq=%d Asset=%s Elapsed=%.3f Remaining=%.3f Desired=%d"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f, *GetNameSafe(OwningCharacter),
			Data.LocomotionContext.TurnInPlaceSequence, *GetNameSafe(HeldAsset), Elapsed,
			Remaining, static_cast<int32>(DesiredState));
	}

	if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("StateControllerExitHold Actor=%s Exiting State=%d to State=%d LandEpoch=%d (Elapsed=%.3f Remaining=%.3f AlmostComp=%s)"),
			*GetNameSafe(OwningCharacter),
			static_cast<int32>(StateControllerRuntime.GetHeldState()),
			static_cast<int32>(DesiredState),
			Data.Landing.PresentationRevision,
			Elapsed,
			Remaining,
			InOutOneShot.bTransitionAnimationAlmostComplete ? TEXT("true") : TEXT("false"));
	}

	Project_J::AnimationFlowDebug::Decision(OwningCharacter, TEXT("Presentation"),
		bInterruptLandForMotionMatching ? TEXT("LandingReleasedToMM") :
		(InOutOneShot.bEarlyTransitionWindowOpen ? TEXT("AuthoredEarlyExit") :
		(bReachedCompletion ? TEXT("AssetCompletion") :
		(!bHasPlayableTransitionAsset ? TEXT("NoPlayableAsset") : TEXT("IntentReplacement")))));
	if (DesiredState != EProject_JStateControllerPresentationState::TransitionToInAir)
	{
		bIsJumpAirReselecting = false;
		LastJumpAirReselectElapsed = 0.0f;
		LastJumpAirBlendDuration = 0.15f;
		SavedJumpAirElapsed = 0.0f;
	}

	StateControllerRuntime.BeginHold(DesiredState, NowSeconds);
	InOutOneShot.PresentationState = DesiredState;
}

void UProject_JCharacterAnimInstance::EvaluateStateControllerAnimationChooserOnGameThread(FProject_JAnimThreadSafeData& Data)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_AnimStateControllerChooser);
	FProject_JAnimOneShotPresentationThreadSafeData& OneShot = Data.OneShotPresentation;
	OneShot.SelectedAnimation = nullptr;
	OneShot.SelectedAnimationOutput = FProject_JStateControllerChooserOutput();
	OneShot.bHasSelectedAnimation = false;
	OneShot.bShouldOverrideMotionMatching = false;
	OneShot.bSelectedAnimationShouldLoop = ShouldStateControllerPresentationLoop(OneShot.PresentationState);
	OneShot.SelectionRevision = StateControllerChooserSelectionRevision;
	OneShot.bForceBlendNextUpdate = false;

	const UProject_JLocomotionProfile* Profile = GetLocomotionProfile();
	UChooserTable* ChooserTable = Profile
		? Profile->MotionMatchingSearchPolicy.StateControllerAnimationChooserTable.Get()
		: nullptr;
	if (!OneShot.bEnabled || !ChooserTable)
	{
		ResetStateControllerChooserCache();
		return;
	}

	// A new TIP sequence forces a chooser evaluation, including a same-asset
	// Blend Stack restart. A bucket change inside the current fixed-target turn
	// must not replace the committed one-shot.
	const bool bLandingContextChanged =
		OneShot.PresentationState == EProject_JStateControllerPresentationState::TransitionToLand &&
		(CachedStateControllerLandingPresentationRevision != Data.Landing.PresentationRevision ||
		 bCachedStateControllerLandWasMoving != Data.Landing.bLandWasMoving ||
		 bCachedStateControllerLandWasSprinting != Data.Landing.bLandWasSprinting ||
		 bCachedStateControllerUseHeavyLand != Data.Landing.bUseHeavyLand);
	// Selection is owned by the active one-shot command. A live combat/rotation,
	// foot or direction column change is not another playback command. Keep both
	// the asset and chooser output (especially StartTime) until a real event or
	// the existing hold/cancel policy releases it. MM still receives live context.
	bool bLockCommittedOneShotChooser = false;
	if (bCachedStateControllerHasSelectedAnimation &&
		CachedStateControllerPresentationState == OneShot.PresentationState)
	{
		switch (OneShot.PresentationState)
		{
		case EProject_JStateControllerPresentationState::TransitionToLocomotion:
			if (OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot)
			{
				bLockCommittedOneShotChooser = StateControllerRuntime.HasCommittedPivot() &&
					StateControllerRuntime.GetPivot().RequestRevision == Data.LocomotionContext.PivotRequestRevision;
			}
			else
			{
				// Preserve the existing provisional Start gait correction window.
				// A qualified Pivot in this same presentation state must select anew.
				bLockCommittedOneShotChooser = !StateControllerRuntime.HasCommittedPivot() &&
					CachedStateControllerPivotRequestRevision == Data.LocomotionContext.PivotRequestRevision &&
					(bStateControllerStartGaitCommitted || CachedStateControllerGaitIntent == GaitIntentForChooser);
			}
			break;
		case EProject_JStateControllerPresentationState::TransitionToIdle:
			bLockCommittedOneShotChooser = !Data.Input.bHasMoveInput;
			break;
		case EProject_JStateControllerPresentationState::TransitionToLand:
			bLockCommittedOneShotChooser =
				CachedStateControllerLandingPresentationRevision == Data.Landing.PresentationRevision;
			break;
		case EProject_JStateControllerPresentationState::TransitionToInAir:
			// Explicit air-direction reselection preserves progress in its existing
			// path below; a mode toggle alone must not restart Jump/FallOff.
			bLockCommittedOneShotChooser = !bIsJumpAirReselecting &&
				bCachedStateControllerFallOff == bStateControllerFallOffForChooser;
			break;
		case EProject_JStateControllerPresentationState::InAirLoop:
			// FallLoop has no direction/foot variants to reselect during this fall.
			// Keep its playback command while movement/trajectory snapshots stay live.
			bLockCommittedOneShotChooser = Data.Air.bIsInAir &&
				Data.LocomotionMode == EProject_JAnimationLocomotionMode::OnFoot &&
				CachedStateControllerChooserTable.Get() == ChooserTable;
			break;
		case EProject_JStateControllerPresentationState::TurnInPlace:
			bLockCommittedOneShotChooser = !StateControllerRuntime.IsTurnReselectRequested();
			break;
		default:
			break;
		}
	}
	const bool bContextChanged = !bLockCommittedOneShotChooser &&
		(bIsJumpAirReselecting ||
		CachedStateControllerChooserTable.Get() != ChooserTable ||
		CachedStateControllerPresentationState != OneShot.PresentationState ||
		CachedStateControllerPivotRequestRevision != Data.LocomotionContext.PivotRequestRevision ||
		CachedStateControllerRotationMode != Data.LocomotionContext.RotationMode ||
		CachedStateControllerGaitIntent != GaitIntentForChooser ||
		CachedStateControllerStance != OneShot.Stance ||
		CachedStateControllerStrafeDirection != OneShot.StrafeDirection ||
		(OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot &&
			CachedStateControllerPreviousStrafeDirection != OneShot.PreviousStrafeDirection) ||
		CachedStateControllerOneShotFoot != OneShot.Foot ||
		StateControllerRuntime.IsTurnReselectRequested() ||
		bCachedStateControllerCombatMode != Data.Combat.bIsCombatMode ||
		bCachedStateControllerFallOff != bStateControllerFallOffForChooser ||
		bLandingContextChanged);

	if (bContextChanged)
	{
		// The direct State Controller chooser is evaluated before the throttled
		// regular PSD chooser. Publish its UObject-backed chooser columns from this
		// exact immutable snapshot first; otherwise a remote semantic boundary can
		// evaluate with the previous MM search frame's Landing booleans (Heavy or no
		// valid Land row). This remains event-driven because it only runs when the
		// State Controller cache key changed.
		PublishChooserProperties(Data);

		FChooserEvaluationContext ChooserContext;
		ChooserContext.AddObjectParam(this);
		FChooserPlayerSettings ChooserPlayerSettings;
		ChooserContext.AddStructParam(ChooserPlayerSettings);
		FProject_JStateControllerChooserOutput ChooserOutput;
		ChooserContext.AddStructParam(ChooserOutput);

		FString EvaluatedChooserPath = GetNameSafe(ChooserTable);
		const FInstancedStruct ChooserObject = UChooserFunctionLibrary::MakeEvaluateChooser(ChooserTable);
		UObject* ResultObject = ChooserObject.IsValid()
			? UChooserFunctionLibrary::EvaluateObjectChooserBase(
				ChooserContext,
				ChooserObject,
				UObject::StaticClass())
			: nullptr;

		// If Parent Chooser returned a Sub-Chooser Table, evaluate the Sub-Chooser recursively
		while (UChooserTable* SubChooserTable = Cast<UChooserTable>(ResultObject))
		{
			EvaluatedChooserPath += FString::Printf(TEXT(" -> %s"), *GetNameSafe(SubChooserTable));
			const FInstancedStruct SubChooserObject = UChooserFunctionLibrary::MakeEvaluateChooser(SubChooserTable);
			if (!SubChooserObject.IsValid())
			{
				ResultObject = nullptr;
				break;
			}
			ResultObject = UChooserFunctionLibrary::EvaluateObjectChooserBase(
				ChooserContext,
				SubChooserObject,
				UObject::StaticClass());
		}

		UAnimationAsset* SelectedAsset = Cast<UAnimationAsset>(ResultObject);
		if (!SelectedAsset &&
			OneShot.PresentationState == EProject_JStateControllerPresentationState::TransitionToLand &&
			Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			const FProject_JRemoteVisualLocomotionPolicy RemotePolicy = GetEffectiveRemoteVisualPolicy();
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("StateControllerChooserMiss Actor=%s World=%s Role=%d Local=%s Path=%s StateProp=%d RotationProp=%d GaitProp=%d FootProp=%d GroundSpeedProp=%.1f HeavyProp=%s IsLandingProp=%s RunLightProp=%s SprintLightProp=%s StandLightProp=%s RunHeavyProp=%s SprintHeavyProp=%s StandHeavyProp=%s Tier=%d FarRows=%s DisableFarLand=%s"),
				*GetNameSafe(OwningCharacter),
				*GetNameSafe(GetWorld()),
				OwningCharacter ? static_cast<int32>(OwningCharacter->GetLocalRole()) : -1,
				IsLocallyControlledCharacter() ? TEXT("true") : TEXT("false"),
				*EvaluatedChooserPath,
				static_cast<int32>(StateControllerPresentationStateForChooser),
				static_cast<int32>(RotationModeForChooser),
				static_cast<int32>(GaitIntentForChooser),
				static_cast<int32>(StateControllerOneShotFootForChooser),
				Data.Movement.GroundSpeed,
				bChooserUseHeavyLand ? TEXT("true") : TEXT("false"),
				bChooserIsLanding ? TEXT("true") : TEXT("false"),
				bChooserUseRunLightLand ? TEXT("true") : TEXT("false"),
				bChooserUseSprintLightLand ? TEXT("true") : TEXT("false"),
				bChooserUseStandLightLand ? TEXT("true") : TEXT("false"),
				bChooserUseRunHeavyLand ? TEXT("true") : TEXT("false"),
				bChooserUseSprintHeavyLand ? TEXT("true") : TEXT("false"),
				bChooserUseStandHeavyLand ? TEXT("true") : TEXT("false"),
				static_cast<int32>(CurrentOptimizationPolicy.Tier),
				CurrentOptimizationPolicy.bUseFarChooserRowsOnly ? TEXT("true") : TEXT("false"),
				RemotePolicy.bDisableLandChooserBeyondFarDistance ? TEXT("true") : TEXT("false"));
		}
		// A phase value can outlive a consumed movement edge for one snapshot. Only
		// the explicit Pivot chooser predicate is allowed to grant direct Pivot
		// ownership; otherwise a Start asset could be mislabeled as a Pivot.
		const bool bPivotRequestSuppressed =
			Data.LocomotionContext.PivotRequestRevision != 0 &&
			Data.LocomotionContext.PivotRequestRevision == StateControllerRuntime.GetPivot().SuppressedRequestRevision;
		const bool bPivotChooserRequest =
			OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot &&
			Data.LocomotionContext.bIsPivoting &&
			bChooserIsPivoting &&
			!bPivotRequestSuppressed;
		// Project_J's first authored Pivot is a direct Blend Stack asset. Do not
		// let imported GASP BranchIn metadata turn it into an entry-MM search.
		if (bPivotChooserRequest)
		{
			ChooserOutput.bUseMotionMatch = false;
		}
		if (SelectedAsset && ChooserOutput.bUseMotionMatch)
		{
			TArray<UObject*> AssetsToSearch;
			AssetsToSearch.Add(SelectedAsset);
			FPoseSearchBlueprintResult MMResult;
			FPoseSearchContinuingProperties ContinuingProperties;
			FPoseSearchFutureProperties FutureProperties;

			FName PoseHistoryName = FName("PoseHistory");
			if (!UPoseSearchLibrary::FindPoseHistoryNode(PoseHistoryName, this))
			{
				PoseHistoryName = FName("PoseSearchHistoryCollector");
				if (!UPoseSearchLibrary::FindPoseHistoryNode(PoseHistoryName, this))
				{
					PoseHistoryName = NAME_None;
				}
			}

			UPoseSearchLibrary::MotionMatch(
				this,
				AssetsToSearch,
				PoseHistoryName,
				ContinuingProperties,
				FutureProperties,
				MMResult);

			if (MMResult.SelectedAnim &&
				(ChooserOutput.MotionMatchCostLimit <= 0.0f || MMResult.SearchCost <= ChooserOutput.MotionMatchCostLimit))
			{
				SelectedAsset = Cast<UAnimationAsset>(const_cast<UObject*>(MMResult.SelectedAnim.Get()));
				ChooserOutput.StartTime = MMResult.SelectedTime;
			}
		}

		if (bIsJumpAirReselecting)
		{
			if (SelectedAsset)
			{
				const float MaxSafeStartTime = FMath::Max(0.0f, SelectedAsset->GetPlayLength() - 0.05f);
				const float PreservedStartTime = FMath::Clamp(
					FMath::Max(ChooserOutput.StartTime, SavedJumpAirElapsed),
					0.0f,
					MaxSafeStartTime);
				ChooserOutput.StartTime = PreservedStartTime;

				const double NowSeconds = AnimationClock.Seconds;
				StateControllerRuntime.AlignHoldToAssetTime(NowSeconds, PreservedStartTime);
				OneShot.TransitionElapsedTime = PreservedStartTime;
				OneShot.TransitionTimeRemaining = FMath::Max(SelectedAsset->GetPlayLength() - PreservedStartTime, 0.0f);

				const UProject_JLocomotionProfile* CurrentProfile = GetLocomotionProfile();
				const float ConfiguredBlendTime = CurrentProfile ? CurrentProfile->MotionMatchingSearchPolicy.AirJumpBlendTime : 0.15f;
				const float EffectiveAirBlendTime = ChooserOutput.BlendTime > 0.0f
					? ChooserOutput.BlendTime
					: (ConfiguredBlendTime > 0.0f ? ConfiguredBlendTime : 0.15f);
				ChooserOutput.BlendTime = EffectiveAirBlendTime;
				LastJumpAirBlendDuration = EffectiveAirBlendTime;
			}
			else
			{
				// Chooser Null Fallback Guard: 새 방향 에셋이 테이블에 없으면 기존 점프 에셋 유지
				SelectedAsset = const_cast<UAnimationAsset*>(CachedStateControllerSelectedAnimation.Get());
				ChooserOutput = CachedStateControllerSelectedAnimationOutput;
			}
			bIsJumpAirReselecting = false;
		}

		const EProject_JStateControllerPresentationState PreviousChooserPresentationState = CachedStateControllerPresentationState;
		const UAnimationAsset* PreviousChooserAsset = CachedStateControllerSelectedAnimation.Get();
		const float PreviousTurnIndex = CachedStateControllerTurnInPlaceIndex;
		const bool bTurnReselectRequested = StateControllerRuntime.IsTurnReselectRequested();
		const bool bIsFreshLandExit =
			PreviousChooserPresentationState == EProject_JStateControllerPresentationState::TransitionToLand &&
			OneShot.PresentationState == EProject_JStateControllerPresentationState::LocomotionLoop;
		if (bIsFreshLandExit)
		{
			Data.MotionMatching.bForceReselect = true;
		}

		CachedStateControllerChooserTable = ChooserTable;
		CachedStateControllerPresentationState = OneShot.PresentationState;
		CachedStateControllerPivotRequestRevision = Data.LocomotionContext.PivotRequestRevision;
		CachedStateControllerRotationMode = Data.LocomotionContext.RotationMode;
		CachedStateControllerGaitIntent = GaitIntentForChooser;
		CachedStateControllerStance = OneShot.Stance;
		CachedStateControllerStrafeDirection = OneShot.StrafeDirection;
		CachedStateControllerPreviousStrafeDirection = OneShot.PreviousStrafeDirection;
		CachedStateControllerOneShotFoot = OneShot.Foot;
		CachedStateControllerTurnInPlaceIndex = StateControllerTurnInPlaceIndexForChooser;
		bCachedStateControllerCombatMode = Data.Combat.bIsCombatMode;
		bCachedStateControllerFallOff = bStateControllerFallOffForChooser;
		bCachedStateControllerLandWasMoving = Data.Landing.bLandWasMoving;
		bCachedStateControllerLandWasSprinting = Data.Landing.bLandWasSprinting;
		bCachedStateControllerUseHeavyLand = Data.Landing.bUseHeavyLand;
		CachedStateControllerLandingPresentationRevision = Data.Landing.PresentationRevision;
		CachedStateControllerSelectedAnimation = SelectedAsset;
		CachedStateControllerSelectedAnimationOutput = ChooserOutput;
		bCachedStateControllerHasSelectedAnimation = CachedStateControllerSelectedAnimation != nullptr;
		// Contact/phase updates can cause the chooser evaluation path to run again
		// during the same Pivot request. A Pivot revision is a one-shot command:
		// only its first valid evaluation may restart the direct Blend Stack.
		// A newer Pivot revision still deliberately supersedes the active one.
		const bool bIsNewPivotCommit =
			bCachedStateControllerHasSelectedAnimation &&
			bPivotChooserRequest &&
			StateControllerRuntime.CommitPivot(
				Data.LocomotionContext.PivotRequestRevision,
				Data.LocomotionContext.MoveIntentRevision,
				Data.LocomotionContext.PivotPreviousMovementDirection,
				Data.LocomotionContext.PivotMoveIntentDirection);
		if (bIsNewPivotCommit)
		{
			// A Pivot can replace another one-shot without changing presentation
			// state. Capture its own camera baseline; movement uses the committed
			// semantic revision rather than aggregate IA_Move / previous CMC input.
			bHasStateControllerOneShotControlYaw = OwningPlayerCharacter && OwningPlayerCharacter->IsLocallyControlled();
			bHasStateControllerOneShotMoveInputYaw = false;
			if (bHasStateControllerOneShotControlYaw)
			{
				StateControllerOneShotControlYaw = OwningPlayerCharacter->GetControlRotation().Yaw;
			}
			if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
			{
				UE_LOG(LogProjectJPlayer, Display,
				TEXT("CombatStrafeRunPivotCommitted Actor=%s PivotRev=%d AnimInstance=%s Primary=%s Asset=%s Start=%.3f Blend=%.3f"),
				*GetNameSafe(OwningCharacter), StateControllerRuntime.GetPivot().RequestRevision,
				*GetPathName(), IsPrimaryMeshAnimInstance() ? TEXT("true") : TEXT("false"),
				*GetNameSafe(SelectedAsset), ChooserOutput.StartTime, ChooserOutput.BlendTime);
			}
		}
		else if (OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot &&
			Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("CombatStrafeRunPivotNotCommitted Actor=%s PivotRev=%d Reason=%s Phase=%d DerivedPivot=%s ChooserPivot=%s Asset=%s"),
				*GetNameSafe(OwningCharacter), Data.LocomotionContext.PivotRequestRevision,
				bPivotRequestSuppressed ? TEXT("RedirectSuppressed") : TEXT("ChooserNotPivot"),
				static_cast<int32>(OneShot.PhaseFamily),
				Data.LocomotionContext.bIsPivoting ? TEXT("true") : TEXT("false"),
				bChooserIsPivoting ? TEXT("true") : TEXT("false"),
				*GetNameSafe(SelectedAsset));
		}
		++StateControllerChooserSelectionRevision;
		// The AnimGraph consumes this pulse from the Blend Stack's On Update
		// binding. It is deliberately not based on asset identity: a repeated
		// 90-degree TIP must be allowed to restart from time zero.
		OneShot.bForceBlendNextUpdate = bCachedStateControllerHasSelectedAnimation &&
			(!bPivotChooserRequest || bIsNewPivotCommit);
		if (Project_J::MotionMatchingCVars::GetTurnInPlaceTraceMode() > 0 &&
			(PreviousChooserPresentationState == EProject_JStateControllerPresentationState::TurnInPlace ||
			 OneShot.PresentationState == EProject_JStateControllerPresentationState::TurnInPlace))
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("TIPTrace Stage=Chooser T=%.3f Actor=%s Local=%d PrevState=%d State=%d Seq=%d Bucket=%d Rev=%d PrevAsset=%s Asset=%s PrevTurnIndex=%.0f TurnIndex=%.0f Reselect=%d Hold=%.3f Start=%.3f Blend=%.3f ForceBlend=%d Path=%s"),
				GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f, *GetNameSafe(OwningCharacter),
				IsLocallyControlledCharacter() ? 1 : 0,
				static_cast<int32>(PreviousChooserPresentationState),
				static_cast<int32>(OneShot.PresentationState),
				Data.LocomotionContext.TurnInPlaceSequence,
				static_cast<int32>(Data.LocomotionContext.TurnInPlaceDirectionBucket),
				StateControllerChooserSelectionRevision,
				*GetNameSafe(PreviousChooserAsset), *GetNameSafe(SelectedAsset),
				PreviousTurnIndex, StateControllerTurnInPlaceIndexForChooser,
				bTurnReselectRequested ? 1 : 0, OneShot.TransitionElapsedTime,
				ChooserOutput.StartTime, ChooserOutput.BlendTime,
				OneShot.bForceBlendNextUpdate ? 1 : 0, *EvaluatedChooserPath);
		}

		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
			TEXT("StateControllerChooser Actor=%s Rev=%d State=%d LandEpoch=%d LandMoving=%s LandSprint=%s LandHeavy=%s Rotation=%d Gait=%d StartGait=%d StartCommitted=%s PivotPhase=%s DerivedPivot=%s ChooserPivot=%s Stance=%d StrafeDir=%d PrevStrafeDir=%d StrafeAngle=%.1f HasStrafeAngle=%s OneShotFoot=%d FallOff=%s ContactL=%.2f ContactR=%.2f HasContactCurves=%s Combat=%s MMMoving=%s Input=%s InputFacingDelta=%.1f StopVelocityDelta=%.1f FutureSpeed=%.1f Accelerating=%s Asset=%s Length=%.3f Start=%.3f Loop=%s Blend=%.3f UseMM=%s Tags=%d HoldElapsed=%.3f HoldRemaining=%.3f AlmostComplete=%s ForceBlend=%s"),
				*GetNameSafe(OwningCharacter),
				StateControllerChooserSelectionRevision,
				static_cast<int32>(OneShot.PresentationState),
				Data.Landing.PresentationRevision,
				Data.Landing.bLandWasMoving ? TEXT("true") : TEXT("false"),
				Data.Landing.bLandWasSprinting ? TEXT("true") : TEXT("false"),
				Data.Landing.bUseHeavyLand ? TEXT("true") : TEXT("false"),
				static_cast<int32>(Data.LocomotionContext.RotationMode),
				static_cast<int32>(GaitIntentForChooser),
				static_cast<int32>(StateControllerStartGaitForChooser),
				bStateControllerStartGaitCommitted ? TEXT("true") : TEXT("false"),
				OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot ? TEXT("true") : TEXT("false"),
				Data.LocomotionContext.bIsPivoting ? TEXT("true") : TEXT("false"),
				bChooserIsPivoting ? TEXT("true") : TEXT("false"),
				static_cast<int32>(OneShot.Stance),
				static_cast<int32>(OneShot.StrafeDirection),
				static_cast<int32>(OneShot.PreviousStrafeDirection),
				OneShot.StrafeDirectionAngle,
				OneShot.bHasStrafeDirectionAngle ? TEXT("true") : TEXT("false"),
				static_cast<int32>(OneShot.Foot),
				bStateControllerFallOffForChooser ? TEXT("true") : TEXT("false"),
				CachedStateControllerLeftFootContact,
				CachedStateControllerRightFootContact,
				bHasStateControllerFootContactCurves ? TEXT("true") : TEXT("false"),
				Data.Combat.bIsCombatMode ? TEXT("true") : TEXT("false"),
				Data.LocomotionContext.bIsMotionMatchingMoving ? TEXT("true") : TEXT("false"),
				Data.Input.bHasMoveInput ? TEXT("true") : TEXT("false"),
				StateControllerInputFacingDeltaYawForChooser,
				StateControllerStopVelocityDeltaYawForChooser,
				Data.Movement.FutureTrajectorySpeed,
				Data.Movement.bIsAccelerating ? TEXT("true") : TEXT("false"),
				CachedStateControllerSelectedAnimation ? *CachedStateControllerSelectedAnimation->GetName() : TEXT("None"),
				CachedStateControllerSelectedAnimation ? CachedStateControllerSelectedAnimation->GetPlayLength() : 0.0f,
				ChooserOutput.StartTime,
				ShouldStateControllerPresentationLoop(OneShot.PresentationState) ? TEXT("true") : TEXT("false"),
				ChooserOutput.BlendTime,
				ChooserOutput.bUseMotionMatch ? TEXT("true") : TEXT("false"),
				ChooserOutput.Tags.Num(),
				OneShot.TransitionElapsedTime,
				OneShot.TransitionTimeRemaining,
				OneShot.bTransitionAnimationAlmostComplete ? TEXT("true") : TEXT("false"),
				OneShot.bForceBlendNextUpdate ? TEXT("true") : TEXT("false"));
		}
	}

	OneShot.SelectedAnimation = CachedStateControllerSelectedAnimation;
	OneShot.SelectedAnimationOutput = CachedStateControllerSelectedAnimationOutput;
	OneShot.bHasSelectedAnimation = bCachedStateControllerHasSelectedAnimation;
	// The logical air state has no output pose. Its selected FallLoop therefore
	// uses the same external stack as Jump/FallOff, until physical air ends.
	// Idle/Cycle loops keep the regular MM owner; landing's legacy air flag is
	// not physical air (Data.Air was collected from CMC::IsFalling on the GT).
	const bool bExternalAirLoop =
		OneShot.PresentationState == EProject_JStateControllerPresentationState::InAirLoop &&
		Data.Air.bIsInAir && Data.LocomotionMode == EProject_JAnimationLocomotionMode::OnFoot;
	OneShot.bShouldOverrideMotionMatching = OneShot.bHasSelectedAnimation &&
		((IsTransitionState(OneShot.PresentationState) && !OneShot.bSelectedAnimationShouldLoop) || bExternalAirLoop);
	OneShot.bShouldEnableCombatStrafeOrientationWarping =
		IsTransitionState(OneShot.PresentationState) &&
		!OneShot.bSelectedAnimationShouldLoop &&
		Data.Combat.bIsCombatMode &&
		OneShot.RotationMode == EProject_JLocomotionRotationMode::Strafe &&
		OneShot.bHasStrafeDirectionAngle;
	OneShot.SelectionRevision = StateControllerChooserSelectionRevision;
}

bool UProject_JCharacterAnimInstance::ShouldCancelLocalOneShotForInput(bool bCommittedPivot, float MouseCancelAngle, float MoveCancelAngle)
{
	const auto* Profile = GetLocomotionProfile();
	if (!bCommittedPivot && Profile && Profile->TransitionPolicy.bEnableOneShotInputResponse &&
		Project_J::MotionMatchingCVars::ShouldUseOneShotInputResponse() && bHasStateControllerOneShotControlYaw)
	{
		UpdateLocomotionSteeringData(ThreadSafeData);
		FProject_JOneShotInputResponse::FInput Input;
		Input.bCanSteer = ThreadSafeData.bLocomotionSteeringEnabled;
		Input.bHasMoveReference = bHasStateControllerOneShotMoveInputYaw;
		const FVector Move = OwningPlayerCharacter->GetLastMovementInputVector();
		Input.bHasMoveInput = Move.SizeSquared2D() > UE_KINDA_SMALL_NUMBER;
		Input.EntryControlYaw = StateControllerOneShotControlYaw; Input.EntryMoveYaw = StateControllerOneShotMoveInputYaw;
		Input.ControlYaw = OwningPlayerCharacter->GetControlRotation().Yaw;
		Input.MoveYaw = Input.bHasMoveInput ? Move.Rotation().Yaw : Input.EntryMoveYaw;
		Input.MouseCancelAngle = MouseCancelAngle; Input.MoveCancelAngle = MoveCancelAngle;
		Input.SteeringLimit = ThreadSafeData.LocomotionSteeringMaxYawError;
		const float TargetYaw = ThreadSafeData.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe
			? Input.ControlYaw : Input.MoveYaw;
		const auto& Feedback = GetCompletedTurnFeedback();
		const bool bVisualFresh = Feedback.IsVisualFresh(GFrameCounter,
			GetWorld() ? GetWorld()->GetTimeSeconds() : 0, ThreadSafeData.LocomotionContext.RotationMode, ThreadSafeData.LocomotionContext.GaitIntent);
		const float VisualYaw = bVisualFresh ? Feedback.VisualFacingYaw : OwningPlayerCharacter->GetActorRotation().Yaw;
		Input.FacingError = FMath::FindDeltaAngleDegrees(VisualYaw, TargetYaw);
		if (ThreadSafeData.Movement.GroundSpeed > Profile->TransitionPolicy.MovingSpeedThreshold && Input.bHasMoveInput)
			Input.PathError = FMath::FindDeltaAngleDegrees(ThreadSafeData.Movement.Velocity.Rotation().Yaw, Input.MoveYaw);
		auto Response = FProject_JOneShotInputResponse::Resolve(Input);
		FProject_JOneShotCurvePolicy::FInput Curve;
		const auto* Movement = OwningPlayerCharacter->GetCharacterMovement();
		const auto& Data = ThreadSafeData;
		const auto& Shot = Data.OneShotPresentation;
		const auto Mode = Data.LocomotionContext.RotationMode;
		const auto Gait = Data.LocomotionContext.GaitIntent;
		const bool bCombat = Mode == EProject_JLocomotionRotationMode::Strafe && Data.Combat.bIsCombatMode;
		const auto* Set = bCombat ? OwningPlayerCharacter->GetCombatStrafeMotionMatchingAssetSet() : OwningPlayerCharacter->GetMotionMatchingAssetSet();
		// Native input resolution precedes chooser publication: a retained Start
		// may already have semantic Cycle and bRequested/override cleared here.
		// Its held presentation is the actual owner at this stage.
		Curve.bEligible = !Response.bRelease && Input.bCanSteer && Input.bHasMoveInput &&
			StateControllerRuntime.GetHeldState() == Shot.PresentationState && !StateControllerRuntime.HasCommittedPivot() &&
			Data.LocomotionContext.PhaseFamily != EProject_JLocomotionPhaseFamily::Pivot &&
			Movement && (Mode == EProject_JLocomotionRotationMode::OrientToMovement || bCombat) &&
			(Gait == EProject_JLocomotionGaitIntent::Run || Gait == EProject_JLocomotionGaitIntent::Sprint) &&
			(Gait != EProject_JLocomotionGaitIntent::Sprint || OwningPlayerCharacter->IsSprintInputDirectionAllowed()) &&
			Data.Movement.bHasFutureTrajectoryVelocity && Set && Set->GetDatabaseFamily(Gait).Cycle &&
			Data.Movement.SnapshotFrame <= GFrameCounter && GFrameCounter - Data.Movement.SnapshotFrame <= 2 &&
			Movement->Velocity.Size2D() > Profile->TransitionPolicy.IdleSpeedThreshold;
		Curve.OwnerRevision = StateControllerChooserSelectionRevision;
		Curve.Mode = static_cast<int32>(Mode); Curve.Gait = static_cast<int32>(Gait);
		Curve.TrajectoryRevision = Data.Movement.TrajectoryResetRevision;
		Curve.Now = AnimationClock.Seconds;
		Curve.Position = OwningPlayerCharacter->GetActorLocation();
		Curve.Velocity = Movement ? Movement->Velocity : FVector::ZeroVector;
		Curve.FutureVelocity = Data.Movement.FutureTrajectoryVelocity;
		Curve.bHasRequestedHeading = Input.bHasMoveInput;
		Curve.RequestedHeading = Input.MoveYaw;
		const bool bLand = Shot.PresentationState == EProject_JStateControllerPresentationState::TransitionToLand;
		Curve.bCanExit = bLand ? LocomotionAnimStateComponent && LocomotionAnimStateComponent->bCanExitLanding :
			Movement && (Shot.bTransitionAnimationAlmostComplete ||
				(Movement->Velocity.Size2D() >= Movement->GetMaxSpeed() * Profile->TransitionPolicy.StartCompletionSpeedFraction &&
				Data.Movement.PredictedSpeedGain <= Profile->TransitionPolicy.StartSpeedGainThreshold));
		FProject_JOneShotCurvePolicy::FSettings CurveSettings;
		CurveSettings.TravelDistance = Profile->TransitionPolicy.OneShotCurveTravelDistance;
		CurveSettings.HeadingSweep = Profile->TransitionPolicy.OneShotCurveHeadingSweep;
		CurveSettings.DirectionConsistency = Profile->TransitionPolicy.OneShotCurveDirectionConsistency;
		CurveSettings.Sustained = Profile->TransitionPolicy.ContinuousCurve;
		const auto CurveResponse = OneShotCurvePolicy.Update(Curve, CurveSettings);
		if (CurveResponse.bYield)
		{
			Response = {true, TEXT("ContinuousCurveToCycle")};
			bOneShotCurveReleasedThisUpdate = true;
		}
		if (Response.bRelease && !CurveResponse.bYield && Input.bCanSteer && LocomotionAnimStateComponent)
		{
			// NativeUpdate can precede the component tick. Probe the existing event
			// using this actual input, rather than first searching last frame's Cycle.
			ThreadSafeData.TurnRequest.bAcuteReturnReady = LocomotionAnimStateComponent->ProbeOneShotTurnReturn(Move,
				ThreadSafeData.LocomotionContext.RotationMode, ThreadSafeData.LocomotionContext.GaitIntent);
		}
		if (Project_J::AnimationFlowDebug::ShouldCapture(OwningCharacter) && (Response.bRelease || GFrameCounter % 12 == 0))
		{
			UE_LOG(LogProjectJPlayer, Display, TEXT("AnimFlow InputResponse Frame=%llu Actor=%s Release=%d Reason=%s CanSteer=%d FacingError=%.1f PathError=%.1f Limit=%.1f VisualFresh=%d"),
				GFrameCounter, *GetNameSafe(OwningCharacter), Response.bRelease, Response.Reason, Input.bCanSteer,
				Input.FacingError, Input.PathError, Input.SteeringLimit, bVisualFresh);
			UE_LOG(LogProjectJPlayer, Display, TEXT("AnimFlow OneShotCurve Eligible=%d CanExit=%d Yield=%d Distance=%.1f Sweep=%.1f Consistency=%.2f"),
				Curve.bEligible, Curve.bCanExit, CurveResponse.bYield, CurveResponse.Distance, CurveResponse.Sweep, CurveResponse.Consistency);
		}
		if (Response.bRelease) Project_J::AnimationFlowDebug::Decision(OwningCharacter, TEXT("PresentationInput"), Response.Reason);
		if (!bHasStateControllerOneShotMoveInputYaw && Input.bHasMoveInput)
		{
			StateControllerOneShotMoveInputYaw = Input.MoveYaw;
			// First movement establishes the key-space basis, not the old landing camera.
			StateControllerOneShotControlYaw = Input.ControlYaw;
			bHasStateControllerOneShotMoveInputYaw = true;
		}
		return Response.bRelease;
	}
	OneShotCurvePolicy.Reset();
	bool bShouldCancelOneShot = false;
	if (bHasStateControllerOneShotControlYaw)
	{
		const float ControlYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(
			StateControllerOneShotControlYaw,
			OwningPlayerCharacter->GetControlRotation().Yaw));
		if (ControlYawDelta >= MouseCancelAngle)
		{
			Project_J::AnimationFlowDebug::Decision(OwningCharacter, TEXT("PresentationInput"), TEXT("ControlYawThreshold"));
			bShouldCancelOneShot = true;
		}
	}

	// Pivot movement was already reconciled from the same resolved intent used
	// for selection. Raw input can disagree during opposed-key overlap.
	if (!bShouldCancelOneShot && !bCommittedPivot)
	{
		const FVector CurrentInput = OwningPlayerCharacter->GetLastMovementInputVector();
		const bool bHasCurrentInput = CurrentInput.SizeSquared2D() > UE_KINDA_SMALL_NUMBER;

		if (bHasStateControllerOneShotMoveInputYaw && bHasCurrentInput)
		{
			const float CurrentMoveInputYaw = CurrentInput.ToOrientationRotator().Yaw;
			const float MoveInputYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(
				StateControllerOneShotMoveInputYaw,
				CurrentMoveInputYaw));
			if (MoveInputYawDelta >= MoveCancelAngle)
			{
				Project_J::AnimationFlowDebug::Decision(OwningCharacter, TEXT("PresentationInput"), TEXT("MoveYawThreshold"));
				bShouldCancelOneShot = true;
			}
		}
		else if (!bHasStateControllerOneShotMoveInputYaw && bHasCurrentInput)
		{
			StateControllerOneShotMoveInputYaw = CurrentInput.ToOrientationRotator().Yaw;
			bHasStateControllerOneShotMoveInputYaw = true;
		}
	}
	return bShouldCancelOneShot;
}


void UProject_JCharacterAnimInstance::ResetStateControllerChooserCache()
{
	CachedStateControllerChooserTable.Reset();
	CachedStateControllerSelectedAnimation = nullptr;
	CachedStateControllerSelectedAnimationOutput = FProject_JStateControllerChooserOutput();
	bCachedStateControllerHasSelectedAnimation = false;
}
