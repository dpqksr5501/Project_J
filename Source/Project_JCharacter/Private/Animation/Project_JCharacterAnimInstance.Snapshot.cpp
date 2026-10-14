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

FProject_JAnimThreadSafeData UProject_JCharacterAnimInstance::BuildThreadSafeData(float DeltaSeconds) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_AnimBuildThreadSafeData);
	FProject_JAnimThreadSafeData Data;
	Data.DeltaTime = DeltaSeconds;
	Data.MotionMatching.MinimumSearchInterval = CurrentOptimizationPolicy.MotionMatchingUpdateInterval;
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
	// Selection consumes the owning semantic mode, not aliases published for BP
	// compatibility. OR-ing aliases can promote a stale display flag to intent.
	Data.Ground.bStartRequested = AnimState->GroundMotionMode == EProject_JGroundMotionMode::Start;
	Data.Ground.bStopRequested = AnimState->GroundMotionMode == EProject_JGroundMotionMode::Stop;
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
	Data.TurnRequest = AnimState->GetTurnRequestSample();
	Data.LocomotionContext.RotationMode = AnimState->AuthoritativeContext.RotationMode;
	Data.LocomotionContext.PhaseFamily = AnimState->DerivedLocomotionContext.PhaseFamily;
	Data.LocomotionContext.DesiredFacingDeltaYaw = AnimState->KinematicContext.DesiredFacingDeltaYaw;
	Data.LocomotionContext.DesiredFacingYaw = AnimState->KinematicContext.DesiredFacingYaw;
	Data.LocomotionContext.RequestedMoveWorldDirection = AnimState->KinematicContext.MoveWorldDirection;
	Data.LocomotionContext.bIsMoving = AnimState->DerivedLocomotionContext.bIsMoving;
	Data.LocomotionContext.bIsMotionMatchingMoving = AnimState->DerivedLocomotionContext.bIsMotionMatchingMoving;
	Data.LocomotionContext.bHasGroundMovementIntent = AnimState->DerivedLocomotionContext.bHasGroundMovementIntent;
	Data.LocomotionContext.bIsStarting = AnimState->DerivedLocomotionContext.bIsStarting;
	Data.LocomotionContext.bIsPivoting = AnimState->DerivedLocomotionContext.bIsPivoting;
	Data.LocomotionContext.MoveIntentRevision = AnimState->DerivedLocomotionContext.MoveIntentRevision;
	Data.LocomotionContext.PivotRequestRevision = AnimState->DerivedLocomotionContext.PivotRequestRevision;
	Data.LocomotionContext.PivotPreviousMovementDirection = AnimState->DerivedLocomotionContext.PivotPreviousMovementDirection;
	Data.LocomotionContext.PivotMoveIntentDirection = AnimState->DerivedLocomotionContext.PivotMoveIntentDirection;
	FProject_JStateControllerRuntime::FPivotIntent PivotIntent;
	PivotIntent.bStopRequested = Data.Ground.bStopRequested;
	PivotIntent.bHasMoveInput = Data.Input.bHasMoveInput;
	PivotIntent.bIsPivoting = Data.LocomotionContext.bIsPivoting;
	PivotIntent.bInAir = Data.Air.bIsInAir;
	PivotIntent.bLanding = Data.Landing.bIsLanding;
	PivotIntent.RequestRevision = Data.LocomotionContext.PivotRequestRevision;
	PivotIntent.MoveIntentRevision = Data.LocomotionContext.MoveIntentRevision;
	PivotIntent.PhaseFamily = Data.LocomotionContext.PhaseFamily;
	const FProject_JStateControllerRuntime::FPivotUpdate PivotUpdate =
		StateControllerRuntime.ReconcilePivot(PivotIntent, AnimationClock.Seconds);
	if (PivotUpdate.Interruption != FProject_JStateControllerRuntime::EPivotInterruption::None &&
		Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("CombatStrafeRunPivotCancelled Actor=%s PivotRev=%d Reason=%d NewPivotRev=%d NewIntentRev=%d"),
			*GetNameSafe(OwningCharacter), PivotUpdate.PreviousRequestRevision,
			static_cast<int32>(PivotUpdate.Interruption), PivotIntent.RequestRevision, PivotIntent.MoveIntentRevision);
	}
	const bool bForceCycleAfterPivotRedirect =
		PivotUpdate.Interruption == FProject_JStateControllerRuntime::EPivotInterruption::Redirect;
	if (PivotUpdate.bForceCycle)
	{
		Data.LocomotionContext.bIsPivoting = false;
		Data.LocomotionContext.bIsStarting = false;
		Data.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	}
	if (PivotUpdate.bKeepCommittedPivot)
	{
		Data.LocomotionContext.bIsPivoting = true;
		Data.LocomotionContext.bIsStarting = false;
		Data.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Pivot;
		Data.LocomotionContext.PivotRequestRevision = StateControllerRuntime.GetPivot().RequestRevision;
		Data.LocomotionContext.PivotPreviousMovementDirection = StateControllerRuntime.GetPivot().PreviousMovementDirection;
		Data.LocomotionContext.PivotMoveIntentDirection = StateControllerRuntime.GetPivot().MoveIntentDirection;
	}
	Data.LocomotionContext.bShouldTurnInPlace = AnimState->DerivedLocomotionContext.bShouldTurnInPlace;
	Data.LocomotionContext.TurnInPlaceDirectionBucket = AnimState->DerivedLocomotionContext.TurnInPlaceDirectionBucket;
	Data.LocomotionContext.TurnInPlaceSequence = AnimState->DerivedLocomotionContext.TurnInPlaceSequence;
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
	if (PivotUpdate.bForceCycle)
	{
		Data.MotionMatching.SelectionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
		Data.MotionMatching.bForceReselect =
			Data.MotionMatching.bForceReselect || bForceCycleAfterPivotRedirect;
	}
	Data.MotionMatching.PostSelection = GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>().GetLatestPostSelection();
	UpdateReleasedLandingMotionContext(Data);

	FProject_JAnimOneShotPresentationThreadSafeData& OneShot = Data.OneShotPresentation;
	OneShot.bEnabled = Data.MotionMatchingSearchPolicy.bEnableExperimentalOneShotPresentation;
	OneShot.bUseMotionMatchOnEntry =
		OneShot.bEnabled && Data.MotionMatchingSearchPolicy.bUseMotionMatchForExperimentalOneShotEntry;
	OneShot.bEarlyTransitionWindowOpen = OneShot.bEnabled &&
		IsAuthoredEarlyTransitionAllowed(Data.LocomotionContext.GaitIntent);
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
		const bool bHasPivotDirectionSnapshot = OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot &&
			!Data.LocomotionContext.PivotPreviousMovementDirection.IsNearlyZero() &&
			!Data.LocomotionContext.PivotMoveIntentDirection.IsNearlyZero();
		FVector DirectionVelocity = bHasPivotDirectionSnapshot
			? Data.LocomotionContext.PivotMoveIntentDirection
			: (Data.Movement.bHasFutureTrajectoryVelocity
			? Data.Movement.FutureTrajectoryVelocity
			: Data.Movement.Velocity);
		DirectionVelocity.Z = 0.0f;
		const UProject_JCombatAnimProfile* CombatProfile = GetCombatAnimProfile();
		const float MinimumDirectionSpeed = CombatProfile ? CombatProfile->StrafeDirectionMinimumSpeed : 10.0f;
		if (bHasPivotDirectionSnapshot)
		{
			const float ActorYaw = OwningCharacter ? OwningCharacter->GetActorRotation().Yaw : 0.0f;
			OneShot.PreviousStrafeDirection = ResolveStateControllerStrafeDirection(
				FMath::FindDeltaAngleDegrees(ActorYaw, Data.LocomotionContext.PivotPreviousMovementDirection.Rotation().Yaw),
				PreviousStrafeDirection,
				CombatProfile ? CombatProfile->StrafeDirectionHysteresisDegrees : 7.5f);
		}
		if (bHasPivotDirectionSnapshot || DirectionVelocity.SizeSquared2D() > FMath::Square(MinimumDirectionSpeed))
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
	else if (OneShot.RotationMode == EProject_JLocomotionRotationMode::OrientToMovement)
	{
		const UProject_JLocomotionProfile* CurrentProfile = GetLocomotionProfile();
		const FProject_JMotionMatchingSearchPolicy& SearchPolicy = CurrentProfile
			? CurrentProfile->MotionMatchingSearchPolicy
			: FProject_JMotionMatchingSearchPolicy();

		const bool bIsEnteringJump = (Data.Air.bIsJumping || OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::JumpStart);
		if (bIsJumpAirReselecting)
		{
			// 공중에서 마우스/입력 회전으로 인한 재선택 시 실시간 꺾인 방향 적용
			OneShot.StrafeDirection = PendingJumpAirDirection;
		}
		else if (bIsEnteringJump)
		{
			// OTM 지상 점프 진입 순간: [Actor Facing vs Velocity] 각도 오차 확인
			const float GroundSpeed = Data.Movement.GroundSpeed;
			if (GroundSpeed > 10.0f && OwningCharacter)
			{
				const float ActorYaw = OwningCharacter->GetActorRotation().Yaw;
				const float VelocityYaw = Data.Movement.Velocity.Rotation().Yaw;
				const float FacingVelocityDeltaYaw = FMath::FindDeltaAngleDegrees(ActorYaw, VelocityYaw);
				const float LaunchThreshold = SearchPolicy.OTMJumpLaunchAngleThreshold > 0.0f
					? SearchPolicy.OTMJumpLaunchAngleThreshold
					: 45.0f;

				if (FMath::Abs(FacingVelocityDeltaYaw) <= LaunchThreshold)
				{
					OneShot.StrafeDirection = EProject_JStateControllerStrafeDirection::Forward;
				}
				else if (FacingVelocityDeltaYaw < -LaunchThreshold && FacingVelocityDeltaYaw >= -135.0f)
				{
					OneShot.StrafeDirection = EProject_JStateControllerStrafeDirection::Left;
				}
				else if (FacingVelocityDeltaYaw > LaunchThreshold && FacingVelocityDeltaYaw <= 135.0f)
				{
					OneShot.StrafeDirection = EProject_JStateControllerStrafeDirection::Right;
				}
				else
				{
					OneShot.StrafeDirection = EProject_JStateControllerStrafeDirection::Backward;
				}
				OneShot.StrafeDirectionAngle = FacingVelocityDeltaYaw;
				OneShot.bHasStrafeDirectionAngle = true;
			}
			else
			{
				// 제자리 점프 (Standing Jump)
				OneShot.StrafeDirection = EProject_JStateControllerStrafeDirection::Forward;
			}
		}
		else
		{
			OneShot.StrafeDirection = EProject_JStateControllerStrafeDirection::Forward;
		}
	}
	else if (bIsJumpAirReselecting)
	{
		// Strafe 모드 공중 재선택 시
		OneShot.StrafeDirection = PendingJumpAirDirection;
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

	if (IsPrimaryMeshAnimInstance())
	{
		ResolveStateControllerPresentationStateWithPlaybackHold(Data, OneShot);
	}
	else
	{
		// A linked combat layer consumes the primary instance's resolved pose. It
		// has no State Controller lifetime of its own, so it must not emit empty
		// hold/exit diagnostics or request a second direct Blend Stack path.
		OneShot.PresentationState = EProject_JStateControllerPresentationState::Disabled;
	}
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
	Data.LocomotionContext.bHasGroundMovementIntent = Data.LocomotionContext.bIsMoving && !Data.Air.bIsInAir;
	Data.MotionMatching.SelectionContext.GaitIntent = Data.LocomotionContext.GaitIntent;
	Data.MotionMatching.SelectionContext.RotationMode = Data.LocomotionContext.RotationMode;
	Data.MotionMatching.SelectionContext.PhaseFamily = Data.LocomotionContext.PhaseFamily;
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
		Data.Movement.SnapshotFrame = GFrameCounter;
		Data.Movement.TrajectoryGenerationFrame = TrajectoryComponent->GetGenerationFrame();
		Data.Movement.TrajectoryResetReason = uint8(TrajectoryComponent->GetLastResetReason());
		Data.Movement.TrajectoryResetReasonAfterConsumption = Data.Movement.TrajectoryResetReason;
		Data.Movement.TrajectoryResetRevisionAfterConsumption = TrajectoryComponent->GetResetRevision();
		Data.Movement.bTrajectoryPredictionUsable = TrajectoryComponent->IsTrajectoryPredictionUsable();
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

	// This snapshot describes the physical rider/mount relation. It must not
	// depend on the broader presentation context: a future vehicle context may
	// still use the same attachment and rider hand-target contract.
	Data.Mount.bIsMounted = true;
	if (const UProject_JRiderAnimationProfile* RiderProfile = MountedMount->GetRiderAnimationProfile())
	{
		Data.Mount.AnimationTags = RiderProfile->AnimationTags;
		Data.Mount.TransitionBlendTime = RiderProfile->TransitionBlendTime;
	}
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
	if (bHasAimData && Data.LocomotionMode == EProject_JAnimationLocomotionMode::OnFoot)
	{
		Data.Aim.AimOffsetAlpha = CalculateAimOffsetAlpha(Data);
	}
	else
	{
		Data.Aim.AimOffsetAlpha = 0.0f;
	}

	const UProject_JLocomotionProfile* Profile = GetLocomotionProfile();
	const FProject_JLocomotionPresentationPolicy* PresentationPolicy = Profile
		? &Profile->PresentationPolicy
		: nullptr;
	if (!PresentationPolicy || !PresentationPolicy->bEnableLean)
	{
		bSprintCurvatureLeanSuppressed = false;
		Data.Movement.bSprintCurvatureLeanSuppressed = false;
		Data.Movement.bShouldApplyLeanAdditive = false;
		Data.Movement.LeanAmount = FVector2D::ZeroVector;
		return;
	}

	const bool bIsActivelySprinting = Data.LocomotionContext.bIsMoving &&
		(Data.LocomotionContext.GaitIntent == EProject_JLocomotionGaitIntent::Sprint);
	const bool bIsAirLeanAllowed = Data.Air.bIsInAir && PresentationPolicy->bEnableLeanInAir;

	bool bShouldApplyLean = (bIsActivelySprinting || bIsAirLeanAllowed);

	// OTM 스프린트 중 코너링/곡선 주행 시 린 차단 판정:
	// 모션 매칭이 자체 뱅킹/기울기가 포함된 Sprint Diamond 애니메이션을 재생하므로,
	// Additive Lean 중첩으로 인한 척추 왜곡을 방지하기 위해 각도 임계값 기반으로 차단합니다.
	if (bShouldApplyLean && !Data.Air.bIsInAir && PresentationPolicy->bDisableSprintLeanOnCurvature)
	{
		const bool bIsOTMSprint = (Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::OrientToMovement) &&
			bIsActivelySprinting &&
			(Data.Movement.GroundSpeed >= PresentationPolicy->SprintLeanMinSpeedThreshold);

		if (bIsOTMSprint)
		{
			const float Angle = Data.Movement.VelocityToMoveInputAngle;
			const float CutoffAngle = PresentationPolicy->SprintLeanCutoffVelocityAngle;
			const float Hysteresis = PresentationPolicy->SprintLeanAngleHysteresis;

			if (bSprintCurvatureLeanSuppressed)
			{
				// 이미 차단 중인 경우: (CutoffAngle - Hysteresis) 이하로 떨어져야 직선 복귀로 판정하여 린 재활성화
				if (Angle <= FMath::Max(0.0f, CutoffAngle - Hysteresis))
				{
					bSprintCurvatureLeanSuppressed = false;
				}
			}
			else
			{
				// 정상 린 적용 중: CutoffAngle 이상이면 곡선 주행(Diamond)으로 판정하여 린 차단
				if (Angle >= CutoffAngle)
				{
					bSprintCurvatureLeanSuppressed = true;
				}
			}

			if (bSprintCurvatureLeanSuppressed)
			{
				bShouldApplyLean = false;
			}
		}
		else
		{
			bSprintCurvatureLeanSuppressed = false;
		}
	}
	else
	{
		bSprintCurvatureLeanSuppressed = false;
	}

	Data.Movement.bSprintCurvatureLeanSuppressed = bSprintCurvatureLeanSuppressed;
	Data.Movement.bShouldApplyLeanAdditive = bShouldApplyLean;
	if (!Data.Movement.bShouldApplyLeanAdditive)
	{
		Data.Movement.LeanAmount = FVector2D::ZeroVector;
		return;
	}

	float LeanMultiplier = Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe
		? PresentationPolicy->CombatStrafeLeanMultiplier
		: PresentationPolicy->OrientToMovementLeanMultiplier;

	if (Data.Air.bIsInAir)
	{
		LeanMultiplier *= PresentationPolicy->AirLeanMultiplier;
	}

	const float LeanClamp = FMath::Max(0.0f, PresentationPolicy->LeanAxisClamp);
	// The state component stores local X as forward/braking and local Y as lateral.
	// Lean consumers conventionally expose X=lateral and Y=forward/back.
	Data.Movement.LeanAmount = FVector2D(
		FMath::Clamp(Data.Movement.RelativeAccelerationAmount.Y * LeanMultiplier, -LeanClamp, LeanClamp),
		FMath::Clamp(Data.Movement.RelativeAccelerationAmount.X * LeanMultiplier, -LeanClamp, LeanClamp));
}

