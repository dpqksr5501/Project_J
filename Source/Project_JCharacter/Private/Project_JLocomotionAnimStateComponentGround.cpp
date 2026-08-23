// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JLocomotionAnimStateComponent.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Project_JPlayerCharacter.h"

void UProject_JLocomotionAnimStateComponent::EnterGroundMotionMode(EProject_JGroundMotionMode NewMode)
{
	if (GroundMotionMode == NewMode)
	{
		RefreshGroundMotionFlags();
		return;
	}

	GroundMotionMode = NewMode;
	GroundMotionModeElapsedTime = 0.0f;
	ResetGroundMotionTransitionRequests();
	HandleGroundMotionModeEntered(NewMode);
	RefreshGroundMotionFlags();
}

void UProject_JLocomotionAnimStateComponent::ResetGroundMotionTransitionRequests()
{
}

void UProject_JLocomotionAnimStateComponent::HandleGroundMotionModeEntered(EProject_JGroundMotionMode NewMode)
{
	switch (NewMode)
	{
	case EProject_JGroundMotionMode::Start:
		EnterStartGroundMotionMode();
		break;
	case EProject_JGroundMotionMode::Stop:
		EnterStopGroundMotionMode();
		ClearRemoteStartTurnReference();
		break;
	case EProject_JGroundMotionMode::Idle:
	case EProject_JGroundMotionMode::Locomotion:
		ClearGroundMotionSprintTransitionState();
		ClearRemoteStartTurnReference();
		break;
	default:
		ClearRemoteStartTurnReference();
		break;
	}
}

void UProject_JLocomotionAnimStateComponent::EnterStartGroundMotionMode()
{
	if (!bUsingLocalInputState && bHasReplicatedStartGait)
	{
		bStartWasSprinting = bReplicatedStartWasSprinting;
		bHasReplicatedStartGait = false;
	}
	else
	{
		bStartWasSprinting = IsSprintRequestedForAnimation() || GroundSpeed >= SprintLocomotionSpeedThreshold;
	}
	CacheRemoteStartTurnReference();
}

void UProject_JLocomotionAnimStateComponent::EnterStopGroundMotionMode()
{
	if (!bUsingLocalInputState && bHasReplicatedStopGait)
	{
		bStopWasSprinting = bReplicatedStopWasSprinting;
		bHasReplicatedStopGait = false;
	}
	else
	{
		bStopWasSprinting =
			bUseSprintLocomotion ||
			bWantsSprint ||
			GroundSpeed >= SprintLocomotionSpeedThreshold ||
			SprintStopMemoryTimeRemaining > 0.0f;
	}
	SprintStopMemoryTimeRemaining = 0.0f;
}

void UProject_JLocomotionAnimStateComponent::ClearGroundMotionSprintTransitionState()
{
	bStartWasSprinting = false;
	bStopWasSprinting = false;
	bStartRequested = false;
	bUseStartDatabase = false;
	bGroundStartFinished = true;
}

void UProject_JLocomotionAnimStateComponent::CacheRemoteStartTurnReference()
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return;
	}

	FVector HorizontalVelocity = PlayerOwner->GetVelocity();
	HorizontalVelocity.Z = 0.0f;
	RemoteStartPreviousMoveWorldDirection = HorizontalVelocity.SizeSquared() > FMath::Square(RemoteMoveSpeedThreshold)
		? HorizontalVelocity.GetSafeNormal()
		: FVector::ZeroVector;
	RemoteStartPreviousActorYaw = PlayerOwner->GetActorRotation().Yaw;
	StartPreviousControlYaw = PlayerOwner->GetControlRotation().Yaw;
	bHasRemoteStartTurnReference = true;
}

void UProject_JLocomotionAnimStateComponent::ClearRemoteStartTurnReference()
{
	RemoteStartPreviousMoveWorldDirection = FVector::ZeroVector;
	RemoteStartPreviousActorYaw = 0.0f;
	StartPreviousControlYaw = 0.0f;
	bHasRemoteStartTurnReference = false;
}

