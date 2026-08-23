// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"

#include "ChooserFunctionLibrary.h"
#include "ChooserTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "IObjectChooser.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JLocomotionAnimStateComponentBase.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/Project_JMotionMatchingCVars.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JWeaponAnimProfile.h"
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

// SyncLegacyFieldsFromStructuredData() removed.
// All code now uses sub-struct paths (e.g., Data.Movement.GroundSpeed) directly.

namespace
{
	constexpr float StateControllerOneShotMouseTurnCancelAngle = 15.0f;
	// Let a very short Shift tap settle before committing the authored Start.
	// After this window, switching the source animation mid-stride is worse than
	// letting the current Start finish and entering the current-gait Cycle.
	constexpr double StateControllerStartGaitCommitWindowSeconds = 0.15;

	constexpr float StrafeDirectionSectorHalfWidth = 22.5f;

	float GetStrafeDirectionCenterDegrees(const EProject_JStateControllerStrafeDirection Direction)
	{
		switch (Direction)
		{
		case EProject_JStateControllerStrafeDirection::Forward:
			return 0.0f;
		case EProject_JStateControllerStrafeDirection::ForwardLeft:
			return -45.0f;
		case EProject_JStateControllerStrafeDirection::Left:
			return -90.0f;
		case EProject_JStateControllerStrafeDirection::BackwardLeft:
			return -135.0f;
		case EProject_JStateControllerStrafeDirection::Backward:
			return 180.0f;
		case EProject_JStateControllerStrafeDirection::BackwardRight:
			return 135.0f;
		case EProject_JStateControllerStrafeDirection::Right:
			return 90.0f;
		case EProject_JStateControllerStrafeDirection::ForwardRight:
			return 45.0f;
		default:
			return 0.0f;
		}
	}

	EProject_JStateControllerStrafeDirection ResolveStateControllerStrafeDirection(
		const float DirectionDegrees,
		const EProject_JStateControllerStrafeDirection PreviousDirection,
		const float HysteresisDegrees)
	{
		const float Direction = FRotator::NormalizeAxis(DirectionDegrees);
		const float ClampedHysteresis = FMath::Clamp(HysteresisDegrees, 0.0f, StrafeDirectionSectorHalfWidth - KINDA_SMALL_NUMBER);
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(GetStrafeDirectionCenterDegrees(PreviousDirection), Direction)) <=
			StrafeDirectionSectorHalfWidth + ClampedHysteresis)
		{
			return PreviousDirection;
		}

		if (Direction >= -22.5f && Direction <= 22.5f)
		{
			return EProject_JStateControllerStrafeDirection::Forward;
		}
		if (Direction < -22.5f && Direction >= -67.5f)
		{
			return EProject_JStateControllerStrafeDirection::ForwardLeft;
		}
		if (Direction < -67.5f && Direction >= -112.5f)
		{
			return EProject_JStateControllerStrafeDirection::Left;
		}
		if (Direction < -112.5f && Direction >= -157.5f)
		{
			return EProject_JStateControllerStrafeDirection::BackwardLeft;
		}
		if (Direction > 22.5f && Direction <= 67.5f)
		{
			return EProject_JStateControllerStrafeDirection::ForwardRight;
		}
		if (Direction > 67.5f && Direction <= 112.5f)
		{
			return EProject_JStateControllerStrafeDirection::Right;
		}
		if (Direction > 112.5f && Direction <= 157.5f)
		{
			return EProject_JStateControllerStrafeDirection::BackwardRight;
		}
		return EProject_JStateControllerStrafeDirection::Backward;
	}

	EProject_JStateControllerPresentationState ResolveStateControllerPresentationState(
		const FProject_JAnimThreadSafeData& Data,
		const FProject_JAnimOneShotPresentationThreadSafeData& OneShot)
	{
		if (!OneShot.bEnabled)
		{
			return EProject_JStateControllerPresentationState::Disabled;
		}

		const bool bIsJumpingOrJumpStart = Data.Air.bIsJumping ||
			Data.Air.bIsFallOffStart ||
			OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::JumpStart ||
			OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::Fall;

		if (Data.Air.bIsInAir || bIsJumpingOrJumpStart)
		{
			return (Data.Air.bIsJumping || Data.Air.bIsFallOffStart || OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::JumpStart)
				? EProject_JStateControllerPresentationState::TransitionToInAir
				: EProject_JStateControllerPresentationState::InAirLoop;
		}

		if (Data.Landing.bIsLanding)
		{
			return EProject_JStateControllerPresentationState::TransitionToLand;
		}

		if (OneShot.bRequested)
		{
			switch (OneShot.PhaseFamily)
			{
			case EProject_JLocomotionPhaseFamily::Start:
			case EProject_JLocomotionPhaseFamily::Pivot:
				// A release/reversal can happen before the component's phase has
				// settled from Start/Pivot to Stop.  Let the trajectory-derived
				// intent preempt it so the State Controller can select a Stop now.
				return Data.LocomotionContext.bIsMotionMatchingMoving
					? EProject_JStateControllerPresentationState::TransitionToLocomotion
					: EProject_JStateControllerPresentationState::TransitionToIdle;
			case EProject_JLocomotionPhaseFamily::Stop:
				return EProject_JStateControllerPresentationState::TransitionToIdle;
			case EProject_JLocomotionPhaseFamily::TurnInPlace:
				return EProject_JStateControllerPresentationState::TurnInPlace;
			case EProject_JLocomotionPhaseFamily::Landing:
				return EProject_JStateControllerPresentationState::TransitionToLand;
			case EProject_JLocomotionPhaseFamily::JumpStart:
			case EProject_JLocomotionPhaseFamily::Fall:
				return EProject_JStateControllerPresentationState::TransitionToInAir;
			default:
				break;
			}
		}

		if (Data.LocomotionContext.bShouldTurnInPlace)
		{
			return EProject_JStateControllerPresentationState::TurnInPlace;
		}

		return Data.LocomotionContext.bIsMotionMatchingMoving
			? EProject_JStateControllerPresentationState::LocomotionLoop
			: EProject_JStateControllerPresentationState::IdleLoop;
	}

	auto IsTransitionState = [](const EProject_JStateControllerPresentationState State)
	{
		return State == EProject_JStateControllerPresentationState::TransitionToLocomotion ||
			State == EProject_JStateControllerPresentationState::TransitionToIdle ||
			State == EProject_JStateControllerPresentationState::TransitionToInAir ||
			State == EProject_JStateControllerPresentationState::TransitionToLand ||
			State == EProject_JStateControllerPresentationState::TurnInPlace;
	};

	auto IsNaturalLoopContinuation = [](const EProject_JStateControllerPresentationState TransitionState,
		const EProject_JStateControllerPresentationState CandidateState)
	{
		return (TransitionState == EProject_JStateControllerPresentationState::TransitionToLocomotion &&
				CandidateState == EProject_JStateControllerPresentationState::LocomotionLoop) ||
			(TransitionState == EProject_JStateControllerPresentationState::TransitionToIdle &&
				CandidateState == EProject_JStateControllerPresentationState::IdleLoop) ||
			(TransitionState == EProject_JStateControllerPresentationState::TransitionToInAir &&
				CandidateState == EProject_JStateControllerPresentationState::InAirLoop) ||
			(TransitionState == EProject_JStateControllerPresentationState::TransitionToLand &&
				(CandidateState == EProject_JStateControllerPresentationState::LocomotionLoop ||
				 CandidateState == EProject_JStateControllerPresentationState::IdleLoop ||
				 CandidateState == EProject_JStateControllerPresentationState::TurnInPlace)) ||
			(TransitionState == EProject_JStateControllerPresentationState::TurnInPlace &&
				(CandidateState == EProject_JStateControllerPresentationState::IdleLoop ||
				 CandidateState == EProject_JStateControllerPresentationState::TurnInPlace));
	};

	bool ShouldStateControllerPresentationLoop(const EProject_JStateControllerPresentationState PresentationState)
	{
		switch (PresentationState)
		{
		case EProject_JStateControllerPresentationState::IdleLoop:
		case EProject_JStateControllerPresentationState::LocomotionLoop:
		case EProject_JStateControllerPresentationState::InAirLoop:
			return true;
		case EProject_JStateControllerPresentationState::TurnInPlace:
		default:
			return false;
		}
	}
}

UProject_JCharacterAnimInstance::UProject_JCharacterAnimInstance()
{
	bUseMultiThreadedAnimationUpdate = true;

	FootPlacementPlantSettingsStops.SpeedThreshold = 80.0f;
	FootPlacementPlantSettingsStops.UnplantRadius = 25.0f;
	FootPlacementPlantSettingsStops.UnplantAngle = 35.0f;
	FootPlacementPlantSettingsStops.ReplantRadiusRatio = 0.5f;
	FootPlacementPlantSettingsStops.ReplantAngleRatio = 0.65f;

	FootPlacementInterpolationSettingsStops.UnplantLinearStiffness = 500.0f;
	FootPlacementInterpolationSettingsStops.UnplantAngularStiffness = 700.0f;
	FootPlacementInterpolationSettingsStops.FloorLinearStiffness = 1200.0f;
	FootPlacementInterpolationSettingsStops.FloorAngularStiffness = 650.0f;
}

