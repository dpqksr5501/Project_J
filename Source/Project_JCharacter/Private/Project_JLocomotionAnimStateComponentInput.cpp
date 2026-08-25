// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JLocomotionAnimStateComponent.h"

#include "Project_JPlayerCharacter.h"


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

void UProject_JLocomotionAnimStateComponent::HandleReplicatedMoveStopped(bool bWasSprintingAtStop)
{
	if (ShouldUseLocalInputState())
	{
		return;
	}
	QueueReplicatedMoveStop(bWasSprintingAtStop);
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
	bHasReplicatedStartGait = true;
	bReplicatedStartWasSprinting = bWasSprintingForStart;
	bHasReplicatedStopGait = false;
	bPendingStartRequest = true;
}

void UProject_JLocomotionAnimStateComponent::QueueReplicatedMoveStop(bool bWasSprintingAtStop)
{
	ClearResolvedMoveInputState();
	bPendingStartRequest = false;
	bPendingStopRequest = true;
	bHasReplicatedStartGait = false;
	bHasReplicatedStopGait = true;
	bReplicatedStopWasSprinting = bWasSprintingAtStop;
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
	// A non-zero final Move Action value wins over a stale Completed/Canceled
	// callback from another mapping of that same action.  This is local-input
	// state only; replicated Stop events continue to use QueueReplicatedMoveStop.
	if (HasCachedMoveInput())
	{
		bPendingStopRequest = false;
	}
	// While the Enhanced Input layer is resolving a semantic chord, do not let
	// the aggregate Axis2D value overwrite its final direction. Gameplay still
	// consumes CachedMoveInput immediately below through the normal player path.
	if (!bHasSemanticMoveIntentInput && !bSemanticMoveIntentUpdatePending)
	{
		UpdateLocalMoveIntentSnapshot(CachedMoveInput);
	}
	QueueLocalMoveStartIfNeeded(bHadMoveInput, HasCachedMoveInput());
}

void UProject_JLocomotionAnimStateComponent::ClearMoveInput()
{
	const bool bHadMoveInput = HasAnyMoveInputState();
	ClearLocalMoveInputState();
	QueueLocalMoveStopIfNeeded(bHadMoveInput);
}

void UProject_JLocomotionAnimStateComponent::BeginSemanticMoveIntentUpdate()
{
	bSemanticMoveIntentUpdatePending = true;
	bHasSemanticPivotKinematicCapture = false;
	SemanticPivotKinematicCaptureIntentRevision = INDEX_NONE;
	SemanticPivotKinematicCapturePreviousDirection = FVector::ZeroVector;
	SemanticPivotKinematicCaptureGroundSpeed = 0.0f;

	if (const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner())
	{
		FVector HorizontalVelocity = PlayerOwner->GetVelocity();
		HorizontalVelocity.Z = 0.0f;
		SemanticPivotKinematicCaptureGroundSpeed = HorizontalVelocity.Size2D();
		SemanticPivotKinematicCapturePreviousDirection = HorizontalVelocity.GetSafeNormal2D();
		bHasSemanticPivotKinematicCapture = !SemanticPivotKinematicCapturePreviousDirection.IsNearlyZero();
	}
}

void UProject_JLocomotionAnimStateComponent::SetSemanticMoveIntentInput(const FVector2D& InMoveIntent, const bool bHasActiveIntent)
{
	const int32 PreviousMoveIntentRevision = MoveIntentRevision;
	bSemanticMoveIntentUpdatePending = false;
	bHasSemanticMoveIntentInput = bHasActiveIntent;
	CachedSemanticMoveIntentInput = bHasActiveIntent
		? InMoveIntent.GetClampedToMaxSize(1.0f)
		: FVector2D::ZeroVector;

	if (bHasSemanticMoveIntentInput)
	{
		UpdateLocalMoveIntentSnapshot(CachedSemanticMoveIntentInput);
		if (bHasSemanticPivotKinematicCapture && MoveIntentRevision != PreviousMoveIntentRevision)
		{
			SemanticPivotKinematicCaptureIntentRevision = MoveIntentRevision;
		}
		else
		{
			bHasSemanticPivotKinematicCapture = false;
			SemanticPivotKinematicCaptureIntentRevision = INDEX_NONE;
		}
	}
	else
	{
		bHasSemanticPivotKinematicCapture = false;
		SemanticPivotKinematicCaptureIntentRevision = INDEX_NONE;
	}
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
