// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JPlayerCharacter.h"
#include "Project_JCombatComponent.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Animation/Project_JAnimationProfileValidation.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "Combat/Project_JCombatMovementPolicy.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "GameplayTagContainer.h"
#include "Project_JGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Components/Project_JReplicatedAnimEventComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "UI/Project_JCharacterUIBindingComponent.h"
#include "UI/Project_JCharacterViewModel.h"
#include "InputCoreTypes.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

namespace
{
FProject_JCombatMovementPolicy BuildCombatMovementPolicy(const AProject_JPlayerCharacter& PlayerCharacter)
{
	FProject_JCombatMovementPolicy Policy;
	Policy.bCombatMode = PlayerCharacter.IsCombatModeActive();
	Policy.bAttacking = PlayerCharacter.IsAttacking();
	Policy.bDodging = PlayerCharacter.IsDodging();
	Policy.bHitReacting = PlayerCharacter.IsHitReacting();
	if (const UProject_JCombatAnimProfile* CombatAnimProfile = PlayerCharacter.GetCombatAnimProfile())
	{
		Policy.bAllowSprintInCombat = CombatAnimProfile->bAllowSprintInCombat;
		Policy.bUseCombatRotationMode = CombatAnimProfile->bUseCombatRotationMode;
		Policy.bInterruptIntroOnHit = CombatAnimProfile->bInterruptCombatIntroOnHit;
	}
	return Policy;
}
}

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
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered;
	GetMesh()->bEnableUpdateRateOptimizations = true;

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
	CharacterUIBindingComponent = CreateDefaultSubobject<UProject_JCharacterUIBindingComponent>(TEXT("CharacterUIBindingComponent"));
	PlayerInputBindingComponent = CreateDefaultSubobject<UProject_JPlayerInputBindingComponent>(TEXT("PlayerInputBindingComponent"));
	ReplicatedAnimEventComponent = CreateDefaultSubobject<UProject_JReplicatedAnimEventComponent>(TEXT("ReplicatedAnimEventComponent"));
	InventoryComponent = CreateDefaultSubobject<UProject_JInventoryComponent>(TEXT("InventoryComponent"));
}

void AProject_JPlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AProject_JPlayerCharacter, bIsSprinting, COND_SkipOwner);
	DOREPLIFETIME(AProject_JPlayerCharacter, bIsCombatMode);
	DOREPLIFETIME(AProject_JPlayerCharacter, CurrentWeaponAnimProfile);
	DOREPLIFETIME_CONDITION(AProject_JPlayerCharacter, ReplicatedAnimEvents, COND_SkipOwner);
}

void AProject_JPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (CharacterUIBindingComponent)
	{
		CharacterUIBindingComponent->InitializeFromAttributes(AbilitySystemComponent, AttributeSet, CharacterLevel);
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

void AProject_JPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	FProject_JPlayerInputActionSet ActionSet;
	ActionSet.JumpAction = JumpAction;
	ActionSet.MoveAction = MoveAction;
	ActionSet.LookAction = LookAction;
	ActionSet.MouseLookAction = MouseLookAction;
	ActionSet.SprintAction = SprintAction;
	ActionSet.ToggleCombatAction = ToggleCombatAction;
	ActionSet.AttackAction = AttackAction;

	const bool bBoundInput = PlayerInputBindingComponent && PlayerInputBindingComponent->BindInput(PlayerInputComponent, this, ActionSet);
	if (!bBoundInput)
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
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
	if (!IsJumpLocomotionAllowed())
	{
		return;
	}

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
	return BuildCombatMovementPolicy(*this).IsSprintBlocked();
}

bool AProject_JPlayerCharacter::ShouldAllowSprintInCombat() const
{
	return BuildCombatMovementPolicy(*this).bAllowSprintInCombat;
}