bool UProject_JLocomotionAnimStateComponent::UpdateLocalStartTurnExitRequest()
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return FMath::Abs(MoveInputTurnAngle) >= StartTurnExitAngle;
	}

	bool bStartTurnExitRequested = FMath::Abs(MoveInputTurnAngle) >= StartTurnExitAngle;
	const float CurrentActorYaw = PlayerOwner->GetActorRotation().Yaw;
	const float CurrentControlYaw = PlayerOwner->GetControlRotation().Yaw;
	if (bHasRemoteStartTurnReference)
	{
		const float ActorYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(RemoteStartPreviousActorYaw, CurrentActorYaw));
		const float ControlYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(StartPreviousControlYaw, CurrentControlYaw));
		bStartTurnExitRequested =
			bStartTurnExitRequested ||
			ActorYawDelta >= StartTurnExitAngle ||
			ControlYawDelta >= StartTurnExitAngle;
	}

	bHasRemoteStartTurnReference = true;
	return bStartTurnExitRequested;
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

void UProject_JLocomotionAnimStateComponent::UpdateSharpTurnRequest(bool bAllowSharpTurn)
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	bSharpTurnRequested =
		bAllowSharpTurn &&
		PlayerOwner &&
		PlayerOwner->IsSprintLocomotionAllowed() &&
		bHasMoveInput &&
		bPrevHasMoveInput &&
		GroundSpeed >= SharpTurnMinSpeed &&
		FMath::Abs(MoveInputTurnAngle) >= SharpTurnAngleThreshold;
}

bool UProject_JLocomotionAnimStateComponent::ShouldInterruptStartForResponsiveTurn(
	const FVector2D& MoveInput,
	bool bAllowLocalControlYaw) const
{
	if (!bHasMoveInput)
	{
		return false;
	}

	const float ResponsiveExitAngle = FMath::Max(StartResponsiveTurnExitAngle, StartTurnExitAngle);
	if (FMath::Abs(MoveInputTurnAngle) >= ResponsiveExitAngle)
	{
		return true;
	}

	if (bSharpTurnRequested)
	{
		return true;
	}

	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return false;
	}

	return bAllowLocalControlYaw && HasLocalStartResponsiveTurn(ResponsiveExitAngle);
}

bool UProject_JLocomotionAnimStateComponent::HasLocalStartResponsiveTurn(float AngleThreshold) const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner || !bHasRemoteStartTurnReference)
	{
		return false;
	}

	const float CurrentActorYaw = PlayerOwner->GetActorRotation().Yaw;
	const float CurrentControlYaw = PlayerOwner->GetControlRotation().Yaw;
	const float ActorYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(RemoteStartPreviousActorYaw, CurrentActorYaw));
	const float ControlYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(StartPreviousControlYaw, CurrentControlYaw));
	return ActorYawDelta >= AngleThreshold || ControlYawDelta >= AngleThreshold;
}

bool UProject_JLocomotionAnimStateComponent::UpdateRemoteStartTurnExitRequest(const AProject_JPlayerCharacter& PlayerOwner, const FVector2D& MoveInput)
{
	FVector CurrentHorizontalVelocity = PlayerOwner.GetVelocity();
	CurrentHorizontalVelocity.Z = 0.0f;
	const bool bHasRemoteMoveDirection = CurrentHorizontalVelocity.SizeSquared() > FMath::Square(RemoteMoveSpeedThreshold);
	const FVector CurrentRemoteMoveWorldDirection = bHasRemoteMoveDirection
		? CurrentHorizontalVelocity.GetSafeNormal()
		: FVector::ZeroVector;
	const float CurrentRemoteActorYaw = PlayerOwner.GetActorRotation().Yaw;

	bool bRemoteStartTurnExitRequested = false;
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

	// Preserve the Start-entry reference. Network smoothing deliberately splits a
	// large turn into small frame deltas, so a previous-frame comparison can miss
	// the turn forever. If Start began before velocity was available, latch only
	// the first meaningful direction and keep it immutable afterwards.
	if (RemoteStartPreviousMoveWorldDirection.IsNearlyZero() && bHasRemoteMoveDirection)
	{
		RemoteStartPreviousMoveWorldDirection = CurrentRemoteMoveWorldDirection;
	}
	bHasRemoteStartTurnReference = true;

	return bRemoteStartTurnExitRequested || FMath::Abs(MoveInputTurnAngle) >= RemoteStartTurnExitAngle;
}

