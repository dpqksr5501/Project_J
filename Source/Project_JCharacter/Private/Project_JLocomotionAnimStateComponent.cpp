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
	bWantsSprint = PlayerOwner->bIsSprinting || bSprintInputHeld;
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
		World->GetTimerManager().ClearTimer(LandingTimerHandle);
	}
	StopFallOffStart();
	bIsLanding = false;
	bLandingRequested = false;
	bCanEnterLand = false;
	bCanEnterGround = false;
	bIsInAir = true;
	bWasInAir = true;
	bSuppressFallOffStart = true;

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
	}

	StopFallOffStart();
	bIsLanding = false;
	bLandingRequested = false;
	bCanEnterLand = false;
	bCanEnterGround = false;
	bIsInAir = true;
	bIsPhysicallyInAir = true;
	bWasInAir = true;
	bSuppressFallOffStart = true;
	bIsJumping = true;
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

	const float ImpactFallSpeed = FMath::Max(LastFallSpeed, FMath::Abs(PlayerOwner->GetVelocity().Z));
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JumpTimerHandle);
	}
	StartLanding(ImpactFallSpeed, true, true);
}

void UProject_JLocomotionAnimStateComponent::FinishLanding()
{
	if (bIsLanding && !bCanExitLanding)
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

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LandingTimerHandle);
	}
	OnLandingTimerFinished();
}

void UProject_JLocomotionAnimStateComponent::FinishStop()
{
	bStopRequested = false;
	bIsStopping = false;
	StopElapsedTime = 0.0f;
}

void UProject_JLocomotionAnimStateComponent::FinishJumpStart()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JumpTimerHandle);
	}
	OnJumpTimerFinished();
}

void UProject_JLocomotionAnimStateComponent::FinishFallOffStart()
{
	StopFallOffStart();
}

void UProject_JLocomotionAnimStateComponent::MarkGroundStartFinished()
{
	if (MoveInputHeldTime < MinStartDatabaseTime)
	{
		bPendingGroundStartFinish = true;
		return;
	}

	bPendingGroundStartFinish = false;
	bGroundStartFinished = true;
	bUseStartDatabase = false;
}

void UProject_JLocomotionAnimStateComponent::SetMoveInput(const FVector2D& InMoveInput)
{
	CachedMoveInput = InMoveInput;
}

void UProject_JLocomotionAnimStateComponent::ClearMoveInput()
{
	CachedMoveInput = FVector2D::ZeroVector;
}

void UProject_JLocomotionAnimStateComponent::HandleSprintStarted()
{
	bSprintInputHeld = true;
	bWantsSprint = true;

	if (bStartRequested || bUseStartDatabase || bPendingGroundStartFinish)
	{
		bStartWasSprinting = true;
	}
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
		FinishLanding();
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
		}
	}

	bIsPhysicallyInAir = bIsCurrentlyInAir;
	bIsJumping = false;
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
	bIsJumping = false;
	bIsInAir = true;
	bIsPhysicallyInAir = false;
	bWasInAir = false;
	bSuppressFallOffStart = false;

	LastFallSpeed = ImpactFallSpeed;
	LandStartGroundSpeed = GroundSpeed;
	LandStartFallSpeed = ImpactFallSpeed;
	bLandWasSprinting = PlayerOwner->bIsSprinting || LandStartGroundSpeed >= RunToSprintSpeedThreshold;
	bLandWasMoving = LandStartGroundSpeed > IdleSpeedThreshold || bHasMoveInput;
	bUseHeavyLand = LandStartFallSpeed >= HeavyLandSpeedThreshold;
	bIsLanding = true;
	bLandingRequested = true;
	bCanEnterLand = true;
	bCanEnterGround = false;
	bCanExitLanding = LandingMinHoldTime <= 0.0f;
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
		World->GetTimerManager().SetTimer(LandingTimerHandle, this, &UProject_JLocomotionAnimStateComponent::OnLandingTimerFinished, FMath::Max(0.05f, LandingRequestDuration), false);
	}

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
		World->GetTimerManager().SetTimer(FallOffStartTimerHandle, this, &UProject_JLocomotionAnimStateComponent::OnFallOffStartFinished, FMath::Max(0.05f, FallOffStartDuration), false);
	}

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
	}
	bIsFallOffStart = false;
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
	LastFallSpeed = 0.0f;
	RemoteAirborneTime = 0.0f;
	LandingElapsedTime = 0.0f;

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
	bIsJumping = false;

	if (!IsInAirForAnimation() && !bIsLanding)
	{
		bIsInAir = false;
		bWasInAir = false;
		bSuppressFallOffStart = false;
	}
}

