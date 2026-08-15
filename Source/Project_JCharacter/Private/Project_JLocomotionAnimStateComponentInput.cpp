// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JLocomotionAnimStateComponent.h"


void UProject_JLocomotionAnimStateComponent::HandleReplicatedMoveStarted(bool bWasSprintingForStart)
{
	if (ShouldUseLocalInputState())
	{
		return;
	}

	ClearRemoteMoveStartTransientState();
	if (TryFinishLandingForReplicatedMoveStart(bWasSprintingForStart))
	{
		return;
	}

	if (TryPromoteReplicatedStartToLocomotion())
	{
		return;
	}

	if (ShouldIgnoreRedundantReplicatedMoveStart())
	{
		return;
	}

	QueueReplicatedMoveStart(bWasSprintingForStart);
}

void UProject_JLocomotionAnimStateComponent::HandleReplicatedMoveStopped()
{
	if (ShouldUseLocalInputState())
	{
		return;
	}
	QueueReplicatedMoveStop();
	MarkRemoteMoveReleasedIfAirborne();
	TryFinishLandingForReplicatedMoveStop();
}

void UProject_JLocomotionAnimStateComponent::ClearRemoteMoveStartTransientState()
{
	RemoteStopStartSuppressTimeRemaining = 0.0f;
	bRemoteMoveReleasedWhileAirborne = false;
	bRemoteStopVisualIntentActive = false;
	bLandingIgnoresRemoteGroundSpeed = false;
}

bool UProject_JLocomotionAnimStateComponent::TryFinishLandingForReplicatedMoveStart(bool bWasSprintingForStart)
{
	if (!IsLandingStateActive())
	{
		return false;
	}

	bStartWasSprinting = bWasSprintingForStart;
	bWantsSprint = bWasSprintingForStart;
	bHasMoveInput = true;
	bPrevHasMoveInput = true;
	bResolvedMoveInputLastUpdate = true;
	bLandWasMoving = true;
	bPendingStartRequest = false;
	bForceLandingFinishToLocomotion = true;
	FinishLandingImmediately();
	return true;
}

bool UProject_JLocomotionAnimStateComponent::TryPromoteReplicatedStartToLocomotion()
{
	if (GroundMotionMode != EProject_JGroundMotionMode::Start)
	{
		return false;
	}

	bPendingStartRequest = false;
	EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
	return true;
}

bool UProject_JLocomotionAnimStateComponent::ShouldIgnoreRedundantReplicatedMoveStart() const
{
	return
		GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		(bHasMoveInput || GroundSpeed > IdleSpeedThreshold);
}

void UProject_JLocomotionAnimStateComponent::QueueReplicatedMoveStart(bool bWasSprintingForStart)
{
	bStartWasSprinting = bWasSprintingForStart;
	bPendingStartRequest = true;
}

void UProject_JLocomotionAnimStateComponent::QueueReplicatedMoveStop()
{
	ClearResolvedMoveInputState();
	bPendingStartRequest = false;
	bPendingStopRequest = true;
	bRemoteStopVisualIntentActive = true;
	RemoteStopStartSuppressTimeRemaining = FMath::Max(RemoteStopStartSuppressTimeRemaining, RemoteStopStartSuppressDuration);
}

void UProject_JLocomotionAnimStateComponent::MarkRemoteMoveReleasedIfAirborne()
{
	const bool bInAirState = bIsInAir || bIsPhysicallyInAir || bIsJumping || bIsFallOffStart;
	if (bInAirState)
	{
		bRemoteMoveReleasedWhileAirborne = true;
	}
}

void UProject_JLocomotionAnimStateComponent::TryFinishLandingForReplicatedMoveStop()
{
	if (IsLandingStateActive())
	{
		// A replicated MoveStop is the remote equivalent of releasing local
		// movement input.  Do not reselect/cancel the in-flight landing pose;
		// only prevent residual replicated velocity from choosing Locomotion
		// when the landing naturally completes.
		bLandingIgnoresRemoteGroundSpeed = true;
		if (bLandWasMoving && bLandingReceivedPostTouchdownMoveInput)
		{
			bForceLandingFinishToStop = true;
			bLandingExitStopWasSprinting = bLandWasSprinting;
			DispatchLandingCancelForAnimation();
			FinishLandingImmediately();
		}
	}
}

