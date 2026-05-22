// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JLocomotionAnimStateComponent.h"

#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
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

	const FVector Velocity = PlayerOwner->GetVelocity();
	const FVector HorizontalVelocity(Velocity.X, Velocity.Y, 0.0f);
	VerticalSpeed = Velocity.Z;
	GroundSpeed = HorizontalVelocity.Size();
	bWantsSprint = IsSprintRequestedForAnimation();
	if (bIsJumping)
	{
		JumpStartElapsedTime += DeltaTime;
	}

	if (bIsLanding)
	{
		LandingElapsedTime += DeltaTime;
		bCanExitLanding = LandingElapsedTime >= LandingMinHoldTime;
	}

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
	if (!PlayerOwner)
	{
		return;
	}

	const bool bHadLandingState = bIsLanding || bLandingRequested || bCanEnterLand;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JumpTimerHandle);
		World->GetTimerManager().ClearTimer(JumpStartExitTimerHandle);
		World->GetTimerManager().ClearTimer(LandingTimerHandle);
		World->GetTimerManager().ClearTimer(LandingExitTimerHandle);
		World->GetTimerManager().ClearTimer(FallOffStartTimerHandle);
		World->GetTimerManager().ClearTimer(FallOffStartExitTimerHandle);
	}
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

	if (bIsLanding || bLandingRequested || bCanEnterLand)
	{
		FinishLanding();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JumpTimerHandle);
		World->GetTimerManager().ClearTimer(JumpStartExitTimerHandle);
		World->GetTimerManager().ClearTimer(LandingTimerHandle);
		World->GetTimerManager().ClearTimer(LandingExitTimerHandle);
		World->GetTimerManager().ClearTimer(FallOffStartTimerHandle);
		World->GetTimerManager().ClearTimer(FallOffStartExitTimerHandle);
	}

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

void UProject_JLocomotionAnimStateComponent::HandleLanded(const FHitResult&)
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return;
	}

	if (bIsJumping && (bIgnoreNextLandingForJumpStart || JumpStartElapsedTime <= IgnoreLandingAfterJumpStartTime || PlayerOwner->GetVelocity().Z > 0.0f))
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
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JumpTimerHandle);
		World->GetTimerManager().ClearTimer(JumpStartExitTimerHandle);
	}
	bJumpStartFinishPendingExit = false;
	StartLanding(ImpactFallSpeed, true, true);
}

void UProject_JLocomotionAnimStateComponent::FinishLanding(bool bForceFinish)
{
	if (!bIsLanding && !bLandingRequested && !bCanEnterLand)
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
	if (!bStopRequested && !bIsStopping)
	{
		return;
	}

	if (FinishedExitWindow > 0.0f && (bStopRequested || bIsStopping) && !bStopFinishPendingExit)
	{
		bStopFinishPendingExit = true;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				StopExitTimerHandle,
				this,
				&UProject_JLocomotionAnimStateComponent::CompleteStop,
				FinishedExitWindow,
				false);
		}
		return;
	}

	if (!bStopFinishPendingExit)
	{
		CompleteStop();
	}
}

void UProject_JLocomotionAnimStateComponent::FinishJumpStart()
{
	if (!bIsJumping)
	{
		return;
	}

	if (bIsLanding || bLandingRequested || bCanEnterLand)
	{
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
	if (!bStartWindowActive && !bUseStartDatabase && !bPendingGroundStartFinish)
	{
		return;
	}

	if (MoveInputHeldTime < MinStartDatabaseTime)
	{
		bPendingGroundStartFinish = true;
		return;
	}

	bPendingGroundStartFinish = false;
	if (FinishedExitWindow > 0.0f && !bGroundStartFinishPendingExit)
	{
		bGroundStartFinishPendingExit = true;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				GroundStartExitTimerHandle,
				this,
				&UProject_JLocomotionAnimStateComponent::CompleteGroundStart,
				FinishedExitWindow,
				false);
		}
		return;
	}

	if (!bGroundStartFinishPendingExit)
	{
		CompleteGroundStart();
	}
}

