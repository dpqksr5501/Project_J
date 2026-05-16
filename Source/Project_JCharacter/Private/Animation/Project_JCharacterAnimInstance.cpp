// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JCharacterAnimInstance.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Project_JPlayerCharacter.h"

void UProject_JCharacterAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	CacheOwningCharacter();
}

void UProject_JCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	DeltaTime = DeltaSeconds;

	if (!OwningPawn || OwningPawn != TryGetPawnOwner())
	{
		CacheOwningCharacter();
	}

	if (!OwningCharacter)
	{
		ResetAnimationState();
		return;
	}

	if (OwningPlayerCharacter)
	{
		UpdateFromPlayerCharacter(DeltaSeconds, *OwningPlayerCharacter);
	}
	else
	{
		UpdateFromGenericCharacter(DeltaSeconds);
	}
}

void UProject_JCharacterAnimInstance::CacheOwningCharacter()
{
	OwningPawn = TryGetPawnOwner();
	OwningCharacter = Cast<ACharacter>(OwningPawn);
	OwningPlayerCharacter = Cast<AProject_JPlayerCharacter>(OwningCharacter);
}

void UProject_JCharacterAnimInstance::ResetAnimationState()
{
	GroundSpeed = 0.0f;
	VerticalSpeed = 0.0f;
	LastFallSpeed = 0.0f;

	bIsInAir = false;
	bIsPhysicallyInAir = false;
	bIsJumping = false;
	bIsFallOffStart = false;
	bIsLanding = false;
	bLandingRequested = false;
	bCanEnterLand = false;
	bCanEnterGround = true;

	MoveInputSize = 0.0f;
	MoveInputHeldTime = 0.0f;
	bHasMoveInput = false;
	bHasSideMoveInput = false;
	bPrevHasMoveInput = false;
	bStartRequested = false;
	bStopRequested = false;
	bStartToLoopRequested = false;
	StartRequestTimer = 0.0f;
	StopRequestTimer = 0.0f;
	StopIntentSpeedThreshold = 80.0f;
	IdleSpeedThreshold = 30.0f;
	RunToSprintSpeedThreshold = 500.0f;
	bJustStartedMoving = false;
	bWantsToStop = false;

	bIsSprinting = false;
	bStartWasSprinting = false;
	StopStartSpeed = 0.0f;

	bIsCombatMode = false;
	bIsAttacking = false;
	bIsDodging = false;
	bIsHitReacting = false;
	bIsPlayingCombatIntro = false;
	bPendingCombatModeFromIntro = false;
	MovementDirection = 0.0f;
	CombatInputForward = 0.0f;
	CombatInputRight = 0.0f;
	CombatForwardSpeed = 0.0f;
	CombatRightSpeed = 0.0f;
}

void UProject_JCharacterAnimInstance::UpdateFromGenericCharacter(float DeltaSeconds)
{
	(void)DeltaSeconds;

	if (!OwningCharacter)
	{
		ResetAnimationState();
		return;
	}

	const FVector Velocity = OwningCharacter->GetVelocity();
	GroundSpeed = FVector(Velocity.X, Velocity.Y, 0.0f).Size();
	VerticalSpeed = Velocity.Z;

	const UCharacterMovementComponent* MovementComponent = OwningCharacter->GetCharacterMovement();
	bIsInAir = MovementComponent ? MovementComponent->IsFalling() : false;
	bIsPhysicallyInAir = bIsInAir;
	bIsJumping = false;
	bIsFallOffStart = false;
	bIsLanding = false;
	bLandingRequested = false;
	bCanEnterLand = false;
	bCanEnterGround = !bIsInAir;

	MoveInputSize = 0.0f;
	MoveInputHeldTime = 0.0f;
	bHasMoveInput = GroundSpeed > 3.0f;
	bHasSideMoveInput = false;
	bPrevHasMoveInput = bHasMoveInput;
	bStartRequested = false;
	bStopRequested = false;
	bStartToLoopRequested = bHasMoveInput;
	StartRequestTimer = 0.0f;
	StopRequestTimer = 0.0f;
	StopIntentSpeedThreshold = 80.0f;
	IdleSpeedThreshold = 30.0f;
	RunToSprintSpeedThreshold = 500.0f;
	bJustStartedMoving = false;
	bWantsToStop = false;

	bIsSprinting = false;
	bStartWasSprinting = false;
	StopStartSpeed = 0.0f;

	bIsCombatMode = false;
	bIsAttacking = false;
	bIsDodging = false;
	bIsHitReacting = false;
	bIsPlayingCombatIntro = false;
	bPendingCombatModeFromIntro = false;
	MovementDirection = 0.0f;
	CombatInputForward = 0.0f;
	CombatInputRight = 0.0f;
	CombatForwardSpeed = 0.0f;
	CombatRightSpeed = 0.0f;
}

