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
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JLocomotionAnimStateComponent::BeginPlay()
{
	Super::BeginPlay();

	CacheOwnerReferences();
}

void UProject_JLocomotionAnimStateComponent::UpdateState(float DeltaTime)
{
	if (!CachedPlayerOwner || !CachedMovementComponent)
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

	bRecentlyRendered = WasRecentlyRendered();
	if (!bUsingLocalInputState && !bRecentlyRendered && HiddenRemoteUpdateInterval > 0.0f)
	{
		HiddenRemoteUpdateAccumulator += DeltaTime;
		if (HiddenRemoteUpdateAccumulator < HiddenRemoteUpdateInterval)
		{
			return;
		}

		DeltaTime = HiddenRemoteUpdateAccumulator;
		HiddenRemoteUpdateAccumulator = 0.0f;
	}
	else
	{
		HiddenRemoteUpdateAccumulator = 0.0f;
	}

	const FVector HorizontalVelocity = UpdateMovementSnapshot(DeltaTime, *PlayerOwner);

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

	UpdateCombatMovementState(HorizontalVelocity);
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

	if (UAbilitySystemComponent* ASC = GetCachedAbilitySystemComponent())
	{
		if (bHadLandingState)
		{
			ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_Landing);
		}
		if (!ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir))
		{
			ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir);
		}
	}
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

void UProject_JLocomotionAnimStateComponent::HandleReplicatedMoveStarted(bool bWasSprintingForStart)
{
	if (ShouldUseLocalInputState())
	{
		return;
	}

	bStartWasSprinting = bWasSprintingForStart;
	bPendingStartRequest = true;
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

void UProject_JLocomotionAnimStateComponent::CacheOwnerReferences()
{
	CachedPlayerOwner = Cast<AProject_JPlayerCharacter>(GetOwner());
	CachedMovementComponent = CachedPlayerOwner ? CachedPlayerOwner->GetCharacterMovement() : nullptr;
	CachedCapsuleComponent = CachedPlayerOwner ? CachedPlayerOwner->GetCapsuleComponent() : nullptr;
	CachedAbilitySystemComponent = CachedPlayerOwner ? CachedPlayerOwner->GetAbilitySystemComponent() : nullptr;
	CachedMeshComponent = CachedPlayerOwner ? CachedPlayerOwner->GetMesh() : nullptr;
}

AProject_JPlayerCharacter* UProject_JLocomotionAnimStateComponent::GetPlayerOwner() const
{
	return CachedPlayerOwner ? CachedPlayerOwner.Get() : Cast<AProject_JPlayerCharacter>(GetOwner());
}

UCharacterMovementComponent* UProject_JLocomotionAnimStateComponent::GetCachedMovementComponent() const
{
	return CachedMovementComponent.Get();
}

UAbilitySystemComponent* UProject_JLocomotionAnimStateComponent::GetCachedAbilitySystemComponent() const
{
	return CachedAbilitySystemComponent.Get();
}

bool UProject_JLocomotionAnimStateComponent::IsInAirForAnimation() const
{
	const UCharacterMovementComponent* MoveComp = GetCachedMovementComponent();
	return MoveComp && MoveComp->IsFalling();
}

bool UProject_JLocomotionAnimStateComponent::ShouldUseLocalInputState() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	return PlayerOwner && (PlayerOwner->IsLocallyControlled() || bUseInputDerivedRequestsForRemotePlayers);
}

bool UProject_JLocomotionAnimStateComponent::IsDedicatedServerContext() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->GetNetMode() == NM_DedicatedServer;
}

bool UProject_JLocomotionAnimStateComponent::WasRecentlyRendered() const
{
	return !CachedMeshComponent || CachedMeshComponent->WasRecentlyRendered(RecentlyRenderedTolerance);
}

