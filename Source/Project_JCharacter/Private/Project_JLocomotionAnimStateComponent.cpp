// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JLocomotionAnimStateComponent.h"

#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"
#include "TimerManager.h"

UProject_JLocomotionAnimStateComponent::UProject_JLocomotionAnimStateComponent()
{
}

void UProject_JLocomotionAnimStateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearOwnedMovementGameplayTags();
	Super::EndPlay(EndPlayReason);
}

void UProject_JLocomotionAnimStateComponent::UpdateState(float DeltaTime)
{
	if (!GetPlayerOwner() || !GetCachedMovementComponent())
	{
		CacheOwnerReferences();
	}

	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return;
	}

	bUsingLocalInputState = ShouldUseLocalInputState();
	bDedicatedServerContext = IsDedicatedServerContext();
	if (bDedicatedServerContext && bSkipDedicatedServerAnimStateUpdate)
	{
		return;
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
				return;
			}
		}
	}
	HiddenRemoteUpdateAccumulator = 0.0f;

	const FProject_JLocomotionRuntimeSnapshot MovementSnapshot = BuildMovementSnapshot(*PlayerOwner);
	ApplyMovementSnapshot(DeltaTime, MovementSnapshot);

	const bool bMovementReportsInAir = IsInAirForAnimation();

	if (bUsingLocalInputState)
	{
		UpdateLocalAirState(bMovementReportsInAir);
		UpdateMovementRequestState(DeltaTime);
	}
	else
	{
		UpdateRemoteAirState(DeltaTime, IsRemoteInAirForAnimation(bMovementReportsInAir));
		UpdateRemoteMovementRequestState(DeltaTime);
	}

	UpdateCombatMovementState(MovementSnapshot.HorizontalVelocity);
}

void UProject_JLocomotionAnimStateComponent::HandleJumpStarted()
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner || !CanStartJumpForAnimation())
	{
		return;
	}

	const bool bHadLandingState = IsLandingStateActive();

	ClearJumpStartTimers();
	ClearFallOffStartTimers();
	ClearLandingTimers();
	StopFallOffStart();
	bIsLanding = false;
	bLandingRequested = false;
	bCanEnterLand = false;
	bCanEnterGround = false;
	bLandingFinished = true;
	bIsInAir = true;
	bIsPhysicallyInAir = true;
	bWasInAir = true;
	bSuppressFallOffStart = true;
	JumpStartElapsedTime = 0.0f;
	bIgnoreNextLandingForJumpStart = true;
	bJumpStartFinishPendingExit = false;
	bLandingFinishPendingExit = false;
	bFallOffStartFinishPendingExit = false;

	bIsJumping = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(JumpTimerHandle, this, &UProject_JLocomotionAnimStateComponent::OnJumpTimerFinished, FMath::Max(0.1f, JumpStartMaxDuration), false);
	}

	if (bHadLandingState)
	{
		RemoveOwnedLandingGameplayTag();
	}
	AddOwnedInAirGameplayTag();
}

void UProject_JLocomotionAnimStateComponent::HandleReplicatedJumpStarted()
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner || ShouldUseLocalInputState())
	{
		return;
	}

	if (IsLandingStateActive())
	{
		FinishLanding();
	}

	ClearJumpStartTimers();
	ClearFallOffStartTimers();
	ClearLandingTimers();

	StopFallOffStart();
	bIsLanding = false;
	bLandingRequested = false;
	bCanEnterLand = false;
	bCanEnterGround = false;
	bLandingFinished = true;
	bIsInAir = true;
	bIsPhysicallyInAir = true;
	bWasInAir = true;
	bSuppressFallOffStart = true;
	bIsJumping = true;
	JumpStartElapsedTime = 0.0f;
	bIgnoreNextLandingForJumpStart = true;
	bJumpStartFinishPendingExit = false;
	bLandingFinishPendingExit = false;
	bFallOffStartFinishPendingExit = false;
	RemoteAirborneTime = 0.0f;
	LastFallSpeed = 0.0f;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(JumpTimerHandle, this, &UProject_JLocomotionAnimStateComponent::OnJumpTimerFinished, FMath::Max(0.05f, ReplicatedJumpStartDuration), false);
	}
}

void UProject_JLocomotionAnimStateComponent::HandleReplicatedFallOffStarted()
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner || ShouldUseLocalInputState())
	{
		return;
	}

	if (IsLandingStateActive())
	{
		return;
	}

	if (bIsJumping)
	{
		CompleteJumpStart();
	}

	bSuppressFallOffStart = false;
	StartFallOffStart(false);
	bIsPhysicallyInAir = true;
	RemoteAirborneTime = 0.0f;
}

void UProject_JLocomotionAnimStateComponent::HandleReplicatedMoveStarted(bool bWasSprintingForStart)
{
	if (ShouldUseLocalInputState())
	{
		return;
	}

	RemoteStopStartSuppressTimeRemaining = 0.0f;
	bRemoteMoveReleasedWhileAirborne = false;
	bLandingIgnoresRemoteGroundSpeed = false;

	if (IsLandingStateActive())
	{
		bStartWasSprinting = bWasSprintingForStart;
		bWantsSprint = bWasSprintingForStart;
		bHasMoveInput = true;
		bPrevHasMoveInput = true;
		bResolvedMoveInputLastUpdate = true;
		bLandWasMoving = true;
		bPendingStartRequest = false;
		bForceLandingFinishToLocomotion = true;
		FinishLandingImmediately();
		return;
	}

	if (GroundMotionMode == EProject_JGroundMotionMode::Start)
	{
		bPendingStartRequest = false;
		EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
		return;
	}

	bStartWasSprinting = bWasSprintingForStart;
	bPendingStartRequest = true;
}

