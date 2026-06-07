// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JLocomotionAnimStateComponent.h"

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
		ScheduleStartAutoPromote();
		break;
	case EProject_JGroundMotionMode::Stop:
		ClearStartAutoPromoteTimer();
		EnterStopGroundMotionMode();
		ClearRemoteStartTurnReference();
		break;
	case EProject_JGroundMotionMode::Idle:
	case EProject_JGroundMotionMode::Locomotion:
		ClearStartAutoPromoteTimer();
		ClearGroundMotionSprintTransitionState();
		ClearRemoteStartTurnReference();
		break;
	default:
		ClearStartAutoPromoteTimer();
		ClearRemoteStartTurnReference();
		break;
	}
}

void UProject_JLocomotionAnimStateComponent::EnterStartGroundMotionMode()
{
	bStartWasSprinting = IsSprintRequestedForAnimation() || GroundSpeed >= SprintLocomotionSpeedThreshold;
	CacheRemoteStartTurnReference();
}

void UProject_JLocomotionAnimStateComponent::EnterStopGroundMotionMode()
{
	bStopWasSprinting = bUseSprintLocomotion || bWantsSprint || GroundSpeed >= SprintLocomotionSpeedThreshold;
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
	if (!bHasMoveInput || GroundMotionModeElapsedTime < StartResponsiveTurnExitMinTime)
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

void UProject_JLocomotionAnimStateComponent::ScheduleStartAutoPromote()
{
	UWorld* World = GetWorld();
	if (!World || StartAutoPromoteDelay <= 0.0f)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(StartAutoPromoteTimerHandle);
	World->GetTimerManager().SetTimer(
		StartAutoPromoteTimerHandle,
		this,
		&UProject_JLocomotionAnimStateComponent::PromoteStartToResolvedGroundMotion,
		StartAutoPromoteDelay,
		false);
}

void UProject_JLocomotionAnimStateComponent::ClearStartAutoPromoteTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StartAutoPromoteTimerHandle);
	}
}

void UProject_JLocomotionAnimStateComponent::PromoteStartToResolvedGroundMotion()
{
	if (GroundMotionMode != EProject_JGroundMotionMode::Start)
	{
		return;
	}

	const bool bHasMovementIntent =
		bHasMoveInput ||
		GetMovementInputForState().SizeSquared() > FMath::Square(MoveInputDeadZone) ||
		GroundSpeed > IdleSpeedThreshold;

	if (bHasMovementIntent)
	{
		EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
		return;
	}

	EnterGroundMotionMode(
		GroundSpeed > StopExitSpeedThreshold
			? EProject_JGroundMotionMode::Stop
			: EProject_JGroundMotionMode::Idle);
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

	RemoteStartPreviousMoveWorldDirection = bHasRemoteMoveDirection ? CurrentRemoteMoveWorldDirection : FVector::ZeroVector;
	RemoteStartPreviousActorYaw = CurrentRemoteActorYaw;
	bHasRemoteStartTurnReference = true;

	return bRemoteStartTurnExitRequested || FMath::Abs(MoveInputTurnAngle) >= RemoteStartTurnExitAngle;
}

void UProject_JLocomotionAnimStateComponent::UpdateStartGroundMotionMode(const FVector2D& MoveInput, bool bAllowSharpTurn)
{
	if (bWantsSprint && bHasMoveInput)
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

	const bool bCanExitStart = GroundMotionModeElapsedTime >= StartMinDuration;
	const bool bCanExitReleasedStart = GroundMotionModeElapsedTime >= StartInputReleaseExitMinTime;
	const bool bRemoteResponsiveTurnExitRequested =
		!bAllowSharpTurn &&
		bStartTurnExitRequested &&
		GroundMotionModeElapsedTime >= StartResponsiveTurnExitMinTime;

	if (!bHasMoveInput)
	{
		if (bCanExitReleasedStart || bCanExitStart)
		{
			EnterGroundMotionMode(
				GroundSpeed > StopExitSpeedThreshold
					? EProject_JGroundMotionMode::Stop
					: EProject_JGroundMotionMode::Idle);
		}
		else
		{
			RefreshGroundMotionFlags();
		}
	}
	else if (bResponsiveTurnExitRequested)
	{
		EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
	}
	else if (bRemoteResponsiveTurnExitRequested)
	{
		EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
	}
	else if (bCanExitStart && (bStartTurnExitRequested || GroundSpeed > DerivedStartMaxGroundSpeed))
	{
		EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
	}
	else if (GroundMotionModeElapsedTime >= StartMaxDuration)
	{
		EnterGroundMotionMode(bHasMoveInput ? EProject_JGroundMotionMode::Locomotion : EProject_JGroundMotionMode::Idle);
	}
	else
	{
		RefreshGroundMotionFlags();
	}
}

void UProject_JLocomotionAnimStateComponent::UpdateStopGroundMotionMode(float DeltaTime)
{
	StopElapsedTime += DeltaTime;
	if (bHasMoveInput)
	{
		EnterGroundMotionMode(EProject_JGroundMotionMode::Start);
	}
	else if (StopElapsedTime >= StopMinDuration && GroundSpeed <= StopExitSpeedThreshold)
	{
		EnterGroundMotionMode(EProject_JGroundMotionMode::Idle);
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

	const bool bStartEdge = bPendingStartRequest || (bHasMoveInput && !bPrevHasMoveInput);
	const bool bStopEdge = bPendingStopRequest || (!bHasMoveInput && bPrevHasMoveInput && GroundSpeed > StopIntentSpeedThreshold);
	bPendingStartRequest = false;
	bPendingStopRequest = false;

	UpdateSharpTurnRequest(bAllowSharpTurn);

	if (bStopEdge && (!bStartEdge || !bHasMoveInput))
	{
		StopElapsedTime = 0.0f;
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
		StopElapsedTime = 0.0f;
		EnterGroundMotionMode(EProject_JGroundMotionMode::Stop);
	}
	else if (GroundMotionMode == EProject_JGroundMotionMode::Start)
	{
		UpdateStartGroundMotionMode(MoveInput, bAllowSharpTurn);
	}
	else if (GroundMotionMode == EProject_JGroundMotionMode::Stop)
	{
		UpdateStopGroundMotionMode(DeltaTime);
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