bool UProject_JLocomotionAnimStateComponent::IsSprintRequestedForAnimation() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	return bSprintInputHeld || (PlayerOwner && PlayerOwner->bIsSprinting);
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
	if (!bUseRemoteGroundProbe || !PlayerOwner || !World || !CachedCapsuleComponent)
	{
		return false;
	}

	const FVector Start = PlayerOwner->GetActorLocation();
	const float TraceDistance = CachedCapsuleComponent->GetScaledCapsuleHalfHeight() + RemoteGroundProbeDistance;
	const FVector End = Start - FVector(0.0f, 0.0f, TraceDistance);

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RemoteGroundProbe), false, PlayerOwner);
	return World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams) && Hit.bBlockingHit;
}

FVector2D UProject_JLocomotionAnimStateComponent::GetMovementInputForState() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return FVector2D::ZeroVector;
	}

	if (ShouldUseLocalInputState())
	{
		return CachedMoveInput.GetClampedToMaxSize(1.0f);
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

FVector UProject_JLocomotionAnimStateComponent::UpdateMovementSnapshot(float DeltaTime, const AProject_JPlayerCharacter& PlayerOwner)
{
	const FVector Velocity = PlayerOwner.GetVelocity();
	const FVector HorizontalVelocity(Velocity.X, Velocity.Y, 0.0f);
	VerticalSpeed = Velocity.Z;
	GroundSpeed = HorizontalVelocity.Size();
	bWantsSprint = IsSprintRequestedForAnimation();
	const bool bHasSprintMovementIntent = CachedMoveInput.Size() > MoveInputDeadZone || GroundSpeed > IdleSpeedThreshold;
	bUseSprintLocomotion =
		GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		bWantsSprint &&
		bHasSprintMovementIntent;
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

	return HorizontalVelocity;
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

	if (bIsFallOffStart)
	{
		StopFallOffStart();
	}

	if (UpdateRemoteJumpStartState(DeltaTime, bIsCurrentlyInAir, bHadRemoteAirborneEvidence))
	{
		return;
	}

	bIsPhysicallyInAir = bIsCurrentlyInAir;
	bIsJumping = false;
	bJumpStartFinishPendingExit = false;
	bIsFallOffStart = false;
	bSuppressFallOffStart = false;

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
		bIsInAir = false;
		bCanEnterLand = false;
		bCanEnterGround = true;
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
	const bool bLandingHasMoveInput = GetMovementInputForState().Size() > MoveInputDeadZone || bHasMoveInput;
	const bool bLandingHasMovementIntent = ShouldUseLocalInputState()
		? bLandingHasMoveInput
		: (bLandingHasMoveInput || LandStartGroundSpeed > IdleSpeedThreshold);
	bLandWasMoving = bLandingHasMovementIntent;
	bLandWasSprinting = bLandWasMoving && IsSprintRequestedForAnimation();
	bUseHeavyLand = LandStartFallSpeed >= HeavyLandSpeedThreshold;
	InitialLandingMoveWorldDirection = FVector::ZeroVector;
	PreviousLandingMoveWorldDirection = FVector::ZeroVector;
	if (ShouldUseLocalInputState() && bLandWasMoving)
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
		if (UAbilitySystemComponent* ASC = GetCachedAbilitySystemComponent())
		{
			if (bHadInAirState)
			{
				ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir);
			}

			if (!ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Movement_Landing))
			{
				ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_Landing);
			}
		}
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