void UProject_JCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (NeedsOwnerReferenceRefresh())
	{
		CacheOwnerReferences();
	}

	if (ShouldSkipNativeUpdate(DeltaSeconds))
	{
		return;
	}

	ThreadSafeData = BuildThreadSafeData(DeltaSeconds);
	// A combat draw/sheathe montage temporarily owns the final pose through its
	// FullBody slot. Do not allow a direct State Controller asset (most visibly Land)
	// to remain cached underneath it and resume when the montage blends out.
	// This is deliberately presentation-only: the locomotion component's landing
	// event, CharacterMovement, and replicated movement state remain untouched.
	const bool bCombatPresentationTransitionActive =
		ThreadSafeData.Combat.bIsPlayingCombatIntro || ThreadSafeData.Combat.bIsPlayingCombatOutro;
	const bool bCombatPresentationTransitionStarted =
		bCombatPresentationTransitionActive && !bWasPlayingCombatPresentationTransitionForStateController;
	const bool bCombatPresentationTransitionEnded =
		!bCombatPresentationTransitionActive && bWasPlayingCombatPresentationTransitionForStateController;
	bWasPlayingCombatPresentationTransitionForStateController = bCombatPresentationTransitionActive;
	if (bCombatPresentationTransitionStarted)
	{
		const bool bDiscardingPhysicalLanding = ThreadSafeData.Landing.bIsLanding;
		const bool bDiscardingHeldLand =
			StateControllerPlaybackHoldState == EProject_JStateControllerPresentationState::TransitionToLand ||
			ThreadSafeData.OneShotPresentation.PresentationState == EProject_JStateControllerPresentationState::TransitionToLand;
		bSuppressPreTransitionLandingPresentationUntilLandingEnds =
			bDiscardingPhysicalLanding || bDiscardingHeldLand;

		// Invalidate both the logical hold and its immutable asset cache.  Merely
		// disabling the Blend Stack branch would leave the old asset eligible to
		// revive on the first post-montage update.
		StateControllerPlaybackHoldState = EProject_JStateControllerPresentationState::Disabled;
		StateControllerPlaybackHoldStartedAtSeconds = FPlatformTime::Seconds();
		CachedStateControllerChooserTable.Reset();
		CachedStateControllerSelectedAnimation = nullptr;
		CachedStateControllerSelectedAnimationOutput = FProject_JStateControllerChooserOutput();
		bCachedStateControllerHasSelectedAnimation = false;
		bStateControllerForceTurnInPlaceReselect = false;
		bHasStateControllerOneShotControlYaw = false;
		bHasStateControllerOneShotMoveInputYaw = false;
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
		bCombatPresentationTransitionActive ||
		bSuppressPreTransitionLandingPresentationUntilLandingEnds;
	if (bSuppressDirectGroundOneShotsForCombatPresentation && !ThreadSafeData.Air.bIsInAir)
	{
		// Keep regular MM current beneath the montage.  On blend-out it therefore
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
		ThreadSafeData.MotionMatching.SelectionContext.bUseHeavyLand = false;
		ThreadSafeData.MotionMatching.SelectionContext.bLandWasMoving = false;
		ThreadSafeData.MotionMatching.SelectionContext.bLandWasSprinting = false;
		// Re-query only at the presentation boundaries.  The fallback context is
		// still published every frame, but repeatedly forcing a Pose Search while
		// a montage is active would be needless per-frame work.
		ThreadSafeData.MotionMatching.bForceReselect =
			ThreadSafeData.MotionMatching.bForceReselect || bCombatPresentationTransitionStarted;
		StateControllerPlaybackHoldState = FallbackPresentationState;
		StateControllerPlaybackHoldStartedAtSeconds = FPlatformTime::Seconds();
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
	if (bEnteringStateControllerTurnCancellableOneShot && OwningPlayerCharacter && OwningPlayerCharacter->IsLocallyControlled())
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

	const bool bIsTurnCancellableOneShot =
		CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToLocomotion ||
		CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToLand;

	const UProject_JLocomotionProfile* LocomotionProfile = GetLocomotionProfile();
	const float EffectiveMouseTurnCancelAngle = LocomotionProfile
		? LocomotionProfile->TransitionPolicy.StartMouseTurnCancelAngle
		: StateControllerOneShotMouseTurnCancelAngle;
	const float EffectiveMoveInputCancelAngle = LocomotionProfile
		? LocomotionProfile->TransitionPolicy.StartMoveInputCancelAngle
		: 30.0f;

	bool bShouldCancelOneShot = false;
	if (bIsTurnCancellableOneShot && OwningPlayerCharacter && OwningPlayerCharacter->IsLocallyControlled())
	{
		if (bHasStateControllerOneShotControlYaw)
		{
			const float ControlYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(
				StateControllerOneShotControlYaw,
				OwningPlayerCharacter->GetControlRotation().Yaw));
			if (ControlYawDelta >= EffectiveMouseTurnCancelAngle)
			{
				bShouldCancelOneShot = true;
			}
		}

		if (!bShouldCancelOneShot)
		{
			const FVector CurrentInput = OwningPlayerCharacter->GetLastMovementInputVector();
			const bool bHasCurrentInput = CurrentInput.SizeSquared2D() > UE_KINDA_SMALL_NUMBER;

			if (bHasStateControllerOneShotMoveInputYaw && bHasCurrentInput)
			{
				const float CurrentMoveInputYaw = CurrentInput.ToOrientationRotator().Yaw;
				const float MoveInputYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(
					StateControllerOneShotMoveInputYaw,
					CurrentMoveInputYaw));
				if (MoveInputYawDelta >= EffectiveMoveInputCancelAngle)
				{
					bShouldCancelOneShot = true;
				}
			}
			else if (!bHasStateControllerOneShotMoveInputYaw && bHasCurrentInput)
			{
				StateControllerOneShotMoveInputYaw = CurrentInput.ToOrientationRotator().Yaw;
				bHasStateControllerOneShotMoveInputYaw = true;
			}
		}
	}
	const bool bRemoteResponsiveStartExit =
		OwningPlayerCharacter &&
		!OwningPlayerCharacter->IsLocallyControlled() &&
		ThreadSafeData.Ground.StartResponsiveExitRevision != LastHandledStartResponsiveExitRevision;
	if (bRemoteResponsiveStartExit)
	{
		LastHandledStartResponsiveExitRevision = ThreadSafeData.Ground.StartResponsiveExitRevision;
		bShouldCancelOneShot =
			StateControllerPlaybackHoldState == EProject_JStateControllerPresentationState::TransitionToLocomotion;
		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("StateControllerResponsiveStartExit Revision=%d Held=%d Cancel=%s"),
				LastHandledStartResponsiveExitRevision,
				static_cast<int32>(StateControllerPlaybackHoldState),
				bShouldCancelOneShot ? TEXT("true") : TEXT("false"));
		}
	}

	if (bShouldCancelOneShot)
	{
		// A local mouse turn or movement input change invalidates the trajectory that selected an authored
		// Start/Land one-shot. Release it and refresh regular MM immediately. We do
		// not route this through a Turn asset: Project_J has not yet authored a
		// dedicated OTM Turn chooser/transition contract, whereas the Cycle PSD is
		// already trajectory-aware and stable for Run and Sprint.
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
		StateControllerPlaybackHoldState = CurrentStateControllerPresentationState;
		StateControllerPlaybackHoldStartedAtSeconds = FPlatformTime::Seconds();
		bHasStateControllerOneShotControlYaw = false;
		bHasStateControllerOneShotMoveInputYaw = false;
	}
	else if (!bIsTurnCancellableOneShot)
	{
		bHasStateControllerOneShotControlYaw = false;
		bHasStateControllerOneShotMoveInputYaw = false;
	}
	// Chooser columns require a reflected property rather than a BlueprintPure
	// enum getter. Publish this game-thread mirror *before* evaluating the
	// chooser: its object-parameter columns read these reflected properties.
	// Publishing afterwards would make every selection use the previous frame's
	// presentation state (Idle -> Start -> Loop -> Stop one state late).
	StateControllerPresentationStateForChooser = CurrentStateControllerPresentationState;
	RotationModeForChooser = ThreadSafeData.LocomotionContext.RotationMode;
	GaitIntentForChooser = ThreadSafeData.LocomotionContext.GaitIntent;
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
		StateControllerStartGaitStartedAtSeconds = FPlatformTime::Seconds();
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
				FPlatformTime::Seconds() - StateControllerStartGaitStartedAtSeconds >=
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
			StateControllerPlaybackHoldState = CurrentStateControllerPresentationState;
			StateControllerPlaybackHoldStartedAtSeconds = FPlatformTime::Seconds();
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
	if (bEnteringStateControllerOneShot)
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
		if (CurrentStateControllerPresentationState == EProject_JStateControllerPresentationState::TransitionToIdle)
		{
			StateControllerStopFootForChooser = StateControllerOneShotFootForChooser;
		}

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
	EvaluateStateControllerAnimationChooserOnGameThread(ThreadSafeData);
	const int32 TurnInPlaceDebugMode = Project_J::MotionMatchingCVars::GetTurnInPlaceDebugMode();
	if (TurnInPlaceDebugMode > 0 && OwningPlayerCharacter)
	{
		const FProject_JAnimOneShotPresentationThreadSafeData& OneShot = ThreadSafeData.OneShotPresentation;
		const bool bTurnRelevant =
			ThreadSafeData.LocomotionContext.bShouldTurnInPlace ||
			ThreadSafeData.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::TurnInPlace ||
			OneShot.PresentationState == EProject_JStateControllerPresentationState::TurnInPlace ||
			StateControllerPlaybackHoldState == EProject_JStateControllerPresentationState::TurnInPlace;
		const float ActorYaw = OwningPlayerCharacter->GetActorRotation().Yaw;
		const float ControlYaw = OwningPlayerCharacter->GetControlRotation().Yaw;
		float RootYaw = ActorYaw;
		float MeshYaw = ActorYaw;
		if (const USkeletalMeshComponent* Mesh = OwningPlayerCharacter->GetMesh())
		{
			MeshYaw = Mesh->GetComponentRotation().Yaw;
			if (Mesh->GetNumBones() > 0)
			{
				RootYaw = Mesh->GetBoneTransform(0).Rotator().Yaw;
			}
		}

		UAnimationAsset* SelectedAsset = OneShot.SelectedAnimation;
		const bool bStateChanged =
			bLastTurnInPlaceDebugRelevant != bTurnRelevant ||
			(bTurnRelevant &&
				(bLastTurnInPlaceDebugShouldTurn != ThreadSafeData.LocomotionContext.bShouldTurnInPlace ||
			LastTurnInPlaceDebugIndex != FMath::RoundToInt(StateControllerTurnInPlaceIndexForChooser) ||
			LastTurnInPlaceDebugPhase != ThreadSafeData.LocomotionContext.PhaseFamily ||
			LastTurnInPlaceDebugPresentationState != OneShot.PresentationState ||
			LastTurnInPlaceDebugAsset.Get() != SelectedAsset ||
			OneShot.bForceBlendNextUpdate));
		const double NowSeconds = FPlatformTime::Seconds();
		const bool bPeriodicSample =
			TurnInPlaceDebugMode >= 2 && bTurnRelevant &&
			NowSeconds - LastTurnInPlaceDebugSampleTime >= 0.10;
		if (bTurnRelevant && (bStateChanged || bPeriodicSample))
		{
			const float ActorTurnSinceLastSample = bHasTurnInPlaceDebugSample
				? FMath::FindDeltaAngleDegrees(LastTurnInPlaceDebugActorYaw, ActorYaw)
				: 0.0f;
			const float RootTurnSinceLastSample = bHasTurnInPlaceDebugSample
				? FMath::FindDeltaAngleDegrees(LastTurnInPlaceDebugRootYaw, RootYaw)
				: 0.0f;
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("TIPDiag Kind=%s Should=%s DesiredYaw=%.1f DesiredDelta=%.1f ActorYaw=%.1f ControlYaw=%.1f RootYaw=%.1f MeshYaw=%.1f RootVsCapsule=%.1f ActorTurn=%.1f RootTurn=%.1f Index=%d CachedIndex=%d Phase=%d Presentation=%d Hold=%d ForceReselect=%s ForceBlend=%s Asset=%s Time=%.3f Remaining=%.3f"),
				bStateChanged ? TEXT("Change") : TEXT("Sample"),
				ThreadSafeData.LocomotionContext.bShouldTurnInPlace ? TEXT("true") : TEXT("false"),
				ThreadSafeData.LocomotionContext.DesiredFacingYaw,
				ThreadSafeData.LocomotionContext.DesiredFacingDeltaYaw,
				ActorYaw,
				ControlYaw,
				RootYaw,
				MeshYaw,
				FMath::FindDeltaAngleDegrees(ActorYaw, RootYaw),
				ActorTurnSinceLastSample,
				RootTurnSinceLastSample,
				FMath::RoundToInt(StateControllerTurnInPlaceIndexForChooser),
				FMath::RoundToInt(CachedStateControllerTurnInPlaceIndex),
				static_cast<int32>(ThreadSafeData.LocomotionContext.PhaseFamily),
				static_cast<int32>(OneShot.PresentationState),
				static_cast<int32>(StateControllerPlaybackHoldState),
				bStateControllerForceTurnInPlaceReselect ? TEXT("true") : TEXT("false"),
				OneShot.bForceBlendNextUpdate ? TEXT("true") : TEXT("false"),
				SelectedAsset ? *SelectedAsset->GetName() : TEXT("None"),
				OneShot.TransitionElapsedTime,
				OneShot.TransitionTimeRemaining);
			LastTurnInPlaceDebugSampleTime = NowSeconds;
		}

		bHasTurnInPlaceDebugSample = true;
		bLastTurnInPlaceDebugRelevant = bTurnRelevant;
		bLastTurnInPlaceDebugShouldTurn = ThreadSafeData.LocomotionContext.bShouldTurnInPlace;
		LastTurnInPlaceDebugIndex = FMath::RoundToInt(StateControllerTurnInPlaceIndexForChooser);
		LastTurnInPlaceDebugPhase = ThreadSafeData.LocomotionContext.PhaseFamily;
		LastTurnInPlaceDebugPresentationState = OneShot.PresentationState;
		LastTurnInPlaceDebugAsset = SelectedAsset;
		LastTurnInPlaceDebugActorYaw = ActorYaw;
		LastTurnInPlaceDebugRootYaw = RootYaw;
	}
	else if (TurnInPlaceDebugMode == 0)
	{
		// Re-enabling the CVar should always emit a complete first snapshot.
		bHasTurnInPlaceDebugSample = false;
		LastTurnInPlaceDebugSampleTime = -DBL_MAX;
	}
	if (IsPrimaryMeshAnimInstance())
	{
		ResetTrajectoryHistoryOnAccelerationStop(ThreadSafeData);
	}
	PublishThreadSafeDataToProxy(ThreadSafeData);
}

void UProject_JCharacterAnimInstance::NativePostEvaluateAnimation()
{
	Super::NativePostEvaluateAnimation();

	if (!IsPrimaryMeshAnimInstance())
	{
		return;
	}

	float LeftContact = 0.0f;
	float RightContact = 0.0f;
	const bool bHasLeftContact = GetCurveValue(TEXT("contact_l"), LeftContact);
	const bool bHasRightContact = GetCurveValue(TEXT("contact_r"), RightContact);
	bHasStateControllerLeftFootContactCurve = bHasLeftContact;
	bHasStateControllerRightFootContactCurve = bHasRightContact;
	bHasStateControllerFootContactCurves = bHasLeftContact && bHasRightContact;
	if (bHasStateControllerFootContactCurves)
	{
		CachedStateControllerLeftFootContact = FMath::Clamp(LeftContact, 0.0f, 1.0f);
		CachedStateControllerRightFootContact = FMath::Clamp(RightContact, 0.0f, 1.0f);
		const float ContactDelta = CachedStateControllerLeftFootContact - CachedStateControllerRightFootContact;
		if (FMath::Abs(ContactDelta) >= StateControllerFootContactDifferenceThreshold)
		{
			StateControllerFootPhaseHistory = ContactDelta < 0.0f
				? EProject_JStateControllerFoot::Left
				: EProject_JStateControllerFoot::Right;
			bHasStateControllerFootPhaseHistory = true;
		}
	}
	else
	{
		CachedStateControllerLeftFootContact = 0.0f;
		CachedStateControllerRightFootContact = 0.0f;
	}
}

EProject_JStateControllerFoot UProject_JCharacterAnimInstance::ResolveStateControllerFootFromContactCurves(
	const bool bAllowPhaseHistoryFallback,
	EProject_JStateControllerFootSelectionReason& OutReason) const
{
	auto UsePhaseHistoryOrDefault = [this, bAllowPhaseHistoryFallback, &OutReason]()
	{
		if (bAllowPhaseHistoryFallback && bHasStateControllerFootPhaseHistory)
		{
			OutReason = EProject_JStateControllerFootSelectionReason::PhaseHistoryFallback;
			return StateControllerFootPhaseHistory;
		}

		OutReason = EProject_JStateControllerFootSelectionReason::DefaultFootFallback;
		return StateControllerNoPhaseFootFallback;
	};

	if (!bHasStateControllerFootContactCurves)
	{
		OutReason = EProject_JStateControllerFootSelectionReason::MissingContactCurve;
		return UsePhaseHistoryOrDefault();
	}

	const bool bLeftUnplanted = CachedStateControllerLeftFootContact < StateControllerFootContactDifferenceThreshold;
	const bool bRightUnplanted = CachedStateControllerRightFootContact < StateControllerFootContactDifferenceThreshold;
	if (bLeftUnplanted && bRightUnplanted)
	{
		OutReason = EProject_JStateControllerFootSelectionReason::BothFeetUnplanted;
		return UsePhaseHistoryOrDefault();
	}

	const float ContactDelta = CachedStateControllerLeftFootContact - CachedStateControllerRightFootContact;
	if (FMath::Abs(ContactDelta) < StateControllerFootContactDifferenceThreshold)
	{
		OutReason = EProject_JStateControllerFootSelectionReason::ContactsTooSimilar;
		return UsePhaseHistoryOrDefault();
	}

	// A _Lfoot/_Rfoot one-shot begins by moving that foot. Pick the foot with
	// lower contact (the current swing/airborne foot) to continue the stride.
	if (ContactDelta < 0.0f)
	{
		OutReason = EProject_JStateControllerFootSelectionReason::LeftFootLowerContact;
		return EProject_JStateControllerFoot::Left;
	}

	OutReason = EProject_JStateControllerFootSelectionReason::RightFootLowerContact;
	return EProject_JStateControllerFoot::Right;
}

FAnimInstanceProxy* UProject_JCharacterAnimInstance::CreateAnimInstanceProxy()
{
	return new FProject_JCharacterAnimInstanceProxy(this);
}

void UProject_JCharacterAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}

FProject_JAnimThreadSafeData UProject_JCharacterAnimInstance::GetThreadSafeData() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
}

FTransformTrajectory UProject_JCharacterAnimInstance::GetThreadSafeTrajectory() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.Trajectory;
}

float UProject_JCharacterAnimInstance::GetThreadSafeAimYaw() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Aim.AimYaw;
}

float UProject_JCharacterAnimInstance::GetThreadSafeAimPitch() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Aim.AimPitch;
}

float UProject_JCharacterAnimInstance::GetThreadSafeAimOffsetAlpha() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Aim.AimOffsetAlpha;
}

float UProject_JCharacterAnimInstance::GetThreadSafeGroundSpeed() const
{
	// Chooser tables are evaluated on the game thread before the newly built
	// immutable snapshot is copied into the animation proxy. CHT_Player_Land is
	// bound to this function, so reading the proxy here would expose the previous
	// animation update (commonly zero on an urgent remote landing frame). The
	// reflected mirror is published from the current snapshot immediately before
	// game-thread chooser evaluation. Worker-thread AnimGraph callers continue to
	// consume the immutable proxy only.
	if (IsInGameThread())
	{
		return ChooserGroundSpeed;
	}
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.GroundSpeed;
}

float UProject_JCharacterAnimInstance::GetThreadSafeCombatLocomotionSpeed() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.GroundSpeed;
}

float UProject_JCharacterAnimInstance::GetThreadSafeCombatLocomotionDirection() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.RelativeVelocityDirection;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeCombatLocomotionStartRequested() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Ground.bStartRequested;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeCombatLocomotionStopRequested() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Ground.bStopRequested;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeUsesFullBodyCombatLocomotion() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Combat.PresentationMode ==
		EProject_JCombatAnimationPresentationMode::FullBodyLocomotion;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeUsesCombatUpperBodyOverlay() const
{
	return !GetThreadSafeUsesFullBodyCombatLocomotion();
}

FVector UProject_JCharacterAnimInstance::GetThreadSafeVelocity() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.Velocity;
}

float UProject_JCharacterAnimInstance::GetThreadSafeVerticalSpeed() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.VerticalSpeed;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsAccelerating() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.bIsAccelerating;
}

FVector UProject_JCharacterAnimInstance::GetThreadSafeRelativeAccelerationAmount() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.RelativeAccelerationAmount;
}

FVector2D UProject_JCharacterAnimInstance::GetThreadSafeLeanAmount() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.LeanAmount;
}

float UProject_JCharacterAnimInstance::GetThreadSafePredictedStopDistance() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.PredictedStopDistance;
}

float UProject_JCharacterAnimInstance::GetThreadSafeVelocityToMoveInputAngle() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.VelocityToMoveInputAngle;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsDecelerating() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.bIsDecelerating;
}

float UProject_JCharacterAnimInstance::GetThreadSafeMoveInputSize() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Input.MoveInputSize;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeHasMoveInput() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Input.bHasMoveInput;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsInAir() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Air.bIsInAir;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsJumping() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Air.bIsJumping;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsLanding() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Landing.bIsLanding;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsCombatMode() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Combat.bIsCombatMode;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsMoving() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().LocomotionContext.bIsMoving;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsMotionMatchingMoving() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().LocomotionContext.bIsMotionMatchingMoving;
}

EProject_JLocomotionGaitIntent UProject_JCharacterAnimInstance::GetThreadSafeGaitIntent() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().LocomotionContext.GaitIntent;
}

EProject_JLocomotionRotationMode UProject_JCharacterAnimInstance::GetThreadSafeRotationMode() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().LocomotionContext.RotationMode;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerWantsLocomotion() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return Data.OneShotPresentation.bEnabled &&
		!Data.Air.bIsInAir &&
		Data.LocomotionContext.bIsMotionMatchingMoving;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerWantsIdle() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return Data.OneShotPresentation.bEnabled &&
		!Data.Air.bIsInAir &&
		!Data.LocomotionContext.bIsMotionMatchingMoving;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerIsGrounded() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return Data.OneShotPresentation.bEnabled && !Data.Air.bIsInAir;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerIsInAir() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return Data.OneShotPresentation.bEnabled && Data.Air.bIsInAir;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeExperimentalOneShotEnabled() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bEnabled;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeOneShotRequested() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bRequested;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeOneShotUseMotionMatchOnEntry() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bUseMotionMatchOnEntry;
}