void UProject_JLocomotionAnimStateComponent::UpdateStartGroundMotionMode(const FVector2D& MoveInput, bool bAllowSharpTurn)
{
	if (bUsingLocalInputState && bWantsSprint && bHasMoveInput)
	{
		bStartWasSprinting = true;
	}

	const bool bResponsiveTurnExitRequested = ShouldInterruptStartForResponsiveTurn(MoveInput, bAllowSharpTurn);
	bool bStartTurnExitRequested = false;
	if (bAllowSharpTurn)
	{
		bStartTurnExitRequested = UpdateLocalStartTurnExitRequest();
	}
	else
	{
		if (const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner())
		{
			bStartTurnExitRequested = UpdateRemoteStartTurnExitRequest(*PlayerOwner, MoveInput);
		}
		else
		{
			bStartTurnExitRequested = FMath::Abs(MoveInputTurnAngle) >= RemoteStartTurnExitAngle;
		}
	}

	if (!bHasMoveInput)
	{
		// Autonomous input release must select a Stop database immediately. A
		// simulated proxy often has a full-speed replicated sample after a local
		// Start request, however; keep its velocity-driven locomotion alive until
		// the replicated stop intent arrives instead of falsely searching Stop.
		if (!bUsingLocalInputState && GroundSpeed > StopExitSpeedThreshold)
		{
			EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
			return;
		}

		EnterGroundMotionMode(
			GroundSpeed > StopExitSpeedThreshold
				? EProject_JGroundMotionMode::Stop
				: EProject_JGroundMotionMode::Idle);
	}
	else if (bResponsiveTurnExitRequested)
	{
		if (!bUsingLocalInputState)
		{
			++StartResponsiveExitRevision;
		}
		EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
	}
	else if (!bAllowSharpTurn && bStartTurnExitRequested)
	{
		++StartResponsiveExitRevision;
		EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
	}
	else if (bStartTurnExitRequested)
	{
		EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
	}
	else if (const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner())
	{
		const UCharacterMovementComponent* MovementComponent = PlayerOwner->GetCharacterMovement();
		const float CompletionSpeed = MovementComponent
			? FMath::Max(DerivedStartMaxGroundSpeed, MovementComponent->GetMaxSpeed() * StartCompletionSpeedFraction)
			: DerivedStartMaxGroundSpeed;
		if (GroundSpeed >= CompletionSpeed ||
			(!KinematicContext.bIsAccelerating && KinematicContext.PredictedSpeedGain < DerivedStartSpeedGainThreshold))
		{
			EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
		}
		else
		{
			RefreshGroundMotionFlags();
		}
	}
	else if (!KinematicContext.bIsAccelerating && KinematicContext.PredictedSpeedGain < DerivedStartSpeedGainThreshold)
	{
		// The player is still steering but CharacterMovement can no longer gain
		// meaningful speed (for example due to a constrained speed policy). This
		// is the semantic equivalent of GASP's start-to-locomotion handoff, not a
		// fixed animation timeout.
		EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
	}
	else
	{
		RefreshGroundMotionFlags();
	}
}

void UProject_JLocomotionAnimStateComponent::UpdateStopGroundMotionMode()
{
	if (bHasMoveInput)
	{
		EnterGroundMotionMode(EProject_JGroundMotionMode::Start);
	}
	else if (GroundSpeed <= StopExitSpeedThreshold)
	{
		EnterGroundMotionMode(EProject_JGroundMotionMode::Idle);
	}
	else if (!KinematicContext.bIsDecelerating)
	{
		// Do not keep a Stop PSD solely because a non-input source (knockback,
		// moving platform or replicated correction) keeps velocity above the exit
		// threshold. It is not a player stop any more.
		EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
	}
	else
	{
		RefreshGroundMotionFlags();
	}
}


