// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JPlayerCharacter.h"
#include "Project_JCombatComponent.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "GameplayTagContainer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Project_JGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "UI/Project_JCharacterViewModel.h"
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
	DOREPLIFETIME(AProject_JPlayerCharacter, bIsCombatMode);
	DOREPLIFETIME_CONDITION(AProject_JPlayerCharacter, ReplicatedAnimEvents, COND_SkipOwner);
}

void AProject_JPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Initialize ViewModel
	CharacterViewModel = NewObject<UProject_JCharacterViewModel>(this);

	// Bind GAS Attributes to ViewModel
	if (AbilitySystemComponent && AttributeSet)
	{
		// Initial Values
		CharacterViewModel->SetHealth(AttributeSet->GetHealth());
		CharacterViewModel->SetMaxHealth(AttributeSet->GetMaxHealth());
		CharacterViewModel->SetMana(AttributeSet->GetMana());
		CharacterViewModel->SetMaxMana(AttributeSet->GetMaxMana());
		CharacterViewModel->SetLevel(CharacterLevel);

		// Bind callbacks for future changes
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(AttributeSet->GetHealthAttribute()).AddUObject(this, &AProject_JPlayerCharacter::OnHealthChanged);
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(AttributeSet->GetMaxHealthAttribute()).AddUObject(this, &AProject_JPlayerCharacter::OnMaxHealthChanged);
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(AttributeSet->GetManaAttribute()).AddUObject(this, &AProject_JPlayerCharacter::OnManaChanged);
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(AttributeSet->GetMaxManaAttribute()).AddUObject(this, &AProject_JPlayerCharacter::OnMaxManaChanged);
	}

	ApplyLocomotionProfile();
	LogAnimationProfileConfiguration();

	// Dynamically find and cache the active combat component if added in Blueprint
	ActiveCombatComponent = FindComponentByClass<UProject_JCombatComponent>();
	if (ActiveCombatComponent && AbilitySystemComponent)
	{
		ActiveCombatComponent->BindToGAS(AbilitySystemComponent);
	}
}

void AProject_JPlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->UpdateState(DeltaTime);
	}

	if (bWasSprintLocomotionAllowed != IsSprintLocomotionAllowed())
	{
		UpdateMaxWalkSpeed();
		ApplySprintAnimationState();
	}
}

void AProject_JPlayerCharacter::OnHealthChanged(const FOnAttributeChangeData& Data)
{
	if (CharacterViewModel)
	{
		CharacterViewModel->SetHealth(Data.NewValue);
	}
}

void AProject_JPlayerCharacter::OnMaxHealthChanged(const FOnAttributeChangeData& Data)
{
	if (CharacterViewModel)
	{
		CharacterViewModel->SetMaxHealth(Data.NewValue);
	}
}

void AProject_JPlayerCharacter::OnManaChanged(const FOnAttributeChangeData& Data)
{
	if (CharacterViewModel)
	{
		CharacterViewModel->SetMana(Data.NewValue);
	}
}

void AProject_JPlayerCharacter::OnMaxManaChanged(const FOnAttributeChangeData& Data)
{
	if (CharacterViewModel)
	{
		CharacterViewModel->SetMaxMana(Data.NewValue);
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
	const bool bHadMoveInput = bHadMoveInputForReplication;
	ResetMoveStartReplicationState();

	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->ClearMoveInput();
	}

	if (bHadMoveInput)
	{
		DispatchMoveStopAnimationEvent();
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

	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->SetMoveInput(MoveInput);
	}

	UpdateMoveStartReplicationState(MoveInput);

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

	DispatchJumpStartAnimationEvent();
	Jump();
}

void AProject_JPlayerCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}

void AProject_JPlayerCharacter::NotifyFallOffStartedForAnimation()
{
	DispatchFallOffStartAnimationEvent();
}

