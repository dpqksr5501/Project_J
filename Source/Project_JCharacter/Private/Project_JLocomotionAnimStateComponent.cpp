// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JLocomotionAnimStateComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JMotionMatchingCVars.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Project_JPlayerCharacter.h"
#include "TimerManager.h"

UProject_JLocomotionAnimStateComponent::UProject_JLocomotionAnimStateComponent()
{
}

void UProject_JLocomotionAnimStateComponent::ApplyTransitionPolicy(const FProject_JLocomotionTransitionPolicy& InPolicy)
{
	IdleSpeedThreshold = InPolicy.IdleSpeedThreshold;
	DerivedMovingSpeedThreshold = InPolicy.MovingSpeedThreshold;
	DerivedStartSpeedGainThreshold = InPolicy.StartSpeedGainThreshold;
	DerivedMovementPredictionTime = InPolicy.MovementPredictionTime;
	StopIntentSpeedThreshold = InPolicy.StopIntentSpeedThreshold;
	StopExitSpeedThreshold = InPolicy.StopExitSpeedThreshold;
	StartCompletionSpeedFraction = InPolicy.StartCompletionSpeedFraction;
	SharpTurnAngleThreshold = InPolicy.SharpTurnAngleThreshold;
	SharpTurnMinSpeed = InPolicy.SharpTurnMinSpeed;
	DerivedPivotAngleThreshold = InPolicy.PivotAngleThreshold;
	DerivedTurnAngleThreshold = InPolicy.TurnRedirectAngleThreshold;
	DerivedTurnMinSpeed = InPolicy.TurnRedirectMinSpeed;
	DerivedPivotMinSpeed = FMath::Max(InPolicy.PivotMinSpeed, InPolicy.TurnRedirectMinSpeed);
	DerivedTurnMinHoldTime = InPolicy.TurnRedirectMinHoldTime;
	DerivedPivotInputReleaseGraceTime = InPolicy.PivotInputReleaseGraceTime;
	DerivedPivotInputReleaseBridgeTime = InPolicy.PivotInputReleaseBridgeTime;
	DerivedPivotInputReleaseMinimumTurnAngle = InPolicy.PivotInputReleaseMinimumTurnAngle;
	TurnRedirectReselectCooldown = InPolicy.TurnRedirectReselectCooldown;
	SprintStopMemoryDuration = InPolicy.SprintStopMemoryDuration;
}


void UProject_JLocomotionAnimStateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearOwnedMovementGameplayTags();
	Super::EndPlay(EndPlayReason);
}

void UProject_JLocomotionAnimStateComponent::UpdateState(float DeltaTime)
{
	if (!IsRegistered())
	{
		return;
	}

	SprintStopMemoryTimeRemaining = FMath::Max(0.0f, SprintStopMemoryTimeRemaining - DeltaTime);

	AProject_JPlayerCharacter* PlayerOwner = nullptr;
	if (!RefreshOwnerReferencesForUpdate(PlayerOwner))
	{
		return;
	}

	if (ShouldSkipUpdateForCurrentContext(DeltaTime))
	{
		return;
	}

	const FProject_JLocomotionRuntimeSnapshot MovementSnapshot = BuildMovementSnapshot(*PlayerOwner);
	ApplyMovementSnapshot(DeltaTime, MovementSnapshot);

	UpdateAirAndMovementRequests(DeltaTime, IsInAirForAnimation());
	UpdateLocomotionContexts(DeltaTime, MovementSnapshot);
	UpdateCombatMovementState(MovementSnapshot.HorizontalVelocity);
	LogMotionMatchingNetworkDebugIfEnabled(*PlayerOwner);
}

bool UProject_JLocomotionAnimStateComponent::RefreshOwnerReferencesForUpdate(AProject_JPlayerCharacter*& OutPlayerOwner)
{
	if (!GetPlayerOwner() || !GetCachedMovementComponent())
	{
		CacheOwnerReferences();
	}

	OutPlayerOwner = GetPlayerOwner();
	return OutPlayerOwner != nullptr;
}

bool UProject_JLocomotionAnimStateComponent::ShouldSkipUpdateForCurrentContext(float DeltaTime)
{
	bUsingLocalInputState = ShouldUseLocalInputState();
	bDedicatedServerContext = IsDedicatedServerContext();
	if (bDedicatedServerContext && bSkipDedicatedServerAnimStateUpdate)
	{
		return true;
	}

	bRecentlyRendered = WasRecentlyRendered(RecentlyRenderedTolerance);
	if (!bUsingLocalInputState && !bRecentlyRendered)
	{
		const float UpdateInterval = HiddenRemoteUpdateInterval;
		if (UpdateInterval > 0.0f)
		{
			HiddenRemoteUpdateAccumulator += DeltaTime;
			if (HiddenRemoteUpdateAccumulator < UpdateInterval)
			{
				return true;
			}
		}
	}

	HiddenRemoteUpdateAccumulator = 0.0f;
	return false;
}

void UProject_JLocomotionAnimStateComponent::UpdateAirAndMovementRequests(float DeltaTime, bool bMovementReportsInAir)
{
	if (bUsingLocalInputState)
	{
		UpdateLocalAirState(bMovementReportsInAir);
		UpdateMovementRequestState(DeltaTime);
		return;
	}

	UpdateRemoteAirState(
		DeltaTime,
		IsRemoteInAirForAnimation(bMovementReportsInAir),
		bMovementReportsInAir);
	UpdateRemoteMovementRequestState(DeltaTime);
}

void UProject_JLocomotionAnimStateComponent::UpdateLocomotionContexts(float DeltaTime, const FProject_JLocomotionRuntimeSnapshot& Snapshot)
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		AuthoritativeContext = FProject_JLocomotionAuthoritativeContext();
		KinematicContext = FProject_JLocomotionKinematicContext();
		DerivedLocomotionContext = FProject_JDerivedLocomotionContext();
		PreviousDerivedPhaseFamily = EProject_JLocomotionPhaseFamily::Idle;
		DerivedPhaseFamilyElapsedTime = 0.0f;
		bMotionMatchingSelectionChanged = true;
		bForceMotionMatchingReselect = false;
		MotionMatchingSelectionContext = FProject_JMotionMatchingSelectionContext();
		bHasPublishedMotionMatchingSelection = false;
		++MotionMatchingSelectionRevision;
		if (MotionMatchingSelectionRevision == 0)
		{
			MotionMatchingSelectionRevision = 1;
		}
		return;
	}

	AuthoritativeContext = BuildAuthoritativeContext(*PlayerOwner, Snapshot);
	KinematicContext = BuildKinematicContext(*PlayerOwner, Snapshot, DeltaTime);
	FProject_JDerivedLocomotionContext NewDerivedContext = BuildDerivedLocomotionContext(AuthoritativeContext, KinematicContext);
	ApplyLocomotionPhaseStability(DeltaTime, NewDerivedContext);
	DerivedLocomotionContext = NewDerivedContext;
	UpdateMotionMatchingSelectionState(*PlayerOwner);
}