int32 UProject_JCharacterAnimInstance::GetThreadSafeOneShotRequestRevision() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.RequestRevision;
}

EProject_JLocomotionPhaseFamily UProject_JCharacterAnimInstance::GetThreadSafeOneShotPhase() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.PhaseFamily;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeOneShotEarlyTransitionWindowOpen() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bEarlyTransitionWindowOpen;
}

float UProject_JCharacterAnimInstance::GetThreadSafeOneShotFallbackLeadTime() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.FallbackLeadTime;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerLocomotionSemanticStateChanged() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bLocomotionSemanticStateChanged;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerIdleSemanticStateChanged() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bIdleSemanticStateChanged;
}

EProject_JStateControllerStrafeDirection UProject_JCharacterAnimInstance::GetThreadSafeStateControllerStrafeDirection() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.StrafeDirection;
}

EProject_JStateControllerStrafeDirection UProject_JCharacterAnimInstance::GetThreadSafeStateControllerPreviousStrafeDirection() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.PreviousStrafeDirection;
}

EProject_JStateControllerStance UProject_JCharacterAnimInstance::GetThreadSafeStateControllerStance() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.Stance;
}

EProject_JStateControllerPresentationState UProject_JCharacterAnimInstance::GetThreadSafeStateControllerPresentationState() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.PresentationState;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerShouldTurnInPlace() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return Data.LocomotionContext.bShouldTurnInPlace;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerShouldAbortTurnInPlace() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	// Do not use GenericMoveInputSpeedThreshold here.  TIP itself is allowed to
	// start during the final stationary braking window (IdleSpeedThreshold), so
	// the generic 3 cm/s movement threshold used to abort the state immediately.
	// The locomotion component is the single source of truth for whether we are
	// still stationary enough to finish this direct animation.
	return !Data.OneShotPresentation.bEnabled ||
		Data.Air.bIsInAir ||
		Data.Input.bHasMoveInput ||
		!Data.LocomotionContext.bShouldTurnInPlace;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerTurnInPlaceSteeringAlpha() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::TurnInPlace
		? 1.0f
		: 0.0f;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerTurnInPlaceIndex() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	const float DeltaYaw = Data.LocomotionContext.DesiredFacingDeltaYaw;

	if (DeltaYaw >= -135.0f && DeltaYaw <= -30.0f)
	{
		return 1.0f; // Left 090
	}
	if (DeltaYaw < -135.0f || DeltaYaw <= -180.0f)
	{
		return 2.0f; // Left 180
	}
	if (DeltaYaw >= 30.0f && DeltaYaw < 135.0f)
	{
		return 3.0f; // Right 090
	}
	if (DeltaYaw >= 135.0f && DeltaYaw <= 180.0f)
	{
		return 4.0f; // Right 180
	}

	return 0.0f;
}

FRotator UProject_JCharacterAnimInstance::GetThreadSafeStateControllerDesiredFacingRotator() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return FRotator(0.0f, Data.LocomotionContext.DesiredFacingYaw, 0.0f);
}

EOffsetRootBoneMode UProject_JCharacterAnimInstance::GetThreadSafeOffsetRootRotationMode() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	if (!Data.OneShotPresentation.bEnabled || Data.Air.bIsInAir)
	{
		return EOffsetRootBoneMode::Release;
	}

	// Orient-to-Movement: Release continuously clears any root rotation offset to 0,
	// keeping the mesh perfectly centered on the capsule during OTM locomotion.
	if (Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::OrientToMovement)
	{
		return EOffsetRootBoneMode::Release;
	}

	// Combat Strafe TIP: allow Steering/OffsetRootBone to absorb procedural root rotation.
	if (Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::TurnInPlace)
	{
		return EOffsetRootBoneMode::Interpolate;
	}

	// Combat Strafe non-TIP: smoothly release any residual offset back to zero.
	return EOffsetRootBoneMode::Release;
}

EOffsetRootBoneMode UProject_JCharacterAnimInstance::GetThreadSafeOffsetRootTranslationMode() const
{
	// Always return Release so the visual mesh root position stays 100% centered
	// on the physical capsule cylinder without off-center translation drifting.
	return EOffsetRootBoneMode::Release;
}

float UProject_JCharacterAnimInstance::GetThreadSafeOffsetRootTranslationHalfLife() const
{
	return 0.1f;
}

float UProject_JCharacterAnimInstance::GetThreadSafeOffsetRootTranslationRadius() const
{
	return 30.0f;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerDisableLegIK() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return Data.Air.bIsInAir;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerLegIKAlpha() const
{
	return GetThreadSafeStateControllerDisableLegIK() ? 0.0f : 1.0f;
}

void UProject_JCharacterAnimInstance::OnStateEntry_TurnInPlace(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	// Empty stub for Blueprint State Machine On Entry Binding
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerIdleBreakEnabled() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bIdleBreakEnabled;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerIdleBreakMinimumStateTime() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.IdleBreakMinimumStateTime;
}

UAnimationAsset* UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimation() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.SelectedAnimation;
}

FProject_JStateControllerChooserOutput UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimationOutput() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.SelectedAnimationOutput;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimationStartTime() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.SelectedAnimationOutput.StartTime;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerPlaybackHoldElapsedTime() const
{
	const double NowSeconds = FPlatformTime::Seconds();
	return FMath::Max(static_cast<float>(NowSeconds - StateControllerPlaybackHoldStartedAtSeconds), 0.0f);
}

int32 UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectionRevision() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.SelectionRevision;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerShouldForceBlend() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bForceBlendNextUpdate;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimationBlendTime() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.SelectedAnimationOutput.BlendTime;
}

UBlendProfile* UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimationBlendProfile() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.SelectedAnimationOutput.BlendProfile;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimationShouldLoop() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bSelectedAnimationShouldLoop;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerHasSelectedAnimation() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bHasSelectedAnimation;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerShouldOverrideMotionMatching() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bShouldOverrideMotionMatching;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerCombatStrafeOrientationWarpingAlpha() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bShouldEnableCombatStrafeOrientationWarping
		? 1.0f
		: 0.0f;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerCombatStrafeOrientationWarpingAngle() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.StrafeDirectionAngle;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimationAlmostComplete() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bTransitionAnimationAlmostComplete;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimationTimeRemaining() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.TransitionTimeRemaining;
}

void UProject_JCharacterAnimInstance::BeginOneShotEarlyTransitionWindow()
{
	++OneShotEarlyTransitionWindowDepth;
}

void UProject_JCharacterAnimInstance::EndOneShotEarlyTransitionWindow()
{
	OneShotEarlyTransitionWindowDepth = FMath::Max(OneShotEarlyTransitionWindowDepth - 1, 0);
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsMounted() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.bIsMounted;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeMountedIsFlying() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.bIsFlying;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeMountedIsGliding() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.bIsGliding;
}

float UProject_JCharacterAnimInstance::GetThreadSafeMountedSpeed() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.Speed;
}

float UProject_JCharacterAnimInstance::GetThreadSafeMountedVerticalSpeed() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.VerticalSpeed;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeHasMountedHandIKTargets() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.bHasHandIKTargets;
}

FVector UProject_JCharacterAnimInstance::GetThreadSafeMountedLeftHandTargetComponentSpace() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.LeftHandTargetComponentSpace;
}

FVector UProject_JCharacterAnimInstance::GetThreadSafeMountedRightHandTargetComponentSpace() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.RightHandTargetComponentSpace;
}

EProject_JAnimationLocomotionMode UProject_JCharacterAnimInstance::GetThreadSafeLocomotionMode() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().LocomotionMode;
}

FFootPlacementPlantSettings UProject_JCharacterAnimInstance::Get_FootPlacementPlantSettings() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Data.Ground.bStopRequested ? Profile->FootPlacementPlantSettingsStops : Profile->FootPlacementPlantSettingsDefault;
	}

	return Data.Ground.bStopRequested ? FootPlacementPlantSettingsStops : FootPlacementPlantSettingsDefault;
}

FFootPlacementInterpolationSettings UProject_JCharacterAnimInstance::Get_FootPlacementInterpolationSettings() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Data.Ground.bStopRequested ? Profile->FootPlacementInterpolationSettingsStops : Profile->FootPlacementInterpolationSettingsDefault;
	}

	return Data.Ground.bStopRequested ? FootPlacementInterpolationSettingsStops : FootPlacementInterpolationSettingsDefault;
}

float UProject_JCharacterAnimInstance::GetThreadSafeFootPlacementAlpha() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().ProceduralIK.FootPlacementAlpha;
}

float UProject_JCharacterAnimInstance::GetThreadSafeLegIKAlpha() const
{
	const float BaseLegIKAlpha = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().ProceduralIK.LegIKAlpha;
	const float StateControllerAlpha = GetThreadSafeStateControllerLegIKAlpha();
	return FMath::Min(BaseLegIKAlpha, StateControllerAlpha);
}

float UProject_JCharacterAnimInstance::GetThreadSafeFullBodyMontageWeight() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().ProceduralIK.FullBodyMontageWeight;
}

UPoseSearchDatabase* UProject_JCharacterAnimInstance::GetCurrentActivePoseSearchDatabaseThreadSafe() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetCurrentActiveDatabase();
}

FString UProject_JCharacterAnimInstance::GetAnimationDebugSummary() const
{
	using Project_J::LocomotionDebug::ToDebugString;

	const FProject_JAnimThreadSafeData& Data = ThreadSafeData;
	const bool bSprintAllowed = OwningPlayerCharacter && OwningPlayerCharacter->IsSprintLocomotionAllowed();
	const bool bJumpAllowed = OwningPlayerCharacter && OwningPlayerCharacter->IsJumpLocomotionAllowed();
	const UProject_JWeaponAnimProfile* WeaponAnimProfile = OwningPlayerCharacter ? OwningPlayerCharacter->GetWeaponAnimProfile() : nullptr;
	return FString::Printf(
		TEXT("Optimization Tier=%d UpdateData=%s FullChooser=%s FarOnly=%s MMInterval=%.3f ActivePSD=%s\n")
		TEXT("Weapon Profile=%s Stance=%s Presentation=%d\n")
		TEXT("Movement GroundSpeed=%.1f VerticalSpeed=%.1f AccelRatio=%.2f RelAccel=(%.2f,%.2f) Lean=(%.2f,%.2f) Decel=%s StopDist=%.1f VelocityToInput=%.1f HasTrajectory=%s StoppedAccel=%s\n")
		TEXT("Input Has=%s Size=%.2f Held=%.2f Turn=%.1f SharpTurn=%s MoveDir=%.1f\n")
		TEXT("Context Gait=%s Rotation=%s Phase=%s Starting=%s Pivoting=%s TurnInPlace=%s Spin=%s DesiredYaw=%.1f\n")
		TEXT("Policy SprintAllowed=%s JumpAllowed=%s Combat=%s Attack=%s Dodge=%s HitReact=%s\n")
		TEXT("Ground Mode=%d Start=%s Stop=%s WantsSprint=%s UseSprint=%s StartSprint=%s StopSprint=%s\n")
		TEXT("Air InAir=%s Jumping=%s FallOff=%s Landing=%s HeavyLand=%s LandMoving=%s LandSprint=%s\n")
		TEXT("MM Revision=%d Changed=%s ForceReselect=%s TrajectorySamples=%d NativePoseHistory=true\n")
		TEXT("Chooser Start=%s Stop=%s RunLoc=%s RemoteRunLoc=%s SprintLoc=%s Jump=%s Fall=%s Land=%s Combat=%s Remote=%s"),
		static_cast<int32>(CurrentOptimizationPolicy.Tier),
		CurrentOptimizationPolicy.bUpdateAnimationData ? TEXT("true") : TEXT("false"),
		CurrentOptimizationPolicy.bUseFullChooserRows ? TEXT("true") : TEXT("false"),
		CurrentOptimizationPolicy.bUseFarChooserRowsOnly ? TEXT("true") : TEXT("false"),
		CurrentOptimizationPolicy.MotionMatchingUpdateInterval,
		*GetNameSafe(CurrentActivePoseSearchDatabase),
		*GetNameSafe(WeaponAnimProfile),
		WeaponAnimProfile ? ToDebugString(WeaponAnimProfile->WeaponStance) : TEXT("None"),
		static_cast<int32>(Data.Combat.PresentationMode),
		Data.Movement.GroundSpeed,
		Data.Movement.VerticalSpeed,
		Data.Movement.AccelerationRatio,
		Data.Movement.RelativeAccelerationAmount.X,
		Data.Movement.RelativeAccelerationAmount.Y,
		Data.Movement.LeanAmount.X,
		Data.Movement.LeanAmount.Y,
		Data.Movement.bIsDecelerating ? TEXT("true") : TEXT("false"),
		Data.Movement.PredictedStopDistance,
		Data.Movement.VelocityToMoveInputAngle,
		Data.Movement.bHasTrajectory ? TEXT("true") : TEXT("false"),
		Data.Movement.bStoppedAcceleratingThisFrame ? TEXT("true") : TEXT("false"),
		Data.Input.bHasMoveInput ? TEXT("true") : TEXT("false"),
		Data.Input.MoveInputSize,
		Data.Input.MoveInputHeldTime,
		Data.Input.MoveInputTurnAngle,
		Data.Input.bSharpTurnRequested ? TEXT("true") : TEXT("false"),
		Data.Input.MovementDirection,
		ToDebugString(Data.LocomotionContext.GaitIntent),
		ToDebugString(Data.LocomotionContext.RotationMode),
		ToDebugString(Data.LocomotionContext.PhaseFamily),
		Data.LocomotionContext.bIsStarting ? TEXT("true") : TEXT("false"),
		Data.LocomotionContext.bIsPivoting ? TEXT("true") : TEXT("false"),
		Data.LocomotionContext.bShouldTurnInPlace ? TEXT("true") : TEXT("false"),
		Data.LocomotionContext.bShouldSpinTransition ? TEXT("true") : TEXT("false"),
		Data.LocomotionContext.DesiredFacingDeltaYaw,
		bSprintAllowed ? TEXT("true") : TEXT("false"),
		bJumpAllowed ? TEXT("true") : TEXT("false"),
		Data.Combat.bIsCombatMode ? TEXT("true") : TEXT("false"),
		Data.Combat.bIsAttacking ? TEXT("true") : TEXT("false"),
		Data.Combat.bIsDodging ? TEXT("true") : TEXT("false"),
		Data.Combat.bIsHitReacting ? TEXT("true") : TEXT("false"),
		static_cast<int32>(Data.Ground.GroundMotionMode),
		Data.Ground.bStartRequested ? TEXT("true") : TEXT("false"),
		Data.Ground.bStopRequested ? TEXT("true") : TEXT("false"),
		Data.Ground.bWantsSprint ? TEXT("true") : TEXT("false"),
		Data.Ground.bUseSprintLocomotion ? TEXT("true") : TEXT("false"),
		Data.Ground.bStartWasSprinting ? TEXT("true") : TEXT("false"),
		Data.Ground.bStopWasSprinting ? TEXT("true") : TEXT("false"),
		Data.Air.bIsInAir ? TEXT("true") : TEXT("false"),
		Data.Air.bIsJumping ? TEXT("true") : TEXT("false"),
		Data.Air.bIsFallOffStart ? TEXT("true") : TEXT("false"),
		Data.Landing.bIsLanding ? TEXT("true") : TEXT("false"),
		Data.Landing.bUseHeavyLand ? TEXT("true") : TEXT("false"),
		Data.Landing.bLandWasMoving ? TEXT("true") : TEXT("false"),
		Data.Landing.bLandWasSprinting ? TEXT("true") : TEXT("false"),
		Data.MotionMatching.SelectionRevision,
		Data.MotionMatching.bSelectionChanged ? TEXT("true") : TEXT("false"),
		Data.MotionMatching.bForceReselect ? TEXT("true") : TEXT("false"),
		Data.MotionMatching.TrajectorySampleCount,
		bChooserStartRequested ? TEXT("true") : TEXT("false"),
		bChooserStopRequested ? TEXT("true") : TEXT("false"),
		bChooserUseRunLocomotion ? TEXT("true") : TEXT("false"),
		bChooserUseRemoteRunLocomotion ? TEXT("true") : TEXT("false"),
		bChooserUseSprintLocomotionRow ? TEXT("true") : TEXT("false"),
		bChooserUseJumpStart ? TEXT("true") : TEXT("false"),
		(bChooserUseFallOff || bChooserUseFallLoop) ? TEXT("true") : TEXT("false"),
		bChooserIsLanding ? TEXT("true") : TEXT("false"),
		bChooserIsCombatMode ? TEXT("true") : TEXT("false"),
		bChooserIsRemoteProxy ? TEXT("true") : TEXT("false"));
}