void UProject_JLocomotionAnimStateComponent::SetMoveInput(const FVector2D& InMoveInput)
{
	CachedMoveInput = InMoveInput;
}

void UProject_JLocomotionAnimStateComponent::ClearMoveInput()
{
	CachedMoveInput = FVector2D::ZeroVector;
	MoveInputSize = 0.0f;
	MoveInputHeldTime = 0.0f;
	bHasMoveInput = false;
	bPrevHasMoveInput = false;
	bResolvedMoveInputLastUpdate = false;
	bStartRequested = false;
	bUseStartDatabase = false;
	bStartWindowActive = false;
	StartWindowElapsedTime = 0.0f;
	bGroundStartFinished = false;
	bPendingGroundStartFinish = false;
	bGroundStartFinishPendingExit = false;
	PreviousMoveInputForTurn = FVector2D::ZeroVector;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GroundStartExitTimerHandle);
	}
}

void UProject_JLocomotionAnimStateComponent::HandleSprintStarted()
{
	bSprintInputHeld = true;
	bWantsSprint = true;
}

void UProject_JLocomotionAnimStateComponent::HandleSprintStopped()
{
	bSprintInputHeld = false;
	bWantsSprint = false;
}

bool UProject_JLocomotionAnimStateComponent::ConsumeRealLandingEventRequested()
{
	const bool bRequested = bRealLandingEventRequested;
	bRealLandingEventRequested = false;
	return bRequested;
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

void UProject_JLocomotionAnimStateComponent::UpdateLocalAirState(bool bIsCurrentlyInAir)
{
	bIsPhysicallyInAir = bIsCurrentlyInAir;
	if (bIsCurrentlyInAir)
	{
		LastFallSpeed = FMath::Max(LastFallSpeed, FMath::Abs(VerticalSpeed));
	}

	if (!bWasInAir && bIsCurrentlyInAir && !bIsJumping && !bIsLanding && !bSuppressFallOffStart)
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

	if (bIsJumping || bIsFallOffStart)
	{
		StopFallOffStart();
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(JumpTimerHandle);
			World->GetTimerManager().ClearTimer(JumpStartExitTimerHandle);
		}
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
	bLandWasSprinting = IsSprintRequestedForAnimation();
	bLandWasMoving = LandStartGroundSpeed > IdleSpeedThreshold || bHasMoveInput;
	bUseHeavyLand = LandStartFallSpeed >= HeavyLandSpeedThreshold;
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
		World->GetTimerManager().ClearTimer(LandingExitTimerHandle);
		World->GetTimerManager().SetTimer(LandingTimerHandle, this, &UProject_JLocomotionAnimStateComponent::OnLandingTimerFinished, FMath::Max(0.05f, LandingRequestDuration), false);
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
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FallOffStartTimerHandle);
		World->GetTimerManager().ClearTimer(FallOffStartExitTimerHandle);
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
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FallOffStartTimerHandle);
		World->GetTimerManager().ClearTimer(FallOffStartExitTimerHandle);
	}
	bIsFallOffStart = false;
	bFallOffStartFinishPendingExit = false;
}

void UProject_JLocomotionAnimStateComponent::CompleteGroundStart()
{
	if (!bStartWindowActive && !bUseStartDatabase && !bPendingGroundStartFinish && !bGroundStartFinishPendingExit)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GroundStartExitTimerHandle);
	}

	bGroundStartFinishPendingExit = false;
	bPendingGroundStartFinish = false;
	bGroundStartFinished = true;
	bUseStartDatabase = false;
	bStartWindowActive = false;
	StartWindowElapsedTime = 0.0f;
}

void UProject_JLocomotionAnimStateComponent::CompleteStop()
{
	if (!bStopRequested && !bIsStopping && !bStopFinishPendingExit)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StopExitTimerHandle);
	}

	bStopFinishPendingExit = false;
	bStopRequested = false;
	bIsStopping = false;
	StopElapsedTime = 0.0f;
}