void UProject_JLocomotionAnimStateComponent::UpdateMotionMatchingSelectionState(const AProject_JPlayerCharacter& PlayerOwner)
{
	MotionMatchingSelectionContext.GaitIntent = AuthoritativeContext.GaitIntent;
	MotionMatchingSelectionContext.RotationMode = AuthoritativeContext.RotationMode;
	MotionMatchingSelectionContext.PhaseFamily = DerivedLocomotionContext.PhaseFamily;
	MotionMatchingSelectionContext.bUseHeavyLand = bUseHeavyLand;
	MotionMatchingSelectionContext.bLandWasMoving = bLandWasMoving;
	MotionMatchingSelectionContext.bLandWasSprinting = bLandWasSprinting;
	MotionMatchingSelectionContext.bUseFallOffStart = bIsFallOffStart;
	MotionMatchingSelectionContext.bUseRemoteStart = !bUsingLocalInputState && bUseForwardOnlyRemoteStart;
	MotionMatchingSelectionContext.bUseGenericFamiliesForNonOrientToMovement = false;

	const bool bSelectionChanged =
		!bHasPublishedMotionMatchingSelection ||
		LastPublishedMotionMatchingGait != AuthoritativeContext.GaitIntent ||
		LastPublishedMotionMatchingRotationMode != AuthoritativeContext.RotationMode ||
		LastPublishedMotionMatchingPhase != DerivedLocomotionContext.PhaseFamily ||
		LastPublishedGroundMotionMode != GroundMotionMode ||
		bLastPublishedHeavyLand != bUseHeavyLand ||
		bLastPublishedLandWasMoving != bLandWasMoving ||
		bLastPublishedLandWasSprinting != bLandWasSprinting ||
		bLastPublishedFallOffStart != bIsFallOffStart ||
		bLastPublishedUseRemoteStart != MotionMatchingSelectionContext.bUseRemoteStart;

	bMotionMatchingSelectionChanged = bSelectionChanged;
	if (bSelectionChanged)
	{
		LastPublishedMotionMatchingGait = AuthoritativeContext.GaitIntent;
		LastPublishedMotionMatchingRotationMode = AuthoritativeContext.RotationMode;
		LastPublishedMotionMatchingPhase = DerivedLocomotionContext.PhaseFamily;
		LastPublishedGroundMotionMode = GroundMotionMode;
		bLastPublishedHeavyLand = bUseHeavyLand;
		bLastPublishedLandWasMoving = bLandWasMoving;
		bLastPublishedLandWasSprinting = bLandWasSprinting;
		bLastPublishedFallOffStart = bIsFallOffStart;
		bLastPublishedUseRemoteStart = MotionMatchingSelectionContext.bUseRemoteStart;
		bHasPublishedMotionMatchingSelection = true;
		++MotionMatchingSelectionRevision;
		if (MotionMatchingSelectionRevision == 0)
		{
			MotionMatchingSelectionRevision = 1;
		}
	}

	// Do not force an interrupt for Start, Stop, Jump, Fall, or Landing. Those
	// PSDs intentionally hold their initial result. The only same-database
	// exception is a local moving Turn/Pivot redirect: a new input direction
	// must discard the old continuing pose, but a short cooldown coalesces rapid
	// W-A-D changes into a single re-search.
	const UProject_JCombatAnimProfile* CombatProfile = PlayerOwner.GetCombatAnimProfile();
	const bool bCanForceTurnRedirectReselect =
		bUsingLocalInputState &&
		(DerivedLocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Cycle ||
			DerivedLocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Turn ||
			DerivedLocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot) &&
		bHasMoveInput &&
		GroundSpeed >= DerivedTurnMinSpeed;
	const bool bUsesCombatStrafeReselectPolicy =
		AuthoritativeContext.bCombatMode &&
		AuthoritativeContext.RotationMode == EProject_JLocomotionRotationMode::Strafe;
	const float ReselectAngle = bUsesCombatStrafeReselectPolicy && CombatProfile
		? CombatProfile->StrafeInputTurnReselectAngle
		: DerivedTurnAngleThreshold;
	const bool bReselectPolicyEnabled = !bUsesCombatStrafeReselectPolicy ||
		(CombatProfile && CombatProfile->bForceReselectOnStrafeInputTurn);
	const bool bRequestsTurnRedirectReselect =
		bCanForceTurnRedirectReselect &&
		bReselectPolicyEnabled &&
		FMath::Abs(MoveInputTurnAngle) >= ReselectAngle;
	const double WorldTimeSeconds = PlayerOwner.GetWorld() ? PlayerOwner.GetWorld()->GetTimeSeconds() : 0.0;
	const float ReselectCooldown = bUsesCombatStrafeReselectPolicy && CombatProfile
		? CombatProfile->StrafeInputTurnReselectCooldown
		: TurnRedirectReselectCooldown;
	bForceMotionMatchingReselect = bRequestsTurnRedirectReselect &&
		(WorldTimeSeconds - LastCombatStrafeReselectTimeSeconds >= ReselectCooldown);
	if (bForceMotionMatchingReselect)
	{
		LastCombatStrafeReselectTimeSeconds = WorldTimeSeconds;
	}
}

void UProject_JLocomotionAnimStateComponent::LogMotionMatchingNetworkDebugIfEnabled(const AProject_JPlayerCharacter& PlayerOwner) const
{
#if !UE_BUILD_SHIPPING
	const int32 DebugMode = Project_J::MotionMatchingCVars::GetNetworkDebugMode();
	if (DebugMode <= 0 ||
		(DebugMode == 1 && !bMotionMatchingSelectionChanged && !bForceMotionMatchingReselect))
	{
		return;
	}

	const UProject_JMotionMatchingTrajectoryComponent* TrajectoryComponent = PlayerOwner.GetMotionMatchingTrajectoryComponent();
	const FTransformTrajectory* Trajectory = TrajectoryComponent ? &TrajectoryComponent->GetTrajectory() : nullptr;
	const TCHAR* Role = PlayerOwner.GetLocalRole() == ROLE_Authority
		? TEXT("Authority")
		: (PlayerOwner.GetLocalRole() == ROLE_AutonomousProxy ? TEXT("Autonomous") : TEXT("Simulated"));

	UE_LOG(LogProjectJPlayer, Display,
		TEXT("MMNetState Actor=%s Role=%s LocalInput=%s Rendered=%s Rev=%d Changed=%s ForceReselect=%s Gait=%d Rotation=%d Phase=%d GroundMode=%d RemoteStart=%s Speed=%.1f InputTurn=%.1f VelocityToInput=%.1f StopDist=%.1f RelativeAccel=(%.2f,%.2f)"),
		*GetNameSafe(&PlayerOwner), Role,
		bUsingLocalInputState ? TEXT("true") : TEXT("false"),
		bRecentlyRendered ? TEXT("true") : TEXT("false"),
		MotionMatchingSelectionRevision,
		bMotionMatchingSelectionChanged ? TEXT("true") : TEXT("false"),
		bForceMotionMatchingReselect ? TEXT("true") : TEXT("false"),
		static_cast<int32>(MotionMatchingSelectionContext.GaitIntent),
		static_cast<int32>(MotionMatchingSelectionContext.RotationMode),
		static_cast<int32>(MotionMatchingSelectionContext.PhaseFamily),
		static_cast<int32>(GroundMotionMode),
		MotionMatchingSelectionContext.bUseRemoteStart ? TEXT("true") : TEXT("false"),
		GroundSpeed, MoveInputTurnAngle,
		KinematicContext.VelocityToMoveInputAngle,
		KinematicContext.PredictedStopDistance,
		KinematicContext.RelativeAccelerationAmount.X,
		KinematicContext.RelativeAccelerationAmount.Y);
	UE_LOG(LogProjectJPlayer, Display,
		TEXT("MMNetTrajectory Actor=%s Samples=%d History=%d Prediction=%d RemoteFacingRepair=%s RemotePosSmoothing=%s RemoteRotSmoothing=%s"),
		*GetNameSafe(&PlayerOwner),
		Trajectory ? Trajectory->Samples.Num() : 0,
		TrajectoryComponent ? TrajectoryComponent->GetSamplingData().NumHistorySamples : 0,
		TrajectoryComponent ? TrajectoryComponent->GetSamplingData().NumPredictionSamples : 0,
		Project_J::MotionMatchingCVars::ShouldRepairRemoteTrajectoryFacing() ? TEXT("true") : TEXT("false"),
		Project_J::MotionMatchingCVars::ShouldSmoothRemoteTrajectoryPosition() ? TEXT("true") : TEXT("false"),
		Project_J::MotionMatchingCVars::ShouldSmoothRemoteTrajectoryRotation() ? TEXT("true") : TEXT("false"));
#endif
}

