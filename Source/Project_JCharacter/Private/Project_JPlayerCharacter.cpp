// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JPlayerCharacter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
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
#include "Camera/Project_JCameraComponent.h"
#include "GameFramework/PlayerState.h"
#include "Project_JAbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Project_JAttributeSet.h"
#include "Components/Project_JReplicatedAnimEventComponent.h"
#include "Components/Project_JReplicatedJumpStateComponent.h"
#include "Components/Project_JCombatIntroComponent.h"
#include "Components/Project_JCombatStateComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Components/Project_JSkillInputRouterComponent.h"
#include "Mount/Project_JMountComponent.h"
#include "Mount/Project_JMountCharacter.h"
#include "Mount/Project_JMountItemDefinition.h"
#include "Interaction/Project_JInteractable.h"
#include "UI/Project_JCharacterUIBindingComponent.h"
#include "UI/Project_JCharacterViewModel.h"
#include "InputCoreTypes.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"

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

int32 GetCharacterLevelForInterfaceObject(const UObject* Object)
{
	return Object ? IProject_JCombatInterface::Execute_GetCharacterLevel(const_cast<UObject*>(Object)) : 1;
}
}

AProject_JPlayerCharacter::AProject_JPlayerCharacter()
{
	RuntimeStateOwnership = EProject_JRuntimeStateOwnership::PlayerStatePreferred;
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->SetIsReplicated(false);
	}
	if (EquipmentManager)
	{
		EquipmentManager->SetIsReplicated(false);
	}

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

	CameraComponent = CreateDefaultSubobject<UProject_JCameraComponent>(TEXT("CameraComponent"));
	CameraComponent->Initialize(CameraBoom, FollowCamera);

	LocomotionAnimStateComponent = CreateDefaultSubobject<UProject_JLocomotionAnimStateComponent>(TEXT("LocomotionAnimStateComponent"));
	MotionMatchingTrajectoryComponent = CreateDefaultSubobject<UProject_JMotionMatchingTrajectoryComponent>(TEXT("MotionMatchingTrajectoryComponent"));
	CharacterUIBindingComponent = CreateDefaultSubobject<UProject_JCharacterUIBindingComponent>(TEXT("CharacterUIBindingComponent"));
	PlayerInputBindingComponent = CreateDefaultSubobject<UProject_JPlayerInputBindingComponent>(TEXT("PlayerInputBindingComponent"));
	SkillInputRouterComponent = CreateDefaultSubobject<UProject_JSkillInputRouterComponent>(TEXT("SkillInputRouterComponent"));
	SkillInputExecutionComponent = CreateDefaultSubobject<UProject_JSkillInputExecutionComponent>(TEXT("SkillInputExecutionComponent"));
	ReplicatedAnimEventComponent = CreateDefaultSubobject<UProject_JReplicatedAnimEventComponent>(TEXT("ReplicatedAnimEventComponent"));
	ReplicatedAnimEventComponent->Initialize(LocomotionAnimStateComponent);
	ReplicatedJumpStateComponent = CreateDefaultSubobject<UProject_JReplicatedJumpStateComponent>(TEXT("ReplicatedJumpStateComponent"));
	ReplicatedJumpStateComponent->Initialize(LocomotionAnimStateComponent);
	CombatStateComponent = CreateDefaultSubobject<UProject_JCombatStateComponent>(TEXT("CombatStateComponent"));
	CombatIntroComponent = CreateDefaultSubobject<UProject_JCombatIntroComponent>(TEXT("CombatIntroComponent"));
	MountComponent = CreateDefaultSubobject<UProject_JMountComponent>(TEXT("MountComponent"));
}

void AProject_JPlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AProject_JPlayerCharacter, SummonedMount);
}