void UProject_JLocomotionAnimStateComponent::HandleReplicatedMoveStopped()
{
	if (ShouldUseLocalInputState())
	{
		return;
	}

	if (GroundMotionMode == EProject_JGroundMotionMode::Start &&
		GetRemoteMovementInputForState().SizeSquared() > FMath::Square(MoveInputDeadZone))
	{
		bPendingStopRequest = false;
		RemoteStopStartSuppressTimeRemaining = 0.0f;
		EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
		return;
	}

	bHasMoveInput = false;
	bPrevHasMoveInput = false;
	bResolvedMoveInputLastUpdate = false;
	MoveInputSize = 0.0f;
	MoveInputHeldTime = 0.0f;
	PreviousMoveInputForTurn = FVector2D::ZeroVector;
	bPendingStartRequest = false;
	bPendingStopRequest = true;
	RemoteStopStartSuppressTimeRemaining = FMath::Max(RemoteStopStartSuppressTimeRemaining, RemoteStopStartSuppressDuration);

	const bool bInAirState = bIsInAir || bIsPhysicallyInAir || bIsJumping || bIsFallOffStart;
	if (bInAirState)
	{
		bRemoteMoveReleasedWhileAirborne = true;
	}

	if (IsLandingStateActive())
	{
		bLandWasMoving = false;
		bLandWasSprinting = false;
		bLandingIgnoresRemoteGroundSpeed = true;
		FinishLandingImmediately();
	}
}

void UProject_JLocomotionAnimStateComponent::HandleReplicatedLandingCancelled()
{
	if (ShouldUseLocalInputState() || !IsLandingStateActive())
	{
		return;
	}

	bLandWasMoving = true;
	bForceLandingFinishToLocomotion = true;
	FinishLandingImmediately();
}

void UProject_JLocomotionAnimStateComponent::HandleLanded(const FHitResult&)
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return;
	}

	const bool bHasRealFallingEvidence =
		LastFallSpeed >= RemoteLandingMinFallSpeed ||
		VerticalSpeed < -RemoteLandingMinFallSpeed ||
		PlayerOwner->GetVelocity().Z < -RemoteLandingMinFallSpeed;
	const bool bIgnoreEarlyJumpStartLanding =
		bIgnoreNextLandingForJumpStart &&
		JumpStartElapsedTime <= IgnoreLandingAfterJumpStartTime &&
		!bHasRealFallingEvidence;

	if (bIsJumping && (bIgnoreEarlyJumpStartLanding || PlayerOwner->GetVelocity().Z > 0.0f))
	{
		bIsInAir = true;
		bIsPhysicallyInAir = true;
		bWasInAir = true;
		bIsLanding = false;
		bLandingRequested = false;
		bCanEnterLand = false;
		bCanEnterGround = false;
		bIgnoreNextLandingForJumpStart = false;
		return;
	}

	const float ImpactFallSpeed = FMath::Max(LastFallSpeed, FMath::Abs(PlayerOwner->GetVelocity().Z));
	ClearJumpStartTimers();
	bJumpStartFinishPendingExit = false;
	bIgnoreNextLandingForJumpStart = false;
	StartLanding(ImpactFallSpeed, true, true);
}

void UProject_JLocomotionAnimStateComponent::FinishLanding(bool bForceFinish)
{
	if (!IsLandingStateActive())
	{
		return;
	}

	if (!bForceFinish && bIsLanding && !bCanExitLanding)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				LandingTimerHandle,
				this,
				&UProject_JLocomotionAnimStateComponent::OnLandingTimerFinished,
				FMath::Max(0.01f, LandingMinHoldTime - LandingElapsedTime),
				false);
		}
		return;
	}

	if (FinishedExitWindow > 0.0f && !bLandingFinishPendingExit)
	{
		bLandingFinishPendingExit = true;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				LandingExitTimerHandle,
				this,
				&UProject_JLocomotionAnimStateComponent::CompleteLanding,
				FinishedExitWindow,
				false);
		}
		return;
	}

	if (!bLandingFinishPendingExit)
	{
		CompleteLanding();
	}
}

void UProject_JLocomotionAnimStateComponent::FinishStop()
{
	bStopFinishPendingExit = false;

	if (GroundMotionMode == EProject_JGroundMotionMode::Stop)
	{
		EnterGroundMotionMode(
			bHasMoveInput || GroundSpeed > StopIntentSpeedThreshold
				? EProject_JGroundMotionMode::Locomotion
				: EProject_JGroundMotionMode::Idle);
	}
}

void UProject_JLocomotionAnimStateComponent::FinishJumpStart()
{
	if (!bIsJumping)
	{
		return;
	}

	if (IsLandingStateActive())
	{
		return;
	}

	if (JumpStartElapsedTime < JumpStartNotifyIgnoreTime)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(JumpStartExitTimerHandle);
		}
		bJumpStartFinishPendingExit = false;
		return;
	}

	const bool bMovementIsAirborne = IsInAirForAnimation();
	const bool bTooEarlyToFinish = JumpStartElapsedTime < JumpStartMinHoldTime || (!bMovementIsAirborne && JumpStartElapsedTime < JumpStartMaxDuration);
	if (bTooEarlyToFinish)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(JumpStartExitTimerHandle);
		}
		bJumpStartFinishPendingExit = false;
		return;
	}

	bJumpStartFinishPendingExit = false;

	const float FinishDelay = FinishedExitWindow;
	if (FinishDelay > 0.0f && !bJumpStartFinishPendingExit)
	{
		bJumpStartFinishPendingExit = true;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				JumpStartExitTimerHandle,
				this,
				&UProject_JLocomotionAnimStateComponent::CompleteJumpStart,
				FMath::Max(0.01f, FinishDelay),
				false);
		}
		return;
	}

	if (!bJumpStartFinishPendingExit)
	{
		CompleteJumpStart();
	}
}

void UProject_JLocomotionAnimStateComponent::FinishFallOffStart()
{
	if (!bIsFallOffStart)
	{
		return;
	}

	if (FinishedExitWindow > 0.0f && bIsFallOffStart && !bFallOffStartFinishPendingExit)
	{
		bFallOffStartFinishPendingExit = true;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				FallOffStartExitTimerHandle,
				this,
				&UProject_JLocomotionAnimStateComponent::CompleteFallOffStart,
				FinishedExitWindow,
				false);
		}
		return;
	}

	if (!bFallOffStartFinishPendingExit)
	{
		CompleteFallOffStart();
	}
}

void UProject_JLocomotionAnimStateComponent::MarkGroundStartFinished()
{
	bPendingGroundStartFinish = false;
	bGroundStartFinishPendingExit = false;

	if (GroundMotionMode == EProject_JGroundMotionMode::Start)
	{
		EnterGroundMotionMode(
			bHasMoveInput || GroundSpeed > IdleSpeedThreshold
				? EProject_JGroundMotionMode::Locomotion
				: EProject_JGroundMotionMode::Idle);
	}
}