void UProject_JLocomotionAnimStateComponent::UpdateDefaultGroundMotionMode()
{
	EnterGroundMotionMode(
		bHasMoveInput || GroundSpeed > IdleSpeedThreshold
			? EProject_JGroundMotionMode::Locomotion
			: EProject_JGroundMotionMode::Idle);
}

void UProject_JLocomotionAnimStateComponent::UpdateGroundMotionModeFromInput(float DeltaTime, const FVector2D& MoveInput, bool bAllowSharpTurn)
{
	if (!CanRequestGroundMotion())
	{
		ClearGroundMotionInputRequests(MoveInput);
		return;
	}

	const bool bDelayRemoteStopUntilVelocitySettles =
		!bAllowSharpTurn &&
		bRemoteStopVisualIntentActive &&
		bPendingStopRequest &&
		GroundSpeed > RemoteStopEntryMaxSpeed;
	if (bDelayRemoteStopUntilVelocitySettles)
	{
		// The replicated MoveStop reliably tells us the owner released input, but its
		// CharacterMovement velocity can still be a full-speed sample for several
		// frames. Searching a Stop PSD against that sample selects its walking entry.
		// Keep the last cycle pose until the smoothed velocity reaches the authored
		// deceleration window, then consume the pending Stop request below.
		EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
		PreviousMoveInputForTurn = FVector2D::ZeroVector;
		bResolvedMoveInputLastUpdate = false;
		return;
	}

	const bool bStartEdge = bPendingStartRequest || (bHasMoveInput && !bPrevHasMoveInput);
	const bool bStopEdge = bPendingStopRequest || (!bHasMoveInput && bPrevHasMoveInput && GroundSpeed > StopIntentSpeedThreshold);
	bPendingStartRequest = false;
	bPendingStopRequest = false;

	UpdateSharpTurnRequest(bAllowSharpTurn);

	if (bStopEdge && (!bStartEdge || !bHasMoveInput))
	{
		EnterGroundMotionMode(
			GroundSpeed > StopExitSpeedThreshold
				? EProject_JGroundMotionMode::Stop
				: EProject_JGroundMotionMode::Idle);
	}
	else if (bStartEdge)
	{
		EnterGroundMotionMode(EProject_JGroundMotionMode::Start);
	}
	else if (bStopEdge)
	{
		EnterGroundMotionMode(EProject_JGroundMotionMode::Stop);
	}
	else if (GroundMotionMode == EProject_JGroundMotionMode::Start)
	{
		UpdateStartGroundMotionMode(MoveInput, bAllowSharpTurn);
	}
	else if (GroundMotionMode == EProject_JGroundMotionMode::Stop)
	{
		UpdateStopGroundMotionMode();
	}
	else
	{
		UpdateDefaultGroundMotionMode();
	}

	PreviousMoveInputForTurn = bHasMoveInput ? MoveInput : FVector2D::ZeroVector;
	bResolvedMoveInputLastUpdate = bHasMoveInput;
}

bool UProject_JLocomotionAnimStateComponent::CanRequestGroundMotion() const
{
	return !bIsInAir && !bIsLanding && !bIsJumping && !bIsFallOffStart;
}

void UProject_JLocomotionAnimStateComponent::ClearGroundMotionInputRequests(const FVector2D& MoveInput)
{
	EnterGroundMotionMode(EProject_JGroundMotionMode::Idle);
	bResolvedMoveInputLastUpdate = bHasMoveInput;
	PreviousMoveInputForTurn = bHasMoveInput ? MoveInput : FVector2D::ZeroVector;
	bPendingStartRequest = false;
	bPendingStopRequest = false;
}
