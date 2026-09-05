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

namespace
{
	// Combat-Strafe Run Pivot has authored cardinal pairs only. A generous axis
	// tolerance keeps an analog stick near a cardinal direction eligible, while
	// rejecting the normalized keyboard diagonals (~0.707/0.707).
	bool IsCardinalPivotIntent(const FVector2D& Intent)
	{
		const FVector2D Direction = Intent.GetSafeNormal();
		const float AbsX = FMath::Abs(Direction.X);
		const float AbsY = FMath::Abs(Direction.Y);
		return (AbsX <= 0.25f && AbsY >= 0.75f) ||
			(AbsY <= 0.25f && AbsX >= 0.75f);
	}

	float GetTurnInPlaceBucketYaw(const uint8 DirectionBucket)
	{
		switch (DirectionBucket)
		{
		case 1: return -90.0f;
		case 2: return -180.0f;
		case 3: return 90.0f;
		case 4: return 180.0f;
		default: return 0.0f;
		}
	}

	uint8 GetTurnInPlaceBucketForDelta(const float DeltaYaw, const float MinimumTurnAngle)
	{
		if (DeltaYaw <= -MinimumTurnAngle && DeltaYaw >= -135.0f)
		{
			return 1; // Left 090
		}
		if (DeltaYaw < -135.0f)
		{
			return 2; // Left 180
		}
		if (DeltaYaw >= MinimumTurnAngle && DeltaYaw < 135.0f)
		{
			return 3; // Right 090
		}
		if (DeltaYaw >= 135.0f)
		{
			return 4; // Right 180
		}
		return 0;
	}
}

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
	RemoteTurnInPlaceTimeRemaining = FMath::Max(0.0f, RemoteTurnInPlaceTimeRemaining - DeltaTime);
	if (RemoteTurnInPlaceTimeRemaining <= 0.0f)
	{
		bRemoteTurnInPlaceActive = false;
		RemoteTurnInPlaceDirectionBucket = 0;
		RemoteTurnInPlaceTargetFacingYaw = 0.0f;
	}

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
	const UWorld* World = PlayerOwner->GetWorld();
	const double LocalNowSeconds = World ? World->GetTimeSeconds() : 0.0;
	if (bUsingLocalInputState && bLocalTurnInPlaceTargetActive)
	{
		// Keep camera intent unwrapped while TIP is active. A one-shot
		// FindDeltaAngleDegrees(ActorYaw, ControlYaw) folds at +/-180 degrees and
		// can mistake a fast continuing left turn for a right-turn reversal.
		float LiveFacingDelta = KinematicContext.DesiredFacingDeltaYaw;
		if (AController* Controller = PlayerOwner->GetController())
		{
			const float CurrentControlYaw = Controller->GetControlRotation().Yaw;
			const float CurrentActorYaw = PlayerOwner->GetActorRotation().Yaw;
			if (bHasLocalTurnInPlaceYawSamples)
			{
				const float ControlYawStep = FMath::FindDeltaAngleDegrees(
					LastLocalTurnInPlaceControlYaw,
					CurrentControlYaw);
				const float ActorYawStep = FMath::FindDeltaAngleDegrees(
					LastLocalTurnInPlaceActorYaw,
					CurrentActorYaw);
				LocalTurnInPlaceUnwrappedFacingDeltaYaw += ControlYawStep - ActorYawStep;
			}
			else
			{
				LocalTurnInPlaceUnwrappedFacingDeltaYaw = LiveFacingDelta;
				bHasLocalTurnInPlaceYawSamples = true;
			}
			LastLocalTurnInPlaceControlYaw = CurrentControlYaw;
			LastLocalTurnInPlaceActorYaw = CurrentActorYaw;
			LiveFacingDelta = LocalTurnInPlaceUnwrappedFacingDeltaYaw;
		}
		const float RemainingTargetDelta = FMath::FindDeltaAngleDegrees(
			PlayerOwner->GetActorRotation().Yaw,
			LocalTurnInPlaceTargetFacingYaw);
		const bool bLocomotionInterrupt = KinematicContext.bHasMoveInput ||
			KinematicContext.GroundSpeed > IdleSpeedThreshold ||
			bIsInAir ||
			IsLandingStateActive();
		const bool bCameraReversedPastTurnThreshold =
			FMath::Abs(LiveFacingDelta) >= DerivedTurnInPlaceAngleThreshold &&
			!FMath::IsNearlyZero(RemainingTargetDelta) &&
			FMath::Sign(LiveFacingDelta) != FMath::Sign(RemainingTargetDelta);
		const bool bCameraExtendsCurrentTurn =
			!FMath::IsNearlyZero(LiveFacingDelta) &&
			!FMath::IsNearlyZero(RemainingTargetDelta) &&
			FMath::Sign(LiveFacingDelta) == FMath::Sign(RemainingTargetDelta) &&
			FMath::Abs(LiveFacingDelta) >= FMath::Abs(RemainingTargetDelta) + 45.0f;
		const bool bCanExtendCurrentTurn = bCameraExtendsCurrentTurn &&
			(LocalNowSeconds - LocalTurnInPlaceTargetStartedAtSeconds) >= 0.75;
		if (bLocomotionInterrupt || bCameraReversedPastTurnThreshold)
		{
			bLocalTurnInPlaceTargetActive = false;
			LocalTurnInPlaceDirectionBucket = 0;
			LocalTurnInPlaceTargetFacingYaw = 0.0f;
			bHasLocalTurnInPlaceYawSamples = false;
			if (bCameraReversedPastTurnThreshold)
			{
				// Let the current Blend Stack and Offset Root Bone release their
				// right/left rotation before selecting the opposite authored asset.
				LocalTurnInPlaceTargetSelectionBlockedUntilSeconds =
					LocalNowSeconds + LocalTurnInPlaceReversalReleaseDuration;
			}
		}
		else if (bCanExtendCurrentTurn)
		{
			// The player has kept turning beyond this clip's fixed target. Start
			// one new authored turn after the re-entry window instead of letting
			// steering drag the current 90/180 asset toward a live camera yaw.
			LocalTurnInPlaceTargetFacingYaw = FRotator::NormalizeAxis(
				PlayerOwner->GetActorRotation().Yaw + GetTurnInPlaceBucketYaw(LocalTurnInPlaceDirectionBucket));
			LocalTurnInPlaceTargetStartedAtSeconds = LocalNowSeconds;
			++LocalTurnInPlaceSequence;
			if (LocalTurnInPlaceSequence <= 0)
			{
				LocalTurnInPlaceSequence = 1;
			}
			bTurnInPlaceReplicationRequestPending = true;
			PendingTurnInPlaceBucket = LocalTurnInPlaceDirectionBucket;
			KinematicContext.DesiredFacingDeltaYaw = FMath::FindDeltaAngleDegrees(
				PlayerOwner->GetActorRotation().Yaw,
				LocalTurnInPlaceTargetFacingYaw);
			KinematicContext.DesiredFacingYaw = LocalTurnInPlaceTargetFacingYaw;
		}
		else if (FMath::Abs(RemainingTargetDelta) <= 5.0f)
		{
			bLocalTurnInPlaceTargetActive = false;
			LocalTurnInPlaceDirectionBucket = 0;
			LocalTurnInPlaceTargetFacingYaw = 0.0f;
			bHasLocalTurnInPlaceYawSamples = false;
		}
		else
		{
			KinematicContext.DesiredFacingDeltaYaw = RemainingTargetDelta;
			KinematicContext.DesiredFacingYaw = LocalTurnInPlaceTargetFacingYaw;
		}
	}
	if (!bUsingLocalInputState && bRemoteTurnInPlaceActive)
	{
		// Remote proxies face the replicated cosmetic target while TIP presentation is active.
		// Abort immediately if the remote character starts moving or becomes airborne.
		if (KinematicContext.bHasMoveInput || KinematicContext.GroundSpeed > IdleSpeedThreshold || bIsInAir || IsLandingStateActive())
		{
			bRemoteTurnInPlaceActive = false;
			RemoteTurnInPlaceTimeRemaining = 0.0f;
			RemoteTurnInPlaceDirectionBucket = 0;
		}
		else
		{
			const float RemainingFacingDelta = FMath::FindDeltaAngleDegrees(
				PlayerOwner->GetActorRotation().Yaw,
				RemoteTurnInPlaceTargetFacingYaw);
			if (FMath::Abs(RemainingFacingDelta) <= 5.0f)
			{
				bRemoteTurnInPlaceActive = false;
				RemoteTurnInPlaceTimeRemaining = 0.0f;
				RemoteTurnInPlaceDirectionBucket = 0;
				KinematicContext.DesiredFacingDeltaYaw = 0.0f;
				KinematicContext.DesiredFacingYaw = PlayerOwner->GetActorRotation().Yaw;
			}
			else
			{
				KinematicContext.DesiredFacingDeltaYaw = RemainingFacingDelta;
				KinematicContext.DesiredFacingYaw = RemoteTurnInPlaceTargetFacingYaw;
			}
		}
	}
	FProject_JDerivedLocomotionContext NewDerivedContext = BuildDerivedLocomotionContext(AuthoritativeContext, KinematicContext);
	const bool bLocalTurnInPlaceSelectionBlocked = bUsingLocalInputState &&
		!bLocalTurnInPlaceTargetActive &&
		LocalNowSeconds < LocalTurnInPlaceTargetSelectionBlockedUntilSeconds;
	if (bUsingLocalInputState && !bLocalTurnInPlaceTargetActive && !bLocalTurnInPlaceSelectionBlocked && NewDerivedContext.bShouldTurnInPlace)
	{
		const uint8 CandidateBucket = GetTurnInPlaceBucketForDelta(
			KinematicContext.DesiredFacingDeltaYaw,
			DerivedTurnInPlaceAngleThreshold);
		if (CandidateBucket != 0)
		{
			bLocalTurnInPlaceTargetActive = true;
			LocalTurnInPlaceDirectionBucket = CandidateBucket;
			LocalTurnInPlaceTargetFacingYaw = FRotator::NormalizeAxis(
				PlayerOwner->GetActorRotation().Yaw + GetTurnInPlaceBucketYaw(CandidateBucket));
			LocalTurnInPlaceTargetStartedAtSeconds = LocalNowSeconds;
			bHasLocalTurnInPlaceYawSamples = false;
			LocalTurnInPlaceUnwrappedFacingDeltaYaw = KinematicContext.DesiredFacingDeltaYaw;
			if (AController* Controller = PlayerOwner->GetController())
			{
				LastLocalTurnInPlaceControlYaw = Controller->GetControlRotation().Yaw;
				LastLocalTurnInPlaceActorYaw = PlayerOwner->GetActorRotation().Yaw;
				bHasLocalTurnInPlaceYawSamples = true;
			}
			++LocalTurnInPlaceSequence;
			if (LocalTurnInPlaceSequence <= 0)
			{
				LocalTurnInPlaceSequence = 1;
			}
			KinematicContext.DesiredFacingYaw = LocalTurnInPlaceTargetFacingYaw;
			KinematicContext.DesiredFacingDeltaYaw = FMath::FindDeltaAngleDegrees(
				PlayerOwner->GetActorRotation().Yaw,
				LocalTurnInPlaceTargetFacingYaw);
			NewDerivedContext = BuildDerivedLocomotionContext(AuthoritativeContext, KinematicContext);
		}
	}

	ApplyLocomotionPhaseStability(DeltaTime, NewDerivedContext);
	if (bLocalTurnInPlaceSelectionBlocked)
	{
		// Keep the active one-shot free to blend/release, but do not let the
		// state selector replace it with an opposite TIP on the same update.
		NewDerivedContext.bShouldTurnInPlace = false;
		NewDerivedContext.TurnInPlaceDirectionBucket = 0;
		NewDerivedContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Idle;
	}
	if (bUsingLocalInputState && bLocalTurnInPlaceTargetActive)
	{
		// Keep the direct Blend Stack path until the target or an interrupt ends it.
		NewDerivedContext.bShouldTurnInPlace = true;
		NewDerivedContext.TurnInPlaceDirectionBucket = LocalTurnInPlaceDirectionBucket;
		NewDerivedContext.PhaseFamily = EProject_JLocomotionPhaseFamily::TurnInPlace;
	}

	const uint8 CurrentLocalTurnBucket = bLocalTurnInPlaceTargetActive
		? LocalTurnInPlaceDirectionBucket
		: GetTurnInPlaceBucketForDelta(KinematicContext.DesiredFacingDeltaYaw, DerivedTurnInPlaceAngleThreshold);
	const bool bLocalTurnInPlace = bUsingLocalInputState && bLocalTurnInPlaceTargetActive;
	if (bLocalTurnInPlace && (!bWasLocallyRequestingTurnInPlace || (CurrentLocalTurnBucket != 0 && CurrentLocalTurnBucket != LastLocalTurnInPlaceBucket)))
	{
		bTurnInPlaceReplicationRequestPending = true;
		LastLocalTurnInPlaceBucket = CurrentLocalTurnBucket;
	}
	else if (!bLocalTurnInPlace)
	{
		LastLocalTurnInPlaceBucket = 0;
	}
	bWasLocallyRequestingTurnInPlace = bLocalTurnInPlace;
	DerivedLocomotionContext = NewDerivedContext;
	UpdateMotionMatchingSelectionState(*PlayerOwner);
}