void UProject_JLocomotionAnimStateComponent::SetMoveInput(const FVector2D& InMoveInput)
{
	const bool bHadMoveInput = CachedMoveInput.Size() > MoveInputDeadZone || bHasMoveInput || bResolvedMoveInputLastUpdate;
	CachedMoveInput = InMoveInput.GetClampedToMaxSize(1.0f);
	const bool bHasNewMoveInput = CachedMoveInput.Size() > MoveInputDeadZone;

	if (bHasNewMoveInput && !bHadMoveInput)
	{
		bPendingStartRequest = true;
	}
}

void UProject_JLocomotionAnimStateComponent::ClearMoveInput()
{
	const bool bHadMoveInput = CachedMoveInput.Size() > MoveInputDeadZone || bHasMoveInput || bResolvedMoveInputLastUpdate;
	CachedMoveInput = FVector2D::ZeroVector;
	MoveInputSize = 0.0f;
	MoveInputHeldTime = 0.0f;
	bHasMoveInput = false;
	PreviousMoveInputForTurn = FVector2D::ZeroVector;

	if (bHadMoveInput)
	{
		bPendingStopRequest = true;
	}

}

void UProject_JLocomotionAnimStateComponent::HandleSprintStarted()
{
	bSprintInputHeld = true;
	bWantsSprint = true;
	bUseSprintLocomotion =
		GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		(CachedMoveInput.Size() > MoveInputDeadZone || GroundSpeed > IdleSpeedThreshold);
	if (GroundMotionMode == EProject_JGroundMotionMode::Start)
	{
		bStartWasSprinting = true;
	}
}

void UProject_JLocomotionAnimStateComponent::HandleSprintStopped()
{
	bSprintInputHeld = false;
	bWantsSprint = false;
	bUseSprintLocomotion = false;

	if (GroundMotionMode == EProject_JGroundMotionMode::Start)
	{
		bStartWasSprinting = false;
		EnterGroundMotionMode(
			CachedMoveInput.Size() > MoveInputDeadZone || bHasMoveInput
				? EProject_JGroundMotionMode::Locomotion
				: (GroundSpeed > StopIntentSpeedThreshold
					? EProject_JGroundMotionMode::Stop
					: EProject_JGroundMotionMode::Idle));
	}

	if (IsLandingStateActive() && bLandWasSprinting)
	{
		bLandWasSprinting = false;
		FinishLandingImmediately();
	}
}

bool UProject_JLocomotionAnimStateComponent::CanStartJumpForAnimation() const
{
	const bool bInLandingRecovery = IsLandingStateActive();
	const bool bCanCancelLandingIntoJump =
		bInLandingRecovery &&
		!bIsPhysicallyInAir &&
		!IsInAirForAnimation();

	return
		!bIsJumping &&
		!bIsFallOffStart &&
		(!bInLandingRecovery || bCanCancelLandingIntoJump);
}

bool UProject_JLocomotionAnimStateComponent::ConsumeRealLandingEventRequested()
{
	const bool bRequested = bRealLandingEventRequested;
	bRealLandingEventRequested = false;
	return bRequested;
}

bool UProject_JLocomotionAnimStateComponent::IsLandingStateActive() const
{
	return bIsLanding || bLandingRequested || bCanEnterLand;
}

void UProject_JLocomotionAnimStateComponent::ClearJumpStartTimers()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JumpTimerHandle);
		World->GetTimerManager().ClearTimer(JumpStartExitTimerHandle);
	}
}

void UProject_JLocomotionAnimStateComponent::ClearFallOffStartTimers()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FallOffStartTimerHandle);
		World->GetTimerManager().ClearTimer(FallOffStartExitTimerHandle);
	}
}

void UProject_JLocomotionAnimStateComponent::ClearLandingTimers()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LandingTimerHandle);
		World->GetTimerManager().ClearTimer(LandingExitTimerHandle);
	}
}

void UProject_JLocomotionAnimStateComponent::FinishLandingImmediately()
{
	ClearLandingTimers();
	bLandingFinishPendingExit = false;
	OnLandingTimerFinished();
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

void UProject_JLocomotionAnimStateComponent::HandleAnimationEvent(EProject_JLocomotionAnimEvent EventType)
{
	switch (EventType)
	{
	case EProject_JLocomotionAnimEvent::GroundStartFinished:
		MarkGroundStartFinished();
		break;
	case EProject_JLocomotionAnimEvent::StopFinished:
		FinishStop();
		break;
	case EProject_JLocomotionAnimEvent::JumpStartFinished:
		FinishJumpStart();
		break;
	case EProject_JLocomotionAnimEvent::FallOffStartFinished:
		FinishFallOffStart();
		break;
	case EProject_JLocomotionAnimEvent::LandingFinished:
		FinishLanding(true);
		break;
	case EProject_JLocomotionAnimEvent::HitReactFinished:
	case EProject_JLocomotionAnimEvent::AttackFinished:
		ClearTransientAnimationRequests();
		break;
	default:
		break;
	}
}

bool UProject_JLocomotionAnimStateComponent::ShouldUseLocalInputState() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	return PlayerOwner && (PlayerOwner->IsLocallyControlled() || bUseInputDerivedRequestsForRemotePlayers);
}

bool UProject_JLocomotionAnimStateComponent::IsSprintRequestedForAnimation() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	return bSprintInputHeld || (PlayerOwner && PlayerOwner->IsSprintLocomotionAllowed());
}