FProject_JLocomotionAuthoritativeContext UProject_JLocomotionAnimStateComponent::BuildAuthoritativeContext(
	const AProject_JPlayerCharacter& PlayerOwner,
	const FProject_JLocomotionRuntimeSnapshot& Snapshot) const
{
	FProject_JLocomotionAuthoritativeContext Context;
	Context.bSprintAllowed = PlayerOwner.IsSprintLocomotionAllowed();
	Context.bJumpAllowed = PlayerOwner.IsJumpLocomotionAllowed();
	Context.bCombatMode = PlayerOwner.IsCombatModeActive();
	Context.GaitIntent = ResolveGaitIntent(PlayerOwner, Snapshot);
	Context.RotationMode = ResolveRotationMode(PlayerOwner);
	return Context;
}

FProject_JLocomotionKinematicContext UProject_JLocomotionAnimStateComponent::BuildKinematicContext(
	const AProject_JPlayerCharacter& PlayerOwner,
	const FProject_JLocomotionRuntimeSnapshot& Snapshot,
	float DeltaTime)
{
	FProject_JLocomotionKinematicContext Context;
	Context.Velocity = Snapshot.Velocity;
	Context.HorizontalVelocity = Snapshot.HorizontalVelocity;
	Context.GroundSpeed = Snapshot.GroundSpeed;
	Context.VerticalSpeed = Snapshot.VerticalSpeed;
	Context.MoveInputTurnAngle = MoveInputTurnAngle;
	Context.bHasMoveInput = bHasMoveInput;
	Context.MoveWorldDirection = CalculateMoveWorldDirection(GetMovementInputForState());
	float BrakingDeceleration = 0.0f;

	if (const UCharacterMovementComponent* MovementComponent = PlayerOwner.GetCharacterMovement())
	{
		Context.Acceleration = MovementComponent->GetCurrentAcceleration();
		Context.bIsAccelerating = Context.Acceleration.SizeSquared2D() > UE_KINDA_SMALL_NUMBER;
		const float MaxAcceleration = FMath::Max(MovementComponent->GetMaxAcceleration(), UE_KINDA_SMALL_NUMBER);
		Context.AccelerationRatio = FMath::Clamp(Context.Acceleration.Size2D() / MaxAcceleration, 0.0f, 1.0f);
		BrakingDeceleration = FMath::Max(MovementComponent->BrakingDecelerationWalking, UE_KINDA_SMALL_NUMBER);
		Context.PredictedStopDistance = FMath::Square(Context.GroundSpeed) / (2.0f * BrakingDeceleration);

		if (bHasPreviousKinematicVelocity && DeltaTime > UE_KINDA_SMALL_NUMBER)
		{
			Context.VelocityAcceleration = (Context.HorizontalVelocity - PreviousKinematicHorizontalVelocity) / DeltaTime;
			const float VelocityAccelerationMagnitude = Context.VelocityAcceleration.Size2D();
			if (Context.GroundSpeed > DerivedMovingSpeedThreshold && VelocityAccelerationMagnitude > UE_KINDA_SMALL_NUMBER)
			{
				Context.bIsDecelerating = FVector::DotProduct(Context.HorizontalVelocity.GetSafeNormal2D(), Context.VelocityAcceleration.GetSafeNormal2D()) < -0.05f;
			}

			const FVector LocalVelocityAcceleration = PlayerOwner.GetActorTransform().InverseTransformVectorNoScale(Context.VelocityAcceleration);
			const float Normalization = Context.bIsDecelerating ? BrakingDeceleration : MaxAcceleration;
			Context.RelativeAccelerationAmount = FVector(
				FMath::Clamp(LocalVelocityAcceleration.X / Normalization, -1.0f, 1.0f),
				FMath::Clamp(LocalVelocityAcceleration.Y / Normalization, -1.0f, 1.0f),
				0.0f);
		}
	}
	PreviousKinematicHorizontalVelocity = Context.HorizontalVelocity;
	bHasPreviousKinematicVelocity = true;

	if (Context.bHasMoveInput && Context.GroundSpeed > DerivedMovingSpeedThreshold && !Context.MoveWorldDirection.IsNearlyZero())
	{
		const float DirectionDot = FMath::Clamp(FVector::DotProduct(Context.HorizontalVelocity.GetSafeNormal2D(), Context.MoveWorldDirection), -1.0f, 1.0f);
		Context.VelocityToMoveInputAngle = FMath::RadiansToDegrees(FMath::Acos(DirectionDot));
	}

	// Pose Search already consumes the full trajectory. Reconstruct the same
	// short-horizon future velocity for C++ state decisions rather than relying
	// only on acceleration/braking extrapolation. This stays on the game thread.
	if (const UProject_JMotionMatchingTrajectoryComponent* TrajectoryComponent = PlayerOwner.GetMotionMatchingTrajectoryComponent())
	{
		const FTransformTrajectory& Trajectory = TrajectoryComponent->GetTrajectory();
		const FTransformTrajectorySample* PresentSample = nullptr;
		const FTransformTrajectorySample* FutureSample = nullptr;
		float BestPresentTime = TNumericLimits<float>::Max();
		float BestFutureTimeDelta = TNumericLimits<float>::Max();

		for (const FTransformTrajectorySample& Sample : Trajectory.Samples)
		{
			const float AbsoluteSampleTime = FMath::Abs(Sample.TimeInSeconds);
			if (AbsoluteSampleTime < BestPresentTime)
			{
				BestPresentTime = AbsoluteSampleTime;
				PresentSample = &Sample;
			}

			if (Sample.TimeInSeconds > UE_KINDA_SMALL_NUMBER)
			{
				const float HorizonDelta = FMath::Abs(Sample.TimeInSeconds - DerivedMovementPredictionTime);
				if (HorizonDelta < BestFutureTimeDelta)
				{
					BestFutureTimeDelta = HorizonDelta;
					FutureSample = &Sample;
				}
			}
		}

		if (PresentSample && FutureSample)
		{
			const float SampleDeltaTime = FutureSample->TimeInSeconds - PresentSample->TimeInSeconds;
			if (SampleDeltaTime > UE_KINDA_SMALL_NUMBER)
			{
				Context.FutureTrajectoryVelocity =
					(FutureSample->GetTransform().GetLocation() - PresentSample->GetTransform().GetLocation()) / SampleDeltaTime;
				Context.FutureTrajectoryVelocity.Z = 0.0f;
				Context.FutureTrajectorySpeed = Context.FutureTrajectoryVelocity.Size2D();
				Context.bHasFutureTrajectoryVelocity = true;

				if (Context.GroundSpeed > DerivedMovingSpeedThreshold &&
					Context.FutureTrajectorySpeed > DerivedMovingSpeedThreshold)
				{
					const float FutureDirectionDot = FMath::Clamp(
						FVector::DotProduct(Context.HorizontalVelocity.GetSafeNormal2D(), Context.FutureTrajectoryVelocity.GetSafeNormal2D()),
						-1.0f,
						1.0f);
					Context.FutureTrajectoryTurnAngle = FMath::RadiansToDegrees(FMath::Acos(FutureDirectionDot));
				}
			}
		}
	}

	const float ApproximatePredictedSpeed = Context.bHasMoveInput
		? Context.GroundSpeed + Context.Acceleration.Size2D() * DerivedMovementPredictionTime
		: FMath::Max(0.0f, Context.GroundSpeed - BrakingDeceleration * DerivedMovementPredictionTime);
	const float PredictedSpeed = Context.bHasFutureTrajectoryVelocity
		? Context.FutureTrajectorySpeed
		: ApproximatePredictedSpeed;
	Context.PredictedSpeedGain = PredictedSpeed - Context.GroundSpeed;
	Context.bHasPredictedMovement = PredictedSpeed > DerivedMovingSpeedThreshold;

	if (!Context.MoveWorldDirection.IsNearlyZero())
	{
		Context.DesiredFacingYaw = Context.MoveWorldDirection.Rotation().Yaw;
	}
	else
	{
		Context.DesiredFacingYaw = PlayerOwner.GetControlRotation().Yaw;
	}
	Context.DesiredFacingDeltaYaw = FMath::FindDeltaAngleDegrees(
		PlayerOwner.GetActorRotation().Yaw,
		Context.DesiredFacingYaw);

	return Context;
}