void UProject_JLocomotionAnimStateComponent::UpdateMotionMatchingSelectionState(const AProject_JPlayerCharacter& PlayerOwner)
{
	MotionMatchingSelectionContext.GaitIntent = AuthoritativeContext.GaitIntent;
	MotionMatchingSelectionContext.RotationMode = AuthoritativeContext.RotationMode;
	MotionMatchingSelectionContext.PhaseFamily = DerivedLocomotionContext.PhaseFamily;
	MotionMatchingSelectionContext.bUseGenericFamiliesForNonOrientToMovement = false;

	const bool bSelectionChanged =
		!bHasPublishedMotionMatchingSelection ||
		LastPublishedMotionMatchingGait != AuthoritativeContext.GaitIntent ||
		LastPublishedMotionMatchingRotationMode != AuthoritativeContext.RotationMode ||
		LastPublishedMotionMatchingPhase != DerivedLocomotionContext.PhaseFamily ||
		LastPublishedGroundMotionMode != GroundMotionMode;

	bMotionMatchingSelectionChanged = bSelectionChanged;
	if (bSelectionChanged)
	{
		LastPublishedMotionMatchingGait = AuthoritativeContext.GaitIntent;
		LastPublishedMotionMatchingRotationMode = AuthoritativeContext.RotationMode;
		LastPublishedMotionMatchingPhase = DerivedLocomotionContext.PhaseFamily;
		LastPublishedGroundMotionMode = GroundMotionMode;
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
		TEXT("MMNetState Actor=%s Role=%s LocalInput=%s Rendered=%s Rev=%d Changed=%s ForceReselect=%s Gait=%d Rotation=%d Phase=%d GroundMode=%d Speed=%.1f InputTurn=%.1f VelocityToInput=%.1f StopDist=%.1f RelativeAccel=(%.2f,%.2f)"),
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
		GroundSpeed, MoveInputTurnAngle,
		KinematicContext.VelocityToMoveInputAngle,
		KinematicContext.PredictedStopDistance,
		KinematicContext.RelativeAccelerationAmount.X,
		KinematicContext.RelativeAccelerationAmount.Y);
	UE_LOG(LogProjectJPlayer, Display,
		TEXT("MMNetTrajectory Actor=%s Samples=%d History=%d Prediction=%d Eligible=%s Age=%.3f GenRev=%d ResetRev=%d ResetReason=%s RemoteFacingRepair=%s RemotePosSmoothing=%s RemoteRotSmoothing=%s"),
		*GetNameSafe(&PlayerOwner),
		Trajectory ? Trajectory->Samples.Num() : 0,
		TrajectoryComponent ? TrajectoryComponent->GetSamplingData().NumHistorySamples : 0,
		TrajectoryComponent ? TrajectoryComponent->GetSamplingData().NumPredictionSamples : 0,
		TrajectoryComponent && TrajectoryComponent->IsTrajectoryGenerationEligible() ? TEXT("true") : TEXT("false"),
		TrajectoryComponent ? TrajectoryComponent->GetTrajectoryAgeSeconds() : -1.0f,
		TrajectoryComponent ? TrajectoryComponent->GetGenerationRevision() : 0,
		TrajectoryComponent ? TrajectoryComponent->GetResetRevision() : 0,
		TrajectoryComponent ? *UEnum::GetValueAsString(TrajectoryComponent->GetLastResetReason()) : TEXT("None"),
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

	// Pose Search already consumes the full trajectory. Reuse the trajectory
	// component's cached sampling indices for the same short-horizon velocity
	// needed by C++ state decisions. This stays on the game thread.
	if (const UProject_JMotionMatchingTrajectoryComponent* TrajectoryComponent = PlayerOwner.GetMotionMatchingTrajectoryComponent())
	{
		float FutureTurnAngle = 0.0f;
		if (TrajectoryComponent->TryGetFuturePlanarVelocity(
			DerivedMovementPredictionTime,
			Context.HorizontalVelocity,
			Context.FutureTrajectoryVelocity,
			FutureTurnAngle))
		{
			Context.FutureTrajectorySpeed = Context.FutureTrajectoryVelocity.Size2D();
			Context.bHasFutureTrajectoryVelocity = true;
			Context.FutureTrajectoryTurnAngle =
				Context.GroundSpeed > DerivedMovingSpeedThreshold &&
				Context.FutureTrajectorySpeed > DerivedMovingSpeedThreshold
					? FutureTurnAngle
					: 0.0f;
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
	else if (ShouldUseLocalInputState())
	{
		Context.DesiredFacingYaw = PlayerOwner.GetControlRotation().Yaw;
	}
	else if (bRemoteTurnInPlaceActive)
	{
		Context.DesiredFacingYaw = RemoteTurnInPlaceTargetFacingYaw;
	}
	else
	{
		Context.DesiredFacingYaw = PlayerOwner.GetActorRotation().Yaw;
	}
	Context.DesiredFacingDeltaYaw = FMath::FindDeltaAngleDegrees(
		PlayerOwner.GetActorRotation().Yaw,
		Context.DesiredFacingYaw);

	return Context;
}

FProject_JDerivedLocomotionContext UProject_JLocomotionAnimStateComponent::BuildDerivedLocomotionContext(
	const FProject_JLocomotionAuthoritativeContext& AuthContext,
	const FProject_JLocomotionKinematicContext& InKinematicContext)
{
	FProject_JDerivedLocomotionContext Context;
	Context.bIsMoving = IsMovingForContext(InKinematicContext);
	Context.bIsMotionMatchingMoving = IsMotionMatchingMovingForContext(InKinematicContext);
	Context.bIsPivoting = IsPivotingForContext(AuthContext, InKinematicContext);
	// Pivot and Start share the same locomotion edge. A committed Pivot owns it.
	Context.bIsStarting = !Context.bIsPivoting && IsStartingForContext(AuthContext, InKinematicContext);
	Context.MoveIntentRevision = MoveIntentRevision;
	Context.PivotRequestRevision = PivotRequestRevision;
	Context.PivotPreviousMovementDirection = LatchedPivotPreviousMovementDirection;
	Context.PivotMoveIntentDirection = LatchedPivotMoveIntentDirection;
	Context.bShouldTurnInPlace = ShouldTurnInPlaceForContext(AuthContext, InKinematicContext);
	Context.TurnInPlaceDirectionBucket = bUsingLocalInputState && bLocalTurnInPlaceTargetActive
		? LocalTurnInPlaceDirectionBucket
		: (!bUsingLocalInputState && bRemoteTurnInPlaceActive
			? RemoteTurnInPlaceDirectionBucket
			: 0);
	Context.TurnInPlaceSequence = bUsingLocalInputState
		? LocalTurnInPlaceSequence
		: RemoteTurnInPlaceSequence;
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

	const bool bKeepMovingRedirect =
		(PreviousDerivedPhaseFamily == EProject_JLocomotionPhaseFamily::Turn ||
			PreviousDerivedPhaseFamily == EProject_JLocomotionPhaseFamily::Pivot) &&
		InOutContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Cycle &&
		DerivedPhaseFamilyElapsedTime < DerivedTurnMinHoldTime &&
		KinematicContext.bHasMoveInput &&
		KinematicContext.GroundSpeed > StopIntentSpeedThreshold &&
		!bIsInAir &&
		!IsLandingStateActive();

	if (bKeepMovingRedirect)
	{
		InOutContext.PhaseFamily = PreviousDerivedPhaseFamily;
		DerivedPhaseFamilyElapsedTime += DeltaTime;
		return;
	}

	const bool bFacingDeltaSufficient = bUsingLocalInputState
		? FMath::Abs(KinematicContext.DesiredFacingDeltaYaw) >= DerivedTurnInPlaceAngleThreshold
		: (bRemoteTurnInPlaceActive && FMath::Abs(KinematicContext.DesiredFacingDeltaYaw) >= DerivedTurnInPlaceAngleThreshold);

	const bool bKeepTurnInPlace =
		PreviousDerivedPhaseFamily == EProject_JLocomotionPhaseFamily::TurnInPlace &&
		(GroundMotionMode == EProject_JGroundMotionMode::Idle ||
			GroundMotionMode == EProject_JGroundMotionMode::Stop) &&
		!KinematicContext.bHasMoveInput &&
		KinematicContext.GroundSpeed <= IdleSpeedThreshold &&
		bFacingDeltaSufficient &&
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
	if (GroundMotionMode == EProject_JGroundMotionMode::Stop)
	{
		return EProject_JLocomotionPhaseFamily::Stop;
	}
	if (Context.bShouldTurnInPlace)
	{
		return EProject_JLocomotionPhaseFamily::TurnInPlace;
	}
	if (Context.bIsPivoting)
	{
		return EProject_JLocomotionPhaseFamily::Pivot;
	}
	if (Context.bIsStarting)
	{
		return EProject_JLocomotionPhaseFamily::Start;
	}
	const bool bHasMovingTurnIntent =
		Context.bIsMoving &&
		KinematicContext.bHasMoveInput &&
		KinematicContext.GroundSpeed >= DerivedTurnMinSpeed;
	// Mouse camera rotation does not change MoveInputTurnAngle, so it remains in
	// Cycle where Arc/Box/Diamond can be selected as continuous movement variants.
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

	// Start is evaluated every locomotion update. Only emit a trace for an
	// accepted Start edge; candidates/rejections are otherwise visible through
	// the State Controller chooser trace without flooding the log.
	if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace() && bResult)
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
	const FProject_JLocomotionKinematicContext& InKinematicContext)
{
	const UProject_JCombatAnimProfile* CombatProfile = GetPlayerOwner()
		? GetPlayerOwner()->GetCombatAnimProfile()
		: nullptr;
	const bool bUsingSemanticPivotKinematicCapture =
		!bSemanticMoveIntentUpdatePending &&
		bHasSemanticPivotKinematicCapture &&
		SemanticPivotKinematicCaptureIntentRevision == MoveIntentRevision;
	const auto ConsumeSemanticPivotKinematicCapture = [this, bUsingSemanticPivotKinematicCapture]()
	{
		if (bUsingSemanticPivotKinematicCapture)
		{
			bHasSemanticPivotKinematicCapture = false;
			SemanticPivotKinematicCaptureIntentRevision = INDEX_NONE;
		}
	};
	const FVector PreviousDirection = bUsingSemanticPivotKinematicCapture
		? SemanticPivotKinematicCapturePreviousDirection
		: InKinematicContext.HorizontalVelocity.GetSafeNormal2D();
	const float PivotGroundSpeed = bUsingSemanticPivotKinematicCapture
		? SemanticPivotKinematicCaptureGroundSpeed
		: InKinematicContext.GroundSpeed;
	// Semantic input is only an owning-client presentation source. It preserves
	// which device-independent directions are held while IA_Move remains the
	// authoritative gameplay input. Never inspect an intermediate chord.
	const FVector IntentDirection = bSemanticMoveIntentUpdatePending
		? FVector::ZeroVector
		: (bHasSemanticMoveIntentInput
			? CalculateMoveWorldDirection(CachedSemanticMoveIntentInput).GetSafeNormal2D()
			: InKinematicContext.MoveWorldDirection.GetSafeNormal2D());
	const float PhysicalReversalAngle = !PreviousDirection.IsNearlyZero() && !IntentDirection.IsNearlyZero()
		? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(PreviousDirection, IntentDirection), -1.0f, 1.0f)))
		: -1.0f;
	const auto LogRejectedPivot = [this, &AuthContext, &InKinematicContext, CombatProfile, PreviousDirection, IntentDirection, PhysicalReversalAngle, PivotGroundSpeed, bUsingSemanticPivotKinematicCapture](const TCHAR* Reason)
	{
		// One line per input-intent revision: useful for a real key/analog edge,
		// without turning the locomotion update into a per-frame trace.
		if (!Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace() ||
			!InKinematicContext.bHasMoveInput ||
			LastLoggedPivotRejectionMoveIntentRevision == MoveIntentRevision)
		{
			return;
		}

		LastLoggedPivotRejectionMoveIntentRevision = MoveIntentRevision;
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("CombatStrafeRunPivotRejected Actor=%s Reason=%s IntentRev=%d Profile=%s Enabled=%s Local=%s Combat=%s Rotation=%d Gait=%d Input=%s Speed=%.1f/%.1f ActualSpeed=%.1f SemanticCapture=%s Previous=(%.2f,%.2f) Intent=(%.2f,%.2f) Angle=%.1f/%.1f RawInput=(%.2f,%.2f) SemanticInput=(%.2f,%.2f) SemanticPending=%s StableInput=(%.2f,%.2f) Velocity=%s IntentValid=%s Air=%s Jump=%s Landing=%s Consumed=%s"),
			*GetNameSafe(GetOwner()), Reason, MoveIntentRevision, *GetNameSafe(CombatProfile),
			CombatProfile && CombatProfile->bEnableCombatStrafeRunPivot ? TEXT("true") : TEXT("false"),
			ShouldUseLocalInputState() ? TEXT("true") : TEXT("false"),
			AuthContext.bCombatMode ? TEXT("true") : TEXT("false"),
			static_cast<int32>(AuthContext.RotationMode), static_cast<int32>(AuthContext.GaitIntent),
			InKinematicContext.bHasMoveInput ? TEXT("true") : TEXT("false"),
			PivotGroundSpeed, DerivedPivotMinSpeed, InKinematicContext.GroundSpeed,
			bUsingSemanticPivotKinematicCapture ? TEXT("true") : TEXT("false"),
			PreviousDirection.X, PreviousDirection.Y, IntentDirection.X, IntentDirection.Y,
			PhysicalReversalAngle, DerivedPivotAngleThreshold,
			CachedMoveInput.X, CachedMoveInput.Y,
			CachedSemanticMoveIntentInput.X, CachedSemanticMoveIntentInput.Y,
			bSemanticMoveIntentUpdatePending ? TEXT("true") : TEXT("false"),
			LastStableMoveInputDirection.X, LastStableMoveInputDirection.Y,
			InKinematicContext.HorizontalVelocity.IsNearlyZero() ? TEXT("zero") : TEXT("valid"),
			InKinematicContext.MoveWorldDirection.IsNearlyZero() ? TEXT("zero") : TEXT("valid"),
			bIsInAir ? TEXT("true") : TEXT("false"), bIsJumping ? TEXT("true") : TEXT("false"),
			IsLandingStateActive() ? TEXT("true") : TEXT("false"),
			MoveIntentRevision == LastConsumedPivotMoveIntentRevision ? TEXT("true") : TEXT("false"));
	};

	if (!CombatProfile)
	{
		LogRejectedPivot(TEXT("NoCombatProfile"));
		ConsumeSemanticPivotKinematicCapture();
		return false;
	}
	if (!CombatProfile->bEnableCombatStrafeRunPivot)
	{
		LogRejectedPivot(TEXT("ProfileDisabled"));
		ConsumeSemanticPivotKinematicCapture();
		return false;
	}
	if (!ShouldUseLocalInputState())
	{
		LogRejectedPivot(TEXT("NotLocalOwner"));
		ConsumeSemanticPivotKinematicCapture();
		return false;
	}
	if (!AuthContext.bCombatMode)
	{
		LogRejectedPivot(TEXT("NotCombat"));
		ConsumeSemanticPivotKinematicCapture();
		return false;
	}
	if (AuthContext.RotationMode != EProject_JLocomotionRotationMode::Strafe)
	{
		LogRejectedPivot(TEXT("NotStrafe"));
		ConsumeSemanticPivotKinematicCapture();
		return false;
	}
	if (AuthContext.GaitIntent != EProject_JLocomotionGaitIntent::Run)
	{
		LogRejectedPivot(TEXT("NotRun"));
		ConsumeSemanticPivotKinematicCapture();
		return false;
	}
	if (bSemanticMoveIntentUpdatePending)
	{
		// A Boolean direction edge is being coalesced in the input component's
		// post-update tick. Evaluate only the completed chord.
		return false;
	}
	if (!InKinematicContext.bHasMoveInput)
	{
		ConsumeSemanticPivotKinematicCapture();
		return false;
	}
	if (PivotGroundSpeed < DerivedPivotMinSpeed)
	{
		LogRejectedPivot(TEXT("BelowMinimumSpeed"));
		ConsumeSemanticPivotKinematicCapture();
		return false;
	}
	if (InKinematicContext.HorizontalVelocity.IsNearlyZero())
	{
		LogRejectedPivot(TEXT("NoActualVelocity"));
		ConsumeSemanticPivotKinematicCapture();
		return false;
	}
	if (IntentDirection.IsNearlyZero())
	{
		LogRejectedPivot(TEXT("NoMoveIntent"));
		ConsumeSemanticPivotKinematicCapture();
		return false;
	}
	const FVector2D CurrentPivotIntent = bHasSemanticMoveIntentInput
		? CachedSemanticMoveIntentInput
		: CachedMoveInput;
	if (!IsCardinalPivotIntent(CurrentPivotIntent) ||
		!bHasPreviousStableMoveInputDirection ||
		!IsCardinalPivotIntent(PreviousStableMoveInputDirection))
	{
		LogRejectedPivot(TEXT("DiagonalIntentUnsupported"));
		ConsumeSemanticPivotKinematicCapture();
		return false;
	}
	if (bIsInAir || bIsJumping || bIsFallOffStart || IsLandingStateActive())
	{
		LogRejectedPivot(TEXT("AirOrLanding"));
		ConsumeSemanticPivotKinematicCapture();
		return false;
	}
	if (MoveIntentRevision == LastConsumedPivotMoveIntentRevision)
	{
		ConsumeSemanticPivotKinematicCapture();
		return false;
	}

	if (PhysicalReversalAngle < DerivedPivotAngleThreshold)
	{
		LogRejectedPivot(TEXT("AngleBelowThreshold"));
		ConsumeSemanticPivotKinematicCapture();
		return false;
	}

	LastConsumedPivotMoveIntentRevision = MoveIntentRevision;
	LatchedPivotPreviousMovementDirection = PreviousDirection;
	LatchedPivotMoveIntentDirection = IntentDirection;
	ConsumeSemanticPivotKinematicCapture();
	++PivotRequestRevision;
	if (PivotRequestRevision == 0)
	{
		PivotRequestRevision = 1;
	}
	if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("CombatStrafeRunPivotAccepted Actor=%s IntentRev=%d PivotRev=%d Speed=%.1f Min=%.1f Angle=%.1f Threshold=%.1f Previous=(%.2f,%.2f) Intent=(%.2f,%.2f) RawInput=(%.2f,%.2f) SemanticInput=(%.2f,%.2f) StableInput=(%.2f,%.2f)"),
			*GetNameSafe(GetOwner()), MoveIntentRevision, PivotRequestRevision,
			PivotGroundSpeed, DerivedPivotMinSpeed, PhysicalReversalAngle, DerivedPivotAngleThreshold,
			PreviousDirection.X, PreviousDirection.Y, IntentDirection.X, IntentDirection.Y,
			CachedMoveInput.X, CachedMoveInput.Y,
			CachedSemanticMoveIntentInput.X, CachedSemanticMoveIntentInput.Y,
			LastStableMoveInputDirection.X, LastStableMoveInputDirection.Y);
	}
	return true;
}