void UProject_JLocomotionAnimStateComponent::EnterGroundMotionMode(EProject_JGroundMotionMode NewMode)
{
	if (GroundMotionMode == NewMode)
	{
		RefreshGroundMotionFlags();
		return;
	}

	GroundMotionMode = NewMode;
	GroundMotionModeElapsedTime = 0.0f;
	bPendingGroundStartFinish = false;
	bGroundStartFinishPendingExit = false;
	bStopFinishPendingExit = false;

	if (NewMode == EProject_JGroundMotionMode::Start)
	{
		bStartWasSprinting = IsSprintRequestedForAnimation() || GroundSpeed >= SprintLocomotionSpeedThreshold;
		if (!ShouldUseLocalInputState())
		{
			if (const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner())
			{
				FVector HorizontalVelocity = PlayerOwner->GetVelocity();
				HorizontalVelocity.Z = 0.0f;
				RemoteStartPreviousMoveWorldDirection = HorizontalVelocity.SizeSquared() > FMath::Square(RemoteMoveSpeedThreshold)
					? HorizontalVelocity.GetSafeNormal()
					: FVector::ZeroVector;
				RemoteStartPreviousActorYaw = PlayerOwner->GetActorRotation().Yaw;
				bHasRemoteStartTurnReference = true;
			}
		}
	}
	else if (NewMode == EProject_JGroundMotionMode::Stop)
	{
		bStopWasSprinting = bUseSprintLocomotion || bWantsSprint || GroundSpeed >= SprintLocomotionSpeedThreshold;
	}
	else if (NewMode == EProject_JGroundMotionMode::Idle)
	{
		bStartWasSprinting = false;
		bStopWasSprinting = false;
	}
	else if (NewMode == EProject_JGroundMotionMode::Locomotion)
	{
		bStartWasSprinting = false;
		bStopWasSprinting = false;
	}

	if (NewMode != EProject_JGroundMotionMode::Start)
	{
		RemoteStartPreviousMoveWorldDirection = FVector::ZeroVector;
		RemoteStartPreviousActorYaw = 0.0f;
		bHasRemoteStartTurnReference = false;
	}

	RefreshGroundMotionFlags();
}

void UProject_JLocomotionAnimStateComponent::RefreshGroundMotionFlags()
{
	bStartRequested = GroundMotionMode == EProject_JGroundMotionMode::Start;
	bUseStartDatabase = bStartRequested;
	bGroundStartFinished = !bStartRequested;
	bUseGroundLocomotionDatabase = GroundMotionMode == EProject_JGroundMotionMode::Locomotion;
	bStopRequested = GroundMotionMode == EProject_JGroundMotionMode::Stop;
	bUseStopDatabase = bStopRequested;
	bIsStopping = bStopRequested;
	bUseGroundLocomotionState =
		GroundMotionMode == EProject_JGroundMotionMode::Locomotion ||
		GroundMotionMode == EProject_JGroundMotionMode::Start ||
		GroundMotionMode == EProject_JGroundMotionMode::Stop ||
		bHasMoveInput ||
		GroundSpeed > IdleSpeedThreshold;
	bUseSprintLocomotion =
		GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		bWantsSprint &&
		(bHasMoveInput || GroundSpeed > IdleSpeedThreshold);
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

void UProject_JLocomotionAnimStateComponent::UpdateLocalAirState(bool bIsCurrentlyInAir)
{
	bIsPhysicallyInAir = bIsCurrentlyInAir;
	if (bIsCurrentlyInAir)
	{
		LastFallSpeed = FMath::Max(LastFallSpeed, FMath::Abs(VerticalSpeed));
	}

	if (bIsJumping && !bIsCurrentlyInAir && JumpStartElapsedTime >= JumpStartGroundContactGraceTime)
	{
		const float ImpactFallSpeed = VerticalSpeed < 0.0f
			? FMath::Max(LastFallSpeed, FMath::Abs(VerticalSpeed))
			: LastFallSpeed;
		StartLanding(ImpactFallSpeed, false, true);
	}
	else if (!bWasInAir && bIsCurrentlyInAir && !bIsJumping && !bIsLanding && !bSuppressFallOffStart)
	{
		StartFallOffStart();
	}
	else if (bIsCurrentlyInAir)
	{
		bIsInAir = true;
	}
	else if (!bIsJumping && !bLandingRequested && !bIsLanding)
	{
		bIsInAir = false;
		bSuppressFallOffStart = false;
		LastFallSpeed = 0.0f;
	}

	bWasInAir = bIsCurrentlyInAir;
	bCanEnterLand = bLandingRequested;
	bCanEnterGround = !bIsInAir && !bIsLanding && !bLandingRequested;
}

void UProject_JLocomotionAnimStateComponent::UpdateRemoteAirState(float DeltaTime, bool bIsCurrentlyInAir)
{
	const bool bWasRemoteInAir = bWasInAir;
	const bool bHadRemoteAirborneEvidence =
		bWasRemoteInAir ||
		RemoteAirborneTime >= RemoteLandingMinAirTime ||
		LastFallSpeed >= RemoteLandingMinFallSpeed;

	if (UpdateRemoteJumpStartState(DeltaTime, bIsCurrentlyInAir, bHadRemoteAirborneEvidence))
	{
		return;
	}

	bIsPhysicallyInAir = bIsCurrentlyInAir;
	bIsJumping = false;
	bJumpStartFinishPendingExit = false;
	if (!bIsFallOffStart)
	{
		bSuppressFallOffStart = false;
	}

	if (bIsCurrentlyInAir)
	{
		RemoteAirborneTime += DeltaTime;
		LastFallSpeed = FMath::Max(LastFallSpeed, FMath::Abs(VerticalSpeed));
	}

	if (!bIsCurrentlyInAir && bHadRemoteAirborneEvidence && !bIsLanding && !bLandingRequested)
	{
		StartLanding(FMath::Max(LastFallSpeed, FMath::Abs(VerticalSpeed)), false, false);
		bWasInAir = false;
		RemoteAirborneTime = 0.0f;
		return;
	}

	if (bIsCurrentlyInAir)
	{
		bIsInAir = true;
		bIsLanding = false;
		bLandingRequested = false;
		bCanEnterLand = false;
		bCanEnterGround = false;
	}
	else if (!bIsLanding && !bLandingRequested)
	{
		bIsInAir = bIsFallOffStart;
		bCanEnterLand = false;
		bCanEnterGround = !bIsFallOffStart;
		RemoteAirborneTime = 0.0f;
		LastFallSpeed = 0.0f;
	}

	bWasInAir = bIsCurrentlyInAir;
}

bool UProject_JLocomotionAnimStateComponent::UpdateRemoteJumpStartState(float DeltaTime, bool bIsCurrentlyInAir, bool bHadRemoteAirborneEvidence)
{
	if (!bIsJumping)
	{
		return false;
	}

	bIsPhysicallyInAir = bIsCurrentlyInAir;
	bIsInAir = true;
	bSuppressFallOffStart = false;
	bCanEnterLand = false;
	bCanEnterGround = false;

	if (bIsCurrentlyInAir)
	{
		RemoteAirborneTime += DeltaTime;
		LastFallSpeed = FMath::Max(LastFallSpeed, FMath::Abs(VerticalSpeed));
	}
	else if (bHadRemoteAirborneEvidence && JumpStartElapsedTime >= JumpStartGroundContactGraceTime && !bIsLanding && !bLandingRequested)
	{
		StartLanding(FMath::Max(LastFallSpeed, FMath::Abs(VerticalSpeed)), false, false);
		bWasInAir = false;
		RemoteAirborneTime = 0.0f;
		return true;
	}

	bWasInAir = bIsCurrentlyInAir;
	return true;
}

void UProject_JLocomotionAnimStateComponent::StartLanding(float ImpactFallSpeed, bool bBroadcastRealLandingEvent, bool bUpdateGameplayTags)
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return;
	}

	const bool bHadInAirState = bIsInAir || bIsPhysicallyInAir || bIsJumping || bIsFallOffStart;

	StopFallOffStart();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JumpStartExitTimerHandle);
	}
	bJumpStartFinishPendingExit = false;
	bIsJumping = false;
	bIgnoreNextLandingForJumpStart = false;
	bIsInAir = true;
	bIsPhysicallyInAir = false;
	bWasInAir = false;
	bSuppressFallOffStart = false;

	LastFallSpeed = ImpactFallSpeed;
	LandStartGroundSpeed = GroundSpeed;
	LandStartFallSpeed = ImpactFallSpeed;
	const bool bUseRemoteStandLandingOverride = !ShouldUseLocalInputState() && bRemoteMoveReleasedWhileAirborne;
	const bool bLandingHasMoveInput = GetMovementInputForState().Size() > MoveInputDeadZone || bHasMoveInput;
	const bool bLandingHasMovementIntent = ShouldUseLocalInputState()
		? bLandingHasMoveInput
		: (!bUseRemoteStandLandingOverride && (bLandingHasMoveInput || LandStartGroundSpeed > IdleSpeedThreshold));
	bLandWasMoving = bLandingHasMovementIntent;
	bLandWasSprinting = bLandWasMoving && IsSprintRequestedForAnimation();
	bLandingIgnoresRemoteGroundSpeed = bUseRemoteStandLandingOverride;
	bRemoteMoveReleasedWhileAirborne = false;
	bUseHeavyLand = LandStartFallSpeed >= HeavyLandSpeedThreshold;
	bLandingCancelEventDispatched = false;
	InitialLandingMoveWorldDirection = FVector::ZeroVector;
	PreviousLandingMoveWorldDirection = FVector::ZeroVector;
	InitialLandingActorYaw = PlayerOwner->GetActorRotation().Yaw;
	PreviousLandingActorYaw = InitialLandingActorYaw;
	if (bLandWasMoving)
	{
		const FVector2D LandingMoveInput = GetMovementInputForState();
		InitialLandingMoveWorldDirection = CalculateMoveWorldDirection(LandingMoveInput);
		PreviousLandingMoveWorldDirection = InitialLandingMoveWorldDirection;
	}
	bIsLanding = true;
	bLandingRequested = true;
	bCanEnterLand = true;
	bCanEnterGround = false;
	bCanExitLanding = LandingMinHoldTime <= 0.0f;
	bLandingFinished = false;
	LandingElapsedTime = 0.0f;

	if (bUpdateGameplayTags)
	{
		if (bHadInAirState)
		{
			RemoveOwnedInAirGameplayTag();
		}

		AddOwnedLandingGameplayTag();
	}

	if (UWorld* World = GetWorld())
	{
		const float LandingFallbackDuration = bLandWasMoving ? LandingRequestDuration : StandLandingRequestDuration;
		ClearLandingTimers();
		World->GetTimerManager().SetTimer(LandingTimerHandle, this, &UProject_JLocomotionAnimStateComponent::OnLandingTimerFinished, FMath::Max(0.05f, LandingFallbackDuration), false);
	}
	bLandingFinishPendingExit = false;

	bRealLandingEventRequested = bBroadcastRealLandingEvent && LastFallSpeed > RealLandingEventSpeedThreshold;
}