void UProject_JCharacterAnimInstance::FillProceduralIKThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
	check(IsInGameThread());
	Data.bIsLocallyControlled = IsLocallyControlledCharacter();
	const UProject_JLocomotionProfile* Profile = GetLocomotionProfile();
	Data.ProceduralIK.PlantSettings = Profile
		? (Data.Ground.bStopRequested ? Profile->FootPlacementPlantSettingsStops : Profile->FootPlacementPlantSettingsDefault)
		: (Data.Ground.bStopRequested ? FootPlacementPlantSettingsStops : FootPlacementPlantSettingsDefault);
	Data.ProceduralIK.InterpolationSettings = Profile
		? (Data.Ground.bStopRequested ? Profile->FootPlacementInterpolationSettingsStops : Profile->FootPlacementInterpolationSettingsDefault)
		: (Data.Ground.bStopRequested ? FootPlacementInterpolationSettingsStops : FootPlacementInterpolationSettingsDefault);

	// Foot contact and standing-leg correction are valid only for the shared
	// on-foot body provider. Mounted, swimming, vehicle, and transformed layers
	// own any applicable procedural work inside their own linked AnimBP.
	if (Data.LocomotionMode != EProject_JAnimationLocomotionMode::OnFoot)
	{
		Data.ProceduralIK.FullBodyMontageWeight = 0.0f;
		Data.ProceduralIK.FootPlacementAlpha = 0.0f;
		Data.ProceduralIK.LegIKAlpha = 0.0f;
		return;
	}

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
