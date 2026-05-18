// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JCharacterAnimInstance.h"

#include "GameFramework/Controller.h"
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

	UpdateAimOffset();
}

void UProject_JCharacterAnimInstance::MarkGroundStartFinished()
{
	if (OwningPlayerCharacter)
	{
		OwningPlayerCharacter->MarkGroundStartFinished();
	}

	bGroundStartFinished = true;
	bUseStartDatabase = false;
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
	LandStartGroundSpeed = 0.0f;
	LandStartFallSpeed = 0.0f;
	bLandWasSprinting = false;
	bLandWasMoving = false;
	bUseHeavyLand = false;

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
	MoveInputTurnAngle = 0.0f;
	bHasMoveInput = false;
	bSharpTurnRequested = false;
	bPrevHasMoveInput = false;
	bStartRequested = false;
	bUseStartDatabase = false;
	bGroundStartFinished = false;
	bPendingGroundStartFinish = false;
	bStartWasSprinting = false;
	bStopRequested = false;
	StopIntentSpeedThreshold = 80.0f;
	IdleSpeedThreshold = 30.0f;
	RunToSprintSpeedThreshold = 500.0f;
	SharpTurnAngleThreshold = 60.0f;
	SharpTurnMinSpeed = 500.0f;

	bIsSprinting = false;

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
	AimYaw = 0.0f;
	AimPitch = 0.0f;
	AimOffsetAlpha = 0.0f;
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
	LastFallSpeed = 0.0f;
	LandStartGroundSpeed = 0.0f;
	LandStartFallSpeed = 0.0f;
	bLandWasSprinting = false;
	bLandWasMoving = false;
	bUseHeavyLand = false;

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
	MoveInputTurnAngle = 0.0f;
	bHasMoveInput = GroundSpeed > GenericMoveInputSpeedThreshold;
	bSharpTurnRequested = false;
	bPrevHasMoveInput = bHasMoveInput;
	bStartRequested = false;
	bUseStartDatabase = false;
	bGroundStartFinished = false;
	bPendingGroundStartFinish = false;
	bStartWasSprinting = false;
	bStopRequested = false;
	StopIntentSpeedThreshold = 80.0f;
	IdleSpeedThreshold = 30.0f;
	RunToSprintSpeedThreshold = 500.0f;
	SharpTurnAngleThreshold = 60.0f;
	SharpTurnMinSpeed = 500.0f;

	bIsSprinting = false;

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
	LandStartGroundSpeed = PlayerCharacter.LandStartGroundSpeed;
	LandStartFallSpeed = PlayerCharacter.LandStartFallSpeed;
	bLandWasSprinting = PlayerCharacter.bLandWasSprinting;
	bLandWasMoving = PlayerCharacter.bLandWasMoving;
	bUseHeavyLand = PlayerCharacter.bUseHeavyLand;

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
	MoveInputTurnAngle = PlayerCharacter.MoveInputTurnAngle;
	bHasMoveInput = PlayerCharacter.bHasMoveInput;
	bSharpTurnRequested = PlayerCharacter.bSharpTurnRequested;
	bPrevHasMoveInput = PlayerCharacter.bPrevHasMoveInput;
	bStartRequested = PlayerCharacter.bStartRequested;
	bUseStartDatabase = PlayerCharacter.bUseStartDatabase;
	bGroundStartFinished = PlayerCharacter.bGroundStartFinished;
	bPendingGroundStartFinish = PlayerCharacter.bPendingGroundStartFinish;
	bStartWasSprinting = PlayerCharacter.bStartWasSprinting;
	bStopRequested = PlayerCharacter.bStopRequested;
	StopIntentSpeedThreshold = PlayerCharacter.StopIntentSpeedThreshold;
	IdleSpeedThreshold = PlayerCharacter.IdleSpeedThreshold;
	RunToSprintSpeedThreshold = PlayerCharacter.RunToSprintSpeedThreshold;
	SharpTurnAngleThreshold = PlayerCharacter.SharpTurnAngleThreshold;
	SharpTurnMinSpeed = PlayerCharacter.SharpTurnMinSpeed;

	bIsSprinting = PlayerCharacter.bIsSprinting;

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

void UProject_JCharacterAnimInstance::UpdateAimOffset()
{
	if (!OwningCharacter || !OwningCharacter->GetController())
	{
		AimYaw = 0.0f;
		AimPitch = 0.0f;
		AimOffsetAlpha = 0.0f;
		return;
	}

	const FRotator ControlRotation = OwningCharacter->GetControlRotation();
	const FRotator ActorRotation = OwningCharacter->GetActorRotation();

	const float RawAimYaw = FMath::FindDeltaAngleDegrees(ActorRotation.Yaw, ControlRotation.Yaw);
	const float RawAimPitch = FRotator::NormalizeAxis(ControlRotation.Pitch);

	AimYaw = FMath::Clamp(RawAimYaw, -MaxAimYaw, MaxAimYaw);
	AimPitch = FMath::Clamp(RawAimPitch, -MaxAimPitch, MaxAimPitch);
	AimOffsetAlpha = CalculateAimOffsetAlpha();
}

float UProject_JCharacterAnimInstance::CalculateAimOffsetAlpha() const
{
	return 1.0f;
}