void UProject_JLocomotionAnimStateComponent::StartFallOffStart(bool bReplicateEvent)
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return;
	}

	bIsInAir = true;
	bIsFallOffStart = true;
	ClearFallOffStartTimers();
	if (bReplicateEvent && ShouldUseLocalInputState())
	{
		PlayerOwner->NotifyFallOffStartedForAnimation();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(FallOffStartTimerHandle, this, &UProject_JLocomotionAnimStateComponent::OnFallOffStartFinished, FMath::Max(0.05f, FallOffStartDuration), false);
	}
	bFallOffStartFinishPendingExit = false;

	AddOwnedInAirGameplayTag();
}

void UProject_JLocomotionAnimStateComponent::StopFallOffStart()
{
	ClearFallOffStartTimers();
	bIsFallOffStart = false;
	bFallOffStartFinishPendingExit = false;
}

void UProject_JLocomotionAnimStateComponent::CompleteGroundStart()
{
	bPendingGroundStartFinish = false;
	bGroundStartFinishPendingExit = false;
	MarkGroundStartFinished();
}

void UProject_JLocomotionAnimStateComponent::CompleteStop()
{
	bStopFinishPendingExit = false;
	FinishStop();
}

void UProject_JLocomotionAnimStateComponent::CompleteJumpStart()
{
	if (!bIsJumping && !bJumpStartFinishPendingExit)
	{
		return;
	}

	ClearJumpStartTimers();

	bJumpStartFinishPendingExit = false;
	bIsJumping = false;
	JumpStartElapsedTime = 0.0f;
	bIgnoreNextLandingForJumpStart = false;

	if (!IsInAirForAnimation() && !bIsLanding)
	{
		bIsInAir = false;
		bWasInAir = false;
		bSuppressFallOffStart = false;
	}
}

void UProject_JLocomotionAnimStateComponent::CompleteFallOffStart()
{
	StopFallOffStart();
}

void UProject_JLocomotionAnimStateComponent::CompleteLanding()
{
	if (!IsLandingStateActive() && !bLandingFinishPendingExit)
	{
		return;
	}

	FinishLandingImmediately();
}