void AProject_JPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (CombatStateComponent)
	{
		CombatStateComponent->OnCombatStateTagChanged.AddUniqueDynamic(this, &AProject_JPlayerCharacter::OnCombatStateTagChanged);
	}
	if (CombatIntroComponent)
	{
		CombatIntroComponent->OnCombatIntroEnded.AddUniqueDynamic(this, &AProject_JPlayerCharacter::OnCombatIntroMontageEnded);
	}
	if (ReplicatedAnimEventComponent)
	{
		ReplicatedAnimEventComponent->Initialize(LocomotionAnimStateComponent);
	}
	if (ReplicatedJumpStateComponent)
	{
		ReplicatedJumpStateComponent->Initialize(LocomotionAnimStateComponent);
	}
	if (MountComponent)
	{
		MountComponent->OnMountChanged.AddUniqueDynamic(this, &AProject_JPlayerCharacter::OnMountChangedForAnimation);
		RefreshMountedAnimationLayer();
	}

	ApplyLocomotionProfile();
	LogAnimationProfileConfiguration();
	RefreshAbilitySystemDependentComponents();

	// 바인딩: 어택/회피/피격 태그가 변경될 때 즉각적으로 Sprint 속도를 갱신
}

void AProject_JPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MountComponent)
	{
		MountComponent->OnMountChanged.RemoveDynamic(this, &AProject_JPlayerCharacter::OnMountChangedForAnimation);
	}
	UnlinkMountedAnimationLayer();
	UnregisterCombatStateTagEvents();
	if (CombatStateComponent)
	{
		CombatStateComponent->OnCombatStateTagChanged.RemoveDynamic(this, &AProject_JPlayerCharacter::OnCombatStateTagChanged);
	}
	if (CombatIntroComponent)
	{
		CombatIntroComponent->OnCombatIntroEnded.RemoveDynamic(this, &AProject_JPlayerCharacter::OnCombatIntroMontageEnded);
	}

	Super::EndPlay(EndPlayReason);
}

void AProject_JPlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->RefreshCachedReferences();
	}

	RefreshAbilitySystemDependentComponents();
}

void AProject_JPlayerCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->RefreshCachedReferences();
	}

	RefreshAbilitySystemDependentComponents();
}

void AProject_JPlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->UpdateState(DeltaTime);
	}
}

AActor* AProject_JPlayerCharacter::GetAbilitySystemOwnerActor() const
{
	if (APlayerState* PS = GetPlayerState())
	{
		return PS;
	}
	return Super::GetAbilitySystemOwnerActor();
}

void AProject_JPlayerCharacter::OnMountChangedForAnimation(AProject_JMountCharacter* PreviousMount, AProject_JMountCharacter* NewMount)
{
	RefreshMountedAnimationLayer();
}

void AProject_JPlayerCharacter::RefreshMountedAnimationLayer()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const bool bShouldLinkMountedLayer = MountComponent && MountComponent->IsMounted() && !MountedAnimationLayerClass.IsNull();
	if (!bShouldLinkMountedLayer)
	{
		UnlinkMountedAnimationLayer();
		return;
	}

	UClass* LoadedLayerClass = MountedAnimationLayerClass.LoadSynchronous();
	if (!LoadedLayerClass || !LoadedLayerClass->IsChildOf(UAnimInstance::StaticClass()))
	{
		UnlinkMountedAnimationLayer();
		return;
	}

	if (LinkedMountedAnimationLayerClass == LoadedLayerClass)
	{
		return;
	}

	UnlinkMountedAnimationLayer();
	if (USkeletalMeshComponent* PlayerMesh = GetMesh())
	{
		PlayerMesh->LinkAnimClassLayers(LoadedLayerClass);
		LinkedMountedAnimationLayerClass = LoadedLayerClass;
	}
}

void AProject_JPlayerCharacter::UnlinkMountedAnimationLayer()
{
	if (!LinkedMountedAnimationLayerClass)
	{
		return;
	}

	if (USkeletalMeshComponent* PlayerMesh = GetMesh())
	{
		PlayerMesh->UnlinkAnimClassLayers(LinkedMountedAnimationLayerClass);
	}

	LinkedMountedAnimationLayerClass = nullptr;
}

