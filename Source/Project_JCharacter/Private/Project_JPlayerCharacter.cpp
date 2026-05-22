// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JPlayerCharacter.h"
#include "Project_JCombatComponent.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Project_JGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "InputCoreTypes.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

AProject_JPlayerCharacter::AProject_JPlayerCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	PrimaryActorTick.bCanEverTick = true; // Tick 활성화

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, WalkRotationRateYaw, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	GetMesh()->SetAnimInstanceClass(UProject_JCharacterAnimInstance::StaticClass());

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	LocomotionAnimStateComponent = CreateDefaultSubobject<UProject_JLocomotionAnimStateComponent>(TEXT("LocomotionAnimStateComponent"));
	MotionMatchingTrajectoryComponent = CreateDefaultSubobject<UProject_JMotionMatchingTrajectoryComponent>(TEXT("MotionMatchingTrajectoryComponent"));
}

void AProject_JPlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AProject_JPlayerCharacter, bIsSprinting, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AProject_JPlayerCharacter, MoveStartEventCounter, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AProject_JPlayerCharacter, bMoveStartWasSprinting, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AProject_JPlayerCharacter, JumpStartEventCounter, COND_SkipOwner);
}

void AProject_JPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Dynamically find and cache the active combat component if added in Blueprint
	ActiveCombatComponent = FindComponentByClass<UProject_JCombatComponent>();
}

void AProject_JPlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->UpdateState(DeltaTime);
	}
}

void AProject_JPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {

		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AProject_JPlayerCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &AProject_JPlayerCharacter::DoJumpEnd);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AProject_JPlayerCharacter::Move);
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &AProject_JPlayerCharacter::StopMoveInput);
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Canceled, this, &AProject_JPlayerCharacter::StopMoveInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AProject_JPlayerCharacter::Look);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AProject_JPlayerCharacter::Look);

		// Sprinting
		EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this, &AProject_JPlayerCharacter::StartSprint);
		EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this, &AProject_JPlayerCharacter::StopSprint);

		// Toggle Combat Mode
		EnhancedInputComponent->BindAction(ToggleCombatAction, ETriggerEvent::Started, this, &AProject_JPlayerCharacter::ToggleCombatMode);

		// Player Melee Attack
		EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Started, this, &AProject_JPlayerCharacter::TriggerPlayerAttack);
	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AProject_JPlayerCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AProject_JPlayerCharacter::StopMoveInput()
{
	bHadMoveInputForReplication = false;

	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->ClearMoveInput();
	}
}

void AProject_JPlayerCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AProject_JPlayerCharacter::DoMove(float Right, float Forward)
{
	const FVector2D MoveInput(Right, Forward);
	const float MoveInputDeadZone = LocomotionAnimStateComponent ? LocomotionAnimStateComponent->MoveInputDeadZone : 0.1f;
	const bool bHasMoveInput = MoveInput.SizeSquared() > FMath::Square(MoveInputDeadZone);

	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->SetMoveInput(MoveInput);
	}

	if (bHasMoveInput && !bHadMoveInputForReplication)
	{
		NotifyMoveStartForRemoteClients();
	}
	bHadMoveInputForReplication = bHasMoveInput;

	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void AProject_JPlayerCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AProject_JPlayerCharacter::DoJumpStart()
{
	if (LocomotionAnimStateComponent)
	{
		if (!LocomotionAnimStateComponent->CanStartJumpForAnimation())
		{
			return;
		}

		LocomotionAnimStateComponent->HandleJumpStarted();
	}

	NotifyJumpStartForRemoteClients();
	Jump();
}

void AProject_JPlayerCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}

void AProject_JPlayerCharacter::FinishFallOffStart()
{
	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->FinishFallOffStart();
	}
}

void AProject_JPlayerCharacter::FinishJumpStart()
{
	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->FinishJumpStart();
	}
}

void AProject_JPlayerCharacter::MarkGroundStartFinished()
{
	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->MarkGroundStartFinished();
	}
}

void AProject_JPlayerCharacter::ApplyCombatRotationMode(bool bEnableCombatRotation)
{
	bUseControllerRotationYaw = bEnableCombatRotation;

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = !bEnableCombatRotation;
	}
}

void AProject_JPlayerCharacter::UpdateMaxWalkSpeed()
{
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		const bool bCanSprint = bIsSprinting && !bIsAttacking && !bIsDodging && !bIsHitReacting;
		MoveComp->MaxWalkSpeed = bCanSprint ? SprintSpeed : WalkSpeed;
		MoveComp->RotationRate = FRotator(0.0f, bCanSprint ? SprintRotationRateYaw : WalkRotationRateYaw, 0.0f);
	}
}

void AProject_JPlayerCharacter::ApplySprintState(bool bNewIsSprinting)
{
	if (bIsSprinting == bNewIsSprinting)
	{
		UpdateMaxWalkSpeed();
		return;
	}

	bIsSprinting = bNewIsSprinting;
	UpdateMaxWalkSpeed();

	if (LocomotionAnimStateComponent)
	{
		if (bIsSprinting)
		{
			LocomotionAnimStateComponent->HandleSprintStarted();
		}
		else
		{
			LocomotionAnimStateComponent->HandleSprintStopped();
		}
	}
}

void AProject_JPlayerCharacter::ServerSetSprinting_Implementation(bool bNewIsSprinting)
{
	ApplySprintState(bNewIsSprinting);
	ForceNetUpdate();
}

void AProject_JPlayerCharacter::NotifyMoveStartForRemoteClients()
{
	if (HasAuthority())
	{
		bMoveStartWasSprinting = bIsSprinting;
		++MoveStartEventCounter;
		ForceNetUpdate();
		return;
	}

	ServerNotifyMoveStarted(bIsSprinting);
}