void UProject_JLocomotionAnimStateComponent::OnLandingTimerFinished()
{
	const bool bHadLandingState = IsLandingStateActive();

	bIsLanding = false;
	bLandingRequested = false;
	bIsInAir = false;
	bWasInAir = false;
	bSuppressFallOffStart = false;
	bCanEnterLand = false;
	bCanEnterGround = true;
	bCanExitLanding = true;
	bLandingFinished = true;
	bLandingFinishPendingExit = false;
	LastFallSpeed = 0.0f;
	RemoteAirborneTime = 0.0f;
	LandingElapsedTime = 0.0f;
	InitialLandingMoveWorldDirection = FVector::ZeroVector;
	PreviousLandingMoveWorldDirection = FVector::ZeroVector;
	InitialLandingActorYaw = 0.0f;
	PreviousLandingActorYaw = 0.0f;

	const bool bIgnoreRemoteMotionForLandingFinish = bLandingIgnoresRemoteGroundSpeed;
	const bool bMoveInputStillHeld = !bIgnoreRemoteMotionForLandingFinish && GetMovementInputForState().Size() > MoveInputDeadZone;
	const bool bForcedLocomotionFinish = bForceLandingFinishToLocomotion;
	const bool bAllowGroundSpeedLocomotion = !bIgnoreRemoteMotionForLandingFinish;
	const bool bShouldEnterLocomotion = bForcedLocomotionFinish || bMoveInputStillHeld || (bAllowGroundSpeedLocomotion && GroundSpeed > IdleSpeedThreshold);
	bForceLandingFinishToLocomotion = false;
	bLandingIgnoresRemoteGroundSpeed = false;
	bLandingCancelEventDispatched = false;

	if (bShouldEnterLocomotion)
	{
		const bool bHasResolvedMoveIntent = bForcedLocomotionFinish || bMoveInputStillHeld;
		bHasMoveInput = bHasResolvedMoveIntent;
		bPrevHasMoveInput = bHasResolvedMoveIntent;
		bResolvedMoveInputLastUpdate = bHasResolvedMoveIntent;
		EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
	}
	else
	{
		EnterGroundMotionMode(EProject_JGroundMotionMode::Idle);
	}

	if (bHadLandingState)
	{
		RemoveOwnedLandingGameplayTag();
	}
}

void UProject_JLocomotionAnimStateComponent::OnJumpTimerFinished()
{
	if (bIsJumping && JumpStartElapsedTime < JumpStartMaxDuration)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				JumpTimerHandle,
				this,
				&UProject_JLocomotionAnimStateComponent::OnJumpTimerFinished,
				FMath::Max(0.01f, JumpStartMaxDuration - JumpStartElapsedTime),
				false);
		}
		return;
	}

	if (bIsJumping && !IsLandingStateActive())
	{
		CompleteJumpStart();
	}
}

void UProject_JLocomotionAnimStateComponent::OnFallOffStartFinished()
{
	FinishFallOffStart();
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
	const bool bSuppressStartFromResidualVelocity = RemoteStopStartSuppressTimeRemaining > 0.0f;
	RemoteStopStartSuppressTimeRemaining = FMath::Max(0.0f, RemoteStopStartSuppressTimeRemaining - DeltaTime);

	RefreshMovementInputState(DeltaTime, MoveInput, true);
	if (bSuppressStartFromResidualVelocity)
	{
		bPendingStartRequest = false;
		bHasMoveInput = false;
		bPrevHasMoveInput = false;
		bResolvedMoveInputLastUpdate = false;
		MoveInputSize = 0.0f;
		MoveInputHeldTime = 0.0f;
	}

	UpdateGroundMotionModeFromInput(DeltaTime, MoveInput, false);
}

void UProject_JLocomotionAnimStateComponent::RefreshMovementInputState(float DeltaTime, const FVector2D& MoveInput, bool bTrackTurnAngle)
{
	bPrevHasMoveInput = bResolvedMoveInputLastUpdate;
	MoveInputSize = MoveInput.Size();
	bHasMoveInput = MoveInputSize > MoveInputDeadZone;
	MoveInputHeldTime = bHasMoveInput ? MoveInputHeldTime + DeltaTime : 0.0f;
	MoveInputTurnAngle = 0.0f;
	bSharpTurnRequested = false;

	if (bTrackTurnAngle && bHasMoveInput && bPrevHasMoveInput && PreviousMoveInputForTurn.Size() > MoveInputDeadZone)
	{
		const FVector2D PreviousDirection = PreviousMoveInputForTurn.GetSafeNormal();
		const FVector2D CurrentDirection = MoveInput.GetSafeNormal();
		const float Dot = FMath::Clamp(FVector2D::DotProduct(PreviousDirection, CurrentDirection), -1.0f, 1.0f);
		const float Cross = PreviousDirection.Y * CurrentDirection.X - PreviousDirection.X * CurrentDirection.Y;
		MoveInputTurnAngle = FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
	}
}