EProject_JAnimationLocomotionMode AProject_JPlayerCharacter::GetAnimationLocomotionMode_Implementation() const
{
	return GetMountComponent() && GetMountComponent()->IsMounted()
		? EProject_JAnimationLocomotionMode::Mounted
		: EProject_JAnimationLocomotionMode::OnFoot;
}

void AProject_JPlayerCharacter::RequestUseMountItem(FGuid ItemInstanceId)
{
	if (!ItemInstanceId.IsValid())
	{
		return;
	}

	if (HasAuthority())
	{
		ServerRequestUseMountItem_Implementation(ItemInstanceId);
	}
	else
	{
		ServerRequestUseMountItem(ItemInstanceId);
	}
}

void AProject_JPlayerCharacter::ServerRequestUseMountItem_Implementation(FGuid ItemInstanceId)
{
	if (GetMountComponent() && GetMountComponent()->IsMounted())
	{
		return;
	}

	FProject_JItemInstanceData ItemInstance;
	UProject_JInventoryComponent* Inventory = GetInventoryComponent();
	if (!Inventory || !Inventory->FindItemInstance(ItemInstanceId, ItemInstance) || ItemInstance.bIsLocked)
	{
		return;
	}

	const UProject_JMountItemDefinition* MountItem = Cast<UProject_JMountItemDefinition>(ItemInstance.ItemDef);
	const TSubclassOf<AProject_JMountCharacter> MountClass = MountItem ? MountItem->MountClass.LoadSynchronous() : nullptr;
	if (!MountClass || !GetWorld())
	{
		return;
	}

	if (SummonedMount)
	{
		if (MountItem->bAutoMountAfterSpawn)
		{
			SummonedMount->TryMountRider(this);
		}
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.Instigator = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
	const FVector SpawnLocation = GetActorLocation() + GetActorForwardVector() * MountItem->SpawnDistance;
	AProject_JMountCharacter* Mount = GetWorld()->SpawnActor<AProject_JMountCharacter>(MountClass, SpawnLocation, GetActorRotation(), SpawnParameters);
	SummonedMount = Mount;
	if (Mount && MountItem->bAutoMountAfterSpawn)
	{
		Mount->TryMountRider(this);
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
	ActionSet.HeavyAttackAction = HeavyAttackAction;
	ActionSet.SkillModifierAction = SkillModifierAction;
	ActionSet.InteractAction = InteractAction;

	if (SkillInputRouterComponent)
	{
		SkillInputRouterComponent->Initialize(this);
	}

	const bool bBoundInput = PlayerInputBindingComponent && PlayerInputBindingComponent->BindInput(PlayerInputComponent, this, ActionSet);
	if (!bBoundInput)
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AProject_JPlayerCharacter::TryInteract()
{
	if (!HasAuthority())
	{
		ServerTryInteract();
		return;
	}
	TArray<FOverlapResult> Results;
	FCollisionObjectQueryParams ObjectTypes; ObjectTypes.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ProjectJInteract), false, this);
	if (!GetWorld()->OverlapMultiByObjectType(Results, GetActorLocation(), FQuat::Identity, ObjectTypes, FCollisionShape::MakeSphere(300.0f), Params)) return;
	AActor* Best = nullptr; float BestDistance = TNumericLimits<float>::Max();
	for (const FOverlapResult& Result : Results) { AActor* Candidate = Result.GetActor(); if (Candidate && Candidate->GetClass()->ImplementsInterface(UProject_JInteractable::StaticClass())) { const float D = FVector::DistSquared(GetActorLocation(), Candidate->GetActorLocation()); if (D < BestDistance && IProject_JInteractable::Execute_CanInteract(Candidate, this)) { Best = Candidate; BestDistance = D; } } }
	if (Best) IProject_JInteractable::Execute_Interact(Best, this);
}

void AProject_JPlayerCharacter::ServerTryInteract_Implementation()
{
	TryInteract();
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


bool AProject_JPlayerCharacter::HasCombatStateTag(const FGameplayTag& StateTag) const
{
	return CombatStateComponent && CombatStateComponent->HasStateTag(StateTag);
}

bool AProject_JPlayerCharacter::TryActivateAbilityByTag(const FGameplayTag& AbilityTag)
{
	return CombatStateComponent && CombatStateComponent->TryActivateAbilityByTag(AbilityTag);
}

void AProject_JPlayerCharacter::CancelAbilitiesByTag(const FGameplayTag& AbilityTag)
{
	if (CombatStateComponent)
	{
		CombatStateComponent->CancelAbilitiesByTag(AbilityTag);
	}
}

bool AProject_JPlayerCharacter::IsCombatActionBlockingSprint() const
{
	return BuildCombatMovementPolicy(*this).IsSprintBlocked();
}

void AProject_JPlayerCharacter::RefreshAbilitySystemDependentComponents()
{
	if (CharacterUIBindingComponent)
	{
		CharacterUIBindingComponent->InitializeFromAttributes(Cast<UProject_JAbilitySystemComponent>(GetAbilitySystemComponent()), GetAttributeSet(), GetCharacterLevelForInterfaceObject(this));
	}

	RefreshActiveCombatComponent();
	RegisterCombatStateTagEvents();
	if (SkillInputExecutionComponent)
	{
		SkillInputExecutionComponent->Initialize(this, CombatStateComponent);
	}
	if (CameraComponent)
	{
		CameraComponent->RefreshAbilitySystemBinding();
	}
}

UProject_JCombatComponent* AProject_JPlayerCharacter::RefreshActiveCombatComponent()
{
	UProject_JCombatComponent* FoundCombatComponent = FindComponentByClass<UProject_JCombatComponent>();
	if (FoundCombatComponent && FoundCombatComponent != ActiveCombatComponent)
	{
		ActiveCombatComponent = FoundCombatComponent;
	}

	if (ActiveCombatComponent && GetAbilitySystemComponent())
	{
		ActiveCombatComponent->BindToGAS(GetAbilitySystemComponent());
	}

	return ActiveCombatComponent;
}

void AProject_JPlayerCharacter::RegisterCombatStateTagEvents()
{
	if (CombatStateComponent)
	{
		CombatStateComponent->BindToAbilitySystem(GetAbilitySystemComponent());
	}
}

void AProject_JPlayerCharacter::UnregisterCombatStateTagEvents()
{
	if (CombatStateComponent)
	{
		CombatStateComponent->ClearAbilitySystemBinding();
	}
}

void AProject_JPlayerCharacter::OnCombatStateTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
	const bool bIsCombatModeActive = HasCombatStateTag(FProject_JGameplayTags::Get().State_CombatMode);

	if (CallbackTag == FProject_JGameplayTags::Get().State_CombatMode)
	{
		UProject_JCombatComponent* CombatComponent = RefreshActiveCombatComponent();
		if (CombatComponent)
		{
			if (bIsCombatModeActive)
			{
				CombatComponent->EquipWeapon();
			}
			else
			{
				CombatComponent->UnequipWeapon();
			}
		}

		if (bIsCombatModeActive)
		{
			ApplyCombatRotationMode(true);
		}
		else
		{
			CancelCombatIntroMontage();
			ApplyCombatRotationMode(false);
		}
	}

	UpdateMaxWalkSpeed();
	ApplySprintAnimationState();
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

UProject_JInventoryComponent* AProject_JPlayerCharacter::GetInventoryComponent() const
{
	if (APlayerState* PS = GetPlayerState())
	{
		return PS->FindComponentByClass<UProject_JInventoryComponent>();
	}
	return nullptr;
}

bool AProject_JPlayerCharacter::IsCombatModeActive() const
{
	return CombatStateComponent && CombatStateComponent->IsCombatModeActive();
}

void AProject_JPlayerCharacter::SetCurrentWeaponAnimProfile(UProject_JWeaponAnimProfile* InWeaponAnimProfile)
{
	if (CurrentWeaponAnimProfile == InWeaponAnimProfile)
	{
		return;
	}

	CurrentWeaponAnimProfile = InWeaponAnimProfile;
}

void AProject_JPlayerCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();

	if (HasAuthority() && ReplicatedJumpStateComponent)
	{
		ReplicatedJumpStateComponent->RecordServerConfirmedJump(GetVelocity());
	}
}

bool AProject_JPlayerCharacter::IsAttacking() const
{
	return bIsAttacking || (CombatStateComponent && CombatStateComponent->IsAttacking());
}

bool AProject_JPlayerCharacter::IsDodging() const
{
	return bIsDodging || (CombatStateComponent && CombatStateComponent->IsDodging());
}

bool AProject_JPlayerCharacter::IsHitReacting() const
{
	return bIsHitReacting || (CombatStateComponent && CombatStateComponent->IsHitReacting());
}

bool AProject_JPlayerCharacter::IsSprintLocomotionAllowed() const
{
	return CombatStateComponent && CombatStateComponent->IsSprintTagActive() && !IsCombatActionBlockingSprint();
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
	if (ReplicatedAnimEventComponent)
	{
		ReplicatedAnimEventComponent->DispatchMoveStarted(bWasSprintingForStart);
	}
}

void AProject_JPlayerCharacter::DispatchMoveStopAnimationEvent()
{
	if (ReplicatedAnimEventComponent)
	{
		ReplicatedAnimEventComponent->DispatchMoveStopped();
	}
}

void AProject_JPlayerCharacter::DispatchFallOffStartAnimationEvent()
{
	if (ReplicatedAnimEventComponent)
	{
		ReplicatedAnimEventComponent->DispatchFallOffStarted();
	}
}

void AProject_JPlayerCharacter::DispatchLandingCancelAnimationEvent()
{
	if (ReplicatedAnimEventComponent)
	{
		ReplicatedAnimEventComponent->DispatchLandingCancelled();
	}
}



void AProject_JPlayerCharacter::StartSprint()
{
	TryActivateAbilityByTag(SprintAbilityTag);
}

void AProject_JPlayerCharacter::StopSprint()
{
	CancelAbilitiesByTag(SprintAbilityTag);
}

void AProject_JPlayerCharacter::ToggleCombatMode()
{
	if (!IsCombatModeActive() && ShouldPlayCombatIntroMontage())
	{
		BeginCombatModeWithIntro();
	}
	else
	{
		TryActivateAbilityByTag(CombatToggleAbilityTag);
	}
}



void AProject_JPlayerCharacter::BeginCombatModeWithIntro()
{
	ApplyCombatRotationMode(true);
	PlayCombatIntroMontage();
}

void AProject_JPlayerCharacter::PlayCombatIntroMontage()
{
	UAnimMontage* EffectiveCombatIntroMontage = GetEffectiveCombatIntroMontage();
	if (!EffectiveCombatIntroMontage || !CombatIntroComponent || CombatIntroComponent->IsPlayingIntro() || IsHitReacting())
	{
		return;
	}

	if (CombatIntroComponent->PlayIntro(*this, EffectiveCombatIntroMontage, GetEffectiveCombatIntroMontagePlayRate()))
	{
		bIsPlayingCombatIntro = CombatIntroComponent->IsPlayingIntro();
		bPendingCombatModeFromIntro = CombatIntroComponent->IsPendingCombatMode();
	}
}

void AProject_JPlayerCharacter::CancelCombatIntroMontage()
{
	if (CombatIntroComponent)
	{
		CombatIntroComponent->CancelIntro(*this, GetEffectiveCombatIntroMontage());
	}

	bIsPlayingCombatIntro = false;
	bPendingCombatModeFromIntro = false;
}

void AProject_JPlayerCharacter::OnCombatIntroMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	bIsPlayingCombatIntro = CombatIntroComponent && CombatIntroComponent->IsPlayingIntro();
	bPendingCombatModeFromIntro = CombatIntroComponent && CombatIntroComponent->IsPendingCombatMode();

	if (bPendingCombatModeFromIntro && !bInterrupted && !IsHitReacting())
	{
		if (CombatIntroComponent)
		{
			CombatIntroComponent->ClearPendingCombatMode();
		}
		bPendingCombatModeFromIntro = false;
		TryActivateAbilityByTag(CombatToggleAbilityTag);
	}
	else if (!IsCombatModeActive())
	{
		if (CombatIntroComponent)
		{
			CombatIntroComponent->ClearPendingCombatMode();
		}
		bPendingCombatModeFromIntro = false;
		ApplyCombatRotationMode(false);
	}
}

