// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JLocomotionAnimStateComponent.h"

#include "AbilitySystemComponent.h"
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

	CachedPlayerOwner = Cast<AProject_JPlayerCharacter>(GetOwner());
}

void UProject_JLocomotionAnimStateComponent::UpdateState(float DeltaTime)
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return;
	}

	const FVector Velocity = PlayerOwner->GetVelocity();
	VerticalSpeed = Velocity.Z;
	GroundSpeed = FVector(Velocity.X, Velocity.Y, 0.0f).Size();

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
	UpdateMovementRequestState(DeltaTime);

	const FVector2D CombatMoveInput = CachedMoveInput.GetClampedToMaxSize(1.0f);
	CombatInputRight = CombatMoveInput.X;
	CombatInputForward = CombatMoveInput.Y;

	if (PlayerOwner->bIsCombatMode && bHasMoveInput)
	{
		const UCharacterMovementComponent* MoveComp = PlayerOwner->GetCharacterMovement();
		const float DesiredSpeed = MoveComp ? MoveComp->MaxWalkSpeed : PlayerOwner->WalkSpeed;
		CombatRightSpeed = CombatMoveInput.X * DesiredSpeed;
		CombatForwardSpeed = CombatMoveInput.Y * DesiredSpeed;
		MovementDirection = FMath::RadiansToDegrees(FMath::Atan2(CombatMoveInput.X, CombatMoveInput.Y));
	}
	else
	{
		MovementDirection = 0.0f;
		CombatForwardSpeed = 0.0f;
		CombatRightSpeed = 0.0f;
	}
}

void UProject_JLocomotionAnimStateComponent::HandleJumpStarted()
{
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		return;
	}

	const bool bHadLandingState = bIsLanding || bLandingRequested || bCanEnterLand;

	GetWorld()->GetTimerManager().ClearTimer(JumpTimerHandle);
	GetWorld()->GetTimerManager().ClearTimer(LandingTimerHandle);
	StopFallOffStart();
	bIsLanding = false;
	bLandingRequested = false;
	bCanEnterLand = false;
	bCanEnterGround = false;
	bIsInAir = true;
	bWasInAir = true;
	bSuppressFallOffStart = true;

	bIsJumping = true;
	GetWorld()->GetTimerManager().SetTimer(JumpTimerHandle, this, &UProject_JLocomotionAnimStateComponent::OnJumpTimerFinished, FMath::Max(0.1f, JumpStartMaxDuration), false);

	if (UAbilitySystemComponent* ASC = PlayerOwner->GetAbilitySystemComponent())
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

	const float ImpactFallSpeed = FMath::Abs(PlayerOwner->GetVelocity().Z);
	const bool bHadInAirState = bIsInAir || bIsPhysicallyInAir || bIsJumping || bIsFallOffStart;

	GetWorld()->GetTimerManager().ClearTimer(JumpTimerHandle);
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

	if (UAbilitySystemComponent* ASC = PlayerOwner->GetAbilitySystemComponent())
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

	GetWorld()->GetTimerManager().SetTimer(LandingTimerHandle, this, &UProject_JLocomotionAnimStateComponent::OnLandingTimerFinished, FMath::Max(0.05f, LandingRequestDuration), false);
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

AProject_JPlayerCharacter* UProject_JLocomotionAnimStateComponent::GetPlayerOwner() const
{
	return CachedPlayerOwner ? CachedPlayerOwner.Get() : Cast<AProject_JPlayerCharacter>(GetOwner());
}

bool UProject_JLocomotionAnimStateComponent::IsInAirForAnimation() const
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	const UCharacterMovementComponent* MoveComp = PlayerOwner ? PlayerOwner->GetCharacterMovement() : nullptr;
	return MoveComp && MoveComp->MovementMode == EMovementMode::MOVE_Falling;
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
	GetWorld()->GetTimerManager().ClearTimer(FallOffStartTimerHandle);
	GetWorld()->GetTimerManager().SetTimer(FallOffStartTimerHandle, this, &UProject_JLocomotionAnimStateComponent::OnFallOffStartFinished, FMath::Max(0.05f, FallOffStartDuration), false);

	if (UAbilitySystemComponent* ASC = PlayerOwner->GetAbilitySystemComponent())
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
	AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	const bool bHadLandingState = bIsLanding || bLandingRequested || bCanEnterLand;

	bIsLanding = false;
	bLandingRequested = false;
	bIsInAir = false;
	bWasInAir = false;
	bSuppressFallOffStart = false;
	bCanEnterLand = false;
	bCanEnterGround = true;

	if (PlayerOwner)
	{
		if (UAbilitySystemComponent* ASC = PlayerOwner->GetAbilitySystemComponent())
		{
			if (bHadLandingState)
			{
				ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_Landing);
			}
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