bool UProject_JLocomotionAnimStateComponent::TryFinishLandingFromMovementInput(const FVector2D& MoveInput, bool bAllowSprintTurnCancel)
{
	if (IsLandingStateActive())
	{
		if (!bLandWasMoving && bHasMoveInput)
		{
			bLandWasMoving = true;
			DispatchLandingCancelForAnimation();
			FinishLandingImmediately();
			return true;
		}

		if (bLandWasMoving && !bHasMoveInput)
		{
			bLandWasMoving = false;
			bLandWasSprinting = false;
			DispatchLandingCancelForAnimation();
			FinishLandingImmediately();
			return true;
		}
	}

	if (!bAllowSprintTurnCancel ||
		!IsLandingStateActive() ||
		!bLandWasSprinting ||
		!bWantsSprint ||
		!bHasMoveInput ||
		LandingElapsedTime < SprintLandingTurnCancelMinTime)
	{
		return false;
	}

	const FVector CurrentLandingMoveWorldDirection = CalculateMoveWorldDirection(MoveInput);
	const FVector ReferenceLandingMoveWorldDirection = !InitialLandingMoveWorldDirection.IsNearlyZero()
		? InitialLandingMoveWorldDirection
		: PreviousLandingMoveWorldDirection;

	if (!ReferenceLandingMoveWorldDirection.IsNearlyZero() && !CurrentLandingMoveWorldDirection.IsNearlyZero())
	{
		const float DirectionDot = FMath::Clamp(FVector::DotProduct(ReferenceLandingMoveWorldDirection, CurrentLandingMoveWorldDirection), -1.0f, 1.0f);
		const float DirectionAngle = FMath::RadiansToDegrees(FMath::Acos(DirectionDot));
		if (DirectionAngle >= SprintLandingTurnCancelAngle)
		{
			DispatchLandingCancelForAnimation();
			FinishLandingImmediately();
			return true;
		}
	}

	if (const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner())
	{
		const float CurrentActorYaw = PlayerOwner->GetActorRotation().Yaw;
		const float InitialYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(InitialLandingActorYaw, CurrentActorYaw));
		const float PreviousYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousLandingActorYaw, CurrentActorYaw));
		if (FMath::Max(InitialYawDelta, PreviousYawDelta) >= SprintLandingTurnCancelAngle)
		{
			DispatchLandingCancelForAnimation();
			FinishLandingImmediately();
			return true;
		}

		PreviousLandingActorYaw = CurrentActorYaw;
	}

	PreviousLandingMoveWorldDirection = CurrentLandingMoveWorldDirection;
	return false;
}

void UProject_JLocomotionAnimStateComponent::UpdateGroundMotionModeFromInput(float DeltaTime, const FVector2D& MoveInput, bool bAllowSharpTurn)
{
	const bool bCanRequestGroundMove = !bIsInAir && !bIsLanding && !bIsJumping && !bIsFallOffStart;
	if (!bCanRequestGroundMove)
	{
		EnterGroundMotionMode(EProject_JGroundMotionMode::Idle);
		bResolvedMoveInputLastUpdate = bHasMoveInput;
		PreviousMoveInputForTurn = bHasMoveInput ? MoveInput : FVector2D::ZeroVector;
		bPendingStartRequest = false;
		bPendingStopRequest = false;
		return;
	}

	const bool bStartEdge = bPendingStartRequest || (bHasMoveInput && !bPrevHasMoveInput);
	const bool bStopEdge = bPendingStopRequest || (!bHasMoveInput && bPrevHasMoveInput && GroundSpeed > StopIntentSpeedThreshold);
	bPendingStartRequest = false;
	bPendingStopRequest = false;

	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	bSharpTurnRequested =
		bAllowSharpTurn &&
		PlayerOwner &&
		PlayerOwner->IsSprintLocomotionAllowed() &&
		bHasMoveInput &&
		bPrevHasMoveInput &&
		GroundSpeed >= SharpTurnMinSpeed &&
		FMath::Abs(MoveInputTurnAngle) >= SharpTurnAngleThreshold;

	if (bStartEdge)
	{
		EnterGroundMotionMode(EProject_JGroundMotionMode::Start);
	}
	else if (bStopEdge)
	{
		StopElapsedTime = 0.0f;
		EnterGroundMotionMode(EProject_JGroundMotionMode::Stop);
	}
	else if (GroundMotionMode == EProject_JGroundMotionMode::Start)
	{
		if (bWantsSprint && bHasMoveInput)
		{
			bStartWasSprinting = true;
		}

		bool bRemoteStartTurnExitRequested = false;
		if (!bAllowSharpTurn && PlayerOwner)
		{
			FVector CurrentHorizontalVelocity = PlayerOwner->GetVelocity();
			CurrentHorizontalVelocity.Z = 0.0f;
			const bool bHasRemoteMoveDirection = CurrentHorizontalVelocity.SizeSquared() > FMath::Square(RemoteMoveSpeedThreshold);
			const FVector CurrentRemoteMoveWorldDirection = bHasRemoteMoveDirection
				? CurrentHorizontalVelocity.GetSafeNormal()
				: FVector::ZeroVector;
			const float CurrentRemoteActorYaw = PlayerOwner->GetActorRotation().Yaw;

			if (bHasRemoteStartTurnReference)
			{
				if (bHasRemoteMoveDirection && !RemoteStartPreviousMoveWorldDirection.IsNearlyZero())
				{
					const float DirectionDot = FMath::Clamp(FVector::DotProduct(RemoteStartPreviousMoveWorldDirection, CurrentRemoteMoveWorldDirection), -1.0f, 1.0f);
					const float DirectionAngle = FMath::RadiansToDegrees(FMath::Acos(DirectionDot));
					bRemoteStartTurnExitRequested = DirectionAngle >= RemoteStartTurnExitAngle;
				}

				const float ActorYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(RemoteStartPreviousActorYaw, CurrentRemoteActorYaw));
				bRemoteStartTurnExitRequested = bRemoteStartTurnExitRequested || ActorYawDelta >= RemoteStartTurnExitAngle;
			}

			RemoteStartPreviousMoveWorldDirection = bHasRemoteMoveDirection ? CurrentRemoteMoveWorldDirection : FVector::ZeroVector;
			RemoteStartPreviousActorYaw = CurrentRemoteActorYaw;
			bHasRemoteStartTurnReference = true;
		}

		if (!bHasMoveInput)
		{
			EnterGroundMotionMode(
				GroundSpeed > StopIntentSpeedThreshold
					? EProject_JGroundMotionMode::Stop
					: EProject_JGroundMotionMode::Idle);
		}
		else if (!bAllowSharpTurn && (bRemoteStartTurnExitRequested || FMath::Abs(MoveInputTurnAngle) >= RemoteStartTurnExitAngle))
		{
			EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
		}
		else if (GroundMotionModeElapsedTime >= StartFallbackDuration)
		{
			EnterGroundMotionMode(bHasMoveInput ? EProject_JGroundMotionMode::Locomotion : EProject_JGroundMotionMode::Idle);
		}
		else
		{
			RefreshGroundMotionFlags();
		}
	}
	else if (GroundMotionMode == EProject_JGroundMotionMode::Stop)
	{
		StopElapsedTime += DeltaTime;
		if (bHasMoveInput)
		{
			EnterGroundMotionMode(EProject_JGroundMotionMode::Start);
		}
		else if (StopElapsedTime >= StopFallbackDuration)
		{
			EnterGroundMotionMode(EProject_JGroundMotionMode::Idle);
		}
		else
		{
			RefreshGroundMotionFlags();
		}
	}
	else
	{
		EnterGroundMotionMode(
			bHasMoveInput || GroundSpeed > IdleSpeedThreshold
				? EProject_JGroundMotionMode::Locomotion
				: EProject_JGroundMotionMode::Idle);
	}

	PreviousMoveInputForTurn = bHasMoveInput ? MoveInput : FVector2D::ZeroVector;
	bResolvedMoveInputLastUpdate = bHasMoveInput;
}

