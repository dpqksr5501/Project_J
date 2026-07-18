#include "Mount/Project_JFlyingMountCharacter.h"

#include "Components/CapsuleComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimTypes.h"
#include "Animation/Project_JAnimNotify_MountFlightCue.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"

AProject_JFlyingMountCharacter::AProject_JFlyingMountCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	MovementComponent->MaxFlySpeed = FlightSpeed;
	DefaultBrakingDecelerationFlying = MovementComponent->BrakingDecelerationFlying;
	DefaultBrakingFriction = MovementComponent->BrakingFriction;
	MovementComponent->bUseSeparateBrakingFriction = true;
}

void AProject_JFlyingMountCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AProject_JFlyingMountCharacter, bIsGliding);
	DOREPLIFETIME(AProject_JFlyingMountCharacter, FlightState);
	DOREPLIFETIME(AProject_JFlyingMountCharacter, FlightPhaseStartServerTime);
}

bool AProject_JFlyingMountCharacter::BeginFlight()
{
	if (!HasAuthority() || !IsOccupied() || Health <= 0.0f || FlightState != EProject_JMountFlightState::Grounded || !GetCharacterMovement()->IsMovingOnGround())
	{
		return false;
	}

	if (!HasVerticalClearance(RequiredTakeOffClearance))
	{
		return false;
	}

	bIsGliding = false;
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	GetCharacterMovement()->MaxFlySpeed = FlightSpeed;
	ApplyFlightBraking(false);
	SetFlightState(EProject_JMountFlightState::TakingOff);
	ActiveTakeOffImpulseTime = ResolveTakeOffImpulseTime();
	return true;
}

bool AProject_JFlyingMountCharacter::CommitTakeOffImpulse()
{
	if (!HasAuthority() || FlightState != EProject_JMountFlightState::TakingOff)
	{
		return false;
	}

	if (!HasVerticalClearance(AutoTakeOffHeight))
	{
		FinishLanding();
		return false;
	}

	AutoAscentTargetZ = GetActorLocation().Z + AutoTakeOffHeight;
	SetFlightState(EProject_JMountFlightState::AutoAscending);
	return true;
}

bool AProject_JFlyingMountCharacter::EndFlight()
{
	return BeginLanding();
}

void AProject_JFlyingMountCharacter::SetGliding(bool bNewGliding)
{
	if (!HasAuthority() || FlightState != EProject_JMountFlightState::Flying)
	{
		return;
	}

	bIsGliding = bNewGliding;
	GetCharacterMovement()->MaxFlySpeed = bIsGliding ? GlideSpeed : FlightSpeed;
	ApplyFlightBraking(bIsGliding);
}

void AProject_JFlyingMountCharacter::HandleFlightAnimationCue(EProject_JMountFlightAnimationCue Cue)
{
	K2_OnFlightAnimationCue(Cue);

	if (Cue == EProject_JMountFlightAnimationCue::TakeOffImpulse && HasAuthority())
	{
		CommitTakeOffImpulse();
	}
	else if (Cue == EProject_JMountFlightAnimationCue::LandingTouchdown && HasAuthority() && FlightState == EProject_JMountFlightState::Landing)
	{
		bLandingTouchdownCueReached = true;
		if (bLandingTouchdownConfirmed)
		{
			FinishLanding();
		}
	}
}

bool AProject_JFlyingMountCharacter::IsFlyingMount() const
{
	return FlightState != EProject_JMountFlightState::Grounded && GetCharacterMovement()->IsFlying();
}

bool AProject_JFlyingMountCharacter::IsFlightInputLocked() const
{
	return bTakeOffRequestPending ||
		FlightState == EProject_JMountFlightState::TakingOff ||
		FlightState == EProject_JMountFlightState::AutoAscending ||
		FlightState == EProject_JMountFlightState::Landing;
}