void UProject_JLocomotionAnimStateComponent::CompleteJumpStart()
{
	if (!bIsJumping && !bJumpStartFinishPendingExit)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JumpTimerHandle);
		World->GetTimerManager().ClearTimer(JumpStartExitTimerHandle);
	}

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
	if (!bIsLanding && !bLandingRequested && !bCanEnterLand && !bLandingFinishPendingExit)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LandingTimerHandle);
		World->GetTimerManager().ClearTimer(LandingExitTimerHandle);
	}

	bLandingFinishPendingExit = false;
	OnLandingTimerFinished();
}

void UProject_JLocomotionAnimStateComponent::OnLandingTimerFinished()
{
	const bool bHadLandingState = bIsLanding || bLandingRequested || bCanEnterLand;

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

	const bool bMoveInputStillHeld = GetMovementInputForState().Size() > MoveInputDeadZone;
	if (bMoveInputStillHeld || GroundSpeed > IdleSpeedThreshold)
	{
		bHasMoveInput = bMoveInputStillHeld;
		bPrevHasMoveInput = bMoveInputStillHeld;
		bResolvedMoveInputLastUpdate = bMoveInputStillHeld;
		bGroundStartFinished = true;
		bPendingGroundStartFinish = false;
		bGroundStartFinishPendingExit = false;
		bUseStartDatabase = false;
		bStartWindowActive = false;
		StartWindowElapsedTime = 0.0f;

		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(GroundStartExitTimerHandle);
		}
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

	if (bIsJumping && !bIsLanding && !bLandingRequested && !bCanEnterLand)
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
	bStartRequested = false;
	bStopRequested = false;

	if (bHasMoveInput && bPrevHasMoveInput && PreviousMoveInputForTurn.Size() > MoveInputDeadZone)
	{
		const FVector2D PreviousDirection = PreviousMoveInputForTurn.GetSafeNormal();
		const FVector2D CurrentDirection = MoveInput.GetSafeNormal();
		const float Dot = FMath::Clamp(FVector2D::DotProduct(PreviousDirection, CurrentDirection), -1.0f, 1.0f);
		const float Cross = PreviousDirection.Y * CurrentDirection.X - PreviousDirection.X * CurrentDirection.Y;
		MoveInputTurnAngle = FMath::RadiansToDegrees(FMath::Atan2(Cross, Dot));
	}

	const bool bCanRequestGroundMove = !bIsInAir && !bIsLanding && !bIsJumping && !bIsFallOffStart;
	if (!bCanRequestGroundMove)
	{
		ClearMovementRequests();
		bResolvedMoveInputLastUpdate = bHasMoveInput;
		PreviousMoveInputForTurn = bHasMoveInput ? MoveInput : FVector2D::ZeroVector;
		return;
	}

	const bool bStartedMoving = !bPrevHasMoveInput && bHasMoveInput;
	const bool bStoppedMoving = bPrevHasMoveInput && !bHasMoveInput;

	if (bStartedMoving)
	{
		MoveInputHeldTime = 0.0f;
		StartWindowElapsedTime = 0.0f;
		bGroundStartFinished = false;
		bPendingGroundStartFinish = false;
		bGroundStartFinishPendingExit = false;
		bStartWindowActive = true;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(GroundStartExitTimerHandle);
		}
	}
	if (!bHasMoveInput)
	{
		bGroundStartFinished = false;
		bPendingGroundStartFinish = false;
		bGroundStartFinishPendingExit = false;
		bStartWindowActive = false;
		StartWindowElapsedTime = 0.0f;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(GroundStartExitTimerHandle);
		}
	}
	else
	{
		FinishStop();
	}

	if (bPendingGroundStartFinish && MoveInputHeldTime >= MinStartDatabaseTime)
	{
		MarkGroundStartFinished();
	}

	bSharpTurnRequested =
		PlayerOwner->bIsSprinting &&
		bHasMoveInput &&
		bPrevHasMoveInput &&
		GroundSpeed >= SharpTurnMinSpeed &&
		FMath::Abs(MoveInputTurnAngle) >= SharpTurnAngleThreshold;

	bStartRequested = bStartedMoving;
	if (bStoppedMoving && GroundSpeed > StopIntentSpeedThreshold)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(StopExitTimerHandle);
		}
		bStopFinishPendingExit = false;
		bStopRequested = true;
		bIsStopping = true;
		StopElapsedTime = 0.0f;
	}
	else if (bIsStopping)
	{
		bStopRequested = true;
		StopElapsedTime += DeltaTime;
		if (StopElapsedTime >= StopRequestDuration || GroundSpeed <= IdleSpeedThreshold)
		{
			FinishStop();
		}
	}

	if (bStartWindowActive && bHasMoveInput)
	{
		StartWindowElapsedTime += DeltaTime;
	}

	if (bHasMoveInput && !bGroundStartFinished && StartWindowElapsedTime >= StartToLoopDelay)
	{
		bGroundStartFinished = true;
		bPendingGroundStartFinish = false;
		bStartWindowActive = false;
		StartWindowElapsedTime = 0.0f;
	}
	else if (bHasMoveInput && !bGroundStartFinished)
	{
		bStartWindowActive = true;
	}
	else
	{
		bStartWindowActive = false;
		StartWindowElapsedTime = 0.0f;
	}

	bUseStartDatabase = bStartWindowActive && !bGroundStartFinished && StartWindowElapsedTime >= StartIntentGraceTime;

	PreviousMoveInputForTurn = bHasMoveInput ? MoveInput : FVector2D::ZeroVector;
	bResolvedMoveInputLastUpdate = bHasMoveInput;
}