FName UProject_JCharacterAnimInstance::GetThreadSafeMotionMatchingSelectedAnimation() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().MotionMatching.PostSelection.SelectedAnimation;
}

float UProject_JCharacterAnimInstance::GetThreadSafeMotionMatchingSelectedAnimationProgress() const
{
	const FProject_JAnimMotionMatchingPostSelectionData& PostSelection =
		GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().MotionMatching.PostSelection;
	return PostSelection.SelectedAnimationLength > UE_KINDA_SMALL_NUMBER
		? FMath::Clamp(PostSelection.SelectedAnimationTime / PostSelection.SelectedAnimationLength, 0.0f, 1.0f)
		: 0.0f;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeMotionMatchingSelectionIsContinuing() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().MotionMatching.PostSelection.bIsContinuingPoseSearch;
}

FString UProject_JCharacterAnimInstance::GetMotionMatchingTraceSummary() const
{
	using Project_J::LocomotionDebug::ToDebugString;

	FString Summary = FString::Printf(TEXT("==== Motion Matching Trace (%d entries) ====\n"), MotionMatchingTrace.Num());
	for (int32 EntryIndex = 0; EntryIndex < MotionMatchingTrace.Num(); ++EntryIndex)
	{
		const FProject_JMotionMatchingTraceEntry& Entry = MotionMatchingTrace[EntryIndex];
		const double UntilNextSeconds = EntryIndex + 1 < MotionMatchingTrace.Num()
			? MotionMatchingTrace[EntryIndex + 1].WorldTimeSeconds - Entry.WorldTimeSeconds
			: -1.0;
		Summary += FString::Printf(
			TEXT("t=%.3f Next=%.3f Rev=%d PSD=%s Anim=%s AnimTime=%.3f/%.3f PlayRate=%.2f Continuing=%s CurveSpeed=%.1f Warp=%.2f CurvePhase=%.2f Phase=%s Ground=%d Age=%.3f Gait=%s Rotation=%s Speed=%.1f FutureSpeed=%.1f Gain=%.1f FutureTurn=%.1f FutureValid=%s StopDist=%.1f Input=%s Accel=%s Decel=%s InputTurn=%.1f Trajectory=%d DBChanged=%s ForceReselect=%s\n"),
			Entry.WorldTimeSeconds,
			UntilNextSeconds,
			Entry.SelectionRevision,
			*Entry.DatabaseName,
			*Entry.SelectedAnimationName,
			Entry.SelectedAnimationTime,
			Entry.SelectedAnimationLength,
			Entry.SelectedAnimationWantedPlayRate,
			Entry.bSelectionIsContinuing ? TEXT("true") : TEXT("false"),
			Entry.MoveDataSpeedCurve,
			Entry.EnableWarpingCurve,
			Entry.PhaseCurve,
			ToDebugString(Entry.PhaseFamily),
			static_cast<int32>(Entry.GroundMotionMode),
			Entry.GroundModeAgeSeconds,
			ToDebugString(Entry.GaitIntent),
			ToDebugString(Entry.RotationMode),
			Entry.GroundSpeed,
			Entry.FutureTrajectorySpeed,
			Entry.PredictedSpeedGain,
			Entry.FutureTrajectoryTurnAngle,
			Entry.bHasFutureTrajectoryVelocity ? TEXT("true") : TEXT("false"),
			Entry.PredictedStopDistance,
			Entry.bHasMoveInput ? TEXT("true") : TEXT("false"),
			Entry.bIsAccelerating ? TEXT("true") : TEXT("false"),
			Entry.bIsDecelerating ? TEXT("true") : TEXT("false"),
			Entry.InputTurnAngle,
			Entry.TrajectorySampleCount,
			Entry.bDatabaseChanged ? TEXT("true") : TEXT("false"),
			Entry.bForceReselect ? TEXT("true") : TEXT("false"));
	}
	return Summary;
}

FString UProject_JCharacterAnimInstance::GetMotionMatchingPivotTraceSummary() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetPivotTraceSummary();
}

FProject_JAnimThreadSafeData UProject_JCharacterAnimInstance::BuildThreadSafeData(float DeltaSeconds) const
{
	FProject_JAnimThreadSafeData Data;
	Data.DeltaTime = DeltaSeconds;
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		Data.MotionMatchingSearchPolicy = Profile->MotionMatchingSearchPolicy;
	}

	if (!OwningCharacter)
	{
		return Data;
	}

	FillMovementThreadSafeData(Data);
	if (LocomotionAnimStateComponent)
	{
		FillLocomotionStateThreadSafeData(Data);
	}
	else
	{
		ApplyGenericMovementFallback(Data);
	}

	const bool bHasAimData = FillPlayerThreadSafeData(Data);
	FillMountThreadSafeData(Data);
	FinalizeThreadSafeData(Data, bHasAimData);
	FillProceduralIKThreadSafeData(Data);

	return Data;
}

void UProject_JCharacterAnimInstance::FillMovementThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
	const FVector CharacterVelocity = OwningCharacter->GetVelocity();
	Data.Movement.Velocity = CharacterVelocity;
	const FVector GroundVelocity(CharacterVelocity.X, CharacterVelocity.Y, 0.0f);
	Data.Movement.GroundSpeed = GroundVelocity.Size();
	if (Data.Movement.GroundSpeed > KINDA_SMALL_NUMBER)
	{
		const FVector LocalGroundVelocity = OwningCharacter->GetActorQuat().UnrotateVector(GroundVelocity);
		Data.Movement.RelativeVelocityDirection = FMath::RadiansToDegrees(FMath::Atan2(LocalGroundVelocity.Y, LocalGroundVelocity.X));
	}
	else
	{
		Data.Movement.RelativeVelocityDirection = 0.0f;
	}
	Data.Movement.VerticalSpeed = CharacterVelocity.Z;

	if (const UCharacterMovementComponent* MovementComponent = OwningCharacter->GetCharacterMovement())
	{
		Data.Movement.Acceleration = MovementComponent->GetCurrentAcceleration();
		Data.Movement.AccelerationDirection = Data.Movement.Acceleration.GetSafeNormal();
		Data.Movement.bIsAccelerating = Data.Movement.Acceleration.SizeSquared2D() > UE_KINDA_SMALL_NUMBER;
		Data.Air.bIsInAir = MovementComponent->IsFalling();

		const float MaxAcceleration = FMath::Max(MovementComponent->GetMaxAcceleration(), UE_KINDA_SMALL_NUMBER);
		Data.Movement.AccelerationRatio = FMath::Clamp(Data.Movement.Acceleration.Size2D() / MaxAcceleration, 0.0f, 1.0f);
	}

	Data.Movement.bWasAccelerating = ThreadSafeData.Movement.bIsAccelerating;
	Data.Movement.bStoppedAcceleratingThisFrame = Data.Movement.bWasAccelerating && !Data.Movement.bIsAccelerating;
}

void UProject_JCharacterAnimInstance::FillLocomotionStateThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
	const UProject_JLocomotionAnimStateComponent* AnimState = LocomotionAnimStateComponent.Get();
	if (!AnimState)
	{
		return;
	}

	Data.Input.MoveInputSize = AnimState->MoveInputSize;
	Data.Input.MoveInputHeldTime = AnimState->MoveInputHeldTime;
	Data.Input.MoveInputTurnAngle = AnimState->MoveInputTurnAngle;
	Data.Input.MovementDirection = AnimState->MovementDirection;
	Data.Input.bHasMoveInput = AnimState->bHasMoveInput;
	Data.Input.bSharpTurnRequested = AnimState->bSharpTurnRequested;
	Data.Ground.bStartRequested = AnimState->bStartRequested || AnimState->bUseStartDatabase;
	Data.Ground.bStopRequested = AnimState->bStopRequested || AnimState->bUseStopDatabase;
	Data.Ground.bWantsSprint = AnimState->bWantsSprint;
	Data.Ground.bUseSprintLocomotion = AnimState->bUseSprintLocomotion;
	Data.Ground.bStartWasSprinting =
		AnimState->bStartWasSprinting ||
		(IsLocallyControlledCharacter() &&
		 Data.Ground.bStartRequested &&
		 Data.Ground.bWantsSprint &&
		 Data.Input.bHasMoveInput);
	Data.Ground.bStopWasSprinting = AnimState->bStopWasSprinting;
	Data.Ground.GroundMotionMode = AnimState->GroundMotionMode;
	Data.Ground.GroundMotionModeElapsedTime = AnimState->GetGroundMotionModeElapsedTime();
	Data.Ground.StartResponsiveExitRevision = AnimState->StartResponsiveExitRevision;
	Data.Air.bIsJumping = AnimState->bIsJumping;
	Data.Air.bIsFallOffStart = AnimState->bIsFallOffStart;
	Data.Landing.PresentationRevision = AnimState->LandingPresentationRevision;
	Data.Landing.bIsLanding = AnimState->bIsLanding || AnimState->bLandingRequested;
	Data.Landing.bUseHeavyLand = AnimState->bUseHeavyLand;
	Data.Landing.bLandWasSprinting = AnimState->bLandWasSprinting;
	Data.Landing.bLandWasMoving = AnimState->bLandWasMoving;
	Data.Landing.LastFallSpeed = AnimState->LastFallSpeed;
	Data.Landing.LandStartFallSpeed = AnimState->LandStartFallSpeed;
	Data.LocomotionContext.GaitIntent = AnimState->AuthoritativeContext.GaitIntent;
	Data.LocomotionContext.RotationMode = AnimState->AuthoritativeContext.RotationMode;
	Data.LocomotionContext.PhaseFamily = AnimState->DerivedLocomotionContext.PhaseFamily;
	Data.LocomotionContext.DesiredFacingDeltaYaw = AnimState->KinematicContext.DesiredFacingDeltaYaw;
	Data.LocomotionContext.DesiredFacingYaw = AnimState->KinematicContext.DesiredFacingYaw;
	Data.LocomotionContext.bIsMoving = AnimState->DerivedLocomotionContext.bIsMoving;
	Data.LocomotionContext.bIsMotionMatchingMoving = AnimState->DerivedLocomotionContext.bIsMotionMatchingMoving;
	Data.LocomotionContext.bIsStarting = AnimState->DerivedLocomotionContext.bIsStarting;
	Data.LocomotionContext.bIsPivoting = AnimState->DerivedLocomotionContext.bIsPivoting;
	Data.LocomotionContext.bShouldTurnInPlace = AnimState->DerivedLocomotionContext.bShouldTurnInPlace;
	Data.LocomotionContext.bShouldSpinTransition = AnimState->DerivedLocomotionContext.bShouldSpinTransition;
	Data.Movement.RelativeAccelerationAmount = AnimState->KinematicContext.RelativeAccelerationAmount;
	Data.Movement.PredictedStopDistance = AnimState->KinematicContext.PredictedStopDistance;
	Data.Movement.PredictedSpeedGain = AnimState->KinematicContext.PredictedSpeedGain;
	Data.Movement.FutureTrajectoryVelocity = AnimState->KinematicContext.FutureTrajectoryVelocity;
	Data.Movement.FutureTrajectorySpeed = AnimState->KinematicContext.FutureTrajectorySpeed;
	Data.Movement.FutureTrajectoryTurnAngle = AnimState->KinematicContext.FutureTrajectoryTurnAngle;
	Data.Movement.bHasFutureTrajectoryVelocity = AnimState->KinematicContext.bHasFutureTrajectoryVelocity;
	Data.Movement.VelocityToMoveInputAngle = AnimState->KinematicContext.VelocityToMoveInputAngle;
	Data.Movement.bIsDecelerating = AnimState->KinematicContext.bIsDecelerating;
	Data.MotionMatching.SelectionRevision = AnimState->MotionMatchingSelectionRevision;
	Data.MotionMatching.bSelectionChanged = AnimState->bMotionMatchingSelectionChanged;
	Data.MotionMatching.bForceReselect = AnimState->bForceMotionMatchingReselect;
	Data.MotionMatching.SelectionContext = AnimState->GetMotionMatchingSelectionContext();
	Data.MotionMatching.PostSelection = GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>().GetLatestPostSelection();

	FProject_JAnimOneShotPresentationThreadSafeData& OneShot = Data.OneShotPresentation;
	OneShot.bEnabled = Data.MotionMatchingSearchPolicy.bEnableExperimentalOneShotPresentation;
	OneShot.bUseMotionMatchOnEntry =
		OneShot.bEnabled && Data.MotionMatchingSearchPolicy.bUseMotionMatchForExperimentalOneShotEntry;
	OneShot.bEarlyTransitionWindowOpen = OneShotEarlyTransitionWindowDepth > 0;
	OneShot.FallbackLeadTime = Data.MotionMatchingSearchPolicy.ExperimentalOneShotFallbackLeadTime;
	OneShot.bIdleBreakEnabled = OneShot.bEnabled && Data.MotionMatchingSearchPolicy.bEnableExperimentalIdleBreak;
	OneShot.IdleBreakMinimumStateTime = OneShot.bIdleBreakEnabled
		? Data.MotionMatchingSearchPolicy.ExperimentalIdleBreakMinimumStateTime
		: 0.0f;
	OneShot.RequestRevision = Data.MotionMatching.SelectionRevision;
	OneShot.PhaseFamily = Data.LocomotionContext.PhaseFamily;
	OneShot.RotationMode = Data.LocomotionContext.RotationMode;
	const FProject_JAnimOneShotPresentationThreadSafeData& PreviousOneShot = ThreadSafeData.OneShotPresentation;
	const bool bWasStrafing = ThreadSafeData.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe;
	const bool bContinuePivot =
		OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot &&
		PreviousOneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot;
	const EProject_JStateControllerStrafeDirection PreviousStrafeDirection = bWasStrafing
		? (bContinuePivot ? PreviousOneShot.PreviousStrafeDirection : PreviousOneShot.StrafeDirection)
		: EProject_JStateControllerStrafeDirection::Forward;
	OneShot.PreviousStrafeDirection = PreviousStrafeDirection;
	OneShot.StrafeDirection = EProject_JStateControllerStrafeDirection::Forward;
	OneShot.StrafeDirectionAngle = 0.0f;
	OneShot.bHasStrafeDirectionAngle = false;
	if (OneShot.RotationMode == EProject_JLocomotionRotationMode::Strafe)
	{
		FVector DirectionVelocity = Data.Movement.bHasFutureTrajectoryVelocity
			? Data.Movement.FutureTrajectoryVelocity
			: Data.Movement.Velocity;
		DirectionVelocity.Z = 0.0f;
		const UProject_JCombatAnimProfile* CombatProfile = GetCombatAnimProfile();
		const float MinimumDirectionSpeed = CombatProfile ? CombatProfile->StrafeDirectionMinimumSpeed : 10.0f;
		if (DirectionVelocity.SizeSquared2D() > FMath::Square(MinimumDirectionSpeed))
		{
			const float ActorYaw = OwningCharacter ? OwningCharacter->GetActorRotation().Yaw : 0.0f;
			OneShot.StrafeDirectionAngle = FMath::FindDeltaAngleDegrees(ActorYaw, DirectionVelocity.Rotation().Yaw);
			OneShot.bHasStrafeDirectionAngle = true;
			OneShot.StrafeDirection = ResolveStateControllerStrafeDirection(
				OneShot.StrafeDirectionAngle,
				PreviousStrafeDirection,
				CombatProfile ? CombatProfile->StrafeDirectionHysteresisDegrees : 7.5f);
		}
		else
		{
			// A stopped combatant has no meaningful movement direction. Preserve the
			// previous sector so Stop/Fall/Pivot chooser rows stay deterministic.
			OneShot.StrafeDirection = PreviousStrafeDirection;
		}
	}
	OneShot.Stance = OwningCharacter && OwningCharacter->bIsCrouched
		? EProject_JStateControllerStance::Crouch
		: EProject_JStateControllerStance::Stand;

	// State Controller re-entry is presentation-only. Compare two immutable
	// game-thread snapshots, then expose the result to the worker thread. This
	// intentionally does not use raw angle deltas: remote velocity can vary by a
	// few degrees every replication update and would otherwise restart a state.
	const FProject_JAnimThreadSafeData& PreviousData = ThreadSafeData;
	const bool bCanCompareStateControllerContext =
		OneShot.bEnabled &&
		PreviousData.OneShotPresentation.bEnabled &&
		!Data.Air.bIsInAir &&
		!PreviousData.Air.bIsInAir;
	const bool bRotationModeChanged =
		Data.LocomotionContext.RotationMode != PreviousData.LocomotionContext.RotationMode;
	const bool bGaitChanged =
		Data.LocomotionContext.GaitIntent != PreviousData.LocomotionContext.GaitIntent;
	const bool bLocomotionStanceChanged =
		OneShot.Stance != PreviousData.OneShotPresentation.Stance;
	const bool bStrafeDirectionChanged =
		Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe &&
		PreviousData.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe &&
		OneShot.StrafeDirection != PreviousData.OneShotPresentation.StrafeDirection &&
		(OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot || Data.Input.bSharpTurnRequested);

	OneShot.bLocomotionSemanticStateChanged = bCanCompareStateControllerContext &&
		(bGaitChanged || bRotationModeChanged || bLocomotionStanceChanged || bStrafeDirectionChanged);
	OneShot.bIdleSemanticStateChanged = bCanCompareStateControllerContext &&
		bLocomotionStanceChanged;

	// Cycle stays in the regular MM node. These phase families are presentation
	// requests only; the future State Controller owns the one-shot lifetime.
	switch (OneShot.PhaseFamily)
	{
	case EProject_JLocomotionPhaseFamily::Start:
		OneShot.bRequested = OneShot.bEnabled;
		break;
	case EProject_JLocomotionPhaseFamily::Pivot:
	case EProject_JLocomotionPhaseFamily::TurnInPlace:
		OneShot.bRequested = OneShot.bEnabled &&
			OneShot.RotationMode == EProject_JLocomotionRotationMode::Strafe;
		break;
	case EProject_JLocomotionPhaseFamily::Stop:
	case EProject_JLocomotionPhaseFamily::Landing:
	case EProject_JLocomotionPhaseFamily::JumpStart:
	case EProject_JLocomotionPhaseFamily::Fall:
		OneShot.bRequested = OneShot.bEnabled;
		break;
	default:
		OneShot.bRequested = false;
		break;
	}

	ResolveStateControllerPresentationStateWithPlaybackHold(Data, OneShot);
}