void AProject_JPlayerCharacter::ApplyLocomotionProfile()
{
	if (const UProject_JLocomotionProfile* EffectiveLocomotionProfile = GetLocomotionProfile())
	{
		if (LocomotionAnimStateComponent)
		{
			LocomotionAnimStateComponent->SprintLocomotionSpeedThreshold = EffectiveLocomotionProfile->SprintLocomotionSpeedThreshold;
			LocomotionAnimStateComponent->HiddenRemoteUpdateInterval = EffectiveLocomotionProfile->AnimStateHiddenRemoteUpdateInterval;
			LocomotionAnimStateComponent->RemoteStartTurnExitAngle = EffectiveLocomotionProfile->RemoteVisualPolicy.RemoteStartTurnExitAngle;
			LocomotionAnimStateComponent->RemoteStopStartSuppressDuration = EffectiveLocomotionProfile->RemoteVisualPolicy.RemoteStopStartSuppressDuration;
		}

		SignificanceNearDistance = EffectiveLocomotionProfile->NearMotionMatchingDistance;
		SignificanceMidDistance = EffectiveLocomotionProfile->MidMotionMatchingDistance;
		SignificanceFarDistance = EffectiveLocomotionProfile->FarMotionMatchingDistance;
		MidSignificanceTickInterval = EffectiveLocomotionProfile->MidMotionMatchingUpdateInterval;
		FarSignificanceTickInterval = EffectiveLocomotionProfile->FarMotionMatchingUpdateInterval;
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

	TArray<FString> ValidationWarnings;
	Project_J::AnimationProfileValidation::ValidatePlayerAnimationConfiguration(
		*this,
		EffectiveLocomotionProfile,
		EffectiveAssetSet,
		GetWeaponAnimProfile(),
		GetCombatAnimProfile(),
		LocomotionAnimStateComponent,
		ValidationWarnings);

	for (const FString& ValidationWarning : ValidationWarnings)
	{
		UE_LOG(LogTemplateCharacter, Warning, TEXT("%s"), *ValidationWarning);
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
	return BuildCombatMovementPolicy(*this).ShouldUseCombatRotationMode();
}

bool AProject_JPlayerCharacter::ShouldInterruptCombatIntroOnHit() const
{
	FProject_JCombatMovementPolicy Policy = BuildCombatMovementPolicy(*this);
	if (!GetCombatAnimProfile())
	{
		Policy.bInterruptIntroOnHit = bInterruptCombatIntroOnHit;
	}
	return Policy.ShouldInterruptIntroOnHit();
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

void AProject_JPlayerCharacter::ServerTriggerPlayerAttack_Implementation()
{
	if (IsDodging() || IsHitReacting())
	{
		return;
	}

	if (ActiveCombatComponent)
	{
		ActiveCombatComponent->Attack();
	}
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
	if (CurrentWeaponAnimProfile)
	{
		return CurrentWeaponAnimProfile.Get();
	}

	return CharacterAnimProfile ? CharacterAnimProfile->WeaponAnimProfile.Get() : nullptr;
}

const UProject_JCombatAnimProfile* AProject_JPlayerCharacter::GetCombatAnimProfile() const
{
	return CharacterAnimProfile ? CharacterAnimProfile->CombatAnimProfile.Get() : nullptr;
}

UProject_JCharacterViewModel* AProject_JPlayerCharacter::GetCharacterViewModel() const
{
	return CharacterUIBindingComponent ? CharacterUIBindingComponent->GetCharacterViewModel() : nullptr;
}

bool AProject_JPlayerCharacter::IsCombatModeActive() const
{
	return bIsCombatMode || HasCombatStateTag(FProject_JGameplayTags::Get().State_CombatMode);
}

void AProject_JPlayerCharacter::SetCurrentWeaponAnimProfile(UProject_JWeaponAnimProfile* InWeaponAnimProfile)
{
	if (CurrentWeaponAnimProfile == InWeaponAnimProfile)
	{
		return;
	}

	CurrentWeaponAnimProfile = InWeaponAnimProfile;
	if (HasAuthority())
	{
		ForceNetUpdate();
	}
}

void AProject_JPlayerCharacter::OnRep_CurrentWeaponAnimProfile()
{
	// AnimInstance reads the effective profile on demand, so no cache invalidation is needed yet.
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

bool AProject_JPlayerCharacter::IsJumpLocomotionAllowed() const
{
	return BuildCombatMovementPolicy(*this).IsJumpAllowed();
}

bool AProject_JPlayerCharacter::IsGroundStartAllowed() const
{
	return BuildCombatMovementPolicy(*this).IsGroundStartAllowed();
}

bool AProject_JPlayerCharacter::IsGroundStopAllowed() const
{
	return BuildCombatMovementPolicy(*this).IsGroundStopAllowed();
}

bool AProject_JPlayerCharacter::IsCombatLocomotionOverlayAllowed() const
{
	return BuildCombatMovementPolicy(*this).IsCombatLocomotionOverlayAllowed();
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
		if (ReplicatedAnimEventComponent)
		{
			ReplicatedAnimEventComponent->MarkMoveStarted(ReplicatedAnimEvents, bWasSprintingForStart);
		}
		ForceNetUpdate();
		return;
	}

	ServerNotifyMoveStarted(bWasSprintingForStart);
}

void AProject_JPlayerCharacter::ServerNotifyMoveStarted_Implementation(bool bWasSprintingForStart)
{
	if (ReplicatedAnimEventComponent)
	{
		ReplicatedAnimEventComponent->MarkMoveStarted(ReplicatedAnimEvents, bWasSprintingForStart || IsSprintLocomotionAllowed());
	}
	ForceNetUpdate();
}

void AProject_JPlayerCharacter::DispatchMoveStopAnimationEvent()
{
	if (HasAuthority())
	{
		if (ReplicatedAnimEventComponent)
		{
			ReplicatedAnimEventComponent->MarkMoveStopped(ReplicatedAnimEvents);
		}
		ForceNetUpdate();
		return;
	}

	ServerNotifyMoveStopped();
}

void AProject_JPlayerCharacter::ServerNotifyMoveStopped_Implementation()
{
	if (ReplicatedAnimEventComponent)
	{
		ReplicatedAnimEventComponent->MarkMoveStopped(ReplicatedAnimEvents);
	}
	ForceNetUpdate();
}

void AProject_JPlayerCharacter::DispatchJumpStartAnimationEvent()
{
	if (HasAuthority())
	{
		if (ReplicatedAnimEventComponent)
		{
			ReplicatedAnimEventComponent->MarkJumpStarted(ReplicatedAnimEvents);
		}
		ForceNetUpdate();
		return;
	}

	ServerNotifyJumpStarted();
}

void AProject_JPlayerCharacter::ServerNotifyJumpStarted_Implementation()
{
	if (ReplicatedAnimEventComponent)
	{
		ReplicatedAnimEventComponent->MarkJumpStarted(ReplicatedAnimEvents);
	}
	ForceNetUpdate();
}

void AProject_JPlayerCharacter::DispatchFallOffStartAnimationEvent()
{
	if (HasAuthority())
	{
		if (ReplicatedAnimEventComponent)
		{
			ReplicatedAnimEventComponent->MarkFallOffStarted(ReplicatedAnimEvents);
		}
		ForceNetUpdate();
		return;
	}

	ServerNotifyFallOffStarted();
}

void AProject_JPlayerCharacter::ServerNotifyFallOffStarted_Implementation()
{
	if (ReplicatedAnimEventComponent)
	{
		ReplicatedAnimEventComponent->MarkFallOffStarted(ReplicatedAnimEvents);
	}
	ForceNetUpdate();
}

void AProject_JPlayerCharacter::DispatchLandingCancelAnimationEvent()
{
	if (HasAuthority())
	{
		if (ReplicatedAnimEventComponent)
		{
			ReplicatedAnimEventComponent->MarkLandingCancelled(ReplicatedAnimEvents);
		}
		ForceNetUpdate();
		return;
	}

	ServerNotifyLandingCancelled();
}

void AProject_JPlayerCharacter::ServerNotifyLandingCancelled_Implementation()
{
	if (ReplicatedAnimEventComponent)
	{
		ReplicatedAnimEventComponent->MarkLandingCancelled(ReplicatedAnimEvents);
	}
	ForceNetUpdate();
}

void AProject_JPlayerCharacter::OnRep_ReplicatedAnimEvents(FProject_JReplicatedAnimEventState PreviousState)
{
	if (ReplicatedAnimEventComponent)
	{
		ReplicatedAnimEventComponent->ApplyReplicatedEvents(ReplicatedAnimEvents, PreviousState, LocomotionAnimStateComponent);
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

	if (!HasAuthority())
	{
		ServerTriggerPlayerAttack();
	}
}