void UProject_JLocomotionAnimStateComponent::OnFallOffStartFinished()
{
	StopFallOffStart();
}

void UProject_JLocomotionAnimStateComponent::UpdateMovementRequestState(float DeltaTime)
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		ClearMovementRequests();
		return;
	}

	bPrevHasMoveInput = bHasMoveInput;

	const FVector2D MoveInput = GetMovementInputForState();
	MoveInputSize = MoveInput.Size();
	bHasMoveInput = MoveInputSize > MoveInputDeadZone;
	MoveInputHeldTime = bHasMoveInput ? MoveInputHeldTime + DeltaTime : 0.0f;
	MoveInputTurnAngle = 0.0f;
	bSharpTurnRequested = false;
	bStartRequested = false;
	bStopRequested = false;
	bUseStartDatabase = false;

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
		PreviousMoveInputForTurn = bHasMoveInput ? MoveInput : FVector2D::ZeroVector;
		return;
	}

	const bool bStartedMoving = !bPrevHasMoveInput && bHasMoveInput;
	const bool bStoppedMoving = bPrevHasMoveInput && !bHasMoveInput;

	if (bStartedMoving)
	{
		MoveInputHeldTime = 0.0f;
		bGroundStartFinished = false;
		bPendingGroundStartFinish = false;
		bStartWasSprinting = bWantsSprint;
	}

	if (!bHasMoveInput)
	{
		bGroundStartFinished = false;
		bPendingGroundStartFinish = false;
		bStartWasSprinting = false;
	}
	else
	{
		FinishStop();
	}

	if (bPendingGroundStartFinish && MoveInputHeldTime >= MinStartDatabaseTime)
	{
		bGroundStartFinished = true;
		bPendingGroundStartFinish = false;
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

	bUseStartDatabase = bHasMoveInput && !bGroundStartFinished && MoveInputHeldTime < StartToLoopDelay;

	PreviousMoveInputForTurn = bHasMoveInput ? MoveInput : FVector2D::ZeroVector;
}

void UProject_JLocomotionAnimStateComponent::UpdateRemoteMovementRequestState(float DeltaTime)
{
	bPrevHasMoveInput = bHasMoveInput;
	const FVector2D MoveInput = GetMovementInputForState();
	MoveInputSize = MoveInput.Size();
	bHasMoveInput = MoveInputSize > MoveInputDeadZone;
	MoveInputHeldTime = bHasMoveInput ? MoveInputHeldTime + DeltaTime : 0.0f;

	bSharpTurnRequested = false;
	bStartRequested = false;
	bStopRequested = false;
	bIsStopping = false;
	bUseStartDatabase = false;
	bPendingGroundStartFinish = false;
	bStartWasSprinting = false;
	MoveInputTurnAngle = 0.0f;
	bGroundStartFinished = bHasMoveInput;
	PreviousMoveInputForTurn = bHasMoveInput ? MoveInput : FVector2D::ZeroVector;
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
	bGroundStartFinished = false;
	bPendingGroundStartFinish = false;
	bStartWasSprinting = false;
	MoveInputHeldTime = 0.0f;
	StopElapsedTime = 0.0f;
	bSharpTurnRequested = false;
	MoveInputTurnAngle = 0.0f;
}

void UProject_JLocomotionAnimStateComponent::ClearTransientAnimationRequests()
{
	bSharpTurnRequested = false;
	bStartRequested = false;
	bStopRequested = false;
	bIsStopping = false;
	bUseStartDatabase = false;
	bPendingGroundStartFinish = false;
	bRealLandingEventRequested = false;
}