void UProject_JCharacterAnimInstance::ResolveStateControllerPresentationStateWithPlaybackHold(
	const FProject_JAnimThreadSafeData& Data,
	FProject_JAnimOneShotPresentationThreadSafeData& InOutOneShot) const
{
	EProject_JStateControllerPresentationState DesiredState =
		ResolveStateControllerPresentationState(Data, InOutOneShot);
	const double NowSeconds = FPlatformTime::Seconds();
	bStateControllerForceTurnInPlaceReselect = false;

	// GASP leaves Locomotion Loop as soon as its trajectory based IsMoving
	// predicate becomes false.  Do the same even while the physical Character
	// Movement component is still decelerating and has not yet entered its Stop
	// phase.  This produces an early authored Stop selection without changing
	// gameplay movement or the regular Motion Matching branch.
	const bool bStoppedFromPresentedLocomotion =
		!Data.Air.bIsInAir &&
		!Data.LocomotionContext.bIsMotionMatchingMoving &&
		DesiredState == EProject_JStateControllerPresentationState::IdleLoop &&
		(StateControllerPlaybackHoldState == EProject_JStateControllerPresentationState::TransitionToLocomotion ||
			StateControllerPlaybackHoldState == EProject_JStateControllerPresentationState::LocomotionLoop);
	if (bStoppedFromPresentedLocomotion)
	{
		DesiredState = EProject_JStateControllerPresentationState::TransitionToIdle;
	}

	const EProject_JStateControllerPresentationState RequestedState = DesiredState;
	const EProject_JStateControllerPresentationState PreviousHeldState = StateControllerPlaybackHoldState;
	const bool bHeldTransition = IsTransitionState(StateControllerPlaybackHoldState);
	const bool bNaturalContinuation = bHeldTransition &&
		IsNaturalLoopContinuation(StateControllerPlaybackHoldState, DesiredState);
	const bool bNewLandingPresentation =
		DesiredState == EProject_JStateControllerPresentationState::TransitionToLand &&
		Data.Landing.PresentationRevision != StateControllerHeldLandingPresentationRevision;

	// Gameplay intent changes must replace a transition immediately.  In
	// particular, releasing movement during Start must enter Stop on that frame;
	// BP_NotifyState_EarlyTransition only controls Transition -> Loop/Re-enter,
	// never Start -> Stop or Stop -> Start intent replacement.
	bool bStartedNewPlaybackHold = bNewLandingPresentation || !bHeldTransition ||
		(!bNaturalContinuation && DesiredState != StateControllerPlaybackHoldState);
	if (bStartedNewPlaybackHold)
	{
		StateControllerPlaybackHoldState = DesiredState;
		StateControllerPlaybackHoldStartedAtSeconds = NowSeconds;
		if (bNewLandingPresentation)
		{
			StateControllerHeldLandingPresentationRevision = Data.Landing.PresentationRevision;
		}
		bStartedNewPlaybackHold = IsTransitionState(DesiredState);
	}

	InOutOneShot.TransitionElapsedTime = 0.0f;
	InOutOneShot.TransitionTimeRemaining = 0.0f;
	InOutOneShot.bTransitionAnimationAlmostComplete = false;

	if (!IsTransitionState(StateControllerPlaybackHoldState))
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
		InOutOneShot.PresentationState = StateControllerPlaybackHoldState;
		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("StateControllerTransition Actor=%s Requested=%d PreviousHeld=%d NewHeld=%d LandEpoch=%d NewLand=%s Reason=GameplayIntentReplacement"),
				*GetNameSafe(OwningCharacter),
				static_cast<int32>(RequestedState),
				static_cast<int32>(PreviousHeldState),
				static_cast<int32>(StateControllerPlaybackHoldState),
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
		StateControllerPlaybackHoldState == EProject_JStateControllerPresentationState::TransitionToInAir &&
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
	const float Elapsed = FMath::Max(static_cast<float>(NowSeconds - StateControllerPlaybackHoldStartedAtSeconds), 0.0f);
	const float LeadTime = FMath::Max(InOutOneShot.FallbackLeadTime, 0.0f);
	const float Remaining = FMath::Max(EffectivePlayableLength - Elapsed, 0.0f);
	const bool bHasPlayableTransitionAsset = HeldAsset &&
		EffectivePlayableLength > KINDA_SMALL_NUMBER;
	const bool bReachedCompletion = bHasPlayableTransitionAsset && Remaining <= LeadTime;

	InOutOneShot.TransitionElapsedTime = Elapsed;
	InOutOneShot.TransitionTimeRemaining = Remaining;
	InOutOneShot.bTransitionAnimationAlmostComplete =
		InOutOneShot.bEarlyTransitionWindowOpen || bReachedCompletion;

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
	const bool bStillRequestingHeldTransition = DesiredState == StateControllerPlaybackHoldState;
	// A different TIP direction bucket must preempt immediately. Keeping a
	// right-turn root-motion clip alive after a left request makes the capsule
	// keep rotating the wrong way until the generic repeat timer expires. The
	// 0.75s delay remains only for repeating the same direction bucket.
	constexpr float TurnInPlaceReentryDelay = 0.75f;
	const int32 RequestedTurnInPlaceIndex = FMath::RoundToInt(StateControllerTurnInPlaceIndexForChooser);
	const int32 CachedTurnInPlaceIndex = FMath::RoundToInt(CachedStateControllerTurnInPlaceIndex);
	const bool bTurnInPlaceDirectionBucketChanged =
		RequestedTurnInPlaceIndex > 0 && RequestedTurnInPlaceIndex != CachedTurnInPlaceIndex;
	const bool bShouldReenterTurnInPlace =
		StateControllerPlaybackHoldState == EProject_JStateControllerPresentationState::TurnInPlace &&
		DesiredState == EProject_JStateControllerPresentationState::TurnInPlace &&
		Data.LocomotionContext.bShouldTurnInPlace &&
		(bTurnInPlaceDirectionBucketChanged || Elapsed >= TurnInPlaceReentryDelay);
	if (bShouldReenterTurnInPlace)
	{
		if (Project_J::MotionMatchingCVars::GetTurnInPlaceDebugMode() > 0)
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("TIPReenter Reason=%s RequestedIndex=%d CachedIndex=%d DesiredDelta=%.1f Elapsed=%.3f"),
				bTurnInPlaceDirectionBucketChanged ? TEXT("DirectionBucketChanged") : TEXT("SameDirectionTimer"),
				RequestedTurnInPlaceIndex,
				CachedTurnInPlaceIndex,
				Data.LocomotionContext.DesiredFacingDeltaYaw,
				Elapsed);
		}
		StateControllerPlaybackHoldStartedAtSeconds = NowSeconds;
		bStateControllerForceTurnInPlaceReselect = true;
		InOutOneShot.PresentationState = EProject_JStateControllerPresentationState::TurnInPlace;
		InOutOneShot.TransitionElapsedTime = 0.0f;
		InOutOneShot.TransitionTimeRemaining = EffectivePlayableLength;
		InOutOneShot.bTransitionAnimationAlmostComplete = false;
		return;
	}

	// When sprint_land is held, if the landing component phase has finished (!bIsLanding)
	// or if the user released sprint input (!bWantsSprint), interrupt the land one-shot
	// early so Motion Matching (PSD_Run / PSD_Sprint_Cycle / PSD_Idle) takes over immediately.
	bool bInterruptLandForMotionMatching = false;
	if (StateControllerPlaybackHoldState == EProject_JStateControllerPresentationState::TransitionToLand &&
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
		InOutOneShot.PresentationState = StateControllerPlaybackHoldState;
		return;
	}

	if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("StateControllerExitHold Actor=%s Exiting State=%d to State=%d LandEpoch=%d (Elapsed=%.3f Remaining=%.3f AlmostComp=%s)"),
			*GetNameSafe(OwningCharacter),
			static_cast<int32>(StateControllerPlaybackHoldState),
			static_cast<int32>(DesiredState),
			Data.Landing.PresentationRevision,
			Elapsed,
			Remaining,
			InOutOneShot.bTransitionAnimationAlmostComplete ? TEXT("true") : TEXT("false"));
	}

	StateControllerPlaybackHoldState = DesiredState;
	StateControllerPlaybackHoldStartedAtSeconds = NowSeconds;
	InOutOneShot.PresentationState = DesiredState;
}

void UProject_JCharacterAnimInstance::EvaluateStateControllerAnimationChooserOnGameThread(FProject_JAnimThreadSafeData& Data)
{
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
		CachedStateControllerChooserTable.Reset();
		CachedStateControllerSelectedAnimation = nullptr;
		CachedStateControllerSelectedAnimationOutput = FProject_JStateControllerChooserOutput();
		bCachedStateControllerHasSelectedAnimation = false;
		return;
	}

	// TIP is selected by a float chooser index rather than the generic Strafe
	// direction columns.  GASP permits a tagged TIP to re-enter after 0.75 s;
	// this lets a continuing mouse turn upgrade the next direct asset (for
	// example 90 -> 180) without waiting for a full two-second clip to end.
	// We only change the chooser result when its direction bucket changes.  A
	// same-asset restart additionally needs the AnimGraph's Force Blend path,
	// because a Blend Stack does not restart an identical asset by itself.
	// Index zero means the remaining yaw is below the selection threshold; keep
	// the already selected one-shot alive until its authored completion.
	const bool bTurnInPlaceIndexChanged =
		OneShot.PresentationState == EProject_JStateControllerPresentationState::TurnInPlace &&
		(bStateControllerForceTurnInPlaceReselect || OneShot.TransitionElapsedTime >= 0.75f) &&
		StateControllerTurnInPlaceIndexForChooser > 0.0f &&
		!FMath::IsNearlyEqual(
			CachedStateControllerTurnInPlaceIndex,
			StateControllerTurnInPlaceIndexForChooser);
	const bool bLandingContextChanged =
		OneShot.PresentationState == EProject_JStateControllerPresentationState::TransitionToLand &&
		(CachedStateControllerLandingPresentationRevision != Data.Landing.PresentationRevision ||
		 bCachedStateControllerLandWasMoving != Data.Landing.bLandWasMoving ||
		 bCachedStateControllerLandWasSprinting != Data.Landing.bLandWasSprinting ||
		 bCachedStateControllerUseHeavyLand != Data.Landing.bUseHeavyLand);
	const bool bContextChanged =
		CachedStateControllerChooserTable.Get() != ChooserTable ||
		CachedStateControllerPresentationState != OneShot.PresentationState ||
		CachedStateControllerRotationMode != Data.LocomotionContext.RotationMode ||
		CachedStateControllerGaitIntent != GaitIntentForChooser ||
		CachedStateControllerStance != OneShot.Stance ||
		CachedStateControllerStrafeDirection != OneShot.StrafeDirection ||
		(OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot &&
			CachedStateControllerPreviousStrafeDirection != OneShot.PreviousStrafeDirection) ||
		CachedStateControllerOneShotFoot != OneShot.Foot ||
		bStateControllerForceTurnInPlaceReselect ||
		bTurnInPlaceIndexChanged ||
		bCachedStateControllerCombatMode != Data.Combat.bIsCombatMode ||
		bCachedStateControllerFallOff != bStateControllerFallOffForChooser ||
		bLandingContextChanged;

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
				GetThreadSafeGroundSpeed(),
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

		CachedStateControllerChooserTable = ChooserTable;
		CachedStateControllerPresentationState = OneShot.PresentationState;
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
		++StateControllerChooserSelectionRevision;
		// The AnimGraph consumes this pulse from the Blend Stack's On Update
		// binding. It is deliberately not based on asset identity: a repeated
		// 90-degree TIP must be allowed to restart from time zero.
		OneShot.bForceBlendNextUpdate = bCachedStateControllerHasSelectedAnimation;

		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
			TEXT("StateControllerChooser Actor=%s Rev=%d State=%d LandEpoch=%d LandMoving=%s LandSprint=%s LandHeavy=%s Rotation=%d Gait=%d StartGait=%d StartCommitted=%s Stance=%d StrafeDir=%d PrevStrafeDir=%d StrafeAngle=%.1f HasStrafeAngle=%s OneShotFoot=%d FallOff=%s ContactL=%.2f ContactR=%.2f HasContactCurves=%s Combat=%s MMMoving=%s Input=%s InputFacingDelta=%.1f StopVelocityDelta=%.1f StopFoot=%d FutureSpeed=%.1f Accelerating=%s Asset=%s Length=%.3f Start=%.3f Loop=%s Blend=%.3f UseMM=%s Tags=%d HoldElapsed=%.3f HoldRemaining=%.3f AlmostComplete=%s ForceBlend=%s"),
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
				static_cast<int32>(StateControllerStopFootForChooser),
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
	OneShot.bShouldOverrideMotionMatching =
		OneShot.bHasSelectedAnimation &&
		IsTransitionState(OneShot.PresentationState) &&
		!OneShot.bSelectedAnimationShouldLoop;
	OneShot.bShouldEnableCombatStrafeOrientationWarping =
		IsTransitionState(OneShot.PresentationState) &&
		!OneShot.bSelectedAnimationShouldLoop &&
		Data.Combat.bIsCombatMode &&
		OneShot.RotationMode == EProject_JLocomotionRotationMode::Strafe &&
		OneShot.bHasStrafeDirectionAngle;
	OneShot.SelectionRevision = StateControllerChooserSelectionRevision;
}