FProject_JDerivedLocomotionContext UProject_JLocomotionAnimStateComponent::BuildDerivedLocomotionContext(
	const FProject_JLocomotionAuthoritativeContext& AuthContext,
	const FProject_JLocomotionKinematicContext& InKinematicContext) const
{
	FProject_JDerivedLocomotionContext Context;
	Context.bIsMoving = IsMovingForContext(InKinematicContext);
	Context.bIsMotionMatchingMoving = IsMotionMatchingMovingForContext(InKinematicContext);
	Context.bIsStarting = IsStartingForContext(AuthContext, InKinematicContext);
	Context.bIsPivoting = IsPivotingForContext(AuthContext, InKinematicContext);
	Context.bShouldTurnInPlace = ShouldTurnInPlaceForContext(AuthContext, InKinematicContext);
	Context.bShouldSpinTransition = ShouldSpinTransitionForContext(AuthContext, InKinematicContext);
	Context.PhaseFamily = ResolvePhaseFamily(Context);
	return Context;
}

void UProject_JLocomotionAnimStateComponent::ApplyLocomotionPhaseStability(
	float DeltaTime,
	FProject_JDerivedLocomotionContext& InOutContext)
{
	if (PreviousDerivedPhaseFamily == InOutContext.PhaseFamily)
	{
		DerivedPhaseFamilyElapsedTime += DeltaTime;
		return;
	}

	const bool bKeepTurnRedirect =
		PreviousDerivedPhaseFamily == EProject_JLocomotionPhaseFamily::Turn &&
		InOutContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Cycle &&
		DerivedPhaseFamilyElapsedTime < DerivedTurnMinHoldTime &&
		KinematicContext.bHasMoveInput &&
		KinematicContext.GroundSpeed > StopIntentSpeedThreshold &&
		!bIsInAir &&
		!IsLandingStateActive();

	if (bKeepTurnRedirect)
	{
		InOutContext.PhaseFamily = PreviousDerivedPhaseFamily;
		DerivedPhaseFamilyElapsedTime += DeltaTime;
		return;
	}

	// GASP caches its previous movement direction and lets its re-entry path
	// survive a short input discontinuity. Keyboard direction reversals commonly
	// contain one released-input frame (W+A -> release -> S+D); do not replace a
	// Pivot already selected on the preceding frame with Stop during that gap.
	const bool bKeepPivotAcrossInputRelease =
		PreviousDerivedPhaseFamily == EProject_JLocomotionPhaseFamily::Pivot &&
		(InOutContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Cycle ||
			InOutContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Stop) &&
		!KinematicContext.bHasMoveInput &&
		KinematicContext.GroundSpeed > StopIntentSpeedThreshold &&
		DerivedPhaseFamilyElapsedTime < DerivedPivotInputReleaseGraceTime &&
		!bIsInAir &&
		!IsLandingStateActive();

	if (bKeepPivotAcrossInputRelease)
	{
		InOutContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Pivot;
		// The StateController maps Pivot to TransitionToLocomotion only while this
		// flag is true. Keep the direct one-shot path selected for the grace frame.
		InOutContext.bIsMotionMatchingMoving = true;
		DerivedPhaseFamilyElapsedTime += DeltaTime;
		return;
	}

	const bool bKeepTurnInPlace =
		PreviousDerivedPhaseFamily == EProject_JLocomotionPhaseFamily::TurnInPlace &&
		(GroundMotionMode == EProject_JGroundMotionMode::Idle ||
			GroundMotionMode == EProject_JGroundMotionMode::Stop) &&
		!KinematicContext.bHasMoveInput &&
		KinematicContext.GroundSpeed <= IdleSpeedThreshold &&
		FMath::Abs(KinematicContext.DesiredFacingDeltaYaw) > 5.0f &&
		DerivedPhaseFamilyElapsedTime < 1.5f &&
		!bIsInAir &&
		!IsLandingStateActive();

	if (bKeepTurnInPlace)
	{
		InOutContext.PhaseFamily = PreviousDerivedPhaseFamily;
		InOutContext.bShouldTurnInPlace = true;
		DerivedPhaseFamilyElapsedTime += DeltaTime;
		return;
	}

	PreviousDerivedPhaseFamily = InOutContext.PhaseFamily;
	DerivedPhaseFamilyElapsedTime = 0.0f;
}

EProject_JLocomotionGaitIntent UProject_JLocomotionAnimStateComponent::ResolveGaitIntent(
	const AProject_JPlayerCharacter&,
	const FProject_JLocomotionRuntimeSnapshot& Snapshot) const
{
	if (bWantsSprint || Snapshot.GroundSpeed >= SprintLocomotionSpeedThreshold)
	{
		return EProject_JLocomotionGaitIntent::Sprint;
	}

	return Snapshot.GroundSpeed > IdleSpeedThreshold || bHasMoveInput
		? EProject_JLocomotionGaitIntent::Run
		: EProject_JLocomotionGaitIntent::Walk;
}

EProject_JLocomotionRotationMode UProject_JLocomotionAnimStateComponent::ResolveRotationMode(
	const AProject_JPlayerCharacter& PlayerOwner) const
{
	return PlayerOwner.IsCombatModeActive()
		? EProject_JLocomotionRotationMode::Strafe
		: EProject_JLocomotionRotationMode::OrientToMovement;
}

