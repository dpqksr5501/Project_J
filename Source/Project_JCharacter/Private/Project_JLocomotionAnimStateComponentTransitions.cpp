#include "Project_JLocomotionAnimStateComponent.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Project_JPlayerCharacter.h"

void UProject_JLocomotionAnimStateComponent::HandleJumpStarted()
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner || !CanStartJumpForAnimation())
	{
		return;
	}

	const bool bHadLandingState = IsLandingStateActive();

	BeginJumpStartState();
	ScheduleJumpStartTimeout(FMath::Max(0.1f, JumpStartMaxDuration));

	if (bHadLandingState)
	{
		RemoveOwnedLandingGameplayTag();
	}
	AddOwnedInAirGameplayTag();
}

void UProject_JLocomotionAnimStateComponent::HandleConfirmedRemoteJump(
	int32 Sequence,
	float ServerStartAgeSeconds,
	const FVector& LaunchVelocity)
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner || ShouldUseLocalInputState() || Sequence == LastConfirmedRemoteJumpSequence)
	{
		return;
	}

	LastConfirmedRemoteJumpSequence = Sequence;
	const float ConfirmedElapsedTime = FMath::Max(0.0f, ServerStartAgeSeconds);
	if (bPredictedRemoteJumpStart && bIsJumping)
	{
		JumpStartElapsedTime = FMath::Max(JumpStartElapsedTime, ConfirmedElapsedTime);
		bPredictedRemoteJumpStart = false;
		LastFallSpeed = FMath::Max(LastFallSpeed, FMath::Abs(LaunchVelocity.Z));

		if (JumpStartElapsedTime >= ReplicatedJumpStartDuration)
		{
			CompleteJumpStart();
		}
		else
		{
			ScheduleJumpStartTimeout(
				FMath::Max(0.05f, ReplicatedJumpStartDuration - JumpStartElapsedTime));
		}
		return;
	}

	if (IsLandingStateActive())
	{
		FinishLanding();
	}

	BeginJumpStartState();
	bPredictedRemoteJumpStart = false;
	JumpStartElapsedTime = ConfirmedElapsedTime;
	RemoteAirborneTime = 0.0f;
	LastFallSpeed = FMath::Abs(LaunchVelocity.Z);

	if (JumpStartElapsedTime >= ReplicatedJumpStartDuration)
	{
		CompleteJumpStart();
		return;
	}

	ScheduleJumpStartTimeout(
		FMath::Max(0.05f, ReplicatedJumpStartDuration - JumpStartElapsedTime));
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

	if (bIsJumping && ShouldIgnoreJumpStartLanding(*PlayerOwner))
	{
		RecordIgnoredJumpStartLanding(*PlayerOwner);
		KeepJumpStartAirborneAfterIgnoredLanding();
		return;
	}

	const float ImpactFallSpeed = FMath::Max(LastFallSpeed, FMath::Abs(PlayerOwner->GetVelocity().Z));
	ClearJumpStartTimers();
	bIgnoreNextLandingForJumpStart = false;
	StartLanding(ImpactFallSpeed, true, true);
}

bool UProject_JLocomotionAnimStateComponent::HasRealFallingEvidenceForLanding(const AProject_JPlayerCharacter& PlayerOwner) const
{
	return
		LastFallSpeed >= RemoteLandingMinFallSpeed ||
		VerticalSpeed < -RemoteLandingMinFallSpeed ||
		PlayerOwner.GetVelocity().Z < -RemoteLandingMinFallSpeed;
}

bool UProject_JLocomotionAnimStateComponent::ShouldIgnoreJumpStartLanding(const AProject_JPlayerCharacter& PlayerOwner) const
{
	const bool bIgnoreEarlyJumpStartLanding =
		bIgnoreNextLandingForJumpStart &&
		JumpStartElapsedTime <= IgnoreLandingAfterJumpStartTime &&
		!HasRealFallingEvidenceForLanding(PlayerOwner);

	return bIgnoreEarlyJumpStartLanding || PlayerOwner.GetVelocity().Z > 0.0f;
}

void UProject_JLocomotionAnimStateComponent::RecordIgnoredJumpStartLanding(const AProject_JPlayerCharacter& PlayerOwner)
{
	++IgnoredJumpStartLandingCount;
	LastIgnoredJumpStartLandingElapsedTime = JumpStartElapsedTime;
	LastIgnoredJumpStartLandingVelocityZ = PlayerOwner.GetVelocity().Z;
	LastIgnoredJumpStartLandingFallSpeed = LastFallSpeed;
	LastIgnoredJumpStartLandingVerticalSpeed = VerticalSpeed;
	bLastIgnoredJumpStartLandingHadFallingEvidence = HasRealFallingEvidenceForLanding(PlayerOwner);
}

void UProject_JLocomotionAnimStateComponent::KeepJumpStartAirborneAfterIgnoredLanding()
{
	bIsInAir = true;
	bIsPhysicallyInAir = true;
	bWasInAir = true;
	bIsLanding = false;
	bLandingRequested = false;
	bCanEnterLand = false;
	bCanEnterGround = false;
	bIgnoreNextLandingForJumpStart = false;
}

void UProject_JLocomotionAnimStateComponent::FinishLanding(bool bForceFinish)
{
	if (!IsLandingStateActive())
	{
		return;
	}

	if (!bForceFinish && bIsLanding && !bCanExitLanding)
	{
		ScheduleLandingMinHoldRetry();
		return;
	}

	if (SchedulePendingExit(
		LandingExitTimerHandle,
		bLandingFinishPendingExit,
		&UProject_JLocomotionAnimStateComponent::CompleteLanding,
		FinishedExitWindow))
	{
		return;
	}

	if (!bLandingFinishPendingExit)
	{
		CompleteLanding();
	}
}