void AProject_JFlyingMountCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		if (bTakeOffRequestPending && GetWorld() && GetWorld()->GetTimeSeconds() >= TakeOffRequestExpiryTime)
		{
			bTakeOffRequestPending = false;
		}
		return;
	}

	switch (FlightState)
	{
	case EProject_JMountFlightState::TakingOff:
		if (GetWorld() && GetWorld()->GetTimeSeconds() - FlightPhaseStartServerTime >= ActiveTakeOffImpulseTime)
		{
			CommitTakeOffImpulse();
		}
		break;
	case EProject_JMountFlightState::AutoAscending:
		UpdateAutoAscent(DeltaSeconds);
		break;
	case EProject_JMountFlightState::Flying:
		if (GetVelocity().Z < -5.0f)
		{
			FHitResult LandingHit;
			if (FindLandingSurface(LandingHit))
			{
				BeginLanding();
			}
		}
		break;
	case EProject_JMountFlightState::Landing:
		UpdateLanding(DeltaSeconds);
		break;
	default:
		break;
	}
}

bool AProject_JFlyingMountCharacter::HasVerticalClearance(float Distance) const
{
	if (!GetWorld() || Distance <= 0.0f)
	{
		return true;
	}

	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Capsule)
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MountTakeOffClearance), false, this);
	QueryParams.AddIgnoredActor(GetRider());
	FHitResult Hit;
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
	return !GetWorld()->SweepSingleByChannel(Hit, GetActorLocation(), GetActorLocation() + FVector::UpVector * Distance, FQuat::Identity, ECC_Visibility, Shape, QueryParams);
}

bool AProject_JFlyingMountCharacter::FindLandingSurface(FHitResult& OutHit) const
{
	if (!GetWorld())
	{
		return false;
	}

	const float ProbeDistance = GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + LandingCompletionTolerance + 60.0f;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MountLandingProbe), false, this);
	QueryParams.AddIgnoredActor(GetRider());
	if (!GetWorld()->LineTraceSingleByChannel(OutHit, GetActorLocation(), GetActorLocation() - FVector::UpVector * ProbeDistance, ECC_Visibility, QueryParams) || !OutHit.bBlockingHit)
	{
		return false;
	}

	const float MinimumNormalZ = FMath::Cos(FMath::DegreesToRadians(MaxLandingSlopeDegrees));
	return OutHit.ImpactNormal.Z >= MinimumNormalZ;
}

bool AProject_JFlyingMountCharacter::BeginLanding()
{
	if (!HasAuthority() || (FlightState != EProject_JMountFlightState::Flying && FlightState != EProject_JMountFlightState::AutoAscending))
	{
		return false;
	}

	bIsGliding = false;
	bLandingTouchdownConfirmed = false;
	bLandingTouchdownCueReached = false;
	GetCharacterMovement()->StopMovementImmediately();
	ApplyFlightBraking(false);
	SetFlightState(EProject_JMountFlightState::Landing);
	ActiveLandingTouchdownTime = ResolveFlightCueTime(LandingCueAnimation, EProject_JMountFlightAnimationCue::LandingTouchdown, 0.0f);
	return true;
}

void AProject_JFlyingMountCharacter::FinishLanding()
{
	if (!HasAuthority())
	{
		return;
	}

	bIsGliding = false;
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	GetCharacterMovement()->MaxFlySpeed = FlightSpeed;
	GetCharacterMovement()->BrakingDecelerationFlying = DefaultBrakingDecelerationFlying;
	GetCharacterMovement()->BrakingFriction = DefaultBrakingFriction;
	bLandingTouchdownConfirmed = false;
	bLandingTouchdownCueReached = false;
	SetFlightState(EProject_JMountFlightState::Grounded);
}

void AProject_JFlyingMountCharacter::UpdateAutoAscent(float DeltaSeconds)
{
	if (GetActorLocation().Z >= AutoAscentTargetZ - LandingCompletionTolerance || !HasVerticalClearance(FMath::Min(AutoAscentSpeed * DeltaSeconds + 20.0f, 100.0f)))
	{
		GetCharacterMovement()->StopMovementImmediately();
		SetFlightState(EProject_JMountFlightState::Flying);
		return;
	}

	GetCharacterMovement()->Velocity = FVector::UpVector * AutoAscentSpeed;
}