void UProject_JCharacterAnimInstance::ApplyGenericMovementFallback(FProject_JAnimThreadSafeData& Data) const
{
	const float MoveInputSpeedThreshold = GetEffectiveGenericMoveInputSpeedThreshold();
	Data.Input.bHasMoveInput = Data.Movement.bIsAccelerating || Data.Movement.GroundSpeed > MoveInputSpeedThreshold;
	Data.Ground.bUseSprintLocomotion = Data.Movement.GroundSpeed >= GetEffectiveSprintLocomotionSpeedThreshold();
	Data.Ground.GroundMotionMode = Data.Movement.GroundSpeed > MoveInputSpeedThreshold
		? EProject_JGroundMotionMode::Locomotion
		: EProject_JGroundMotionMode::Idle;
	Data.LocomotionContext.GaitIntent = Data.Ground.bUseSprintLocomotion
		? EProject_JLocomotionGaitIntent::Sprint
		: EProject_JLocomotionGaitIntent::Run;
	Data.LocomotionContext.PhaseFamily = Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion
		? EProject_JLocomotionPhaseFamily::Cycle
		: EProject_JLocomotionPhaseFamily::Idle;
	Data.LocomotionContext.bIsMoving = Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion;
	Data.LocomotionContext.bIsMotionMatchingMoving = Data.LocomotionContext.bIsMoving;
	Data.MotionMatching.SelectionContext.GaitIntent = Data.LocomotionContext.GaitIntent;
	Data.MotionMatching.SelectionContext.RotationMode = Data.LocomotionContext.RotationMode;
	Data.MotionMatching.SelectionContext.PhaseFamily = Data.LocomotionContext.PhaseFamily;
	Data.MotionMatching.SelectionContext.bUseHeavyLand = false;
	Data.MotionMatching.SelectionContext.bLandWasMoving = false;
	Data.MotionMatching.SelectionContext.bLandWasSprinting = false;
	Data.MotionMatching.SelectionContext.bUseFallOffStart = false;
	Data.MotionMatching.SelectionContext.bUseRemoteStart = false;
	Data.MotionMatching.SelectionContext.bUseGenericFamiliesForNonOrientToMovement = false;
}

bool UProject_JCharacterAnimInstance::FillPlayerThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
	if (!OwningPlayerCharacter)
	{
		return false;
	}

	Data.Ground.bWantsSprint =
		(Data.Ground.bWantsSprint || OwningPlayerCharacter->IsSprintLocomotionAllowed()) &&
		OwningPlayerCharacter->IsSprintLocomotionAllowed();
	if (OwningPlayerCharacter->IsLocallyControlled())
	{
		Data.Ground.bStartWasSprinting =
			Data.Ground.bStartWasSprinting ||
			(Data.Ground.bStartRequested && Data.Ground.bWantsSprint && Data.Input.bHasMoveInput);
	}
	Data.Ground.bUseSprintLocomotion =
		Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		Data.Ground.bWantsSprint &&
		(Data.Input.bHasMoveInput || Data.Movement.GroundSpeed > GetEffectiveGenericMoveInputSpeedThreshold());
	Data.Combat.bIsCombatMode = OwningPlayerCharacter->IsCombatModeActive();
	Data.Combat.bIsAttacking = OwningPlayerCharacter->IsAttacking();
	Data.Combat.bIsDodging = OwningPlayerCharacter->IsDodging();
	Data.Combat.bIsHitReacting = OwningPlayerCharacter->IsHitReacting();
	Data.Combat.bIsPlayingCombatIntro = OwningPlayerCharacter->IsCombatIntroPlaying();
	Data.Combat.bIsPlayingCombatOutro = OwningPlayerCharacter->IsCombatOutroPlaying();
	if (const UProject_JWeaponAnimProfile* WeaponAnimProfile = OwningPlayerCharacter->GetWeaponAnimProfile())
	{
		Data.Combat.PresentationMode = WeaponAnimProfile->CombatPresentationMode;
	}

	if (const UProject_JMotionMatchingTrajectoryComponent* TrajectoryComponent = OwningPlayerCharacter->GetMotionMatchingTrajectoryComponent())
	{
		Data.Movement.Trajectory = TrajectoryComponent->GetTrajectory();
		Data.Movement.bHasTrajectory = !Data.Movement.Trajectory.Samples.IsEmpty();
		Data.Movement.TrajectoryGenerationRevision = TrajectoryComponent->GetGenerationRevision();
		Data.Movement.TrajectoryResetRevision = TrajectoryComponent->GetResetRevision();
		Data.Movement.TrajectoryAgeSeconds = TrajectoryComponent->GetTrajectoryAgeSeconds();
		Data.Movement.bTrajectoryGenerationEligible = TrajectoryComponent->IsTrajectoryGenerationEligible();
		Data.MotionMatching.TrajectorySampleCount = Data.Movement.Trajectory.Samples.Num();
	}

	// GetBaseAimRotation uses the owning controller for the autonomous proxy and
	// the engine-replicated remote view pitch for simulated proxies. Reading the
	// controller directly left remote combat overlays at zero aim whenever that
	// client did not own the pawn.
	const FRotator AimRotation = OwningCharacter->GetBaseAimRotation();
	const FRotator ActorRotation = OwningCharacter->GetActorRotation();
	Data.Aim.AimYaw = FMath::Clamp(FMath::FindDeltaAngleDegrees(ActorRotation.Yaw, AimRotation.Yaw), -MaxAimYaw, MaxAimYaw);
	Data.Aim.AimPitch = FMath::Clamp(FRotator::NormalizeAxis(AimRotation.Pitch), -MaxAimPitch, MaxAimPitch);
	return true;
}

void UProject_JCharacterAnimInstance::FillMountThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
	if (!OwningPlayerCharacter)
	{
		return;
	}

	Data.LocomotionMode = OwningPlayerCharacter->GetAnimationLocomotionMode();

	const UProject_JMountComponent* MountComponent = OwningPlayerCharacter->GetMountComponent();
	const AProject_JMountCharacter* MountedMount = MountComponent ? MountComponent->GetMountedMount() : nullptr;
	if (!MountedMount)
	{
		return;
	}

	Data.Mount.bIsMounted = Data.LocomotionMode == EProject_JAnimationLocomotionMode::Mounted;
	const FVector MountVelocity = MountedMount->GetVelocity();
	Data.Mount.Speed = MountVelocity.Size2D();
	Data.Mount.VerticalSpeed = MountVelocity.Z;

	if (const AProject_JFlyingMountCharacter* FlyingMount = Cast<AProject_JFlyingMountCharacter>(MountedMount))
	{
		Data.Mount.bIsFlying = FlyingMount->IsFlyingMount();
		Data.Mount.bIsGliding = FlyingMount->IsGliding();
	}

	USkeletalMeshComponent* RiderMesh = OwningPlayerCharacter->GetMesh();
	FVector LeftHandTargetWorld;
	FVector RightHandTargetWorld;
	if (RiderMesh && MountedMount->GetRiderHandIKTargetsWorld(LeftHandTargetWorld, RightHandTargetWorld))
	{
		const FTransform RiderMeshTransform = RiderMesh->GetComponentTransform();
		Data.Mount.LeftHandTargetComponentSpace = RiderMeshTransform.InverseTransformPosition(LeftHandTargetWorld);
		Data.Mount.RightHandTargetComponentSpace = RiderMeshTransform.InverseTransformPosition(RightHandTargetWorld);
		Data.Mount.bHasHandIKTargets = true;
	}
}

void UProject_JCharacterAnimInstance::FinalizeThreadSafeData(FProject_JAnimThreadSafeData& Data, bool bHasAimData) const
{
	if (bHasAimData)
	{
		Data.Aim.AimOffsetAlpha = CalculateAimOffsetAlpha(Data);
	}

	const UProject_JLocomotionProfile* Profile = GetLocomotionProfile();
	const FProject_JLocomotionPresentationPolicy* PresentationPolicy = Profile
		? &Profile->PresentationPolicy
		: nullptr;
	if (!PresentationPolicy || !PresentationPolicy->bEnableLean || Data.Air.bIsInAir)
	{
		Data.Movement.LeanAmount = FVector2D::ZeroVector;
		return;
	}

	const float LeanMultiplier = Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe
		? PresentationPolicy->CombatStrafeLeanMultiplier
		: PresentationPolicy->OrientToMovementLeanMultiplier;
	const float LeanClamp = FMath::Max(0.0f, PresentationPolicy->LeanAxisClamp);
	// The state component stores local X as forward/braking and local Y as lateral.
	// Lean consumers conventionally expose X=lateral and Y=forward/back.
	Data.Movement.LeanAmount = FVector2D(
		FMath::Clamp(Data.Movement.RelativeAccelerationAmount.Y * LeanMultiplier, -LeanClamp, LeanClamp),
		FMath::Clamp(Data.Movement.RelativeAccelerationAmount.X * LeanMultiplier, -LeanClamp, LeanClamp));
}

void UProject_JCharacterAnimInstance::FillProceduralIKThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
	// Slot state belongs to UAnimInstance and is therefore sampled on the game
	// thread here. The result is copied to the proxy before AnimGraph worker
	// thread evaluation, avoiding an unsafe montage query from Blueprint.
	const float FullBodyMontageWeight = FMath::Clamp(
		GetSlotMontageGlobalWeight(FullBodyMontageIKPolicy.FullBodyMontageSlotName),
		0.0f,
		1.0f);

	Data.ProceduralIK.FullBodyMontageWeight = FullBodyMontageWeight;
	Data.ProceduralIK.FootPlacementAlpha = FMath::Lerp(
		1.0f,
		FullBodyMontageIKPolicy.FootPlacementAlphaDuringFullBodyMontage,
		FullBodyMontageWeight);
	const float MontageLegIKAlpha = FMath::Lerp(
		1.0f,
		FullBodyMontageIKPolicy.LegIKAlphaDuringFullBodyMontage,
		FullBodyMontageWeight);

	// Only a weapon profile that explicitly supplies full-body locomotion owns
	// the lower-body pose. Upper-body overlays deliberately retain the shared
	// Motion Matching lower body and therefore retain normal procedural Leg IK.
	// Full-body draw/attack montages are handled independently by their slot
	// weight above, so combat intro blending cannot produce an IK 0->1->0 pulse.
	const bool bUsesAuthoredCombatLowerBody =
		Data.Combat.PresentationMode == EProject_JCombatAnimationPresentationMode::FullBodyLocomotion &&
		(Data.Combat.bIsCombatMode || Data.Combat.bIsPlayingCombatIntro);
	Data.ProceduralIK.LegIKAlpha = bUsesAuthoredCombatLowerBody
		? FMath::Min(MontageLegIKAlpha, FullBodyMontageIKPolicy.LegIKAlphaDuringFullBodyCombat)
		: MontageLegIKAlpha;
}

void UProject_JCharacterAnimInstance::PublishThreadSafeDataToProxy(const FProject_JAnimThreadSafeData& Data)
{
	const bool bMotionMatchingEnabled =
		IsPrimaryMeshAnimInstance() &&
		OwningCharacter &&
		!IsDedicatedServerAnimationContext() &&
		Data.LocomotionMode == EProject_JAnimationLocomotionMode::OnFoot;
	const bool bForceMotionMatchingRefresh = bMotionMatchingEnabled && ShouldForceMotionMatchingContextRefresh(Data);
	const bool bForceRemoteCombatStopReselect =
		bForceMotionMatchingRefresh &&
		OwningCharacter &&
		OwningCharacter->GetLocalRole() == ROLE_SimulatedProxy &&
		Data.Combat.bIsCombatMode &&
		Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe &&
		Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Stop;
	const bool bUpdateMotionMatchingThisFrame =
		bMotionMatchingEnabled &&
		(bForceMotionMatchingRefresh || ShouldEvaluateMotionMatchingThisFrame(Data.DeltaTime));
	UPoseSearchDatabase* PreviousActiveDatabase = CurrentActivePoseSearchDatabase.Get();

	if (bUpdateMotionMatchingThisFrame)
	{
		// Chooser properties only need refreshing when we're actually re-evaluating the PSD this frame.
		// Skipping on throttled frames (mid/far distance, hidden remote) reduces Game Thread cost
		// proportionally to how aggressively the Motion Matching update interval is throttled.
		PublishChooserProperties(Data);
		CurrentActivePoseSearchDatabase = EvaluatePoseSearchDatabaseOnGameThread(Data);
		CacheEvaluatedMotionMatchingContext(Data);
	}
	if (!bMotionMatchingEnabled)
	{
		CurrentActivePoseSearchDatabase = nullptr;
		bHasEvaluatedMotionMatchingContext = false;
	}
	const bool bForceMotionMatchingReselect = ShouldForceMotionMatchingReselect(Data) || bForceRemoteCombatStopReselect;
	const bool bDatabaseChanged = PreviousActiveDatabase != CurrentActivePoseSearchDatabase.Get();
	if (bUpdateMotionMatchingThisFrame && (bForceMotionMatchingRefresh || bDatabaseChanged || bForceMotionMatchingReselect))
	{
		RecordMotionMatchingTrace(Data, bDatabaseChanged, bForceMotionMatchingReselect);
		if (Project_J::MotionMatchingCVars::GetNetworkDebugMode() > 0)
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("MMNetSelection Actor=%s Rev=%d PSD=%s Changed=%s ForceReselect=%s Phase=%d Gait=%d Rotation=%d"),
				*GetNameSafe(OwningCharacter),
				Data.MotionMatching.SelectionRevision,
				*GetNameSafe(CurrentActivePoseSearchDatabase.Get()),
				bDatabaseChanged ? TEXT("true") : TEXT("false"),
				bForceMotionMatchingReselect ? TEXT("true") : TEXT("false"),
				static_cast<int32>(Data.MotionMatching.SelectionContext.PhaseFamily),
				static_cast<int32>(Data.MotionMatching.SelectionContext.GaitIntent),
				static_cast<int32>(Data.MotionMatching.SelectionContext.RotationMode));
		}
	}

	FProject_JCharacterAnimInstanceProxy& ProjectProxy = GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
	ProjectProxy.QueueGameThreadData(
		Data,
		CurrentActivePoseSearchDatabase,
		bMotionMatchingEnabled,
		bUpdateMotionMatchingThisFrame,
		bForceMotionMatchingReselect);
}

bool UProject_JCharacterAnimInstance::IsPrimaryMeshAnimInstance() const
{
	return OwningCharacter &&
		OwningCharacter->GetMesh() &&
		OwningCharacter->GetMesh()->GetAnimInstance() == this;
}

