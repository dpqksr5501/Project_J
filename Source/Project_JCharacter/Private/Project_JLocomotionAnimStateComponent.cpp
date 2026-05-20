// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JLocomotionAnimStateComponent.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
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

	const FVector Velocity = CachedMovementComponent ? CachedMovementComponent->Velocity : PlayerOwner->GetVelocity();
	const FVector HorizontalVelocity(Velocity.X, Velocity.Y, 0.0f);
	VerticalSpeed = Velocity.Z;
	GroundSpeed = HorizontalVelocity.Size();

	bUsingLocalInputState = ShouldUseLocalInputState();
	bRecentlyRendered = WasRecentlyRendered();
	bDedicatedServerContext = IsDedicatedServerContext();

	const bool bIsCurrentlyInAir = IsInAirForAnimation();
	bIsPhysicallyInAir = bIsCurrentlyInAir;
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
	}

	bWasInAir = bIsCurrentlyInAir;
	bCanEnterLand = bLandingRequested;
	bCanEnterGround = !bIsInAir && !bIsLanding && !bLandingRequested;

	if (bUsingLocalInputState)
	{
		UpdateMovementRequestState(DeltaTime);
	}
	else
	{
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

void UProject_JLocomotionAnimStateComponent::HandleLanded(const FHitResult&)
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return;
	}

	const float ImpactFallSpeed = FMath::Abs(CachedMovementComponent ? CachedMovementComponent->Velocity.Z : PlayerOwner->GetVelocity().Z);
	const bool bHadInAirState = bIsInAir || bIsPhysicallyInAir || bIsJumping || bIsFallOffStart;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(JumpTimerHandle);
	}
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

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(LandingTimerHandle, this, &UProject_JLocomotionAnimStateComponent::OnLandingTimerFinished, FMath::Max(0.05f, LandingRequestDuration), false);
	}
	bRealLandingEventRequested = LastFallSpeed > RealLandingEventSpeedThreshold;
}

void UProject_JLocomotionAnimStateComponent::FinishLanding()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LandingTimerHandle);
	}
	OnLandingTimerFinished();
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

bool UProject_JLocomotionAnimStateComponent::ConsumeRealLandingEventRequested()
{
	const bool bRequested = bRealLandingEventRequested;
	bRealLandingEventRequested = false;
	return bRequested;
}

void UProject_JLocomotionAnimStateComponent::CacheOwnerReferences()
{
	CachedPlayerOwner = Cast<AProject_JPlayerCharacter>(GetOwner());
	CachedMovementComponent = CachedPlayerOwner ? CachedPlayerOwner->GetCharacterMovement() : nullptr;
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

	const FVector2D MoveInput = CachedMoveInput.GetClampedToMaxSize(1.0f);
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
		bStartWasSprinting = PlayerOwner->bIsSprinting;
	}

	if (!bHasMoveInput)
	{
		bGroundStartFinished = false;
		bPendingGroundStartFinish = false;
		bStartWasSprinting = false;
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
	bStopRequested = bStoppedMoving && GroundSpeed > StopIntentSpeedThreshold;
	bUseStartDatabase = bHasMoveInput && !bGroundStartFinished && MoveInputHeldTime < StartToLoopDelay;

	PreviousMoveInputForTurn = bHasMoveInput ? MoveInput : FVector2D::ZeroVector;
}

void UProject_JLocomotionAnimStateComponent::UpdateRemoteMovementRequestState(float DeltaTime)
{
	bPrevHasMoveInput = bHasMoveInput;
	bHasMoveInput = GroundSpeed > RemoteMoveSpeedThreshold;
	MoveInputSize = bHasMoveInput ? 1.0f : 0.0f;
	MoveInputHeldTime = bHasMoveInput ? MoveInputHeldTime + DeltaTime : 0.0f;

	bSharpTurnRequested = false;
	bStartRequested = false;
	bStopRequested = false;
	bUseStartDatabase = false;
	bPendingGroundStartFinish = false;
	bStartWasSprinting = false;
	MoveInputTurnAngle = 0.0f;
	bGroundStartFinished = bHasMoveInput;
	PreviousMoveInputForTurn = FVector2D::ZeroVector;
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
	bSharpTurnRequested = false;
	MoveInputTurnAngle = 0.0f;
}