void AProject_JFlyingMountCharacter::UpdateLanding(float DeltaSeconds)
{
	if (!bLandingTouchdownCueReached && GetWorld() && GetWorld()->GetTimeSeconds() - FlightPhaseStartServerTime >= ActiveLandingTouchdownTime)
	{
		bLandingTouchdownCueReached = true;
	}

	if (bLandingTouchdownConfirmed)
	{
		if (bLandingTouchdownCueReached)
		{
			FinishLanding();
		}
		return;
	}

	FHitResult LandingHit;
	if (FindLandingSurface(LandingHit) && LandingHit.Distance <= GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + LandingCompletionTolerance)
	{
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		bLandingTouchdownConfirmed = true;
		if (bLandingTouchdownCueReached)
		{
			FinishLanding();
		}
		return;
	}

	GetCharacterMovement()->Velocity = FVector::DownVector * LandingDescentSpeed;
}

void AProject_JFlyingMountCharacter::ApplyFlightBraking(bool bGliding)
{
	UCharacterMovementComponent* MovementComponent = GetCharacterMovement();
	MovementComponent->BrakingDecelerationFlying = bGliding ? GlideBrakingDeceleration : FlyingBrakingDeceleration;
	MovementComponent->BrakingFriction = bGliding ? GlideBrakingFriction : FlyingBrakingFriction;
}

void AProject_JFlyingMountCharacter::SetFlightState(EProject_JMountFlightState NewState)
{
	if (FlightState == NewState)
	{
		return;
	}

	const EProject_JMountFlightState PreviousState = FlightState;
	FlightState = NewState;
	FlightPhaseStartServerTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	ApplyFlightStateTags(PreviousState, NewState);
	ForceNetUpdate();
	K2_OnFlightStateChanged(PreviousState, NewState);
}

void AProject_JFlyingMountCharacter::OnRep_FlightState(EProject_JMountFlightState PreviousState)
{
	if (FlightState != EProject_JMountFlightState::Grounded)
	{
		bTakeOffRequestPending = false;
	}

	// Clear locally predicted horizontal velocity as soon as the server enters
	// the landing phase; input is already rejected by IsFlightInputLocked().
	if (FlightState == EProject_JMountFlightState::Landing)
	{
		GetCharacterMovement()->StopMovementImmediately();
	}

	K2_OnFlightStateChanged(PreviousState, FlightState);
}

void AProject_JFlyingMountCharacter::ApplyFlightStateTags(EProject_JMountFlightState PreviousState, EProject_JMountFlightState NewState)
{
	if (!HasAuthority() || !MountAbilitySystemComponent)
	{
		return;
	}

	const FProject_JGameplayTags& GameplayTags = FProject_JGameplayTags::Get();
	MountAbilitySystemComponent->RemoveProjectJLooseGameplayTag(GameplayTags.State_Mount_TakingOff);
	MountAbilitySystemComponent->RemoveProjectJLooseGameplayTag(GameplayTags.State_Mount_AutoAscending);
	MountAbilitySystemComponent->RemoveProjectJLooseGameplayTag(GameplayTags.State_Mount_Flying);
	MountAbilitySystemComponent->RemoveProjectJLooseGameplayTag(GameplayTags.State_Mount_Landing);

	switch (NewState)
	{
	case EProject_JMountFlightState::TakingOff:
		MountAbilitySystemComponent->AddProjectJLooseGameplayTag(GameplayTags.State_Mount_TakingOff);
		break;
	case EProject_JMountFlightState::AutoAscending:
		MountAbilitySystemComponent->AddProjectJLooseGameplayTag(GameplayTags.State_Mount_AutoAscending);
		break;
	case EProject_JMountFlightState::Flying:
		MountAbilitySystemComponent->AddProjectJLooseGameplayTag(GameplayTags.State_Mount_Flying);
		break;
	case EProject_JMountFlightState::Landing:
		MountAbilitySystemComponent->AddProjectJLooseGameplayTag(GameplayTags.State_Mount_Landing);
		break;
	default:
		break;
	}
}