void UProject_JCharacterAnimInstance::RecordMotionMatchingTrace(
	const FProject_JAnimThreadSafeData& Data,
	bool bDatabaseChanged,
	bool bForceReselect)
{
	FProject_JMotionMatchingTraceEntry& Entry = MotionMatchingTrace.AddDefaulted_GetRef();
	Entry.WorldTimeSeconds = OwningCharacter && OwningCharacter->GetWorld()
		? OwningCharacter->GetWorld()->GetTimeSeconds()
		: 0.0;
	Entry.SelectionRevision = Data.MotionMatching.SelectionRevision;
	Entry.DatabaseName = GetNameSafe(CurrentActivePoseSearchDatabase);
	Entry.SelectedAnimationName = Data.MotionMatching.PostSelection.SelectedAnimation.ToString();
	Entry.SelectedAnimationTime = Data.MotionMatching.PostSelection.SelectedAnimationTime;
	Entry.SelectedAnimationLength = Data.MotionMatching.PostSelection.SelectedAnimationLength;
	Entry.SelectedAnimationWantedPlayRate = Data.MotionMatching.PostSelection.WantedPlayRate;
	Entry.bSelectionIsContinuing = Data.MotionMatching.PostSelection.bIsContinuingPoseSearch;
	Entry.MoveDataSpeedCurve = GetCurveValue(TEXT("movedata_speed"));
	Entry.EnableWarpingCurve = GetCurveValue(TEXT("enable_warping"));
	Entry.PhaseCurve = GetCurveValue(TEXT("phase"));
	Entry.PhaseFamily = Data.LocomotionContext.PhaseFamily;
	Entry.GaitIntent = Data.LocomotionContext.GaitIntent;
	Entry.RotationMode = Data.LocomotionContext.RotationMode;
	Entry.GroundMotionMode = Data.Ground.GroundMotionMode;
	Entry.GroundModeAgeSeconds = Data.Ground.GroundMotionModeElapsedTime;
	Entry.GroundSpeed = Data.Movement.GroundSpeed;
	Entry.PredictedSpeedGain = Data.Movement.PredictedSpeedGain;
	Entry.FutureTrajectorySpeed = Data.Movement.FutureTrajectorySpeed;
	Entry.FutureTrajectoryTurnAngle = Data.Movement.FutureTrajectoryTurnAngle;
	Entry.PredictedStopDistance = Data.Movement.PredictedStopDistance;
	Entry.InputTurnAngle = Data.Input.MoveInputTurnAngle;
	Entry.TrajectorySampleCount = Data.MotionMatching.TrajectorySampleCount;
	Entry.bHasMoveInput = Data.Input.bHasMoveInput;
	Entry.bIsAccelerating = Data.Movement.bIsAccelerating;
	Entry.bIsDecelerating = Data.Movement.bIsDecelerating;
	Entry.bHasFutureTrajectoryVelocity = Data.Movement.bHasFutureTrajectoryVelocity;
	Entry.bDatabaseChanged = bDatabaseChanged;
	Entry.bForceReselect = bForceReselect;

	const int32 MaxEntries = FMath::Max(MaxMotionMatchingTraceEntries, 1);
	if (MotionMatchingTrace.Num() > MaxEntries)
	{
		MotionMatchingTrace.RemoveAt(0, MotionMatchingTrace.Num() - MaxEntries, EAllowShrinking::No);
	}
}

UPoseSearchDatabase* UProject_JCharacterAnimInstance::EvaluatePoseSearchDatabaseOnGameThread(const FProject_JAnimThreadSafeData& Data)
{
	if (!OwningCharacter || IsDedicatedServerAnimationContext())
	{
		return nullptr;
	}

	const UProject_JMotionMatchingAssetSet* AssetSet = OwningPlayerCharacter
		? OwningPlayerCharacter->GetMotionMatchingAssetSet()
		: nullptr;
	const bool bUsesCombatStrafe =
		OwningPlayerCharacter && Data.Combat.bIsCombatMode &&
		Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe;
	const UProject_JMotionMatchingAssetSet* CombatStrafeAssetSet = bUsesCombatStrafe
			? OwningPlayerCharacter->GetCombatStrafeMotionMatchingAssetSet()
			: nullptr;
	UPoseSearchDatabase* IdleDatabase = AssetSet && AssetSet->IdlePoseSearchDatabase
		? AssetSet->IdlePoseSearchDatabase.Get()
		: DefaultIdlePoseSearchDatabase.Get();
	UPoseSearchDatabase* LocomotionDatabase = AssetSet && AssetSet->DefaultPoseSearchDatabase
		? AssetSet->DefaultPoseSearchDatabase.Get()
		: DefaultPoseSearchDatabase.Get();
	const FProject_JMotionMatchingSelectionContext& SelectionContext = Data.MotionMatching.SelectionContext;
	FProject_JMotionMatchingSelectionContext CombatSelectionContext = SelectionContext;
	// State Controller owns authored Start, Stop, Pivot, air and landing one-shots.
	// Regular Motion Matching still owns the continuous combat Cycle and the
	// moving TurnRedirect PSD. Do not flatten Turn to Cycle: the combat asset
	// set can therefore supply its authored turn database through the DA.
	switch (SelectionContext.PhaseFamily)
	{
	case EProject_JLocomotionPhaseFamily::Idle:
	case EProject_JLocomotionPhaseFamily::Cycle:
	case EProject_JLocomotionPhaseFamily::Turn:
		CombatSelectionContext.PhaseFamily = SelectionContext.PhaseFamily;
		break;
	case EProject_JLocomotionPhaseFamily::TurnInPlace:
		// A Turn In Place is a direct Blend Stack one-shot while the capsule is
		// stationary.  Keep Combat Idle evaluated underneath it so that releasing
		// the one-shot cannot reveal a Run Cycle pose for one blend-out frame.
		CombatSelectionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Idle;
		break;
	default:
		// Keep a stable locomotion pose beneath a direct State Controller one-shot.
		CombatSelectionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
		break;
	}
	CombatSelectionContext.bUseGenericFamiliesForNonOrientToMovement = true;
	UPoseSearchDatabase* SelectedDatabase = CombatStrafeAssetSet
		? CombatStrafeAssetSet->FindDatabaseForContext(CombatSelectionContext)
		: nullptr;
	if (!SelectedDatabase &&
		CombatStrafeAssetSet &&
		CombatSelectionContext.GaitIntent == EProject_JLocomotionGaitIntent::Sprint &&
		(CombatSelectionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Cycle ||
			CombatSelectionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Turn))
	{
		// Combat sprint is intentionally limited to forward / forward-diagonal input.
		// Until dedicated sprint Cycle or TurnRedirect PSDs are assigned, retain
		// a coherent combat result by falling back to the Run family, never OTM.
		FProject_JMotionMatchingSelectionContext RunFallbackContext = CombatSelectionContext;
		RunFallbackContext.GaitIntent = EProject_JLocomotionGaitIntent::Run;
		SelectedDatabase = CombatStrafeAssetSet->FindDatabaseForContext(RunFallbackContext);
	}
	bool bSelectedCombatStrafeDatabase = SelectedDatabase != nullptr;
	if (!SelectedDatabase && !bUsesCombatStrafe && AssetSet)
	{
		SelectedDatabase = AssetSet->FindDatabaseForContext(SelectionContext);
	}

	// An upper-body overlay may use the normal lower-body loops only when no
	// combat loop set is assigned. It must never borrow an OTM one-shot PSD.
	if (!SelectedDatabase &&
		!CombatStrafeAssetSet &&
		Data.Combat.bIsCombatMode &&
		Data.Combat.PresentationMode == EProject_JCombatAnimationPresentationMode::UpperBodyOverlay &&
		Data.LocomotionContext.RotationMode != EProject_JLocomotionRotationMode::OrientToMovement &&
		AssetSet)
	{
		FProject_JMotionMatchingSelectionContext OverlayFallbackContext = SelectionContext;
		OverlayFallbackContext.RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
		SelectedDatabase = AssetSet->FindDatabaseForContext(OverlayFallbackContext);
	}
	if (!SelectedDatabase)
	{
		if (bUsesCombatStrafe && CombatStrafeAssetSet)
		{
			// The combat DA intentionally uses DefaultPoseSearchDatabase for Idle
			// (see DA_Player_Combat_Strafe). A missing moving slot must therefore
			// retry the appropriate Cycle family before using that default; otherwise
			// an incomplete Sprint/Turn setup would visibly fall back to idle.
			if (CombatSelectionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Idle)
			{
				SelectedDatabase = CombatStrafeAssetSet->IdlePoseSearchDatabase.Get();
			}
			else
			{
				FProject_JMotionMatchingSelectionContext CycleFallbackContext = CombatSelectionContext;
				CycleFallbackContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
				SelectedDatabase = CombatStrafeAssetSet->FindDatabaseForContext(CycleFallbackContext);
				if (!SelectedDatabase && CycleFallbackContext.GaitIntent == EProject_JLocomotionGaitIntent::Sprint)
				{
					CycleFallbackContext.GaitIntent = EProject_JLocomotionGaitIntent::Run;
					SelectedDatabase = CombatStrafeAssetSet->FindDatabaseForContext(CycleFallbackContext);
				}
			}

			// Keep this last-resort fallback inside the combat DA. It must never
			// borrow an Orient-to-Movement database while combat owns the body.
			if (!SelectedDatabase)
			{
				SelectedDatabase = CombatStrafeAssetSet->DefaultPoseSearchDatabase.Get();
			}
			bSelectedCombatStrafeDatabase = SelectedDatabase != nullptr;
		}
	}
	if (!SelectedDatabase)
	{
		SelectedDatabase = Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Idle && IdleDatabase
			? IdleDatabase
			: LocomotionDatabase;
	}
	const UChooserTable* ChooserTable = bSelectedCombatStrafeDatabase
		? nullptr
		: AssetSet && AssetSet->MotionMatchingChooserTable
			? AssetSet->MotionMatchingChooserTable.Get()
			: MotionMatchingChooserTable.Get();

	bool bIsFarDistance = false;
	if (AProject_JBaseCharacter* BaseChar = Cast<AProject_JBaseCharacter>(OwningCharacter))
	{
		bIsFarDistance = BaseChar->GetSignificance() >= 2.0f;
	}

	if (ShouldDisableMotionMatchingBeyondFarDistance() && bIsFarDistance)
	{
		return nullptr;
	}

	if (!ChooserTable)
	{
		return SelectedDatabase;
	}

	FChooserEvaluationContext ChooserContext;
	ChooserContext.AddObjectParam(const_cast<UProject_JCharacterAnimInstance*>(this));
	FChooserPlayerSettings ChooserPlayerSettings;
	ChooserContext.AddStructParam(ChooserPlayerSettings);

	const FInstancedStruct ChooserObject = UChooserFunctionLibrary::MakeEvaluateChooser(const_cast<UChooserTable*>(ChooserTable));
	if (!ChooserObject.IsValid())
	{
		return SelectedDatabase;
	}

	UObject* ResultObject = UChooserFunctionLibrary::EvaluateObjectChooserBase(
		ChooserContext,
		ChooserObject,
		UPoseSearchDatabase::StaticClass());

	if (UPoseSearchDatabase* ResultDatabase = Cast<UPoseSearchDatabase>(ResultObject))
	{
		// An upper-body combat overlay deliberately keeps the shared Motion
		// Matching lower body.  A combat-specific Chooser row must therefore
		// never replace a valid moving/start/stop database with the idle
		// database; doing so freezes the legs while the CharacterMovement
		// component continues moving.  Full-body combat locomotion remains free
		// to provide its own database selection through its presentation layer.
		const bool bUsesSharedCombatLowerBody =
			Data.Combat.bIsCombatMode &&
			Data.Combat.PresentationMode == EProject_JCombatAnimationPresentationMode::UpperBodyOverlay;
		const bool bHasNonIdleLocomotionContext =
			Data.Ground.GroundMotionMode != EProject_JGroundMotionMode::Idle ||
			Data.LocomotionContext.PhaseFamily != EProject_JLocomotionPhaseFamily::Idle;
		const bool bChooserWouldReplaceMovingPoseWithIdle =
			bUsesSharedCombatLowerBody &&
			bHasNonIdleLocomotionContext &&
			SelectedDatabase &&
			ResultDatabase == IdleDatabase;

		if (!bChooserWouldReplaceMovingPoseWithIdle)
		{
			SelectedDatabase = ResultDatabase;
		}
	}

	return SelectedDatabase;
}

void UProject_JCharacterAnimInstance::PublishChooserProperties(const FProject_JAnimThreadSafeData& Data)
{
	// All accesses now use sub-struct paths; no legacy flat fields.
	const FProject_JAnimOptimizationPolicy OptimizationPolicy = BuildOptimizationPolicy();
	CurrentOptimizationPolicy = OptimizationPolicy;

	PublishChooserMovementProperties(Data);
	PublishChooserGroundProperties(Data);
	PublishChooserAirProperties(Data);
	PublishChooserLandingProperties(Data);
	PublishChooserCombatProperties(Data);

	if (OptimizationPolicy.bUseFarChooserRowsOnly)
	{
		ApplyFarChooserOverrides(Data);
	}
}

void UProject_JCharacterAnimInstance::PublishChooserMovementProperties(const FProject_JAnimThreadSafeData& Data)
{
	ChooserGroundSpeed = Data.Movement.GroundSpeed;
	ChooserVerticalSpeed = Data.Movement.VerticalSpeed;
	ChooserAccelerationRatio = Data.Movement.AccelerationRatio;
	ChooserMoveInputSize = Data.Input.MoveInputSize;
	ChooserMoveInputHeldTime = Data.Input.MoveInputHeldTime;
	ChooserMoveInputTurnAngle = Data.Input.MoveInputTurnAngle;
	bChooserHasMoveInput = Data.Input.bHasMoveInput;
	bChooserSharpTurnRequested = Data.Input.bSharpTurnRequested;
	bChooserIsRemoteProxy = OwningPlayerCharacter && !IsLocallyControlledCharacter();
}

void UProject_JCharacterAnimInstance::PublishChooserGroundProperties(const FProject_JAnimThreadSafeData& Data)
{
	const bool bDerivedStart = Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Start;
	const bool bDerivedStop = Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Stop;
	const bool bDerivedCycle = Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Cycle;
	const bool bDerivedIdle = Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Idle;

	bChooserStartRequested = Data.Ground.bStartRequested || bDerivedStart;
	bChooserStopRequested = Data.Ground.bStopRequested || bDerivedStop;
	bChooserWantsSprint = Data.Ground.bWantsSprint;
	bChooserUseSprintLocomotion =
		(Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion || bDerivedCycle) &&
		Data.Ground.bUseSprintLocomotion;
	bChooserUseRunStart = bChooserStartRequested && !Data.Ground.bStartWasSprinting && !bChooserIsRemoteProxy;
	bChooserUseRemoteRunStart = bChooserStartRequested && !Data.Ground.bStartWasSprinting && bChooserIsRemoteProxy;
	bChooserUseSprintStart = bChooserStartRequested && Data.Ground.bStartWasSprinting;
	bChooserUseRunStop = bChooserStopRequested && !Data.Ground.bStopWasSprinting;
	bChooserUseSprintStop = bChooserStopRequested && Data.Ground.bStopWasSprinting;
	bChooserUseRunLocomotion =
		(Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion || bDerivedCycle) &&
		!Data.Ground.bUseSprintLocomotion &&
		!bChooserIsRemoteProxy;
	bChooserUseRemoteRunLocomotion =
		(Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion || bDerivedCycle) &&
		!Data.Ground.bUseSprintLocomotion &&
		bChooserIsRemoteProxy;
	bChooserUseSprintLocomotionRow =
		(Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion || bDerivedCycle) &&
		Data.Ground.bUseSprintLocomotion;
	bChooserStartWasSprinting = Data.Ground.bStartWasSprinting;
	bChooserStopWasSprinting = Data.Ground.bStopWasSprinting;
	bChooserIsIdle = Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Idle || bDerivedIdle;
	ChooserGroundMotionMode = Data.Ground.GroundMotionMode;
	ChooserGaitIntent = Data.LocomotionContext.GaitIntent;
	ChooserRotationMode = Data.LocomotionContext.RotationMode;
	ChooserDesiredFacingDeltaYaw = Data.LocomotionContext.DesiredFacingDeltaYaw;
	bChooserIsStartingDerived = Data.LocomotionContext.bIsStarting;
	bChooserIsPivoting = Data.LocomotionContext.bIsPivoting;
	bChooserShouldTurnInPlace = Data.LocomotionContext.bShouldTurnInPlace;
	bChooserShouldSpinTransition = Data.LocomotionContext.bShouldSpinTransition;
}

void UProject_JCharacterAnimInstance::PublishChooserAirProperties(const FProject_JAnimThreadSafeData& Data)
{
	bChooserUseJumpStart =
		Data.Air.bIsJumping &&
		!Data.Landing.bIsLanding;
	bChooserUseFallOff =
		Data.Air.bIsFallOffStart &&
		!Data.Air.bIsJumping &&
		!Data.Landing.bIsLanding;
	bChooserUseFallLoop =
		Data.Air.bIsInAir &&
		!Data.Air.bIsJumping &&
		!Data.Air.bIsFallOffStart &&
		!Data.Landing.bIsLanding;
	bChooserIsInAir = Data.Air.bIsInAir;
	bChooserIsJumping = Data.Air.bIsJumping;
}