void UProject_JLocomotionAnimStateComponent::StartFallOffStart()
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return;
	}

	bIsInAir = true;
	bIsFallOffStart = true;
	ClearFallOffStartTimers();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(FallOffStartTimerHandle, this, &UProject_JLocomotionAnimStateComponent::OnFallOffStartFinished, FMath::Max(0.05f, FallOffStartDuration), false);
	}
	bFallOffStartFinishPendingExit = false;

	if (UAbilitySystemComponent* ASC = GetCachedAbilitySystemComponent())
	{
		if (!ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir))
		{
			ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir);
		}
	}
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

	const bool bMoveInputStillHeld = GetMovementInputForState().Size() > MoveInputDeadZone;
	if (bMoveInputStillHeld || GroundSpeed > IdleSpeedThreshold)
	{
		bHasMoveInput = bMoveInputStillHeld;
		bPrevHasMoveInput = bMoveInputStillHeld;
		bResolvedMoveInputLastUpdate = bMoveInputStillHeld;
		EnterGroundMotionMode(EProject_JGroundMotionMode::Locomotion);
	}
	else
	{
		EnterGroundMotionMode(EProject_JGroundMotionMode::Idle);
	}

	if (UAbilitySystemComponent* ASC = GetCachedAbilitySystemComponent())
	{
		if (bHadLandingState)
		{
			ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_Landing);
		}
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

	bPrevHasMoveInput = bResolvedMoveInputLastUpdate;

	const FVector2D MoveInput = GetMovementInputForState();
	MoveInputSize = MoveInput.Size();
	bHasMoveInput = MoveInputSize > MoveInputDeadZone;
	MoveInputHeldTime = bHasMoveInput ? MoveInputHeldTime + DeltaTime : 0.0f;
	MoveInputTurnAngle = 0.0f;
	bSharpTurnRequested = false;

	if (bHasMoveInput && bPrevHasMoveInput && PreviousMoveInputForTurn.Size() > MoveInputDeadZone)
	{
		const FVector2D PreviousDirection = PreviousMoveInputForTurn.GetSafeNormal();
		const FVector2D CurrentDirection = MoveInput.GetSafeNormal();
		const float Dot = FMath::Clamp(FVector2D::DotProduct(PreviousDirection, CurrentDirection), -1.0f, 1.0f);
		const float Cross = PreviousDirection.Y * CurrentDirection.X - PreviousDirection.X * CurrentDirection.Y;
		MoveInputTurnAngle = FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
	}

	if (IsLandingStateActive() && !bLandWasMoving && bHasMoveInput)
	{
		bLandWasMoving = true;
		FinishLandingImmediately();
		return;
	}

	if (IsLandingStateActive() &&
		bLandWasSprinting &&
		bWantsSprint &&
		bHasMoveInput &&
		LandingElapsedTime >= SprintLandingTurnCancelMinTime)
	{
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
				FinishLandingImmediately();
				return;
			}
		}

		PreviousLandingMoveWorldDirection = CurrentLandingMoveWorldDirection;
	}

	UpdateGroundMotionModeFromInput(DeltaTime, MoveInput, true);
}

void UProject_JLocomotionAnimStateComponent::UpdateRemoteMovementRequestState(float DeltaTime)
{
	bPrevHasMoveInput = bResolvedMoveInputLastUpdate;
	const FVector2D MoveInput = GetMovementInputForState();
	MoveInputSize = MoveInput.Size();
	bHasMoveInput = MoveInputSize > MoveInputDeadZone;
	MoveInputHeldTime = bHasMoveInput ? MoveInputHeldTime + DeltaTime : 0.0f;

	bSharpTurnRequested = false;
	MoveInputTurnAngle = 0.0f;

	UpdateGroundMotionModeFromInput(DeltaTime, MoveInput, false);
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
		PlayerOwner->bIsSprinting &&
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

		if (!bHasMoveInput)
		{
			EnterGroundMotionMode(
				GroundSpeed > StopIntentSpeedThreshold
					? EProject_JGroundMotionMode::Stop
					: EProject_JGroundMotionMode::Idle);
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

	if (!PlayerOwner->bIsCombatMode || !bHasMoveInput)
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
		const float DesiredSpeed = CachedMovementComponent ? CachedMovementComponent->MaxWalkSpeed : PlayerOwner->WalkSpeed;
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

	const float MaxSpeed = CachedMovementComponent ? FMath::Max(CachedMovementComponent->MaxWalkSpeed, 1.0f) : FMath::Max(PlayerOwner->WalkSpeed, 1.0f);
	CombatInputForward = FMath::Clamp(CombatForwardSpeed / MaxSpeed, -1.0f, 1.0f);
	CombatInputRight = FMath::Clamp(CombatRightSpeed / MaxSpeed, -1.0f, 1.0f);
	MovementDirection = FMath::RadiansToDegrees(FMath::Atan2(CombatRightSpeed, CombatForwardSpeed));
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