EProject_JLocomotionPhaseFamily UProject_JLocomotionAnimStateComponent::ResolvePhaseFamily(
	const FProject_JDerivedLocomotionContext& Context) const
{
	if (IsLandingStateActive())
	{
		return EProject_JLocomotionPhaseFamily::Landing;
	}
	if (bIsJumping)
	{
		return EProject_JLocomotionPhaseFamily::JumpStart;
	}
	if (bIsFallOffStart || bIsInAir)
	{
		return EProject_JLocomotionPhaseFamily::Fall;
	}
	if (Context.bShouldTurnInPlace)
	{
		return EProject_JLocomotionPhaseFamily::TurnInPlace;
	}
	if (Context.bIsPivoting)
	{
		return EProject_JLocomotionPhaseFamily::Pivot;
	}
	// A released input may have put the semantic ground state in Stop one frame
	// earlier. A new high-speed reverse input is a Pivot, not a Start/Stop pair.
	if (GroundMotionMode == EProject_JGroundMotionMode::Stop)
	{
		return EProject_JLocomotionPhaseFamily::Stop;
	}
	if (Context.bIsStarting)
	{
		return EProject_JLocomotionPhaseFamily::Start;
	}
	const bool bHasMovingTurnIntent =
		Context.bIsMoving &&
		KinematicContext.bHasMoveInput &&
		KinematicContext.GroundSpeed >= DerivedTurnMinSpeed;
	const bool bIsCombatStrafe =
		AuthoritativeContext.RotationMode == EProject_JLocomotionRotationMode::Strafe;
	// TurnRedirect triggers when the player changes WASD input heading (e.g. W -> D).
	// Mouse camera rotation while holding W does not change MoveInputTurnAngle, allowing
	// smooth Orient-To-Movement capsule turning in Motion Matching Cycle phase.
	const bool bShouldUseTurnRedirect = bHasMovingTurnIntent &&
		FMath::Abs(KinematicContext.MoveInputTurnAngle) >= DerivedTurnAngleThreshold;
	if (bShouldUseTurnRedirect)
	{
		return EProject_JLocomotionPhaseFamily::Turn;
	}

	return Context.bIsMoving ? EProject_JLocomotionPhaseFamily::Cycle : EProject_JLocomotionPhaseFamily::Idle;
}

bool UProject_JLocomotionAnimStateComponent::IsMovingForContext(const FProject_JLocomotionKinematicContext& InKinematicContext) const
{
	return InKinematicContext.GroundSpeed > DerivedMovingSpeedThreshold ||
		(InKinematicContext.bHasMoveInput &&
			(InKinematicContext.bIsAccelerating || InKinematicContext.bHasPredictedMovement));
}

bool UProject_JLocomotionAnimStateComponent::IsMotionMatchingMovingForContext(
	const FProject_JLocomotionKinematicContext& InKinematicContext) const
{
	// This deliberately differs from gameplay "is moving".  The State Controller
	// needs GASP-style *locomotion intent*: current velocity, future trajectory,
	// and acceleration/input must all still agree that the character wants to move.
	// Consequently a released input enters Stop while the capsule is still braking,
	// instead of waiting for GroundMotionMode to eventually become Stop.
	if (bIsInAir || IsLandingStateActive())
	{
		return false;
	}

	const float FutureSpeed = InKinematicContext.bHasFutureTrajectoryVelocity
		? InKinematicContext.FutureTrajectorySpeed
		: FMath::Max(InKinematicContext.GroundSpeed + InKinematicContext.PredictedSpeedGain, 0.0f);
	const bool bCurrentVelocityMoving = InKinematicContext.GroundSpeed > DerivedMovingSpeedThreshold;
	const bool bFutureVelocityMoving = FutureSpeed > DerivedMovingSpeedThreshold;
	const bool bSustainedLocomotion = bCurrentVelocityMoving &&
		(InKinematicContext.bHasMoveInput || bFutureVelocityMoving);
	// The initial Start frame commonly has near-zero physical velocity.  Preserve
	// that valid start request as long as the input/trajectory predicts movement.
	const bool bStartingFromRest = InKinematicContext.bHasMoveInput &&
		bFutureVelocityMoving &&
		(InKinematicContext.bIsAccelerating || InKinematicContext.bHasPredictedMovement);

	return bSustainedLocomotion || bStartingFromRest;
}

bool UProject_JLocomotionAnimStateComponent::IsStartingForContext(
	const FProject_JLocomotionAuthoritativeContext& AuthContext,
	const FProject_JLocomotionKinematicContext& InKinematicContext) const
{
	const bool bResult =
		InKinematicContext.bHasMoveInput &&
		MoveInputHeldTime <= DerivedStartInputHoldWindow &&
		InKinematicContext.GroundSpeed <= DerivedStartMaxGroundSpeed &&
		(InKinematicContext.bIsAccelerating || InKinematicContext.PredictedSpeedGain >= DerivedStartSpeedGainThreshold) &&
		!bIsInAir &&
		!IsLandingStateActive();

	if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace() && InKinematicContext.bHasMoveInput)
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("IsStartingForContext: Result=%s HasInput=%s HeldTime=%.3f/%.3f Speed=%.1f/%.1f Accel=%s Gain=%.1f/%.1f Air=%s Landing=%s RotationMode=%d"),
			bResult ? TEXT("true") : TEXT("false"),
			InKinematicContext.bHasMoveInput ? TEXT("true") : TEXT("false"),
			MoveInputHeldTime, DerivedStartInputHoldWindow,
			InKinematicContext.GroundSpeed, DerivedStartMaxGroundSpeed,
			InKinematicContext.bIsAccelerating ? TEXT("true") : TEXT("false"),
			InKinematicContext.PredictedSpeedGain, DerivedStartSpeedGainThreshold,
			bIsInAir ? TEXT("true") : TEXT("false"),
			IsLandingStateActive() ? TEXT("true") : TEXT("false"),
			static_cast<int32>(AuthContext.RotationMode));
	}

	return bResult;
}

bool UProject_JLocomotionAnimStateComponent::IsPivotingForContext(
	const FProject_JLocomotionAuthoritativeContext& AuthContext,
	const FProject_JLocomotionKinematicContext& InKinematicContext) const
{
	const float TurnEvidence = FMath::Max3(
		FMath::Abs(InKinematicContext.MoveInputTurnAngle),
		InKinematicContext.VelocityToMoveInputAngle,
		InKinematicContext.FutureTrajectoryTurnAngle);
	const bool bRotationAllowsPivot =
		AuthContext.RotationMode == EProject_JLocomotionRotationMode::Strafe;
	const bool bHasInput = InKinematicContext.bHasMoveInput;
	// A keyboard release can let CharacterMovement decelerate for a frame before
	// the opposite chord arrives. Retain the outgoing speed only inside the same
	// tiny input bridge window; outside it the normal live-speed requirement is
	// unchanged.
	const bool bHasReleaseBridgeSpeed =
		bLastMoveInputTurnUsedReleaseBridge &&
		FMath::Abs(InKinematicContext.MoveInputTurnAngle) >= DerivedPivotInputReleaseMinimumTurnAngle &&
		PivotInputReleaseSpeedReference >= DerivedPivotMinSpeed;
	const bool bHasPivotSpeed =
		InKinematicContext.GroundSpeed >= DerivedPivotMinSpeed || bHasReleaseBridgeSpeed;
	// The input turn is authoritative for a local keyboard redirect. In
	// particular, W+A -> release -> S+D has not yet had time to bend velocity or
	// future trajectory, but is still an intentional 180-degree Pivot. The short
	// release bridge in RefreshMovementInputState makes that input sample valid.
	const bool bHasPivotTurn = FMath::Max3(
		FMath::Abs(InKinematicContext.MoveInputTurnAngle),
		InKinematicContext.VelocityToMoveInputAngle,
		InKinematicContext.FutureTrajectoryTurnAngle) >= DerivedPivotAngleThreshold;
	const bool bResult = bRotationAllowsPivot && bHasInput && bHasPivotSpeed && bHasPivotTurn;

	// The bridge itself reports every partial keyboard chord.  Keep this second
	// line only for a committed Pivot gate so the trace remains event-oriented.
	if (Project_J::MotionMatchingCVars::ShouldCapturePivotDebugTrace() && bResult)
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("PivotDiag GateAccepted Source=%s InputTurn=%.1f Speed=%.1f/%.1f BridgeSpeed=%.1f BridgeAge=%.3f VelToInput=%.1f FutureTurn=%.1f Threshold=%.1f"),
			bHasReleaseBridgeSpeed ? TEXT("ReleaseBridge") : TEXT("LiveKinematics"),
			InKinematicContext.MoveInputTurnAngle,
			InKinematicContext.GroundSpeed,
			DerivedPivotMinSpeed,
			PivotInputReleaseSpeedReference,
			PivotInputReleaseElapsedTime,
			InKinematicContext.VelocityToMoveInputAngle,
			InKinematicContext.FutureTrajectoryTurnAngle,
			DerivedPivotAngleThreshold);
	}

	// Orient-to-Movement already rotates the character toward the requested
	// WASD direction.  A Pivot there would fight CharacterMovement's natural
	// heading update and select a strafe-authored foot redirect unnecessarily.
	if (!bRotationAllowsPivot)
	{
		return false;
	}

	if (!bHasInput || !bHasPivotSpeed)
	{
		return false;
	}

	// A Pivot requires a high-commitment reversal in either current intent or
	// actual/prospective trajectory. Lesser direction changes remain owned by the
	// combat TurnRedirect PSD.
	return bHasPivotTurn;
}