void AProject_JPlayerCharacter::NotifyLandingCancelledForAnimation()
{
	DispatchLandingCancelAnimationEvent();
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
	const bool bShouldUseCombatRotation = bEnableCombatRotation && ShouldUseCombatRotationMode();
	bUseControllerRotationYaw = bShouldUseCombatRotation;

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = !bShouldUseCombatRotation;
	}
}

void AProject_JPlayerCharacter::ApplyCombatModeState(bool bNewCombatMode)
{
	bIsCombatMode = bNewCombatMode;

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

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		if (bIsCombatMode)
		{
			if (!bAppliedCombatModeTag)
			{
				ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
				bAppliedCombatModeTag = true;
			}
		}
		else
		{
			if (bAppliedCombatModeTag)
			{
				ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
				bAppliedCombatModeTag = false;
			}
		}
	}

	if (bIsCombatMode)
	{
		ApplyCombatRotationMode(true);
	}
	else
	{
		CancelCombatIntroMontage();
		ApplyCombatRotationMode(false);
	}

	UpdateMaxWalkSpeed();
	ApplySprintAnimationState();
}

bool AProject_JPlayerCharacter::HasCombatStateTag(const FGameplayTag& StateTag) const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	return ASC && StateTag.IsValid() && ASC->HasMatchingGameplayTag(StateTag);
}

bool AProject_JPlayerCharacter::IsCombatActionBlockingSprint() const
{
	return
		(IsCombatModeActive() && !ShouldAllowSprintInCombat()) ||
		IsAttacking() ||
		IsDodging() ||
		IsHitReacting();
}

bool AProject_JPlayerCharacter::ShouldAllowSprintInCombat() const
{
	if (const UProject_JCombatAnimProfile* EffectiveCombatAnimProfile = GetCombatAnimProfile())
	{
		return EffectiveCombatAnimProfile->bAllowSprintInCombat;
	}

	return false;
}

void AProject_JPlayerCharacter::ApplyLocomotionProfile()
{
	if (const UProject_JLocomotionProfile* EffectiveLocomotionProfile = GetLocomotionProfile())
	{
		if (LocomotionAnimStateComponent)
		{
			LocomotionAnimStateComponent->SprintLocomotionSpeedThreshold = EffectiveLocomotionProfile->SprintLocomotionSpeedThreshold;
			LocomotionAnimStateComponent->HiddenRemoteUpdateInterval = EffectiveLocomotionProfile->AnimStateHiddenRemoteUpdateInterval;
		}
	}

	UpdateMaxWalkSpeed();
}

void AProject_JPlayerCharacter::LogAnimationProfileConfiguration() const
{
	const UProject_JLocomotionProfile* EffectiveLocomotionProfile = GetLocomotionProfile();
	const UProject_JMotionMatchingAssetSet* EffectiveAssetSet = GetMotionMatchingAssetSet();

	if (!CharacterAnimProfile && LocomotionProfile)
	{
		UE_LOG(
			LogTemplateCharacter,
			Display,
			TEXT("%s uses LocomotionProfile directly. Prefer assigning CharacterAnimProfile for new characters."),
			*GetNameSafe(this));
	}
	else if (!CharacterAnimProfile && MotionMatchingAssetSet)
	{
		UE_LOG(
			LogTemplateCharacter,
			Display,
			TEXT("%s uses MotionMatchingAssetSet directly. Prefer CharacterAnimProfile -> LocomotionProfile -> MotionMatchingAssetSet."),
			*GetNameSafe(this));
	}

	if (CharacterAnimProfile && !EffectiveLocomotionProfile)
	{
		UE_LOG(
			LogTemplateCharacter,
			Warning,
			TEXT("%s has CharacterAnimProfile %s, but it does not provide a LocomotionProfile."),
			*GetNameSafe(this),
			*GetNameSafe(CharacterAnimProfile));
	}

	if (EffectiveLocomotionProfile && !EffectiveAssetSet)
	{
		UE_LOG(
			LogTemplateCharacter,
			Warning,
			TEXT("%s uses LocomotionProfile %s, but no MotionMatchingAssetSet is assigned."),
			*GetNameSafe(this),
			*GetNameSafe(EffectiveLocomotionProfile));
	}
	else if (!EffectiveLocomotionProfile && !EffectiveAssetSet)
	{
		UE_LOG(
			LogTemplateCharacter,
			Warning,
			TEXT("%s has no CharacterAnimProfile, LocomotionProfile, or MotionMatchingAssetSet assigned."),
			*GetNameSafe(this));
	}
}