void UProject_JLocomotionAnimStateComponent::UpdateCombatMovementState(const FVector& HorizontalVelocity)
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		MovementDirection = 0.0f;
		CombatInputForward = 0.0f;
		CombatInputRight = 0.0f;
		CombatForwardSpeed = 0.0f;
		CombatRightSpeed = 0.0f;
		return;
	}

	if (!PlayerOwner->IsCombatModeActive() || !bHasMoveInput)
	{
		MovementDirection = 0.0f;
		CombatInputForward = 0.0f;
		CombatInputRight = 0.0f;
		CombatForwardSpeed = 0.0f;
		CombatRightSpeed = 0.0f;
		return;
	}

	if (bUsingLocalInputState)
	{
		const FVector2D CombatMoveInput = CachedMoveInput.GetClampedToMaxSize(1.0f);
		const UCharacterMovementComponent* MovementComponent = GetCachedMovementComponent();
		const float DesiredSpeed = MovementComponent ? MovementComponent->MaxWalkSpeed : PlayerOwner->WalkSpeed;
		CombatInputRight = CombatMoveInput.X;
		CombatInputForward = CombatMoveInput.Y;
		CombatRightSpeed = CombatMoveInput.X * DesiredSpeed;
		CombatForwardSpeed = CombatMoveInput.Y * DesiredSpeed;
		MovementDirection = FMath::RadiansToDegrees(FMath::Atan2(CombatMoveInput.X, CombatMoveInput.Y));
		return;
	}

	const FVector Forward = PlayerOwner->GetActorForwardVector();
	const FVector Right = PlayerOwner->GetActorRightVector();
	CombatForwardSpeed = FVector::DotProduct(HorizontalVelocity, Forward);
	CombatRightSpeed = FVector::DotProduct(HorizontalVelocity, Right);

	const UCharacterMovementComponent* MovementComponent = GetCachedMovementComponent();
	const float MaxSpeed = MovementComponent ? FMath::Max(MovementComponent->MaxWalkSpeed, 1.0f) : FMath::Max(PlayerOwner->WalkSpeed, 1.0f);
	CombatInputForward = FMath::Clamp(CombatForwardSpeed / MaxSpeed, -1.0f, 1.0f);
	CombatInputRight = FMath::Clamp(CombatRightSpeed / MaxSpeed, -1.0f, 1.0f);
	MovementDirection = FMath::RadiansToDegrees(FMath::Atan2(CombatRightSpeed, CombatForwardSpeed));
}

void UProject_JLocomotionAnimStateComponent::DispatchLandingCancelForAnimation()
{
	if (!ShouldUseLocalInputState() || bLandingCancelEventDispatched)
	{
		return;
	}

	if (AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner())
	{
		bLandingCancelEventDispatched = true;
		PlayerOwner->NotifyLandingCancelledForAnimation();
	}
}

void UProject_JLocomotionAnimStateComponent::AddOwnedInAirGameplayTag()
{
	if (bAppliedInAirGameplayTag)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetCachedAbilitySystemComponent())
	{
		ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir);
		bAppliedInAirGameplayTag = true;
	}
}

void UProject_JLocomotionAnimStateComponent::RemoveOwnedInAirGameplayTag()
{
	if (!bAppliedInAirGameplayTag)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetCachedAbilitySystemComponent())
	{
		ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir);
	}
	bAppliedInAirGameplayTag = false;
}

void UProject_JLocomotionAnimStateComponent::AddOwnedLandingGameplayTag()
{
	if (bAppliedLandingGameplayTag)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetCachedAbilitySystemComponent())
	{
		ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_Landing);
		bAppliedLandingGameplayTag = true;
	}
}

void UProject_JLocomotionAnimStateComponent::RemoveOwnedLandingGameplayTag()
{
	if (!bAppliedLandingGameplayTag)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetCachedAbilitySystemComponent())
	{
		ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_Landing);
	}
	bAppliedLandingGameplayTag = false;
}

void UProject_JLocomotionAnimStateComponent::ClearOwnedMovementGameplayTags()
{
	RemoveOwnedInAirGameplayTag();
	RemoveOwnedLandingGameplayTag();
}

void UProject_JLocomotionAnimStateComponent::ClearMovementRequests()
{
	bStartRequested = false;
	bPendingStartRequest = false;
	bPendingStopRequest = false;
	bPendingGroundStartFinish = false;
	bGroundStartFinishPendingExit = false;
	bStopFinishPendingExit = false;
	bResolvedMoveInputLastUpdate = false;
	MoveInputHeldTime = 0.0f;
	StopElapsedTime = 0.0f;
	bSharpTurnRequested = false;
	MoveInputTurnAngle = 0.0f;
	EnterGroundMotionMode(EProject_JGroundMotionMode::Idle);
}

void UProject_JLocomotionAnimStateComponent::ClearTransientAnimationRequests()
{
	bSharpTurnRequested = false;
	bStartRequested = false;
	bPendingStartRequest = false;
	bPendingStopRequest = false;
	bPendingGroundStartFinish = false;
	bGroundStartFinishPendingExit = false;
	bStopFinishPendingExit = false;
	bJumpStartFinishPendingExit = false;
	bFallOffStartFinishPendingExit = false;
	bLandingFinishPendingExit = false;
	bLandingCancelEventDispatched = false;
	bRealLandingEventRequested = false;
	bResolvedMoveInputLastUpdate = bHasMoveInput;
	EnterGroundMotionMode(bHasMoveInput ? EProject_JGroundMotionMode::Locomotion : EProject_JGroundMotionMode::Idle);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JumpStartExitTimerHandle);
		World->GetTimerManager().ClearTimer(FallOffStartExitTimerHandle);
	}
	ClearLandingTimers();
}