bool UProject_JLocomotionAnimStateComponent::ShouldTurnInPlaceForContext(
	const FProject_JLocomotionAuthoritativeContext& AuthContext,
	const FProject_JLocomotionKinematicContext& InKinematicContext) const
{
	// A simulated proxy does not have the owning player's current controller
	// yaw. Treating its replicated/fallback control rotation as authoritative
	// produces a permanent facing delta and repeatedly selects a Turn PSD while
	// the remote character is actually stationary. Remote idle must remain Idle;
	// future replicated aim/lock-on state can opt into a dedicated remote turn.
	if (!ShouldUseLocalInputState())
	{
		return false;
	}

	// TIP is deliberately a stationary combat-Strafe presentation.  Movement
	// input (including strafe input while the mouse rotates) remains entirely
	// owned by the normal locomotion/MM path.  Landing is allowed to finish its
	// authored animation first; after that it naturally enters Idle or Stop and
	// can request TIP on the next update if the player is still facing away.
	const bool bStationaryGroundPresentation =
		GroundMotionMode == EProject_JGroundMotionMode::Idle ||
		GroundMotionMode == EProject_JGroundMotionMode::Stop;

	return
		AuthContext.RotationMode == EProject_JLocomotionRotationMode::Strafe &&
		bStationaryGroundPresentation &&
		!InKinematicContext.bHasMoveInput &&
		InKinematicContext.GroundSpeed <= IdleSpeedThreshold &&
		FMath::Abs(InKinematicContext.DesiredFacingDeltaYaw) >= DerivedTurnInPlaceAngleThreshold &&
		!bIsInAir &&
		!IsLandingStateActive();
}

bool UProject_JLocomotionAnimStateComponent::ShouldSpinTransitionForContext(
	const FProject_JLocomotionAuthoritativeContext&,
	const FProject_JLocomotionKinematicContext& InKinematicContext) const
{
	return
		InKinematicContext.bHasMoveInput &&
		InKinematicContext.GroundSpeed > StopIntentSpeedThreshold &&
		FMath::Abs(InKinematicContext.DesiredFacingDeltaYaw) >= DerivedSpinTransitionAngleThreshold;
}

FVector UProject_JLocomotionAnimStateComponent::CalculateMoveWorldDirection(const FVector2D& MoveInput) const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner || MoveInput.SizeSquared() <= FMath::Square(MoveInputDeadZone))
	{
		return FVector::ZeroVector;
	}

	if (!ShouldUseLocalInputState())
	{
		FVector HorizontalVelocity = PlayerOwner->GetVelocity();
		HorizontalVelocity.Z = 0.0f;
		return HorizontalVelocity.GetSafeNormal();
	}

	const FRotator ControlYawRotation(0.0f, PlayerOwner->GetControlRotation().Yaw, 0.0f);
	const FVector ForwardDirection = FRotationMatrix(ControlYawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(ControlYawRotation).GetUnitAxis(EAxis::Y);
	return (ForwardDirection * MoveInput.Y + RightDirection * MoveInput.X).GetSafeNormal();
}

bool UProject_JLocomotionAnimStateComponent::ShouldUseLocalInputState() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	return PlayerOwner && (PlayerOwner->IsLocallyControlled() || bUseInputDerivedRequestsForRemotePlayers);
}

bool UProject_JLocomotionAnimStateComponent::IsSprintRequestedForAnimation() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	return (bSprintInputHeld && (!PlayerOwner || PlayerOwner->IsSprintInputDirectionAllowed())) ||
		(PlayerOwner && PlayerOwner->IsSprintLocomotionAllowed());
}

bool UProject_JLocomotionAnimStateComponent::IsRemoteInAirForAnimation(bool bMovementReportsInAir) const
{
	if (!bMovementReportsInAir)
	{
		return false;
	}

	if (FMath::Abs(VerticalSpeed) <= RemoteGroundedVerticalSpeedTolerance && IsRemoteGroundedByProbe())
	{
		return false;
	}

	return true;
}

bool UProject_JLocomotionAnimStateComponent::IsRemoteGroundedByProbe() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	const UWorld* World = GetWorld();
	const UCapsuleComponent* CapsuleComponent = GetCachedCapsuleComponent();
	if (!bUseRemoteGroundProbe || !PlayerOwner || !World || !CapsuleComponent)
	{
		return false;
	}

	const FVector Start = PlayerOwner->GetActorLocation();
	const float TraceDistance = CapsuleComponent->GetScaledCapsuleHalfHeight() + RemoteGroundProbeDistance;
	const FVector End = Start - FVector(0.0f, 0.0f, TraceDistance);

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RemoteGroundProbe), false, PlayerOwner);
	return World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams) && Hit.bBlockingHit;
}

FVector2D UProject_JLocomotionAnimStateComponent::GetMovementInputForState() const
{
	return ShouldUseLocalInputState() ? GetLocalMovementInputForState() : GetRemoteMovementInputForState();
}

FVector2D UProject_JLocomotionAnimStateComponent::GetLocalMovementInputForState() const
{
	return CachedMoveInput.GetClampedToMaxSize(1.0f);
}

FVector2D UProject_JLocomotionAnimStateComponent::GetRemoteMovementInputForState() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return FVector2D::ZeroVector;
	}

	FVector HorizontalVelocity = PlayerOwner->GetVelocity();
	HorizontalVelocity.Z = 0.0f;
	if (HorizontalVelocity.SizeSquared() <= FMath::Square(RemoteMoveSpeedThreshold))
	{
		return FVector2D::ZeroVector;
	}

	const FVector LocalVelocity = PlayerOwner->GetActorTransform().InverseTransformVectorNoScale(HorizontalVelocity.GetSafeNormal());
	return FVector2D(LocalVelocity.Y, LocalVelocity.X).GetClampedToMaxSize(1.0f);
}