bool UProject_JLocomotionAnimStateComponent::CanStartJumpForAnimation() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (PlayerOwner && !PlayerOwner->IsJumpLocomotionAllowed())
	{
		return false;
	}

	const UCharacterMovementComponent* MovementComponent = GetCachedMovementComponent();
	const bool bIsGroundedForJump = MovementComponent && MovementComponent->IsMovingOnGround();
	const bool bInLandingRecovery = IsLandingStateActive();
	const bool bCanCancelLandingIntoJump =
		bInLandingRecovery &&
		bIsGroundedForJump &&
		!bIsPhysicallyInAir &&
		!IsInAirForAnimation();

	return
		(bIsGroundedForJump || bCanCancelLandingIntoJump) &&
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
	}
}

void UProject_JLocomotionAnimStateComponent::ClearFallOffStartTimers()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FallOffStartTimerHandle);
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

bool UProject_JLocomotionAnimStateComponent::SchedulePendingExit(
	FTimerHandle& TimerHandle,
	bool& bPendingExit,
	void (UProject_JLocomotionAnimStateComponent::*Callback)(),
	float Delay)
{
	if (Delay <= 0.0f || bPendingExit)
	{
		return false;
	}

	bPendingExit = true;
	if (UWorld* World = GetWorld())
	{
		FTimerDelegate TimerDelegate;
		TimerDelegate.BindUObject(this, Callback);
		World->GetTimerManager().SetTimer(TimerHandle, TimerDelegate, FMath::Max(0.01f, Delay), false);
	}

	return true;
}

bool UProject_JLocomotionAnimStateComponent::ScheduleLandingMinHoldRetry()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			LandingTimerHandle,
			this,
			&UProject_JLocomotionAnimStateComponent::OnLandingTimerFinished,
			FMath::Max(0.01f, LandingMinHoldTime - LandingElapsedTime),
			false);
		return true;
	}

	return false;
}

void UProject_JLocomotionAnimStateComponent::FinishLandingImmediately()
{
	ClearLandingTimers();
	bLandingFinishPendingExit = false;
	OnLandingTimerFinished();
}

void UProject_JLocomotionAnimStateComponent::BeginJumpStartState()
{
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
	bLandingFinishPendingExit = false;
}

void UProject_JLocomotionAnimStateComponent::ScheduleJumpStartTimeout(float Duration)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			JumpTimerHandle,
			this,
			&UProject_JLocomotionAnimStateComponent::OnJumpTimerFinished,
			Duration,
			false);
	}
}

void UProject_JLocomotionAnimStateComponent::BeginLandingState(const AProject_JPlayerCharacter& PlayerOwner, float ImpactFallSpeed)
{
	StopFallOffStart();

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
	InitialLandingActorYaw = PlayerOwner.GetActorRotation().Yaw;
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
	bLandingFinishPendingExit = false;
}

void UProject_JLocomotionAnimStateComponent::ScheduleLandingTimeout()
{
	if (UWorld* World = GetWorld())
	{
		const float LandingFallbackDuration = bLandWasMoving ? LandingRequestDuration : StandLandingRequestDuration;
		ClearLandingTimers();
		World->GetTimerManager().SetTimer(
			LandingTimerHandle,
			this,
			&UProject_JLocomotionAnimStateComponent::OnLandingTimerFinished,
			FMath::Max(0.05f, LandingFallbackDuration),
			false);
	}
}

void UProject_JLocomotionAnimStateComponent::ClearActiveLandingState()
{
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
}

void UProject_JLocomotionAnimStateComponent::StartLanding(float ImpactFallSpeed, bool bBroadcastRealLandingEvent, bool bUpdateGameplayTags)
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return;
	}

	const bool bHadInAirState = bIsInAir || bIsPhysicallyInAir || bIsJumping || bIsFallOffStart;

	BeginLandingState(*PlayerOwner, ImpactFallSpeed);

	if (bUpdateGameplayTags)
	{
		if (bHadInAirState)
		{
			RemoveOwnedInAirGameplayTag();
		}

		AddOwnedLandingGameplayTag();
	}

	ScheduleLandingTimeout();
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

	AddOwnedInAirGameplayTag();
}

void UProject_JLocomotionAnimStateComponent::StopFallOffStart()
{
	ClearFallOffStartTimers();
	bIsFallOffStart = false;
}

void UProject_JLocomotionAnimStateComponent::CompleteJumpStart()
{
	if (!bIsJumping)
	{
		return;
	}

	ClearJumpStartTimers();

	bIsJumping = false;
	bPredictedRemoteJumpStart = false;
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

	const bool bIgnoreRemoteMotionForLandingFinish = bLandingIgnoresRemoteGroundSpeed;
	const bool bMoveInputStillHeld = !bIgnoreRemoteMotionForLandingFinish && GetMovementInputForState().Size() > MoveInputDeadZone;
	const bool bForcedLocomotionFinish = bForceLandingFinishToLocomotion;
	const bool bAllowGroundSpeedLocomotion = !bIgnoreRemoteMotionForLandingFinish;
	const bool bShouldEnterLocomotion = bForcedLocomotionFinish || bMoveInputStillHeld || (bAllowGroundSpeedLocomotion && GroundSpeed > IdleSpeedThreshold);

	ClearActiveLandingState();

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
	CompleteFallOffStart();
}