void UProject_JLocomotionAnimStateComponent::SetMoveInput(const FVector2D& InMoveInput)
{
	const bool bHadMoveInput = HasAnyMoveInputState();
	CachedMoveInput = InMoveInput.GetClampedToMaxSize(1.0f);
	QueueLocalMoveStartIfNeeded(bHadMoveInput, HasCachedMoveInput());
}

void UProject_JLocomotionAnimStateComponent::ClearMoveInput()
{
	const bool bHadMoveInput = HasAnyMoveInputState();
	ClearLocalMoveInputState();
	QueueLocalMoveStopIfNeeded(bHadMoveInput);
}

void UProject_JLocomotionAnimStateComponent::HandleSprintStarted()
{
	bSprintInputHeld = true;
	bWantsSprint = true;
	UpdateSprintLocomotionRequest();
	if (GroundMotionMode == EProject_JGroundMotionMode::Start)
	{
		bStartWasSprinting = true;
	}
}

void UProject_JLocomotionAnimStateComponent::HandleSprintStopped()
{
	const bool bWasSprintingAtRelease =
		bUseSprintLocomotion ||
		bWantsSprint ||
		bSprintInputHeld ||
		GroundSpeed >= SprintLocomotionSpeedThreshold;
	if (bWasSprintingAtRelease)
	{
		SprintStopMemoryTimeRemaining = SprintStopMemoryDuration;
	}

	bSprintInputHeld = false;
	bWantsSprint = false;
	bUseSprintLocomotion = false;

	if (GroundMotionMode == EProject_JGroundMotionMode::Start)
	{
		bStartWasSprinting = false;
		EnterGroundMotionMode(ResolveGroundMotionModeAfterSprintStop());
	}

	TryFinishSprintLandingAfterSprintStop();
}

bool UProject_JLocomotionAnimStateComponent::HasAnyMoveInputState() const
{
	return HasCachedMoveInput() || bHasMoveInput || bResolvedMoveInputLastUpdate;
}

bool UProject_JLocomotionAnimStateComponent::HasCachedMoveInput() const
{
	return CachedMoveInput.Size() > MoveInputDeadZone;
}

void UProject_JLocomotionAnimStateComponent::QueueLocalMoveStartIfNeeded(bool bHadMoveInput, bool bHasNewMoveInput)
{
	if (bHasNewMoveInput && !bHadMoveInput)
	{
		bPendingStartRequest = true;
	}
}

void UProject_JLocomotionAnimStateComponent::ClearLocalMoveInputState()
{
	// Keyboard direction changes can contain a one-frame all-keys-up gap.
	// Preserve the outgoing intent solely for Pivot angle classification; the
	// actual cached input is still cleared immediately, so movement/Stop logic
	// retains its existing behavior.
	if (CachedMoveInput.Size() > MoveInputDeadZone)
	{
		PivotInputReleaseReference = CachedMoveInput.GetClampedToMaxSize(1.0f);
		PivotInputReleaseElapsedTime = 0.0f;
		PivotInputReleaseSpeedReference = GroundSpeed;
		PivotInputCandidate = FVector2D::ZeroVector;
		bPivotInputReleaseSequenceActive = true;
	}
	CachedMoveInput = FVector2D::ZeroVector;
	MoveInputSize = 0.0f;
	MoveInputHeldTime = 0.0f;
	bHasMoveInput = false;
	PreviousMoveInputForTurn = FVector2D::ZeroVector;
}

void UProject_JLocomotionAnimStateComponent::QueueLocalMoveStopIfNeeded(bool bHadMoveInput)
{
	if (bHadMoveInput)
	{
		bPendingStopRequest = true;
	}
}

void UProject_JLocomotionAnimStateComponent::UpdateSprintLocomotionRequest()
{
	bUseSprintLocomotion =
		GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		(HasCachedMoveInput() || GroundSpeed > IdleSpeedThreshold);
}

EProject_JGroundMotionMode UProject_JLocomotionAnimStateComponent::ResolveGroundMotionModeAfterSprintStop() const
{
	if (HasCachedMoveInput() || bHasMoveInput)
	{
		return EProject_JGroundMotionMode::Locomotion;
	}

	return GroundSpeed > StopIntentSpeedThreshold
		? EProject_JGroundMotionMode::Stop
		: EProject_JGroundMotionMode::Idle;
}

void UProject_JLocomotionAnimStateComponent::TryFinishSprintLandingAfterSprintStop()
{
	// Sprint release must not turn a selected Sprint landing into a Run/Idle
	// transition mid-pose.  The regular landing completion will evaluate the
	// current input and enter Idle or Locomotion as appropriate.
}