FProject_JLocomotionRuntimeSnapshot UProject_JLocomotionAnimStateComponent::BuildMovementSnapshot(const AProject_JPlayerCharacter& PlayerOwner) const
{
	FProject_JLocomotionRuntimeSnapshot Snapshot;
	Snapshot.Velocity = PlayerOwner.GetVelocity();
	Snapshot.HorizontalVelocity = FVector(Snapshot.Velocity.X, Snapshot.Velocity.Y, 0.0f);
	Snapshot.VerticalSpeed = Snapshot.Velocity.Z;
	Snapshot.GroundSpeed = Snapshot.HorizontalVelocity.Size();
	Snapshot.bWantsSprint = IsSprintRequestedForAnimation();
	Snapshot.bHasSprintMovementIntent = CachedMoveInput.Size() > MoveInputDeadZone || Snapshot.GroundSpeed > IdleSpeedThreshold;
	return Snapshot;
}

void UProject_JLocomotionAnimStateComponent::ApplyMovementSnapshot(float DeltaTime, const FProject_JLocomotionRuntimeSnapshot& Snapshot)
{
	VerticalSpeed = Snapshot.VerticalSpeed;
	GroundSpeed = Snapshot.GroundSpeed;
	bWantsSprint = Snapshot.bWantsSprint;
	bUseSprintLocomotion =
		GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		bWantsSprint &&
		Snapshot.bHasSprintMovementIntent;
	GroundMotionModeElapsedTime += DeltaTime;
	if (bIsJumping)
	{
		JumpStartElapsedTime += DeltaTime;
	}

	if (bIsLanding)
	{
		LandingElapsedTime += DeltaTime;
		bCanExitLanding = LandingElapsedTime >= LandingMinHoldTime;
	}
}

void UProject_JLocomotionAnimStateComponent::UpdateMovementRequestState(float DeltaTime)
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		ClearMovementRequests();
		return;
	}

	const FVector2D MoveInput = GetLocalMovementInputForState();
	RefreshMovementInputState(DeltaTime, MoveInput, true);
	if (TryFinishLandingFromMovementInput(MoveInput, true))
	{
		return;
	}

	UpdateGroundMotionModeFromInput(DeltaTime, MoveInput, true);
}

void UProject_JLocomotionAnimStateComponent::UpdateRemoteMovementRequestState(float DeltaTime)
{
	const FVector2D MoveInput = GetRemoteMovementInputForState();
	const bool bSuppressStartFromResidualVelocity = ConsumeRemoteStopStartSuppress(DeltaTime);

	RefreshMovementInputState(DeltaTime, MoveInput, true);
	if (bSuppressStartFromResidualVelocity)
	{
		ApplyRemoteStopStartSuppress();
	}

	// CharacterMovement velocity on a simulated proxy decays several replicated
	// frames after the owner releases input. A confirmed MoveStop is more
	// authoritative for visual intent than that residual velocity; otherwise a
	// remote Stop enters for one frame and is immediately overwritten by Start.
	const FVector2D VisualMoveInput = bRemoteStopVisualIntentActive
		? FVector2D::ZeroVector
		: MoveInput;
	if (bRemoteStopVisualIntentActive)
	{
		bHasMoveInput = false;
		MoveInputSize = 0.0f;
		MoveInputHeldTime = 0.0f;
		MoveInputTurnAngle = 0.0f;
	}

	UpdateGroundMotionModeFromInput(DeltaTime, VisualMoveInput, false);
}

bool UProject_JLocomotionAnimStateComponent::ConsumeRemoteStopStartSuppress(float DeltaTime)
{
	const bool bSuppressStartFromResidualVelocity = RemoteStopStartSuppressTimeRemaining > 0.0f;
	RemoteStopStartSuppressTimeRemaining = FMath::Max(0.0f, RemoteStopStartSuppressTimeRemaining - DeltaTime);
	return bSuppressStartFromResidualVelocity;
}

void UProject_JLocomotionAnimStateComponent::ApplyRemoteStopStartSuppress()
{
	bPendingStartRequest = false;
	ClearResolvedMoveInputState();
}

void UProject_JLocomotionAnimStateComponent::RefreshMovementInputState(float DeltaTime, const FVector2D& MoveInput, bool bTrackTurnAngle)
{
	bPrevHasMoveInput = bResolvedMoveInputLastUpdate;
	MoveInputSize = MoveInput.Size();
	bHasMoveInput = MoveInputSize > MoveInputDeadZone;
	MoveInputHeldTime = bHasMoveInput ? MoveInputHeldTime + DeltaTime : 0.0f;
	MoveInputTurnAngle = 0.0f;
	if (bHasMoveInput)
	{
		// This is deliberately independent from the Combat-only MovementDirection.
		// State Controller Start cancellation needs the same heading in OTM and
		// Strafe, including intermediate W -> WA -> A keyboard chords.
		MoveInputDirection = FMath::RadiansToDegrees(FMath::Atan2(MoveInput.X, MoveInput.Y));
	}
	bHasPivotInputReleaseDirections = false;
	bSharpTurnRequested = false;
	bLastMoveInputTurnUsedReleaseBridge = false;

	// A moving landing may hand off into Stop only when movement intent survived
	// touchdown for a short, real interval. This rejects the common case where
	// input was released in the air or on the landing frame.
	if (bIsLanding && bHasMoveInput)
	{
		LandingPostTouchdownMoveInputTime += DeltaTime;
		bLandingReceivedPostTouchdownMoveInput =
			LandingPostTouchdownMoveInputTime >= LandingExitStopInputHoldTime;
	}

	const bool bHasPreviousContinuousInput =
		bPrevHasMoveInput && PreviousMoveInputForTurn.Size() > MoveInputDeadZone;
	const bool bReleaseSequenceStillValid =
		bPivotInputReleaseSequenceActive &&
		PivotInputReleaseElapsedTime <= DerivedPivotInputReleaseBridgeTime;

	if (bTrackTurnAngle && bHasMoveInput && bReleaseSequenceStillValid)
	{
		const FVector2D NormalizedMoveInput = MoveInput.GetClampedToMaxSize(1.0f);
		const bool bCandidateChanged =
			PivotInputCandidate.Size() <= MoveInputDeadZone ||
			!PivotInputCandidate.Equals(NormalizedMoveInput, 0.01f);
		if (bCandidateChanged)
		{
			// Do not let a remaining W/A/S/D key replace the outgoing direction.
			// It is only a candidate until the complete chord has settled.
			PivotInputCandidate = NormalizedMoveInput;
			const FVector2D PreviousDirection = PivotInputReleaseReference.GetSafeNormal();
			const FVector2D CurrentDirection = PivotInputCandidate.GetSafeNormal();
			const float Dot = FMath::Clamp(FVector2D::DotProduct(PreviousDirection, CurrentDirection), -1.0f, 1.0f);
			const float Cross = PreviousDirection.Y * CurrentDirection.X - PreviousDirection.X * CurrentDirection.Y;
			const float CandidateTurnAngle = FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
			const bool bAcceptsReleaseBridge =
				FMath::Abs(CandidateTurnAngle) >= DerivedPivotInputReleaseMinimumTurnAngle;
			if (Project_J::MotionMatchingCVars::ShouldCapturePivotDebugTrace())
			{
				UE_LOG(LogProjectJPlayer, Display,
					TEXT("PivotDiag InputCandidate Verdict=%s From=(%.2f,%.2f) Candidate=(%.2f,%.2f) Turn=%.1f ReleaseAge=%.3f MinBridgeTurn=%.1f"),
					bAcceptsReleaseBridge ? TEXT("Accepted") : TEXT("PartialChord"),
					PivotInputReleaseReference.X,
					PivotInputReleaseReference.Y,
					PivotInputCandidate.X,
					PivotInputCandidate.Y,
					CandidateTurnAngle,
					PivotInputReleaseElapsedTime,
					DerivedPivotInputReleaseMinimumTurnAngle);
			}

			if (bAcceptsReleaseBridge)
			{
				MoveInputTurnAngle = CandidateTurnAngle;
				PivotInputReleaseFromMovementDirection = FMath::RadiansToDegrees(
					FMath::Atan2(PivotInputReleaseReference.X, PivotInputReleaseReference.Y));
				PivotInputReleaseToMovementDirection = FMath::RadiansToDegrees(
					FMath::Atan2(PivotInputCandidate.X, PivotInputCandidate.Y));
				bHasPivotInputReleaseDirections = true;
				bLastMoveInputTurnUsedReleaseBridge = true;
				bPivotInputReleaseSequenceActive = false;
			}
		}
	}
	else if (bTrackTurnAngle && bHasMoveInput && bHasPreviousContinuousInput)
	{
		const FVector2D PreviousDirection = (bHasPreviousContinuousInput
			? PreviousMoveInputForTurn
			: PivotInputReleaseReference).GetSafeNormal();
		const FVector2D CurrentDirection = MoveInput.GetSafeNormal();
		const float Dot = FMath::Clamp(FVector2D::DotProduct(PreviousDirection, CurrentDirection), -1.0f, 1.0f);
		const float Cross = PreviousDirection.Y * CurrentDirection.X - PreviousDirection.X * CurrentDirection.Y;
		MoveInputTurnAngle = FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
	}

	if (bHasMoveInput && !bPivotInputReleaseSequenceActive)
	{
		PivotInputReleaseReference = MoveInput.GetClampedToMaxSize(1.0f);
		PivotInputReleaseElapsedTime = 0.0f;
	}
	if (bPivotInputReleaseSequenceActive)
	{
		PivotInputReleaseElapsedTime = FMath::Min(
			PivotInputReleaseElapsedTime + DeltaTime,
			DerivedPivotInputReleaseBridgeTime + 1.0f);
		if (PivotInputReleaseElapsedTime > DerivedPivotInputReleaseBridgeTime)
		{
			bPivotInputReleaseSequenceActive = false;
			PivotInputCandidate = FVector2D::ZeroVector;
		}
	}
}