void AProject_JPlayerCharacter::ServerNotifyMoveStarted_Implementation(bool bWasSprintingForStart)
{
	bMoveStartWasSprinting = bWasSprintingForStart || bIsSprinting;
	++MoveStartEventCounter;
	ForceNetUpdate();
}

void AProject_JPlayerCharacter::OnRep_MoveStartEventCounter()
{
	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->HandleReplicatedMoveStarted(bMoveStartWasSprinting);
	}
}

void AProject_JPlayerCharacter::NotifyJumpStartForRemoteClients()
{
	if (HasAuthority())
	{
		++JumpStartEventCounter;
		ForceNetUpdate();
		return;
	}

	ServerNotifyJumpStarted();
}

void AProject_JPlayerCharacter::ServerNotifyJumpStarted_Implementation()
{
	++JumpStartEventCounter;
	ForceNetUpdate();
}

void AProject_JPlayerCharacter::OnRep_JumpStartEventCounter()
{
	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->HandleReplicatedJumpStarted();
	}
}

void AProject_JPlayerCharacter::OnRep_IsSprinting()
{
	UpdateMaxWalkSpeed();

	if (LocomotionAnimStateComponent)
	{
		if (bIsSprinting)
		{
			LocomotionAnimStateComponent->HandleSprintStarted();
		}
		else
		{
			LocomotionAnimStateComponent->HandleSprintStopped();
		}
	}
}

void AProject_JPlayerCharacter::StartSprint()
{
	ApplySprintState(true);
	if (!HasAuthority())
	{
		ServerSetSprinting(true);
	}
}

void AProject_JPlayerCharacter::StopSprint()
{
	ApplySprintState(false);
	if (!HasAuthority())
	{
		ServerSetSprinting(false);
	}
}

void AProject_JPlayerCharacter::ToggleCombatMode()
{
	if (bIsCombatMode)
	{
		CancelCombatIntroMontage();
		SetCombatMode(false);
		return;
	}

	if (bIsPlayingCombatIntro || bPendingCombatModeFromIntro)
	{
		CancelCombatIntroMontage();
		ApplyCombatRotationMode(false);
		return;
	}

	BeginCombatModeWithIntro();
}

void AProject_JPlayerCharacter::SetCombatMode(bool bInCombatMode)
{
	if (bIsCombatMode == bInCombatMode)
	{
		return;
	}

	bIsCombatMode = bInCombatMode;

	// Update weapon visibility
	if (ActiveCombatComponent)
	{
		if (bIsCombatMode)
		{
			ActiveCombatComponent->EquipWeapon();
		}
		else
		{
			ActiveCombatComponent->UnequipWeapon();
		}
	}

	// Update Gameplay Tags
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		if (bIsCombatMode)
		{
			ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		}
		else
		{
			ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		}
	}

	// Update movement rotation settings for Strafe vs Free movement
	if (bIsCombatMode)
	{
		ApplyCombatRotationMode(true);
	}
	else
	{
		CancelCombatIntroMontage();
		ApplyCombatRotationMode(false);
	}
}

void AProject_JPlayerCharacter::BeginCombatModeWithIntro()
{
	if (bIsCombatMode || bIsPlayingCombatIntro || bPendingCombatModeFromIntro || bIsHitReacting)
	{
		return;
	}

	ApplyCombatRotationMode(true);
	PlayCombatIntroMontage();

	if (!bPendingCombatModeFromIntro)
	{
		SetCombatMode(true);
	}
}

void AProject_JPlayerCharacter::PlayCombatIntroMontage()
{
	if (!CombatIntroMontage || bIsPlayingCombatIntro || bIsHitReacting)
	{
		return;
	}

	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return;
	}

	const float Duration = PlayAnimMontage(CombatIntroMontage, CombatIntroMontagePlayRate);
	if (Duration <= 0.0f)
	{
		return;
	}

	bIsPlayingCombatIntro = true;
	bPendingCombatModeFromIntro = true;

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AProject_JPlayerCharacter::OnCombatIntroMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, CombatIntroMontage);
}

void AProject_JPlayerCharacter::CancelCombatIntroMontage()
{
	if (bIsPlayingCombatIntro)
	{
		StopAnimMontage(CombatIntroMontage);
	}

	bIsPlayingCombatIntro = false;
	bPendingCombatModeFromIntro = false;
}

void AProject_JPlayerCharacter::OnCombatIntroMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage == CombatIntroMontage)
	{
		bIsPlayingCombatIntro = false;

		if (bPendingCombatModeFromIntro)
		{
			bPendingCombatModeFromIntro = false;

			if (!bInterrupted && !bIsHitReacting)
			{
				SetCombatMode(true);
			}
			else if (!bIsCombatMode)
			{
				ApplyCombatRotationMode(false);
			}
		}
	}
}

void AProject_JPlayerCharacter::InterruptCombatIntroForHit()
{
	if (!bInterruptCombatIntroOnHit || !bIsPlayingCombatIntro)
	{
		return;
	}

	StopAnimMontage(CombatIntroMontage);
	bIsPlayingCombatIntro = false;
	bPendingCombatModeFromIntro = false;
	if (!bIsCombatMode)
	{
		ApplyCombatRotationMode(false);
	}
}

void AProject_JPlayerCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->HandleLanded(Hit);

		if (LocomotionAnimStateComponent->ConsumeRealLandingEventRequested())
		{
			K2_OnRealLanded();
		}
	}
}

void AProject_JPlayerCharacter::FinishLanding(bool bForceFinish)
{
	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->FinishLanding(bForceFinish);
	}
}

void AProject_JPlayerCharacter::TriggerPlayerAttack()
{
	if (ActiveCombatComponent)
	{
		ActiveCombatComponent->Attack();
	}
}