void AProject_JPlayerCharacter::InterruptCombatIntroForHit()
{
	if (!ShouldInterruptCombatIntroOnHit() || !CombatIntroComponent || !CombatIntroComponent->IsPlayingIntro())
	{
		return;
	}

	CombatIntroComponent->CancelIntro(*this, GetEffectiveCombatIntroMontage());
	bIsPlayingCombatIntro = false;
	bPendingCombatModeFromIntro = false;
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
	HandleSkillInputTagPressed(FProject_JGameplayTags::Get().InputTag_Weapon_LightAttack);
}

void AProject_JPlayerCharacter::HandleSkillInputTagPressed(FGameplayTag InputTag)
{
	if (SkillInputExecutionComponent)
	{
		SkillInputExecutionComponent->HandleInputTagPressed(InputTag);
	}
}

void AProject_JPlayerCharacter::HandleSkillInputTagReleased(FGameplayTag InputTag)
{
	if (SkillInputExecutionComponent)
	{
		SkillInputExecutionComponent->HandleInputTagReleased(InputTag);
	}
}

void AProject_JPlayerCharacter::SerializeForHandover(TArray<uint8>& OutData)
{
	OutData.Reset();

	FProject_JPlayerHandoverSnapshot Snapshot;
	Snapshot.Version = 1;
	Snapshot.Level = GetCharacterLevel_Implementation();
	Snapshot.Location = GetActorLocation();
	Snapshot.Rotation = GetActorRotation();

	FMemoryWriter Writer(OutData, true);
	Writer << Snapshot.Version;
	Writer << Snapshot.Level;
	Writer << Snapshot.Location;
	Writer << Snapshot.Rotation;
}

void AProject_JPlayerCharacter::DeserializeFromHandover(const TArray<uint8>& InData)
{
	if (InData.Num() == 0)
	{
		return;
	}

	FProject_JPlayerHandoverSnapshot Snapshot;
	TArray<uint8> LocalData = InData;
	FMemoryReader Reader(LocalData, true);

	Reader << Snapshot.Version;
	Reader << Snapshot.Level;
	Reader << Snapshot.Location;
	Reader << Snapshot.Rotation;

	if (Reader.IsError())
	{
		UE_LOG(LogTemplateCharacter, Warning, TEXT("Failed to deserialize player handover snapshot."));
		return;
	}

	if (Snapshot.Version != 1)
	{
		UE_LOG(LogTemplateCharacter, Warning, TEXT("Unsupported handover snapshot version: %d"), Snapshot.Version);
		return;
	}

	SetCharacterLevel(Snapshot.Level);

	// Physics and teleport compensation
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
	}

	SetActorLocationAndRotation(Snapshot.Location, Snapshot.Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	ForceNetUpdate();
}

void AProject_JPlayerCharacter::SetCharacterLevel(int32 NewLevel)
{
	Super::SetCharacterLevel(NewLevel);
	RefreshAbilitySystemDependentComponents();
}
