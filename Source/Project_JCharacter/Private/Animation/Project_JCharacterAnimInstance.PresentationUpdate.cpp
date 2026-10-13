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

void UProject_JCharacterAnimInstance::UpdatePresentationFromSnapshot()
{
	if (ThreadSafeData.Mount.bIsMounted != bWasMountedForStateController)
	{
		bWasMountedForStateController = ThreadSafeData.Mount.bIsMounted;
		FProject_JStateControllerRuntime::FMountBoundary Boundary;
		Boundary.PivotRequestRevision = ThreadSafeData.LocomotionContext.PivotRequestRevision;
		Boundary.LandingRevision = ThreadSafeData.Landing.PresentationRevision;
		Boundary.TurnSequence = ThreadSafeData.LocomotionContext.TurnInPlaceSequence;
		Boundary.bLanding = ThreadSafeData.Landing.bIsLanding;
		Boundary.bLocalTurn = IsLocallyControlledCharacter();
		Boundary.bHasMoveInput = ThreadSafeData.Input.bHasMoveInput;
		StateControllerRuntime.OnMountBoundary(Boundary);
		ResetStateControllerChooserCache();
		++StateControllerChooserSelectionRevision;
		ThreadSafeData.OneShotPresentation.PresentationState = EProject_JStateControllerPresentationState::Disabled;
		ThreadSafeData.OneShotPresentation.bRequested = false;
		if (ThreadSafeData.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot)
		{
			ThreadSafeData.LocomotionContext.bIsPivoting = false;
			ThreadSafeData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
			ThreadSafeData.MotionMatching.SelectionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
		}
		ThreadSafeData.MotionMatching.bForceReselect = true;
	}
	// A full-body montage owns the final pose and must not leave a committed Pivot
	// cached beneath it for later resurrection. Upper-body overlays have zero
	// weight here and intentionally do not interrupt locomotion presentation.
	if (StateControllerRuntime.GetPivot().RequestRevision != 0 &&
		ThreadSafeData.ProceduralIK.FullBodyMontageWeight > KINDA_SMALL_NUMBER)
	{
		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("CombatStrafeRunPivotCancelled Actor=%s PivotRev=%d Reason=FullBodyMontage Weight=%.2f"),
				*GetNameSafe(OwningCharacter), StateControllerRuntime.GetPivot().RequestRevision,
				ThreadSafeData.ProceduralIK.FullBodyMontageWeight);
		}
		StateControllerRuntime.CancelPivotAndHold(AnimationClock.Seconds);
		ThreadSafeData.OneShotPresentation.PresentationState = EProject_JStateControllerPresentationState::Disabled;
		ThreadSafeData.OneShotPresentation.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
		ThreadSafeData.OneShotPresentation.bRequested = false;
		ThreadSafeData.LocomotionContext.bIsPivoting = false;
		ThreadSafeData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
		ThreadSafeData.MotionMatching.SelectionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	}
	// A full-body combat draw/sheathe montage owns the final pose. Upper-body
	// variants leave locomotion active, including an authored Stop on input release.
	// Do not let a direct Land asset cached beneath a full-body montage resume
	// when the montage blends out.
	// This is deliberately presentation-only: the locomotion component's landing
	// event, CharacterMovement, and replicated movement state remain untouched.
	const bool bCombatPresentationTransitionActive =
		ThreadSafeData.Combat.bIsPlayingCombatIntro || ThreadSafeData.Combat.bIsPlayingCombatOutro;
	const bool bCombatPresentationTransitionStarted =
		bCombatPresentationTransitionActive && !bWasPlayingCombatPresentationTransitionForStateController;
	const bool bCombatPresentationTransitionEnded =
		!bCombatPresentationTransitionActive && bWasPlayingCombatPresentationTransitionForStateController;
	bWasPlayingCombatPresentationTransitionForStateController = bCombatPresentationTransitionActive;

	// Full-body action montages (attacks, skills, dodges) also own the character pose.
	// Discard any held ground one-shots and suppress new Start/Stop one-shots while active.
	const bool bFullBodyActionMontageActive =
		ThreadSafeData.ProceduralIK.FullBodyMontageWeight > 0.05f ||
		ThreadSafeData.Combat.bIsAttacking ||
		ThreadSafeData.Combat.bIsDodging ||
		ThreadSafeData.Combat.bIsHitReacting;
	const bool bFullBodyActionMontageStarted =
		bFullBodyActionMontageActive && !bWasPlayingFullBodyMontageForStateController;
	const bool bFullBodyActionMontageEnded =
		!bFullBodyActionMontageActive && bWasPlayingFullBodyMontageForStateController;
	bWasPlayingFullBodyMontageForStateController = bFullBodyActionMontageActive;

	if (bFullBodyActionMontageStarted)
	{
		StateControllerRuntime.OnFullBodyActionStart(AnimationClock.Seconds);
		ResetStateControllerChooserCache();
		StateControllerRuntime.ClearTurnReselect();
		bHasStateControllerOneShotControlYaw = false;
		bHasStateControllerOneShotMoveInputYaw = false;
		bHasStateControllerRemoteStartReference = false;
		bHasStateControllerRemoteStartMoveYaw = false;
		++StateControllerChooserSelectionRevision;
	}

	if (bFullBodyActionMontageEnded)
	{
		LastFullBodyMontageEndedAtSeconds = AnimationClock.Seconds;
		StateControllerRuntime.OnFullBodyActionEnd(AnimationClock.Seconds);
		ResetStateControllerChooserCache();
		++StateControllerChooserSelectionRevision;
		ThreadSafeData.MotionMatching.bForceReselect = true;
	}

	if (bCombatPresentationTransitionActive &&
		(bFullBodyActionMontageStarted ||
			(bCombatPresentationTransitionStarted && bFullBodyActionMontageActive)))
	{
		const bool bDiscardingPhysicalLanding = ThreadSafeData.Landing.bIsLanding;
		const bool bDiscardingHeldLand =
			StateControllerRuntime.GetHeldState() == EProject_JStateControllerPresentationState::TransitionToLand ||
			ThreadSafeData.OneShotPresentation.PresentationState == EProject_JStateControllerPresentationState::TransitionToLand;
		bSuppressPreTransitionLandingPresentationUntilLandingEnds =
			bDiscardingPhysicalLanding || bDiscardingHeldLand;

		// Invalidate both the logical hold and its immutable asset cache.  Merely
		// disabling the Blend Stack branch would leave the old asset eligible to
		// revive on the first post-montage update.
		StateControllerRuntime.OnCombatPresentationBoundary(
			AnimationClock.Seconds, ThreadSafeData.Landing.PresentationRevision,
			bDiscardingPhysicalLanding || bDiscardingHeldLand);
		ResetStateControllerChooserCache();
		StateControllerRuntime.ClearTurnReselect();
		bHasStateControllerOneShotControlYaw = false;
		bHasStateControllerOneShotMoveInputYaw = false;
		bHasStateControllerRemoteStartReference = false;
		bHasStateControllerRemoteStartMoveYaw = false;
		++StateControllerChooserSelectionRevision;

		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("StateControllerCombatPresentationReset: Intro=%s Outro=%s HeldLand=%s PhysicalLanding=%s PreviousAssetDiscarded=true"),
				ThreadSafeData.Combat.bIsPlayingCombatIntro ? TEXT("true") : TEXT("false"),
				ThreadSafeData.Combat.bIsPlayingCombatOutro ? TEXT("true") : TEXT("false"),
				bDiscardingHeldLand ? TEXT("true") : TEXT("false"),
				bDiscardingPhysicalLanding ? TEXT("true") : TEXT("false"));
		}
	}

	const bool bLandingPresentationStillActive =
		ThreadSafeData.Landing.bIsLanding ||
		ThreadSafeData.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Landing;
	if (bSuppressPreTransitionLandingPresentationUntilLandingEnds && !bLandingPresentationStillActive)
	{
		bSuppressPreTransitionLandingPresentationUntilLandingEnds = false;
		ThreadSafeData.MotionMatching.bForceReselect = true;
	}
	// At either montage's blend-out, the locomotion profile may have changed
	// (Combat Strafe -> OTM on sheathe, or OTM -> Combat Strafe on draw).  Ask
	// regular MM for the current profile immediately even when no Land was
	// active, rather than relying on its normal search throttle.
	if (bCombatPresentationTransitionEnded)
	{
		ThreadSafeData.MotionMatching.bForceReselect = true;
	}

	const bool bSuppressDirectGroundOneShotsForCombatPresentation =
		bSuppressPreTransitionLandingPresentationUntilLandingEnds ||
		bFullBodyActionMontageActive;
	UpdateReleasedLandingMotionContext(ThreadSafeData);
	if (bSuppressDirectGroundOneShotsForCombatPresentation && !ThreadSafeData.Air.bIsInAir)
	{
		// Keep regular MM current beneath a full-body montage. On blend-out it therefore
		// resolves from the *current* combat idle/strafe context rather than a
		// stale non-combat Land, Start, Stop, or Pivot direct asset.
		const bool bMoving = ThreadSafeData.LocomotionContext.bIsMotionMatchingMoving;
		const EProject_JStateControllerPresentationState FallbackPresentationState = bMoving
			? EProject_JStateControllerPresentationState::LocomotionLoop
			: EProject_JStateControllerPresentationState::IdleLoop;
		const EProject_JLocomotionPhaseFamily FallbackPhaseFamily = bMoving
			? EProject_JLocomotionPhaseFamily::Cycle
			: EProject_JLocomotionPhaseFamily::Idle;
		ThreadSafeData.OneShotPresentation.PresentationState = FallbackPresentationState;
		ThreadSafeData.OneShotPresentation.PhaseFamily = FallbackPhaseFamily;
		ThreadSafeData.OneShotPresentation.bRequested = false;
		ThreadSafeData.LocomotionContext.PhaseFamily = FallbackPhaseFamily;
		ThreadSafeData.MotionMatching.SelectionContext.PhaseFamily = FallbackPhaseFamily;
		// Re-query only at the presentation boundaries.  The fallback context is
		// still published every frame, but repeatedly forcing a Pose Search while
		// a montage is active would be needless per-frame work.
		ThreadSafeData.MotionMatching.bForceReselect =
			ThreadSafeData.MotionMatching.bForceReselect || bCombatPresentationTransitionStarted;
		StateControllerRuntime.SetFallbackHold(FallbackPresentationState, AnimationClock.Seconds);
	}
	const EProject_JStateControllerPresentationState PreviousStateControllerPresentationState =
		StateControllerPresentationStateForChooser;
	EProject_JStateControllerPresentationState CurrentStateControllerPresentationState =
		ThreadSafeData.OneShotPresentation.PresentationState;
	// CharacterMovement can report Falling one animation update before the
	// locomotion-state component publishes bIsFallOffStart. Without this bridge,
	// the first airborne frame selects InAirLoop and visibly blends FallLoop into
	// the authored FallOff one-shot on the next update. Infer FallOff only on a
	// fresh non-jump ground-to-air boundary; normal Jump and ongoing air states
	// retain their authored state-component policy.
	const bool bWasInAirPresentationState =
		PreviousStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToInAir ||
		PreviousStateControllerPresentationState == EProject_JStateControllerPresentationState::InAirLoop;
	const bool bInferFallOffFromFreshAirEntry =
		CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::InAirLoop &&
		!bWasInAirPresentationState &&
		ThreadSafeData.Air.bIsInAir &&
		!ThreadSafeData.Air.bIsJumping &&
		!ThreadSafeData.Landing.bIsLanding;
	if (bInferFallOffFromFreshAirEntry)
	{
		CurrentStateControllerPresentationState = EProject_JStateControllerPresentationState::TransitionToInAir;
		ThreadSafeData.OneShotPresentation.PresentationState = CurrentStateControllerPresentationState;
	}
	const bool bEnteringStateControllerTurnCancellableOneShot =
		PreviousStateControllerPresentationState != CurrentStateControllerPresentationState &&
		(CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToLocomotion ||
			CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToLand);
	if (bEnteringStateControllerTurnCancellableOneShot && OwningPlayerCharacter)
	{
		if (OwningPlayerCharacter->IsLocallyControlled())
		{
			StateControllerOneShotControlYaw = OwningPlayerCharacter->GetControlRotation().Yaw;
			bHasStateControllerOneShotControlYaw = true;

			const FVector LastInput = OwningPlayerCharacter->GetLastMovementInputVector();
			if (LastInput.SizeSquared2D() > UE_KINDA_SMALL_NUMBER)
			{
				StateControllerOneShotMoveInputYaw = LastInput.ToOrientationRotator().Yaw;
				bHasStateControllerOneShotMoveInputYaw = true;
			}
			else
			{
				bHasStateControllerOneShotMoveInputYaw = false;
			}
		}
		else if (CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToLocomotion)
		{
			StateControllerRemoteStartActorYaw = OwningPlayerCharacter->GetActorRotation().Yaw;
			bHasStateControllerRemoteStartReference = true;

			FVector HorizontalVelocity = OwningPlayerCharacter->GetVelocity();
			HorizontalVelocity.Z = 0.0f;
			const float RemoteMoveSpeedThreshold = LocomotionAnimStateComponent
				? LocomotionAnimStateComponent->RemoteMoveSpeedThreshold
				: 3.0f;
			bHasStateControllerRemoteStartMoveYaw =
				HorizontalVelocity.SizeSquared2D() > FMath::Square(RemoteMoveSpeedThreshold);
			if (bHasStateControllerRemoteStartMoveYaw)
			{
				StateControllerRemoteStartMoveYaw = HorizontalVelocity.ToOrientationRotator().Yaw;
			}
		}
	}

	// A committed Pivot uses ReconcilePivot's resolved move-intent revision for
	// movement redirects. Only camera yaw shares Start's local cancel path.
	const bool bIsCommittedPivotOneShot =
		StateControllerRuntime.GetPivot().RequestRevision != 0 &&
		ThreadSafeData.OneShotPresentation.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot;
	const bool bIsTurnCancellableOneShot =
		(ThreadSafeData.OneShotPresentation.PhaseFamily != EProject_JLocomotionPhaseFamily::Pivot || bIsCommittedPivotOneShot) &&
		(CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToLocomotion ||
			CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToLand);

	const UProject_JLocomotionProfile* LocomotionProfile = GetLocomotionProfile();
	const bool bIsLandOneShot =
		CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToLand;
	const float EffectiveMouseTurnCancelAngle = LocomotionProfile
		? (bIsLandOneShot ? LocomotionProfile->TransitionPolicy.LandMouseTurnCancelAngle : LocomotionProfile->TransitionPolicy.StartMouseTurnCancelAngle)
		: (bIsLandOneShot ? 25.0f : StateControllerOneShotMouseTurnCancelAngle);
	const float EffectiveMoveInputCancelAngle = LocomotionProfile
		? LocomotionProfile->TransitionPolicy.StartMoveInputCancelAngle
		: 30.0f;

	bOneShotCurveReleasedThisUpdate = false;
	bool bShouldCancelOneShot = bIsTurnCancellableOneShot &&
		OwningPlayerCharacter && OwningPlayerCharacter->IsLocallyControlled() &&
		ShouldCancelLocalOneShotForInput(bIsCommittedPivotOneShot, EffectiveMouseTurnCancelAngle, EffectiveMoveInputCancelAngle);

	float RemoteStartActorYawDelta = 0.0f;
	float RemoteStartMoveYawDelta = 0.0f;
	const bool bRemoteHeldStart =
		ThreadSafeData.OneShotPresentation.PhaseFamily != EProject_JLocomotionPhaseFamily::Pivot &&
		CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToLocomotion &&
		OwningPlayerCharacter &&
		!OwningPlayerCharacter->IsLocallyControlled();
	if (!bShouldCancelOneShot && bRemoteHeldStart)
	{
		const float RemoteStartExitAngle = LocomotionProfile
			? LocomotionProfile->RemoteVisualPolicy.RemoteStartTurnExitAngle
			: 15.0f;
		if (!bHasStateControllerRemoteStartReference)
		{
			StateControllerRemoteStartActorYaw = OwningPlayerCharacter->GetActorRotation().Yaw;
			bHasStateControllerRemoteStartReference = true;
		}
		else
		{
			RemoteStartActorYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(
				StateControllerRemoteStartActorYaw,
				OwningPlayerCharacter->GetActorRotation().Yaw));
			bShouldCancelOneShot = RemoteStartActorYawDelta >= RemoteStartExitAngle;
		}

		FVector HorizontalVelocity = OwningPlayerCharacter->GetVelocity();
		HorizontalVelocity.Z = 0.0f;
		const float RemoteMoveSpeedThreshold = LocomotionAnimStateComponent
			? LocomotionAnimStateComponent->RemoteMoveSpeedThreshold
			: 3.0f;
		if (HorizontalVelocity.SizeSquared2D() > FMath::Square(RemoteMoveSpeedThreshold))
		{
			const float CurrentMoveYaw = HorizontalVelocity.ToOrientationRotator().Yaw;
			if (bHasStateControllerRemoteStartMoveYaw)
			{
				RemoteStartMoveYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(
					StateControllerRemoteStartMoveYaw,
					CurrentMoveYaw));
				bShouldCancelOneShot = bShouldCancelOneShot || RemoteStartMoveYawDelta >= RemoteStartExitAngle;
			}
			else
			{
				StateControllerRemoteStartMoveYaw = CurrentMoveYaw;
				bHasStateControllerRemoteStartMoveYaw = true;
			}
		}

		if (bShouldCancelOneShot && Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("StateControllerRemoteStartTurnExit Actor=%s ActorDelta=%.1f MoveDelta=%.1f Threshold=%.1f"),
				*GetNameSafe(OwningPlayerCharacter),
				RemoteStartActorYawDelta,
				RemoteStartMoveYawDelta,
				RemoteStartExitAngle);
		}
	}
	const bool bRemoteResponsiveStartExit =
		OwningPlayerCharacter &&
		!OwningPlayerCharacter->IsLocallyControlled() &&
		ThreadSafeData.Ground.StartResponsiveExitRevision != LastHandledStartResponsiveExitRevision;
	if (bRemoteResponsiveStartExit)
	{
		LastHandledStartResponsiveExitRevision = ThreadSafeData.Ground.StartResponsiveExitRevision;
		bShouldCancelOneShot = bShouldCancelOneShot ||
			StateControllerRuntime.GetHeldState() == EProject_JStateControllerPresentationState::TransitionToLocomotion;
		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("StateControllerResponsiveStartExit Revision=%d Held=%d Cancel=%s"),
				LastHandledStartResponsiveExitRevision,
				static_cast<int32>(StateControllerRuntime.GetHeldState()),
				bShouldCancelOneShot ? TEXT("true") : TEXT("false"));
		}
	}

	if (bShouldCancelOneShot)
	{
		Project_J::AnimationFlowDebug::Decision(OwningCharacter, TEXT("Presentation"),
			bRemoteHeldStart ? TEXT("RemoteStartRedirect") :
			(bRemoteResponsiveStartExit ? TEXT("RemoteStartExitRevision") : TEXT("LocalInputRedirectRelease")));
		if (bIsCommittedPivotOneShot)
		{
			StateControllerRuntime.CancelPivotForInputRedirect(AnimationClock.Seconds);
			ThreadSafeData.LocomotionContext.bIsPivoting = false;
		}
		// A local input turn, or its remote replicated-movement equivalent, invalidates
		// the trajectory that selected an authored Start/Pivot/Land one-shot. Release it and
		// refresh regular MM immediately. The publication below admits compatible
		// Cycle/Turn candidates from observed physical demand; it never commands
		// a Turn clip or replaces the dedicated Pivot owner.
		if (bIsLandOneShot) { UpdateReleasedLandingMotionContext(ThreadSafeData, true); }
		CurrentStateControllerPresentationState = ThreadSafeData.LocomotionContext.bIsMotionMatchingMoving
			? EProject_JStateControllerPresentationState::LocomotionLoop
			: EProject_JStateControllerPresentationState::IdleLoop;
		ThreadSafeData.OneShotPresentation.PresentationState = CurrentStateControllerPresentationState;
		ThreadSafeData.OneShotPresentation.PhaseFamily =
			CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::LocomotionLoop
				? EProject_JLocomotionPhaseFamily::Cycle
				: EProject_JLocomotionPhaseFamily::Idle;
		ThreadSafeData.OneShotPresentation.bRequested = false;
		ThreadSafeData.LocomotionContext.PhaseFamily = ThreadSafeData.OneShotPresentation.PhaseFamily;
		ThreadSafeData.MotionMatching.SelectionContext.PhaseFamily = ThreadSafeData.OneShotPresentation.PhaseFamily;
		ThreadSafeData.MotionMatching.bForceReselect = true;
		StateControllerRuntime.SetFallbackHold(CurrentStateControllerPresentationState, AnimationClock.Seconds);
		bHasStateControllerOneShotControlYaw = false;
		bHasStateControllerOneShotMoveInputYaw = false;
		bHasStateControllerRemoteStartReference = false;
		bHasStateControllerRemoteStartMoveYaw = false;
	}
	else if (!bIsTurnCancellableOneShot)
	{
		OneShotCurvePolicy.Reset();
		bHasStateControllerOneShotControlYaw = false;
		bHasStateControllerOneShotMoveInputYaw = false;
		bHasStateControllerRemoteStartReference = false;
		bHasStateControllerRemoteStartMoveYaw = false;
	}
	// Chooser columns require a reflected property rather than a BlueprintPure
	// enum getter. Publish this game-thread mirror *before* evaluating the
	// chooser: its object-parameter columns read these reflected properties.
	// Publishing afterwards would make every selection use the previous frame's
	// presentation state (Idle -> Start -> Loop -> Stop one state late).
	StateControllerPresentationStateForChooser = CurrentStateControllerPresentationState;
	RotationModeForChooser = ThreadSafeData.LocomotionContext.RotationMode;
	GaitIntentForChooser = ThreadSafeData.LocomotionContext.GaitIntent;
	if (ThreadSafeData.LocomotionContext.TurnInPlaceDirectionBucket >= 1 &&
		ThreadSafeData.LocomotionContext.TurnInPlaceDirectionBucket <= 4)
	{
		StateControllerTurnInPlaceIndexForChooser = static_cast<float>(ThreadSafeData.LocomotionContext.TurnInPlaceDirectionBucket);
	}
	else if (IsLocallyControlledCharacter())
	{
		const float DeltaYawForTurn = ThreadSafeData.LocomotionContext.DesiredFacingDeltaYaw;
		if (DeltaYawForTurn >= -135.0f && DeltaYawForTurn <= -30.0f)
		{
			StateControllerTurnInPlaceIndexForChooser = 1.0f; // Left 090
		}
		else if (DeltaYawForTurn < -135.0f)
		{
			StateControllerTurnInPlaceIndexForChooser = 2.0f; // Left 180
		}
		else if (DeltaYawForTurn >= 30.0f && DeltaYawForTurn < 135.0f)
		{
			StateControllerTurnInPlaceIndexForChooser = 3.0f; // Right 090
		}
		else if (DeltaYawForTurn >= 135.0f)
		{
			StateControllerTurnInPlaceIndexForChooser = 4.0f; // Right 180
		}
		else
		{
			StateControllerTurnInPlaceIndexForChooser = 0.0f;
		}
	}
	else
	{
		StateControllerTurnInPlaceIndexForChooser = 0.0f;
	}
	const bool bEnteringStateControllerStart =
		PreviousStateControllerPresentationState != CurrentStateControllerPresentationState &&
		CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToLocomotion;
	if (bEnteringStateControllerStart)
	{
		// The locomotion component retains this semantic fact even if Character
		// Movement has not reached its target speed yet.
		StateControllerStartGaitForChooser = ThreadSafeData.Ground.bStartWasSprinting
			? EProject_JLocomotionGaitIntent::Sprint
			: EProject_JLocomotionGaitIntent::Run;
		StateControllerStartGaitStartedAtSeconds = AnimationClock.Seconds;
		bHasStateControllerStartGaitForChooser = true;
		bStateControllerStartGaitCommitted = false;
	}
	else if (CurrentStateControllerPresentationState != EProject_JStateControllerPresentationState::TransitionToLocomotion)
	{
		bHasStateControllerStartGaitForChooser = false;
		bStateControllerStartGaitCommitted = false;
	}
	if (CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToLocomotion)
	{
		const EProject_JLocomotionGaitIntent CurrentStartGait =
			ThreadSafeData.LocomotionContext.GaitIntent == EProject_JLocomotionGaitIntent::Sprint
				? EProject_JLocomotionGaitIntent::Sprint
				: EProject_JLocomotionGaitIntent::Run;
		if (!bStateControllerStartGaitCommitted)
		{
			// During the small grace window a released Shift can still choose the
			// correct Run Start instead of briefly showing Sprint Start. A simulated
			// proxy keeps the replicated edge gait during this window because its GAS
			// sprint tag may arrive on a different replication frame.
			if (OwningPlayerCharacter && OwningPlayerCharacter->IsLocallyControlled())
			{
				StateControllerStartGaitForChooser = CurrentStartGait;
			}
			bStateControllerStartGaitCommitted =
				AnimationClock.Seconds - StateControllerStartGaitStartedAtSeconds >=
				StateControllerStartGaitCommitWindowSeconds;
		}

		const bool bStartGaitChangedAfterCommit =
			bHasStateControllerStartGaitForChooser &&
			bStateControllerStartGaitCommitted &&
			CurrentStartGait != StateControllerStartGaitForChooser;
		if (bStartGaitChangedAfterCommit && ThreadSafeData.LocomotionContext.bIsMotionMatchingMoving)
		{
			// Do not keep playing Sprint Start at a Run velocity (or vice versa).
			// Once the authored start has genuinely begun, a gait change is better
			// represented by the trajectory-aware Cycle PSD. This also avoids
			// swapping Sprint Start directly to Run Start mid-stride.
			CurrentStateControllerPresentationState = EProject_JStateControllerPresentationState::LocomotionLoop;
			ThreadSafeData.OneShotPresentation.PresentationState = CurrentStateControllerPresentationState;
			ThreadSafeData.OneShotPresentation.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
			ThreadSafeData.OneShotPresentation.bRequested = false;
			ThreadSafeData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
			ThreadSafeData.MotionMatching.SelectionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
			ThreadSafeData.MotionMatching.bForceReselect = true;
			StateControllerPresentationStateForChooser = CurrentStateControllerPresentationState;
			StateControllerRuntime.SetFallbackHold(CurrentStateControllerPresentationState, AnimationClock.Seconds);
			bHasStateControllerStartGaitForChooser = false;
			bStateControllerStartGaitCommitted = false;
			GaitIntentForChooser = CurrentStartGait;
		}
		else
		{
			GaitIntentForChooser = bHasStateControllerStartGaitForChooser
				? StateControllerStartGaitForChooser
				: CurrentStartGait;
		}
	}
	const bool bEnteringStateControllerStop =
		PreviousStateControllerPresentationState != CurrentStateControllerPresentationState &&
		CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToIdle;
	if (bEnteringStateControllerStop)
	{
		StateControllerStopGaitForChooser = ThreadSafeData.Ground.bStopWasSprinting
			? EProject_JLocomotionGaitIntent::Sprint
			: EProject_JLocomotionGaitIntent::Run;
		bHasStateControllerStopGaitForChooser = true;
	}
	else if (CurrentStateControllerPresentationState != EProject_JStateControllerPresentationState::TransitionToIdle)
	{
		bHasStateControllerStopGaitForChooser = false;
	}
	// Character movement returns to Walk intent as soon as the sprint/run input
	// is released. A Stop one-shot must instead keep the gait that initiated the
	// stop; otherwise the chooser re-evaluates as Walk and clears the selected
	// Run/Sprint Stop asset during its first few frames.
	if (CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToIdle)
	{
		GaitIntentForChooser = bHasStateControllerStopGaitForChooser
			? StateControllerStopGaitForChooser
			: EProject_JLocomotionGaitIntent::Run;
	}
	const bool bEnteringStateControllerLand =
		PreviousStateControllerPresentationState != CurrentStateControllerPresentationState &&
		CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToLand;
	if (bEnteringStateControllerLand)
	{
		StateControllerLandGaitForChooser = Project_J::Locomotion::ResolveLandingGaitIntent(
			ThreadSafeData.Landing.bLandWasMoving,
			ThreadSafeData.Landing.bLandWasSprinting);
		bHasStateControllerLandGaitForChooser = true;
	}
	else if (CurrentStateControllerPresentationState != EProject_JStateControllerPresentationState::TransitionToLand)
	{
		bHasStateControllerLandGaitForChooser = false;
	}
	if (CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToLand)
	{
		GaitIntentForChooser = bHasStateControllerLandGaitForChooser
			? StateControllerLandGaitForChooser
			: EProject_JLocomotionGaitIntent::Run;
	}
	StateControllerStanceForChooser = ThreadSafeData.OneShotPresentation.Stance;
	StateControllerStrafeDirectionForChooser = ThreadSafeData.OneShotPresentation.StrafeDirection;
	StateControllerPreviousStrafeDirectionForChooser = ThreadSafeData.OneShotPresentation.PreviousStrafeDirection;
	StateControllerStrafeDirectionAngleForChooser = ThreadSafeData.OneShotPresentation.StrafeDirectionAngle;
	bStateControllerHasStrafeDirectionAngleForChooser = ThreadSafeData.OneShotPresentation.bHasStrafeDirectionAngle;
	bCombatModeForChooser = ThreadSafeData.Combat.bIsCombatMode;
	const bool bEnteringStateControllerInAir =
		PreviousStateControllerPresentationState != CurrentStateControllerPresentationState &&
		CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToInAir;
	if (bEnteringStateControllerInAir)
	{
		bStateControllerFallOffForChooser =
			ThreadSafeData.Air.bIsFallOffStart || bInferFallOffFromFreshAirEntry;
		bHasStateControllerFallOffForChooser = true;
	}
	else if (CurrentStateControllerPresentationState != EProject_JStateControllerPresentationState::TransitionToInAir)
	{
		bStateControllerFallOffForChooser = false;
		bHasStateControllerFallOffForChooser = false;
	}
	StateControllerLeftFootContactForChooser = CachedStateControllerLeftFootContact;
	StateControllerRightFootContactForChooser = CachedStateControllerRightFootContact;
	bStateControllerHasLeftFootContactCurveForChooser = bHasStateControllerLeftFootContactCurve;
	bStateControllerHasRightFootContactCurveForChooser = bHasStateControllerRightFootContactCurve;
	const bool bEnteringStateControllerOneShot =
		PreviousStateControllerPresentationState != CurrentStateControllerPresentationState &&
		(CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToLocomotion ||
			CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToIdle ||
			CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToInAir ||
			CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToLand);
	if (IsPrimaryMeshAnimInstance() && bEnteringStateControllerOneShot)
	{
		// Start begins from Idle, which deliberately has no remembered stride.
		// Stop/Fall/Land continue a prior moving phase when both contact curves
		// are transiently 0/0 or 1/1 at the transition boundary.
		const bool bAllowPhaseHistoryFallback =
			CurrentStateControllerPresentationState != EProject_JStateControllerPresentationState::TransitionToLocomotion &&
			PreviousStateControllerPresentationState != EProject_JStateControllerPresentationState::IdleLoop;
		StateControllerOneShotFootForChooser = ResolveStateControllerFootFromContactCurves(
			bAllowPhaseHistoryFallback,
			StateControllerOneShotFootSelectionReasonForChooser);
		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("StateControllerFootLatch State=%d Foot=%d Reason=%d HasCurveL=%s HasCurveR=%s ContactL=%.3f ContactR=%.3f Delta=%.3f Threshold=%.3f PhaseCache=%d AllowPhaseCache=%s DefaultFoot=%d StopGait=%d FallOff=%s"),
				static_cast<int32>(CurrentStateControllerPresentationState),
				static_cast<int32>(StateControllerOneShotFootForChooser),
				static_cast<int32>(StateControllerOneShotFootSelectionReasonForChooser),
				bHasStateControllerLeftFootContactCurve ? TEXT("true") : TEXT("false"),
				bHasStateControllerRightFootContactCurve ? TEXT("true") : TEXT("false"),
				CachedStateControllerLeftFootContact,
				CachedStateControllerRightFootContact,
				CachedStateControllerLeftFootContact - CachedStateControllerRightFootContact,
				StateControllerFootContactDifferenceThreshold,
				bHasStateControllerFootPhaseHistory ? static_cast<int32>(StateControllerFootPhaseHistory) : -1,
				bAllowPhaseHistoryFallback ? TEXT("true") : TEXT("false"),
				static_cast<int32>(StateControllerNoPhaseFootFallback),
				static_cast<int32>(GaitIntentForChooser),
				bStateControllerFallOffForChooser ? TEXT("true") : TEXT("false"));
		}
	}
	// A replacement Pivot stays in TransitionToLocomotion, so it does not create
	// a state-entry edge and must not inherit the prior Pivot's foot variant.
	// Latch once per semantic Pivot request from the latest contact sample, then
	// fall back to phase history (and finally the authored default) when contact
	// curves are ambiguous. The revision guard keeps a single direct Pivot from
	// being changed by a later contact refresh.
	const int32 PivotRequestRevision = ThreadSafeData.LocomotionContext.PivotRequestRevision;
	const bool bNeedsPivotFootLatch =
		IsPrimaryMeshAnimInstance() &&
		ThreadSafeData.OneShotPresentation.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot &&
		PivotRequestRevision != 0 &&
		PivotRequestRevision != StateControllerPivotFootLatchRequestRevision;
	if (bNeedsPivotFootLatch)
	{
		StateControllerOneShotFootForChooser = ResolveStateControllerFootFromContactCurves(
			true,
			StateControllerOneShotFootSelectionReasonForChooser);
		StateControllerPivotFootLatchRequestRevision = PivotRequestRevision;
		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("CombatStrafeRunPivotFootLatch Actor=%s PivotRev=%d Foot=%d Reason=%d ContactL=%.3f ContactR=%.3f HasContactCurves=%s PhaseCache=%d"),
				*GetNameSafe(OwningCharacter),
				PivotRequestRevision,
				static_cast<int32>(StateControllerOneShotFootForChooser),
				static_cast<int32>(StateControllerOneShotFootSelectionReasonForChooser),
				CachedStateControllerLeftFootContact,
				CachedStateControllerRightFootContact,
				bHasStateControllerFootContactCurves ? TEXT("true") : TEXT("false"),
				bHasStateControllerFootPhaseHistory ? static_cast<int32>(StateControllerFootPhaseHistory) : -1);
		}
	}
	StateControllerFootPhaseHistoryForChooser = bHasStateControllerFootPhaseHistory
		? StateControllerFootPhaseHistory
		: EProject_JStateControllerFoot::None;
	if (CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::IdleLoop)
	{
		bHasStateControllerFootPhaseHistory = false;
		StateControllerFootPhaseHistory = EProject_JStateControllerFoot::None;
		StateControllerFootPhaseHistoryForChooser = EProject_JStateControllerFoot::None;
	}
	ThreadSafeData.OneShotPresentation.Foot = StateControllerOneShotFootForChooser;
	// State Controller Choosers are evaluated on the game thread. Keep this
	// separate from ChooserDesiredFacingDeltaYaw, which is published only when
	// the throttled regular Motion Matching chooser is evaluated.
	StateControllerInputFacingDeltaYawForChooser = ThreadSafeData.LocomotionContext.DesiredFacingDeltaYaw;
	if (PreviousStateControllerPresentationState != EProject_JStateControllerPresentationState::TransitionToIdle &&
		CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToIdle)
	{
		StateControllerStopVelocityDeltaYawForChooser = 0.0f;
		FVector StopVelocity = ThreadSafeData.Movement.Velocity;
		StopVelocity.Z = 0.0f;
		if (StopVelocity.SizeSquared2D() > UE_KINDA_SMALL_NUMBER)
		{
			const float ActorYaw = GetOwningActor() ? GetOwningActor()->GetActorRotation().Yaw : 0.0f;
			StateControllerStopVelocityDeltaYawForChooser = FMath::FindDeltaAngleDegrees(
				ActorYaw,
				StopVelocity.Rotation().Yaw);
		}

	}
	// Linked combat layers share the character and can therefore receive the same
	// native update as the mesh's master AnimInstance. They are consumers of the
	// resolved locomotion pose, not a second owner of its direct Blend Stack. If
	// they evaluate this chooser too, their independent foot-contact history can
	// issue a second L/R Pivot for the same request revision.
	if (IsPrimaryMeshAnimInstance())
	{
		EvaluateStateControllerAnimationChooserOnGameThread(ThreadSafeData);
	}
	else
	{
		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace() &&
			ThreadSafeData.OneShotPresentation.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot &&
			ThreadSafeData.LocomotionContext.PivotRequestRevision != 0 &&
			ThreadSafeData.LocomotionContext.PivotRequestRevision != LastLoggedNonPrimaryPivotRequestRevision)
		{
			LastLoggedNonPrimaryPivotRequestRevision = ThreadSafeData.LocomotionContext.PivotRequestRevision;
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("CombatStrafeRunPivotNonPrimarySuppressed Actor=%s PivotRev=%d AnimInstance=%s PrimaryAnimInstance=%s"),
				*GetNameSafe(OwningCharacter),
				ThreadSafeData.LocomotionContext.PivotRequestRevision,
				*GetPathName(),
				OwningCharacter && OwningCharacter->GetMesh()
					? *GetPathNameSafe(OwningCharacter->GetMesh()->GetAnimInstance())
					: TEXT("None"));
		}
		FProject_JAnimOneShotPresentationThreadSafeData& OneShot = ThreadSafeData.OneShotPresentation;
		OneShot.SelectedAnimation = nullptr;
		OneShot.SelectedAnimationOutput = FProject_JStateControllerChooserOutput();
		OneShot.bHasSelectedAnimation = false;
		OneShot.bShouldOverrideMotionMatching = false;
		OneShot.bForceBlendNextUpdate = false;
	}
}