bool UProject_JLocomotionAnimStateComponent::ShouldTurnInPlaceForContext(
	const FProject_JLocomotionAuthoritativeContext& AuthContext,
	const FProject_JLocomotionKinematicContext& InKinematicContext) const
{
	// A simulated proxy does not have the owning player's current controller
	// yaw. Treating its replicated/fallback control rotation as authoritative
	// produces a permanent facing delta and repeatedly selects a Turn PSD while
	// the remote character is actually stationary. It may only enter TIP through
	// the short server-confirmed event window, whose direction is injected into
	// the kinematic context before this method is evaluated.
	if (!ShouldUseLocalInputState())
	{
		return bRemoteTurnInPlaceActive &&
			(InKinematicContext.GroundSpeed <= IdleSpeedThreshold) &&
			!InKinematicContext.bHasMoveInput &&
			!bIsInAir &&
			!IsLandingStateActive();
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
	bSharpTurnRequested = false;

	// A moving landing may hand off into Stop only when movement intent survived
	// touchdown for a short, real interval. This rejects the common case where
	// input was released in the air or on the landing frame.
	if (bIsLanding && bHasMoveInput)
	{
		LandingPostTouchdownMoveInputTime += DeltaTime;
		bLandingReceivedPostTouchdownMoveInput =
			LandingPostTouchdownMoveInputTime >= LandingExitStopInputHoldTime;
	}

	if (bTrackTurnAngle && bHasMoveInput && bPrevHasMoveInput && PreviousMoveInputForTurn.Size() > MoveInputDeadZone)
	{
		const FVector2D PreviousDirection = PreviousMoveInputForTurn.GetSafeNormal();
		const FVector2D CurrentDirection = MoveInput.GetSafeNormal();
		const float Dot = FMath::Clamp(FVector2D::DotProduct(PreviousDirection, CurrentDirection), -1.0f, 1.0f);
		const float Cross = PreviousDirection.Y * CurrentDirection.X - PreviousDirection.X * CurrentDirection.Y;
		MoveInputTurnAngle = FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
	}
}

void UProject_JLocomotionAnimStateComponent::UpdateLocalMoveIntentSnapshot(const FVector2D& MoveInput)
{
	if (MoveInput.Size() <= MoveInputDeadZone)
	{
		// Preserve the last non-zero intent across a keyboard release gap. We do
		// not guess why it was zero; a later non-zero action produces the edge.
		return;
	}

	const FVector2D StableDirection = MoveInput.GetSafeNormal();
	const UProject_JCombatAnimProfile* CombatProfile = GetPlayerOwner()
		? GetPlayerOwner()->GetCombatAnimProfile()
		: nullptr;
	const float DirectionHysteresisDegrees = CombatProfile
		? FMath::Max(CombatProfile->StrafeDirectionHysteresisDegrees, 1.0f)
		: 7.5f;
	if (bHasLastStableMoveInputDirection)
	{
		const float Dot = FMath::Clamp(
			FVector2D::DotProduct(StableDirection, LastStableMoveInputDirection),
			-1.0f,
			1.0f);
		const float DirectionDeltaDegrees = FMath::RadiansToDegrees(FMath::Acos(Dot));
		if (DirectionDeltaDegrees < DirectionHysteresisDegrees)
		{
			return;
		}
	}

	if (bHasLastStableMoveInputDirection)
	{
		PreviousStableMoveInputDirection = LastStableMoveInputDirection;
		bHasPreviousStableMoveInputDirection = true;
	}
	LastStableMoveInputDirection = StableDirection;
	bHasLastStableMoveInputDirection = true;
	++MoveIntentRevision;
	if (MoveIntentRevision == 0)
	{
		MoveIntentRevision = 1;
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
