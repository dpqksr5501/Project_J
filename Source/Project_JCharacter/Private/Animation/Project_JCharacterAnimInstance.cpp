// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JCharacterAnimInstance.h"

#include "GameFramework/Controller.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Project_JLocomotionAnimStateComponent.h"
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
	else if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->MarkGroundStartFinished();
	}

	bGroundStartFinished = true;
	bUseStartDatabase = false;
}

void UProject_JCharacterAnimInstance::MarkLandingFinished()
{
	if (OwningPlayerCharacter)
	{
		OwningPlayerCharacter->FinishLanding();
	}
	else if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->FinishLanding();
	}

	bIsLanding = false;
	bCanEnterGround = true;
}

void UProject_JCharacterAnimInstance::HandleLocomotionAnimEvent(EProject_JLocomotionAnimEvent EventType)
{
	if (OwningPlayerCharacter)
	{
		if (UProject_JLocomotionAnimStateComponent* AnimState = OwningPlayerCharacter->GetLocomotionAnimStateComponent())
		{
			AnimState->HandleAnimationEvent(EventType);
		}
	}
	else if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->HandleAnimationEvent(EventType);
	}

	switch (EventType)
	{
	case EProject_JLocomotionAnimEvent::GroundStartFinished:
		bGroundStartFinished = true;
		bUseStartDatabase = false;
		break;
	case EProject_JLocomotionAnimEvent::StopFinished:
		bStopRequested = false;
		bIsStopping = false;
		break;
	case EProject_JLocomotionAnimEvent::JumpStartFinished:
		bIsJumping = false;
		break;
	case EProject_JLocomotionAnimEvent::FallOffStartFinished:
		bIsFallOffStart = false;
		break;
	case EProject_JLocomotionAnimEvent::LandingFinished:
		bIsLanding = false;
		bCanEnterGround = true;
		break;
	case EProject_JLocomotionAnimEvent::HitReactFinished:
		bIsHitReacting = false;
		break;
	case EProject_JLocomotionAnimEvent::AttackFinished:
		bIsAttacking = false;
		break;
	default:
		break;
	}
}

void UProject_JCharacterAnimInstance::CacheOwningCharacter()
{
	OwningPawn = TryGetPawnOwner();
	OwningCharacter = Cast<ACharacter>(OwningPawn);
	OwningPlayerCharacter = Cast<AProject_JPlayerCharacter>(OwningCharacter);
	LocomotionAnimStateComponent = OwningPlayerCharacter ? OwningPlayerCharacter->GetLocomotionAnimStateComponent() : nullptr;
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
		bCanExitLanding = true;

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
	bIsStopping = false;
	StopIntentSpeedThreshold = 80.0f;
	StopRequestDuration = 0.35f;
	IdleSpeedThreshold = 30.0f;
	RunToSprintSpeedThreshold = 500.0f;
	SharpTurnAngleThreshold = 60.0f;
	SharpTurnMinSpeed = 500.0f;

	bIsSprinting = false;
	bWantsSprint = false;

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
	bCanExitLanding = true;

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
	bIsStopping = false;
	StopIntentSpeedThreshold = 80.0f;
	StopRequestDuration = 0.35f;
	IdleSpeedThreshold = 30.0f;
	RunToSprintSpeedThreshold = 500.0f;
	SharpTurnAngleThreshold = 60.0f;
	SharpTurnMinSpeed = 500.0f;

	bIsSprinting = false;
	bWantsSprint = false;

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

	if (!LocomotionAnimStateComponent || LocomotionAnimStateComponent->GetOwner() != &PlayerCharacter)
	{
		LocomotionAnimStateComponent = PlayerCharacter.GetLocomotionAnimStateComponent();
	}

	const UProject_JLocomotionAnimStateComponent* AnimState = LocomotionAnimStateComponent.Get();
	if (AnimState)
	{
		GroundSpeed = AnimState->GroundSpeed;
		VerticalSpeed = AnimState->VerticalSpeed;
		LastFallSpeed = AnimState->LastFallSpeed;
		LandStartGroundSpeed = AnimState->LandStartGroundSpeed;
		LandStartFallSpeed = AnimState->LandStartFallSpeed;
		bLandWasSprinting = AnimState->bLandWasSprinting;
		bLandWasMoving = AnimState->bLandWasMoving;
		bUseHeavyLand = AnimState->bUseHeavyLand;

		bIsInAir = AnimState->bIsInAir;
		bIsPhysicallyInAir = AnimState->bIsPhysicallyInAir;
		bIsJumping = AnimState->bIsJumping;
		bIsFallOffStart = AnimState->bIsFallOffStart;
		bIsLanding = AnimState->bIsLanding;
		bLandingRequested = AnimState->bLandingRequested;
		bCanEnterLand = AnimState->bCanEnterLand;
		bCanEnterGround = AnimState->bCanEnterGround;
		bCanExitLanding = AnimState->bCanExitLanding;

		MoveInputSize = AnimState->MoveInputSize;
		MoveInputHeldTime = AnimState->MoveInputHeldTime;
		MoveInputTurnAngle = AnimState->MoveInputTurnAngle;
		bHasMoveInput = AnimState->bHasMoveInput;
		bSharpTurnRequested = AnimState->bSharpTurnRequested;
		bPrevHasMoveInput = AnimState->bPrevHasMoveInput;
		bStartRequested = AnimState->bStartRequested;
		bUseStartDatabase = AnimState->bUseStartDatabase;
		bGroundStartFinished = AnimState->bGroundStartFinished;
		bPendingGroundStartFinish = AnimState->bPendingGroundStartFinish;
		bStartWasSprinting = AnimState->bStartWasSprinting;
		bWantsSprint = AnimState->bWantsSprint;
		bStopRequested = AnimState->bStopRequested;
		bIsStopping = AnimState->bIsStopping;
		StopIntentSpeedThreshold = AnimState->StopIntentSpeedThreshold;
		StopRequestDuration = AnimState->StopRequestDuration;
		IdleSpeedThreshold = AnimState->IdleSpeedThreshold;
		RunToSprintSpeedThreshold = AnimState->RunToSprintSpeedThreshold;
		SharpTurnAngleThreshold = AnimState->SharpTurnAngleThreshold;
		SharpTurnMinSpeed = AnimState->SharpTurnMinSpeed;
		MovementDirection = AnimState->MovementDirection;
		CombatInputForward = AnimState->CombatInputForward;
		CombatInputRight = AnimState->CombatInputRight;
		CombatForwardSpeed = AnimState->CombatForwardSpeed;
		CombatRightSpeed = AnimState->CombatRightSpeed;
	}
	else
	{
		UpdateFromGenericCharacter(DeltaSeconds);
	}

	bIsSprinting = PlayerCharacter.bIsSprinting;
	bWantsSprint = bWantsSprint || bIsSprinting;

	bIsCombatMode = PlayerCharacter.bIsCombatMode;
	bIsAttacking = PlayerCharacter.bIsAttacking;
	bIsDodging = PlayerCharacter.bIsDodging;
	bIsHitReacting = PlayerCharacter.bIsHitReacting;
	bIsPlayingCombatIntro = PlayerCharacter.bIsPlayingCombatIntro;
	bPendingCombatModeFromIntro = PlayerCharacter.bPendingCombatModeFromIntro;
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