void UProject_JCharacterAnimInstance::UpdateFromPlayerCharacter(float DeltaSeconds, const AProject_JPlayerCharacter& PlayerCharacter)
{
	(void)DeltaSeconds;

	GroundSpeed = PlayerCharacter.GroundSpeed;
	VerticalSpeed = PlayerCharacter.VerticalSpeed;
	LastFallSpeed = PlayerCharacter.LastFallSpeed;

	bIsInAir = PlayerCharacter.bIsInAir;
	bIsPhysicallyInAir = PlayerCharacter.bIsPhysicallyInAir;
	bIsJumping = PlayerCharacter.bIsJumping;
	bIsFallOffStart = PlayerCharacter.bIsFallOffStart;
	bIsLanding = PlayerCharacter.bIsLanding;
	bLandingRequested = PlayerCharacter.bLandingRequested;
	bCanEnterLand = PlayerCharacter.bCanEnterLand;
	bCanEnterGround = PlayerCharacter.bCanEnterGround;

	MoveInputSize = PlayerCharacter.MoveInputSize;
	MoveInputHeldTime = PlayerCharacter.MoveInputHeldTime;
	bHasMoveInput = PlayerCharacter.bHasMoveInput;
	bHasSideMoveInput = PlayerCharacter.bHasSideMoveInput;
	bPrevHasMoveInput = PlayerCharacter.bPrevHasMoveInput;
	bStartRequested = PlayerCharacter.bStartRequested;
	bStopRequested = PlayerCharacter.bStopRequested;
	bStartToLoopRequested = PlayerCharacter.bStartToLoopRequested;
	StartRequestTimer = PlayerCharacter.StartRequestTimer;
	StopRequestTimer = PlayerCharacter.StopRequestTimer;
	StopIntentSpeedThreshold = PlayerCharacter.StopIntentSpeedThreshold;
	IdleSpeedThreshold = PlayerCharacter.IdleSpeedThreshold;
	RunToSprintSpeedThreshold = PlayerCharacter.RunToSprintSpeedThreshold;
	bJustStartedMoving = PlayerCharacter.bJustStartedMoving;
	bWantsToStop = PlayerCharacter.bWantsToStop;

	bIsSprinting = PlayerCharacter.bIsSprinting;
	bStartWasSprinting = PlayerCharacter.bStartWasSprinting;
	StopStartSpeed = PlayerCharacter.StopStartSpeed;

	bIsCombatMode = PlayerCharacter.bIsCombatMode;
	bIsAttacking = PlayerCharacter.bIsAttacking;
	bIsDodging = PlayerCharacter.bIsDodging;
	bIsHitReacting = PlayerCharacter.bIsHitReacting;
	bIsPlayingCombatIntro = PlayerCharacter.bIsPlayingCombatIntro;
	bPendingCombatModeFromIntro = PlayerCharacter.bPendingCombatModeFromIntro;
	MovementDirection = PlayerCharacter.MovementDirection;
	CombatInputForward = PlayerCharacter.CombatInputForward;
	CombatInputRight = PlayerCharacter.CombatInputRight;
	CombatForwardSpeed = PlayerCharacter.CombatForwardSpeed;
	CombatRightSpeed = PlayerCharacter.CombatRightSpeed;
}