void UProject_JLocomotionAnimStateComponent::UpdateRemoteMovementRequestState(float DeltaTime)
{
	bPrevHasMoveInput = bResolvedMoveInputLastUpdate;
	const FVector2D MoveInput = GetMovementInputForState();
	MoveInputSize = MoveInput.Size();
	bHasMoveInput = MoveInputSize > MoveInputDeadZone;
	MoveInputHeldTime = bHasMoveInput ? MoveInputHeldTime + DeltaTime : 0.0f;

	bSharpTurnRequested = false;
	bStartRequested = false;
	bStopRequested = false;
	bIsStopping = false;
	bUseStartDatabase = false;
	bStartWindowActive = false;
	bPendingGroundStartFinish = false;
	MoveInputTurnAngle = 0.0f;
	bGroundStartFinished = bHasMoveInput;
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
	bStopRequested = false;
	bUseStartDatabase = false;
	bStartWindowActive = false;
	StartWindowElapsedTime = 0.0f;
	bGroundStartFinished = false;
	bPendingGroundStartFinish = false;
	bGroundStartFinishPendingExit = false;
	bStopFinishPendingExit = false;
	bResolvedMoveInputLastUpdate = false;
	MoveInputHeldTime = 0.0f;
	StopElapsedTime = 0.0f;
	bSharpTurnRequested = false;
	MoveInputTurnAngle = 0.0f;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GroundStartExitTimerHandle);
		World->GetTimerManager().ClearTimer(StopExitTimerHandle);
	}
}

void UProject_JLocomotionAnimStateComponent::ClearTransientAnimationRequests()
{
	bSharpTurnRequested = false;
	bStartRequested = false;
	bStopRequested = false;
	bIsStopping = false;
	bUseStartDatabase = false;
	bStartWindowActive = false;
	StartWindowElapsedTime = 0.0f;
	bPendingGroundStartFinish = false;
	bGroundStartFinishPendingExit = false;
	bStopFinishPendingExit = false;
	bJumpStartFinishPendingExit = false;
	bFallOffStartFinishPendingExit = false;
	bLandingFinishPendingExit = false;
	bRealLandingEventRequested = false;
	bResolvedMoveInputLastUpdate = bHasMoveInput;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GroundStartExitTimerHandle);
		World->GetTimerManager().ClearTimer(StopExitTimerHandle);
		World->GetTimerManager().ClearTimer(JumpStartExitTimerHandle);
		World->GetTimerManager().ClearTimer(FallOffStartExitTimerHandle);
		World->GetTimerManager().ClearTimer(LandingExitTimerHandle);
	}
}