float AProject_JFlyingMountCharacter::ResolveTakeOffImpulseTime() const
{
	return ResolveFlightCueTime(TakeOffCueAnimation, EProject_JMountFlightAnimationCue::TakeOffImpulse, 0.45f);
}

float AProject_JFlyingMountCharacter::ResolveFlightCueTime(const UAnimSequenceBase* Animation, EProject_JMountFlightAnimationCue Cue, float FallbackTime) const
{
	if (Animation)
	{
		for (const FAnimNotifyEvent& NotifyEvent : Animation->Notifies)
		{
			const UProject_JAnimNotify_MountFlightCue* FlightCue =
				Cast<UProject_JAnimNotify_MountFlightCue>(NotifyEvent.Notify);

			if (FlightCue && FlightCue->GetCue() == Cue)
			{
				return FMath::Max(0.0f, NotifyEvent.GetTime());
			}
		}
	}

	// Dedicated servers do not evaluate animation notify tracks. This fallback
	// keeps existing mounts functional until their source animation has a cue.
	return FallbackTime;
}

void AProject_JFlyingMountCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction) Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AProject_JFlyingMountCharacter::HandleMove);
		if (LookAction) Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &AProject_JFlyingMountCharacter::HandleLook);
		if (AscendAction)
		{
			Input->BindAction(AscendAction, ETriggerEvent::Started, this, &AProject_JFlyingMountCharacter::HandleTakeOff);
			Input->BindAction(AscendAction, ETriggerEvent::Triggered, this, &AProject_JFlyingMountCharacter::HandleAscend);
		}
		if (DescendAction) Input->BindAction(DescendAction, ETriggerEvent::Triggered, this, &AProject_JFlyingMountCharacter::HandleDescend);
		if (InteractAction) Input->BindAction(InteractAction, ETriggerEvent::Started, this, &AProject_JFlyingMountCharacter::HandleDismount);
	}
}

void AProject_JFlyingMountCharacter::HandleMove(const FInputActionValue& Value)
{
	if (IsFlightInputLocked()) return;
	const FVector2D Input = Value.Get<FVector2D>();
	const FRotator ControlRotation = GetControlRotation();
	const FRotator YawRotation(0.0f, ControlRotation.Yaw, 0.0f);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Input.Y);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Input.X);
}

void AProject_JFlyingMountCharacter::HandleLook(const FInputActionValue& Value)
{
	const FVector2D Input = Value.Get<FVector2D>();
	AddControllerYawInput(Input.X);
	AddControllerPitchInput(Input.Y);
}

void AProject_JFlyingMountCharacter::HandleAscend(const FInputActionValue& Value)
{
	if (FlightState == EProject_JMountFlightState::Flying)
	{
		AddMovementInput(FVector::UpVector, Value.Get<float>());
	}
}

void AProject_JFlyingMountCharacter::HandleDescend(const FInputActionValue& Value)
{
	if (FlightState == EProject_JMountFlightState::Flying)
	{
		AddMovementInput(FVector::DownVector, Value.Get<float>());
	}
}

void AProject_JFlyingMountCharacter::HandleTakeOff()
{
	if (FlightState == EProject_JMountFlightState::Grounded)
	{
		if (!HasAuthority() && GetWorld())
		{
			bTakeOffRequestPending = true;
			TakeOffRequestExpiryTime = GetWorld()->GetTimeSeconds() + 0.75f;
		}
		ServerRequestBeginFlight();
	}
}

void AProject_JFlyingMountCharacter::ServerRequestBeginFlight_Implementation()
{
	BeginFlight();
}

void AProject_JFlyingMountCharacter::HandleDismount()
{
	if (!IsFlightInputLocked())
	{
		ServerRequestDismount();
	}
}