bool UProject_JLocomotionAnimStateComponent::TryFinishLandingFromMovementInput(const FVector2D& MoveInput, bool bAllowSprintTurnCancel)
{
	if (TryFinishLandingFromInputChange())
	{
		return true;
	}

	if (TryFinishLandingRedirectCancel(MoveInput))
	{
		return true;
	}

	return bAllowSprintTurnCancel && TryFinishSprintLandingTurnCancel(MoveInput);
}

bool UProject_JLocomotionAnimStateComponent::TryFinishLandingFromInputChange()
{
	if (!IsLandingStateActive())
	{
		return false;
	}

	if (ShouldUseLocalInputState() && LandingElapsedTime < LandingInputCancelGraceTime)
	{
		return false;
	}

	if (!bLandWasMoving && bHasMoveInput)
	{
		bLandWasMoving = true;
		DispatchLandingCancelForAnimation();
		FinishLandingImmediately();
		return true;
	}

	if (bLandWasMoving &&
		!bHasMoveInput &&
		bLandingReceivedPostTouchdownMoveInput)
	{
		// Input release after a genuine moving touchdown is a directional stop,
		// not an abrupt transition to Idle. The current Strafe sector remains
		// cached by the AnimInstance while velocity falls to zero.
		bForceLandingFinishToStop = true;
		bLandingExitStopWasSprinting = bLandWasSprinting;
		DispatchLandingCancelForAnimation();
		FinishLandingImmediately();
		return true;
	}

	// Releasing movement input is not a redirect.  Keep the selected landing
	// one-shot alive until its normal completion so a brief post-landing input
	// cannot collapse a moving land directly into Idle.  This also preserves the
	// landing gait/family selected at impact; only a new input or a meaningful
	// redirect is allowed to cancel the landing responsively.

	return false;
}

bool UProject_JLocomotionAnimStateComponent::TryFinishLandingRedirectCancel(const FVector2D& MoveInput)
{
	if (!IsLandingStateActive() ||
		!bLandWasMoving ||
		!bHasMoveInput ||
		LandingElapsedTime < LandingRedirectCancelMinTime)
	{
		return false;
	}

	if (HasLandingDirectionTurnCancel(MoveInput, LandingRedirectCancelAngle) ||
		HasLandingActorTurnCancel(LandingRedirectCancelAngle))
	{
		DispatchLandingCancelForAnimation();
		FinishLandingImmediately();
		return true;
	}

	return false;
}

bool UProject_JLocomotionAnimStateComponent::TryFinishSprintLandingTurnCancel(const FVector2D& MoveInput)
{
	if (!IsLandingStateActive() ||
		!bLandWasSprinting ||
		!bWantsSprint ||
		!bHasMoveInput ||
		LandingElapsedTime < SprintLandingTurnCancelMinTime)
	{
		return false;
	}

	if (HasLandingDirectionTurnCancel(MoveInput, SprintLandingTurnCancelAngle) ||
		HasLandingActorTurnCancel(SprintLandingTurnCancelAngle))
	{
		DispatchLandingCancelForAnimation();
		FinishLandingImmediately();
		return true;
	}

	return false;
}

bool UProject_JLocomotionAnimStateComponent::HasLandingDirectionTurnCancel(const FVector2D& MoveInput, float AngleThreshold)
{
	const FVector CurrentLandingMoveWorldDirection = CalculateMoveWorldDirection(MoveInput);
	const FVector ReferenceLandingMoveWorldDirection = !InitialLandingMoveWorldDirection.IsNearlyZero()
		? InitialLandingMoveWorldDirection
		: PreviousLandingMoveWorldDirection;

	if (!ReferenceLandingMoveWorldDirection.IsNearlyZero() && !CurrentLandingMoveWorldDirection.IsNearlyZero())
	{
		const float DirectionDot = FMath::Clamp(FVector::DotProduct(ReferenceLandingMoveWorldDirection, CurrentLandingMoveWorldDirection), -1.0f, 1.0f);
		const float DirectionAngle = FMath::RadiansToDegrees(FMath::Acos(DirectionDot));
		if (DirectionAngle >= AngleThreshold)
		{
			return true;
		}
	}

	PreviousLandingMoveWorldDirection = CurrentLandingMoveWorldDirection;
	return false;
}

bool UProject_JLocomotionAnimStateComponent::HasLandingActorTurnCancel(float AngleThreshold)
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return false;
	}

	const float CurrentActorYaw = PlayerOwner->GetActorRotation().Yaw;
	const float InitialYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(InitialLandingActorYaw, CurrentActorYaw));
	const float PreviousYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousLandingActorYaw, CurrentActorYaw));
	if (FMath::Max(InitialYawDelta, PreviousYawDelta) >= AngleThreshold)
	{
		return true;
	}

	PreviousLandingActorYaw = CurrentActorYaw;
	return false;
}