void UProject_JCharacterAnimInstance::PublishChooserLandingProperties(const FProject_JAnimThreadSafeData& Data)
{
	ChooserLastFallSpeed = Data.Landing.LastFallSpeed;
	ChooserLandStartFallSpeed = Data.Landing.LandStartFallSpeed;
	bChooserUseLightLand =
		Data.Landing.bIsLanding &&
		!Data.Landing.bUseHeavyLand;
	bChooserUseHeavyLandRow =
		Data.Landing.bIsLanding &&
		Data.Landing.bUseHeavyLand;
	bChooserUseStandLightLand =
		Data.Landing.bIsLanding &&
		!Data.Landing.bUseHeavyLand &&
		!Data.Landing.bLandWasMoving;
	bChooserUseStandHeavyLand =
		Data.Landing.bIsLanding &&
		Data.Landing.bUseHeavyLand &&
		!Data.Landing.bLandWasMoving;
	bChooserUseRunLightLand =
		Data.Landing.bIsLanding &&
		!Data.Landing.bUseHeavyLand &&
		Data.Landing.bLandWasMoving &&
		!Data.Landing.bLandWasSprinting;
	bChooserUseSprintLightLand =
		Data.Landing.bIsLanding &&
		!Data.Landing.bUseHeavyLand &&
		Data.Landing.bLandWasMoving &&
		Data.Landing.bLandWasSprinting;
	bChooserUseRunHeavyLand =
		Data.Landing.bIsLanding &&
		Data.Landing.bUseHeavyLand &&
		Data.Landing.bLandWasMoving &&
		!Data.Landing.bLandWasSprinting;
	bChooserUseSprintHeavyLand =
		Data.Landing.bIsLanding &&
		Data.Landing.bUseHeavyLand &&
		Data.Landing.bLandWasMoving &&
		Data.Landing.bLandWasSprinting;
	bChooserLandWasSprinting = Data.Landing.bLandWasSprinting;
	bChooserLandWasMoving = Data.Landing.bLandWasMoving;
	bChooserIsLanding = Data.Landing.bIsLanding;
	bChooserUseHeavyLand = Data.Landing.bUseHeavyLand;
}

void UProject_JCharacterAnimInstance::PublishChooserCombatProperties(const FProject_JAnimThreadSafeData& Data)
{
	bChooserIsCombatMode = Data.Combat.bIsCombatMode;
}

void UProject_JCharacterAnimInstance::ApplyFarChooserOverrides(const FProject_JAnimThreadSafeData& Data)
{
	const FProject_JRemoteVisualLocomotionPolicy RemotePolicy = GetEffectiveRemoteVisualPolicy();
	if (!RemotePolicy.bDisableStartStopChooserBeyondFarDistance &&
		!RemotePolicy.bDisableLandChooserBeyondFarDistance)
	{
		return;
	}

	const bool bUseFarLocomotion =
		Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		!Data.Air.bIsInAir &&
		!Data.Air.bIsJumping;
	const bool bUseFarRunLocomotion = bUseFarLocomotion && !Data.Ground.bUseSprintLocomotion;
	const bool bUseFarSprintLocomotion = bUseFarLocomotion && Data.Ground.bUseSprintLocomotion;

	if (RemotePolicy.bDisableStartStopChooserBeyondFarDistance)
	{
		bChooserStartRequested = false;
		bChooserStopRequested = false;
		bChooserSharpTurnRequested = false;
		bChooserUseRunStart = false;
		bChooserUseRemoteRunStart = false;
		bChooserUseSprintStart = false;
		bChooserUseRunStop = false;
		bChooserUseSprintStop = false;
	}
	if (RemotePolicy.bDisableLandChooserBeyondFarDistance)
	{
		bChooserUseLightLand = false;
		bChooserUseHeavyLandRow = false;
		bChooserUseStandLightLand = false;
		bChooserUseStandHeavyLand = false;
		bChooserUseRunLightLand = false;
		bChooserUseSprintLightLand = false;
		bChooserUseRunHeavyLand = false;
		bChooserUseSprintHeavyLand = false;
		bChooserIsLanding = false;
		bChooserUseHeavyLand = false;
		bChooserLandWasMoving = false;
		bChooserLandWasSprinting = false;
	}
	bChooserUseSprintLocomotion = bUseFarSprintLocomotion;
	bChooserUseRunLocomotion = false;
	bChooserUseRemoteRunLocomotion = bUseFarRunLocomotion;
	bChooserUseSprintLocomotionRow = bUseFarSprintLocomotion;
	bChooserUseJumpStart = Data.Air.bIsJumping;
	bChooserUseFallLoop = Data.Air.bIsInAir && !Data.Air.bIsJumping;
	bChooserIsIdle = !Data.Air.bIsInAir && Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Idle;
}

bool UProject_JCharacterAnimInstance::ShouldEvaluateMotionMatchingThisFrame(float DeltaSeconds)
{
	if (!OwningCharacter || IsDedicatedServerAnimationContext())
	{
		MotionMatchingUpdateAccumulator = 0.0f;
		return false;
	}

	CurrentOptimizationPolicy = BuildOptimizationPolicy();
	const float UpdateInterval = CurrentOptimizationPolicy.MotionMatchingUpdateInterval;
	if (UpdateInterval <= 0.0f)
	{
		MotionMatchingUpdateAccumulator = 0.0f;
		return true;
	}

	MotionMatchingUpdateAccumulator += DeltaSeconds;
	if (MotionMatchingUpdateAccumulator < UpdateInterval)
	{
		return false;
	}

	MotionMatchingUpdateAccumulator = 0.0f;
	return true;
}

bool UProject_JCharacterAnimInstance::ShouldForceMotionMatchingContextRefresh(const FProject_JAnimThreadSafeData& Data) const
{
	if (!bHasEvaluatedMotionMatchingContext)
	{
		return true;
	}

	return
		LastEvaluatedMotionMatchingSelectionRevision != Data.MotionMatching.SelectionRevision ||
		LastEvaluatedGroundMotionMode != Data.Ground.GroundMotionMode ||
		LastEvaluatedGaitIntent != Data.LocomotionContext.GaitIntent ||
		LastEvaluatedRotationMode != Data.LocomotionContext.RotationMode ||
		LastEvaluatedPhaseFamily != Data.LocomotionContext.PhaseFamily ||
		bLastEvaluatedStartRequested != Data.Ground.bStartRequested ||
		bLastEvaluatedStartWasSprinting != Data.Ground.bStartWasSprinting;
}

bool UProject_JCharacterAnimInstance::ShouldForceMotionMatchingReselect(const FProject_JAnimThreadSafeData& Data) const
{
	return Data.MotionMatching.bForceReselect;
}

void UProject_JCharacterAnimInstance::CacheEvaluatedMotionMatchingContext(const FProject_JAnimThreadSafeData& Data)
{
	LastEvaluatedGroundMotionMode = Data.Ground.GroundMotionMode;
	LastEvaluatedGaitIntent = Data.LocomotionContext.GaitIntent;
	LastEvaluatedRotationMode = Data.LocomotionContext.RotationMode;
	LastEvaluatedPhaseFamily = Data.LocomotionContext.PhaseFamily;
	bLastEvaluatedStartRequested = Data.Ground.bStartRequested;
	bLastEvaluatedStartWasSprinting = Data.Ground.bStartWasSprinting;
	LastEvaluatedMotionMatchingSelectionRevision = Data.MotionMatching.SelectionRevision;
	bHasEvaluatedMotionMatchingContext = true;
	MotionMatchingUpdateAccumulator = 0.0f;
}

FProject_JAnimOptimizationPolicy UProject_JCharacterAnimInstance::BuildOptimizationPolicy() const
{
	FProject_JAnimOptimizationPolicy Policy;
	if (!OwningCharacter)
	{
		Policy.Tier = EProject_JAnimBudgetTier::Hidden;
		Policy.bUpdateAnimationData = false;
		Policy.bUseFullChooserRows = false;
		Policy.bUseFarChooserRowsOnly = false;
		return Policy;
	}

	if (IsLocallyControlledCharacter())
	{
		return Policy;
	}

	const bool bRecentlyRendered = WasOwnerRecentlyRendered(RecentlyRenderedTolerance);
	if (!bRecentlyRendered)
	{
		Policy.Tier = EProject_JAnimBudgetTier::Hidden;
		Policy.bUpdateAnimationData = false;
		Policy.bUseFullChooserRows = false;
		Policy.bUseFarChooserRowsOnly = false;
		Policy.MotionMatchingUpdateInterval = GetEffectiveHiddenRemoteUpdateInterval();
		return Policy;
	}

	if (const AProject_JBaseCharacter* BaseChar = Cast<AProject_JBaseCharacter>(OwningCharacter))
	{
		const float Significance = BaseChar->GetSignificance();
		if (Significance <= 0.0f)
		{
			Policy.Tier = EProject_JAnimBudgetTier::Near;
			return Policy;
		}

		if (Significance <= 1.0f)
		{
			Policy.Tier = EProject_JAnimBudgetTier::Mid;
			Policy.MotionMatchingUpdateInterval = GetEffectiveMidMotionMatchingUpdateInterval();
			return Policy;
		}

		Policy.Tier = EProject_JAnimBudgetTier::Far;
		Policy.bUseFullChooserRows = false;
		Policy.bUseFarChooserRowsOnly = true;
		Policy.MotionMatchingUpdateInterval = GetEffectiveFarMotionMatchingUpdateInterval();
		return Policy;
	}

	Policy.Tier = EProject_JAnimBudgetTier::Near;
	return Policy;
}

void UProject_JCharacterAnimInstance::ResetTrajectoryHistoryOnAccelerationStop(const FProject_JAnimThreadSafeData& Data) const
{
	const bool bPreserveLocalCombatStrafeHistory =
		IsLocallyControlledCharacter() &&
		Data.Combat.bIsCombatMode &&
		Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe;
	if (!Data.Movement.bStoppedAcceleratingThisFrame || bPreserveLocalCombatStrafeHistory)
	{
		return;
	}

	// Combat Strafe Stop searches need the movement history that immediately precedes
	// input release. Resetting here only on the autonomous proxy made its Stop query
	// roughly one tenth as long as the simulated proxy query, causing wrong-direction
	// candidates to win. Normal locomotion retains its existing reset behavior.


	if (OwningCharacter &&
		OwningCharacter->GetLocalRole() == ROLE_SimulatedProxy &&
		Project_J::MotionMatchingCVars::ShouldDisableRemoteAccelerationReset())
	{
		// Simulated proxies do not own reliable input acceleration; replicated velocity can be valid while acceleration flickers.
		return;
	}

	if (CachedTrajectoryComponent)
	{
		CachedTrajectoryComponent->ResetTrajectoryHistoryWithReason(
			EProject_JTrajectoryResetReason::AccelerationStopped);
	}
}

float UProject_JCharacterAnimInstance::CalculateAimOffsetAlpha(const FProject_JAnimThreadSafeData& Data) const
{
	if (Data.Combat.bIsCombatMode)
	{
		return GetEffectiveCombatAimAlpha();
	}

	if (Data.Ground.bUseSprintLocomotion)
	{
		return SprintAimAlpha;
	}

	return Data.Movement.GroundSpeed > GetEffectiveGenericMoveInputSpeedThreshold() ? MovingAimAlpha : StandingAimAlpha;
}

bool UProject_JCharacterAnimInstance::ShouldSkipNativeUpdate(float DeltaSeconds)
{
	if (!OwningCharacter)
	{
		ThreadSafeData = FProject_JAnimThreadSafeData();
		PublishThreadSafeDataToProxy(ThreadSafeData);
		return true;
	}

	if (bSkipDedicatedServerAnimationDataUpdate && IsDedicatedServerAnimationContext())
	{
		ThreadSafeData = BuildThreadSafeData(DeltaSeconds);
		PublishThreadSafeDataToProxy(ThreadSafeData);
		return true;
	}

	CurrentOptimizationPolicy = BuildOptimizationPolicy();
	if (CurrentOptimizationPolicy.bUpdateAnimationData)
	{
		HiddenRemoteUpdateAccumulator = 0.0f;
		return false;
	}

	const float UpdateInterval = CurrentOptimizationPolicy.MotionMatchingUpdateInterval;
	if (UpdateInterval > 0.0f)
	{
		HiddenRemoteUpdateAccumulator += DeltaSeconds;
		if (HiddenRemoteUpdateAccumulator >= UpdateInterval)
		{
			HiddenRemoteUpdateAccumulator = 0.0f;
			return false;
		}
	}

	FProject_JCharacterAnimInstanceProxy& ProjectProxy = GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
	ProjectProxy.QueueGameThreadData(ThreadSafeData, CurrentActivePoseSearchDatabase, true, false, false);
	return true;
}

const UProject_JLocomotionProfile* UProject_JCharacterAnimInstance::GetLocomotionProfile() const
{
	return OwningPlayerCharacter ? OwningPlayerCharacter->GetLocomotionProfile() : nullptr;
}

FProject_JAnimationBudgetSettings UProject_JCharacterAnimInstance::GetEffectiveAnimationBudgetSettings() const
{
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Profile->GetResolvedAnimationBudgetSettings();
	}

	FProject_JAnimationBudgetSettings Settings;
	Settings.NearDistance = NearMotionMatchingDistance;
	Settings.MidDistance = MidMotionMatchingDistance;
	Settings.FarDistance = FarMotionMatchingDistance;
	Settings.MidUpdateInterval = MidMotionMatchingUpdateInterval;
	Settings.FarUpdateInterval = FarMotionMatchingUpdateInterval;
	Settings.HiddenUpdateInterval = HiddenRemoteUpdateInterval;
	Settings.bDisableMotionMatchingBeyondFarDistance = bDisableMotionMatchingBeyondFarDistance;
	return Settings;
}

float UProject_JCharacterAnimInstance::GetEffectiveGenericMoveInputSpeedThreshold() const
{
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Profile->GenericMoveInputSpeedThreshold;
	}

	return GenericMoveInputSpeedThreshold;
}

float UProject_JCharacterAnimInstance::GetEffectiveSprintLocomotionSpeedThreshold() const
{
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Profile->SprintLocomotionSpeedThreshold;
	}

	return SprintLocomotionSpeedThreshold;
}

FProject_JRemoteVisualLocomotionPolicy UProject_JCharacterAnimInstance::GetEffectiveRemoteVisualPolicy() const
{
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Profile->RemoteVisualPolicy;
	}

	return FProject_JRemoteVisualLocomotionPolicy();
}

float UProject_JCharacterAnimInstance::GetEffectiveHiddenRemoteUpdateInterval() const
{
	return GetEffectiveAnimationBudgetSettings().HiddenUpdateInterval;
}

float UProject_JCharacterAnimInstance::GetEffectiveNearMotionMatchingDistance() const
{
	return GetEffectiveAnimationBudgetSettings().NearDistance;
}

float UProject_JCharacterAnimInstance::GetEffectiveMidMotionMatchingDistance() const
{
	return GetEffectiveAnimationBudgetSettings().MidDistance;
}

float UProject_JCharacterAnimInstance::GetEffectiveMidMotionMatchingUpdateInterval() const
{
	return GetEffectiveAnimationBudgetSettings().MidUpdateInterval;
}

float UProject_JCharacterAnimInstance::GetEffectiveFarMotionMatchingUpdateInterval() const
{
	return GetEffectiveAnimationBudgetSettings().FarUpdateInterval;
}

bool UProject_JCharacterAnimInstance::ShouldDisableMotionMatchingBeyondFarDistance() const
{
	return GetEffectiveAnimationBudgetSettings().bDisableMotionMatchingBeyondFarDistance;
}

const UProject_JCombatAnimProfile* UProject_JCharacterAnimInstance::GetCombatAnimProfile() const
{
	return OwningPlayerCharacter ? OwningPlayerCharacter->GetCombatAnimProfile() : nullptr;
}

float UProject_JCharacterAnimInstance::GetEffectiveCombatAimAlpha() const
{
	if (const UProject_JCombatAnimProfile* Profile = GetCombatAnimProfile())
	{
		return Profile->CombatAimAlpha;
	}

	return CombatAimAlpha;
}