void AProject_JPlayerCharacter::UpdateMaxWalkSpeed()
{
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		const bool bCanSprint = IsSprintLocomotionAllowed();
		MoveComp->MaxWalkSpeed = bCanSprint ? GetEffectiveSprintSpeed() : GetEffectiveWalkSpeed();
		MoveComp->RotationRate = FRotator(0.0f, bCanSprint ? GetEffectiveSprintRotationRateYaw() : GetEffectiveWalkRotationRateYaw(), 0.0f);
	}
}

float AProject_JPlayerCharacter::GetEffectiveWalkSpeed() const
{
	if (const UProject_JLocomotionProfile* EffectiveLocomotionProfile = GetLocomotionProfile())
	{
		return EffectiveLocomotionProfile->WalkSpeed;
	}

	return WalkSpeed;
}

float AProject_JPlayerCharacter::GetEffectiveSprintSpeed() const
{
	if (const UProject_JLocomotionProfile* EffectiveLocomotionProfile = GetLocomotionProfile())
	{
		return EffectiveLocomotionProfile->SprintSpeed;
	}

	return SprintSpeed;
}

float AProject_JPlayerCharacter::GetEffectiveWalkRotationRateYaw() const
{
	if (const UProject_JLocomotionProfile* EffectiveLocomotionProfile = GetLocomotionProfile())
	{
		return EffectiveLocomotionProfile->WalkRotationRateYaw;
	}

	return WalkRotationRateYaw;
}

float AProject_JPlayerCharacter::GetEffectiveSprintRotationRateYaw() const
{
	if (const UProject_JLocomotionProfile* EffectiveLocomotionProfile = GetLocomotionProfile())
	{
		return EffectiveLocomotionProfile->SprintRotationRateYaw;
	}

	return SprintRotationRateYaw;
}

UAnimMontage* AProject_JPlayerCharacter::GetEffectiveCombatIntroMontage() const
{
	if (const UProject_JWeaponAnimProfile* EffectiveWeaponAnimProfile = GetWeaponAnimProfile())
	{
		if (EffectiveWeaponAnimProfile->CombatIntroMontage)
		{
			return EffectiveWeaponAnimProfile->CombatIntroMontage.Get();
		}
	}

	return CombatIntroMontage;
}

float AProject_JPlayerCharacter::GetEffectiveCombatIntroMontagePlayRate() const
{
	if (const UProject_JWeaponAnimProfile* EffectiveWeaponAnimProfile = GetWeaponAnimProfile())
	{
		if (EffectiveWeaponAnimProfile->CombatIntroMontage)
		{
			return EffectiveWeaponAnimProfile->CombatIntroMontagePlayRate;
		}
	}

	return CombatIntroMontagePlayRate;
}

bool AProject_JPlayerCharacter::ShouldPlayCombatIntroMontage() const
{
	if (const UProject_JCombatAnimProfile* EffectiveCombatAnimProfile = GetCombatAnimProfile())
	{
		return EffectiveCombatAnimProfile->bPlayIntroMontageWhenEnteringCombat;
	}

	return true;
}

bool AProject_JPlayerCharacter::ShouldUseCombatRotationMode() const
{
	if (const UProject_JCombatAnimProfile* EffectiveCombatAnimProfile = GetCombatAnimProfile())
	{
		return EffectiveCombatAnimProfile->bUseCombatRotationMode;
	}

	return true;
}

