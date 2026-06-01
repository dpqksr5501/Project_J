// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JLocomotionAnimStateComponent.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"
#include "TimerManager.h"

void UProject_JLocomotionAnimStateComponent::UpdateCombatMovementState(const FVector& HorizontalVelocity)
{
	const AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner();
	if (!PlayerOwner)
	{
		ClearCombatMovementState();
		return;
	}

	if (!PlayerOwner->IsCombatModeActive() || !bHasMoveInput)
	{
		ClearCombatMovementState();
		return;
	}

	if (bUsingLocalInputState)
	{
		UpdateLocalCombatMovementState(*PlayerOwner);
		return;
	}

	UpdateRemoteCombatMovementState(*PlayerOwner, HorizontalVelocity);
}

void UProject_JLocomotionAnimStateComponent::ClearCombatMovementState()
{
	MovementDirection = 0.0f;
	CombatInputForward = 0.0f;
	CombatInputRight = 0.0f;
	CombatForwardSpeed = 0.0f;
	CombatRightSpeed = 0.0f;
}

void UProject_JLocomotionAnimStateComponent::UpdateLocalCombatMovementState(const AProject_JPlayerCharacter& PlayerOwner)
{
	const FVector2D CombatMoveInput = CachedMoveInput.GetClampedToMaxSize(1.0f);
	const UCharacterMovementComponent* MovementComponent = GetCachedMovementComponent();
	const float DesiredSpeed = MovementComponent ? MovementComponent->MaxWalkSpeed : PlayerOwner.WalkSpeed;
	CombatInputRight = CombatMoveInput.X;
	CombatInputForward = CombatMoveInput.Y;
	CombatRightSpeed = CombatMoveInput.X * DesiredSpeed;
	CombatForwardSpeed = CombatMoveInput.Y * DesiredSpeed;
	MovementDirection = FMath::RadiansToDegrees(FMath::Atan2(CombatMoveInput.X, CombatMoveInput.Y));
}

void UProject_JLocomotionAnimStateComponent::UpdateRemoteCombatMovementState(const AProject_JPlayerCharacter& PlayerOwner, const FVector& HorizontalVelocity)
{
	const FVector Forward = PlayerOwner.GetActorForwardVector();
	const FVector Right = PlayerOwner.GetActorRightVector();
	CombatForwardSpeed = FVector::DotProduct(HorizontalVelocity, Forward);
	CombatRightSpeed = FVector::DotProduct(HorizontalVelocity, Right);

	const UCharacterMovementComponent* MovementComponent = GetCachedMovementComponent();
	const float MaxSpeed = MovementComponent ? FMath::Max(MovementComponent->MaxWalkSpeed, 1.0f) : FMath::Max(PlayerOwner.WalkSpeed, 1.0f);
	CombatInputForward = FMath::Clamp(CombatForwardSpeed / MaxSpeed, -1.0f, 1.0f);
	CombatInputRight = FMath::Clamp(CombatRightSpeed / MaxSpeed, -1.0f, 1.0f);
	MovementDirection = FMath::RadiansToDegrees(FMath::Atan2(CombatRightSpeed, CombatForwardSpeed));
}

void UProject_JLocomotionAnimStateComponent::DispatchLandingCancelForAnimation()
{
	if (!ShouldUseLocalInputState() || bLandingCancelEventDispatched)
	{
		return;
	}

	if (AProject_JPlayerCharacter* PlayerOwner = GetPlayerOwner())
	{
		bLandingCancelEventDispatched = true;
		PlayerOwner->NotifyLandingCancelledForAnimation();
	}
}

void UProject_JLocomotionAnimStateComponent::AddOwnedInAirGameplayTag()
{
	if (bAppliedInAirGameplayTag)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetCachedAbilitySystemComponent())
	{
		ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir);
		bAppliedInAirGameplayTag = true;
	}
}

void UProject_JLocomotionAnimStateComponent::RemoveOwnedInAirGameplayTag()
{
	if (!bAppliedInAirGameplayTag)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetCachedAbilitySystemComponent())
	{
		ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_InAir);
	}
	bAppliedInAirGameplayTag = false;
}

void UProject_JLocomotionAnimStateComponent::AddOwnedLandingGameplayTag()
{
	if (bAppliedLandingGameplayTag)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetCachedAbilitySystemComponent())
	{
		ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_Landing);
		bAppliedLandingGameplayTag = true;
	}
}

void UProject_JLocomotionAnimStateComponent::RemoveOwnedLandingGameplayTag()
{
	if (!bAppliedLandingGameplayTag)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetCachedAbilitySystemComponent())
	{
		ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_Movement_Landing);
	}
	bAppliedLandingGameplayTag = false;
}

void UProject_JLocomotionAnimStateComponent::ClearOwnedMovementGameplayTags()
{
	RemoveOwnedInAirGameplayTag();
	RemoveOwnedLandingGameplayTag();
}

void UProject_JLocomotionAnimStateComponent::ClearMovementRequests()
{
	bStartRequested = false;
	bPendingStartRequest = false;
	bPendingStopRequest = false;
	ResetGroundMotionTransitionRequests();
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
	ClearPendingAnimationExitRequests();
	bLandingCancelEventDispatched = false;
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

void UProject_JLocomotionAnimStateComponent::ClearPendingAnimationExitRequests()
{
	bPendingGroundStartFinish = false;
	bGroundStartFinishPendingExit = false;
	bStopFinishPendingExit = false;
	bJumpStartFinishPendingExit = false;
	bFallOffStartFinishPendingExit = false;
	bLandingFinishPendingExit = false;
}

void UProject_JLocomotionAnimStateComponent::ClearResolvedMoveInputState()
{
	bHasMoveInput = false;
	bPrevHasMoveInput = false;
	bResolvedMoveInputLastUpdate = false;
	MoveInputSize = 0.0f;
	MoveInputHeldTime = 0.0f;
	PreviousMoveInputForTurn = FVector2D::ZeroVector;
}