bool AProject_JPlayerCharacter::ShouldInterruptCombatIntroOnHit() const
{
	if (const UProject_JCombatAnimProfile* EffectiveCombatAnimProfile = GetCombatAnimProfile())
	{
		return EffectiveCombatAnimProfile->bInterruptCombatIntroOnHit;
	}

	return bInterruptCombatIntroOnHit;
}

float AProject_JPlayerCharacter::GetEffectiveCombatAimAlpha() const
{
	if (const UProject_JCombatAnimProfile* EffectiveCombatAnimProfile = GetCombatAnimProfile())
	{
		return EffectiveCombatAnimProfile->CombatAimAlpha;
	}

	return 1.0f;
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
	ApplySprintAnimationState();
}

void AProject_JPlayerCharacter::ApplySprintAnimationState()
{
	bWasSprintLocomotionAllowed = IsSprintLocomotionAllowed();

	if (LocomotionAnimStateComponent)
	{
		if (bWasSprintLocomotionAllowed)
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

void AProject_JPlayerCharacter::ServerSetCombatMode_Implementation(bool bNewCombatMode)
{
	ApplyCombatModeState(bNewCombatMode);
	ForceNetUpdate();
}

float AProject_JPlayerCharacter::GetMoveInputDeadZoneForAnimation() const
{
	return LocomotionAnimStateComponent ? LocomotionAnimStateComponent->MoveInputDeadZone : 0.1f;
}

const UProject_JLocomotionProfile* AProject_JPlayerCharacter::GetLocomotionProfile() const
{
	// Preferred path: a top-level character profile owns the effective locomotion profile.
	// Direct LocomotionProfile remains as a migration fallback for existing Blueprints.
	if (CharacterAnimProfile && CharacterAnimProfile->LocomotionProfile)
	{
		return CharacterAnimProfile->LocomotionProfile.Get();
	}

	return LocomotionProfile.Get();
}

const UProject_JMotionMatchingAssetSet* AProject_JPlayerCharacter::GetMotionMatchingAssetSet() const
{
	// AssetSet is normally resolved from the effective locomotion profile.
	// Direct assignment remains as the last character-level migration fallback.
	if (const UProject_JLocomotionProfile* EffectiveLocomotionProfile = GetLocomotionProfile())
	{
		if (EffectiveLocomotionProfile->MotionMatchingAssetSet)
		{
			return EffectiveLocomotionProfile->MotionMatchingAssetSet.Get();
		}
	}

	return MotionMatchingAssetSet.Get();
}

const UProject_JWeaponAnimProfile* AProject_JPlayerCharacter::GetWeaponAnimProfile() const
{
	return CharacterAnimProfile ? CharacterAnimProfile->WeaponAnimProfile.Get() : nullptr;
}

const UProject_JCombatAnimProfile* AProject_JPlayerCharacter::GetCombatAnimProfile() const
{
	return CharacterAnimProfile ? CharacterAnimProfile->CombatAnimProfile.Get() : nullptr;
}

bool AProject_JPlayerCharacter::IsCombatModeActive() const
{
	return bIsCombatMode || HasCombatStateTag(FProject_JGameplayTags::Get().State_CombatMode);
}

bool AProject_JPlayerCharacter::IsAttacking() const
{
	return bIsAttacking || HasCombatStateTag(FProject_JGameplayTags::Get().State_Attacking);
}

bool AProject_JPlayerCharacter::IsDodging() const
{
	return bIsDodging || HasCombatStateTag(FProject_JGameplayTags::Get().State_Dodging);
}

bool AProject_JPlayerCharacter::IsHitReacting() const
{
	return bIsHitReacting || HasCombatStateTag(FProject_JGameplayTags::Get().State_HitReacting);
}

bool AProject_JPlayerCharacter::IsSprintLocomotionAllowed() const
{
	return bIsSprinting && !IsCombatActionBlockingSprint();
}

void AProject_JPlayerCharacter::UpdateMoveStartReplicationState(const FVector2D& MoveInput)
{
	const bool bHasMoveInput = MoveInput.SizeSquared() > FMath::Square(GetMoveInputDeadZoneForAnimation());
	if (bHasMoveInput && !bHadMoveInputForReplication)
	{
		DispatchMoveStartAnimationEvent(IsSprintLocomotionAllowed());
	}

	bHadMoveInputForReplication = bHasMoveInput;
}

void AProject_JPlayerCharacter::ResetMoveStartReplicationState()
{
	bHadMoveInputForReplication = false;
}

void AProject_JPlayerCharacter::DispatchMoveStartAnimationEvent(bool bWasSprintingForStart)
{
	if (HasAuthority())
	{
		ReplicatedAnimEvents.bMoveStartWasSprinting = bWasSprintingForStart;
		++ReplicatedAnimEvents.MoveStartCounter;
		ForceNetUpdate();
		return;
	}

	ServerNotifyMoveStarted(bWasSprintingForStart);
}

void AProject_JPlayerCharacter::ServerNotifyMoveStarted_Implementation(bool bWasSprintingForStart)
{
	ReplicatedAnimEvents.bMoveStartWasSprinting = bWasSprintingForStart || IsSprintLocomotionAllowed();
	++ReplicatedAnimEvents.MoveStartCounter;
	ForceNetUpdate();
}

void AProject_JPlayerCharacter::DispatchMoveStopAnimationEvent()
{
	if (HasAuthority())
	{
		++ReplicatedAnimEvents.MoveStopCounter;
		ForceNetUpdate();
		return;
	}

	ServerNotifyMoveStopped();
}

void AProject_JPlayerCharacter::ServerNotifyMoveStopped_Implementation()
{
	++ReplicatedAnimEvents.MoveStopCounter;
	ForceNetUpdate();
}

void AProject_JPlayerCharacter::DispatchJumpStartAnimationEvent()
{
	if (HasAuthority())
	{
		++ReplicatedAnimEvents.JumpStartCounter;
		ForceNetUpdate();
		return;
	}

	ServerNotifyJumpStarted();
}

void AProject_JPlayerCharacter::ServerNotifyJumpStarted_Implementation()
{
	++ReplicatedAnimEvents.JumpStartCounter;
	ForceNetUpdate();
}

void AProject_JPlayerCharacter::DispatchFallOffStartAnimationEvent()
{
	if (HasAuthority())
	{
		++ReplicatedAnimEvents.FallOffStartCounter;
		ForceNetUpdate();
		return;
	}

	ServerNotifyFallOffStarted();
}

void AProject_JPlayerCharacter::ServerNotifyFallOffStarted_Implementation()
{
	++ReplicatedAnimEvents.FallOffStartCounter;
	ForceNetUpdate();
}

void AProject_JPlayerCharacter::DispatchLandingCancelAnimationEvent()
{
	if (HasAuthority())
	{
		++ReplicatedAnimEvents.LandingCancelCounter;
		ForceNetUpdate();
		return;
	}

	ServerNotifyLandingCancelled();
}

void AProject_JPlayerCharacter::ServerNotifyLandingCancelled_Implementation()
{
	++ReplicatedAnimEvents.LandingCancelCounter;
	ForceNetUpdate();
}

void AProject_JPlayerCharacter::OnRep_ReplicatedAnimEvents(FProject_JReplicatedAnimEventState PreviousState)
{
	if (!LocomotionAnimStateComponent)
	{
		return;
	}

	if (ReplicatedAnimEvents.MoveStopCounter != PreviousState.MoveStopCounter)
	{
		LocomotionAnimStateComponent->HandleReplicatedMoveStopped();
	}

	if (ReplicatedAnimEvents.MoveStartCounter != PreviousState.MoveStartCounter)
	{
		LocomotionAnimStateComponent->HandleReplicatedMoveStarted(ReplicatedAnimEvents.bMoveStartWasSprinting);
	}

	if (ReplicatedAnimEvents.JumpStartCounter != PreviousState.JumpStartCounter)
	{
		LocomotionAnimStateComponent->HandleReplicatedJumpStarted();
	}

	if (ReplicatedAnimEvents.FallOffStartCounter != PreviousState.FallOffStartCounter)
	{
		LocomotionAnimStateComponent->HandleReplicatedFallOffStarted();
	}

	if (ReplicatedAnimEvents.LandingCancelCounter != PreviousState.LandingCancelCounter)
	{
		LocomotionAnimStateComponent->HandleReplicatedLandingCancelled();
	}
}

void AProject_JPlayerCharacter::OnRep_IsSprinting()
{
	UpdateMaxWalkSpeed();
	ApplySprintAnimationState();
}

void AProject_JPlayerCharacter::OnRep_CombatMode()
{
	ApplyCombatModeState(bIsCombatMode);
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
	if (IsCombatModeActive())
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
	if (IsCombatModeActive() == bInCombatMode)
	{
		return;
	}

	ApplyCombatModeState(bInCombatMode);
	if (!HasAuthority())
	{
		ServerSetCombatMode(bInCombatMode);
	}
}

void AProject_JPlayerCharacter::BeginCombatModeWithIntro()
{
	if (IsCombatModeActive() || bIsPlayingCombatIntro || bPendingCombatModeFromIntro || IsHitReacting())
	{
		return;
	}

	if (!ShouldPlayCombatIntroMontage())
	{
		SetCombatMode(true);
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
	UAnimMontage* EffectiveCombatIntroMontage = GetEffectiveCombatIntroMontage();
	if (!EffectiveCombatIntroMontage || bIsPlayingCombatIntro || IsHitReacting())
	{
		return;
	}

	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return;
	}

	const float Duration = PlayAnimMontage(EffectiveCombatIntroMontage, GetEffectiveCombatIntroMontagePlayRate());
	if (Duration <= 0.0f)
	{
		return;
	}

	bIsPlayingCombatIntro = true;
	bPendingCombatModeFromIntro = true;
	ActiveCombatIntroMontage = EffectiveCombatIntroMontage;

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AProject_JPlayerCharacter::OnCombatIntroMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, EffectiveCombatIntroMontage);
}

void AProject_JPlayerCharacter::CancelCombatIntroMontage()
{
	UAnimMontage* MontageToStop = ActiveCombatIntroMontage ? ActiveCombatIntroMontage.Get() : GetEffectiveCombatIntroMontage();
	if (bIsPlayingCombatIntro)
	{
		StopAnimMontage(MontageToStop);
	}

	bIsPlayingCombatIntro = false;
	bPendingCombatModeFromIntro = false;
	ActiveCombatIntroMontage = nullptr;
}

void AProject_JPlayerCharacter::OnCombatIntroMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage == ActiveCombatIntroMontage)
	{
		bIsPlayingCombatIntro = false;
		ActiveCombatIntroMontage = nullptr;

		if (bPendingCombatModeFromIntro)
		{
			bPendingCombatModeFromIntro = false;

			if (!bInterrupted && !IsHitReacting())
			{
				SetCombatMode(true);
			}
			else if (!IsCombatModeActive())
			{
				ApplyCombatRotationMode(false);
			}
		}
	}
}

void AProject_JPlayerCharacter::InterruptCombatIntroForHit()
{
	if (!ShouldInterruptCombatIntroOnHit() || !bIsPlayingCombatIntro)
	{
		return;
	}

	UAnimMontage* MontageToStop = ActiveCombatIntroMontage ? ActiveCombatIntroMontage.Get() : GetEffectiveCombatIntroMontage();
	StopAnimMontage(MontageToStop);
	bIsPlayingCombatIntro = false;
	bPendingCombatModeFromIntro = false;
	ActiveCombatIntroMontage = nullptr;
	if (!IsCombatModeActive())
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
