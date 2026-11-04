// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JPlayerCharacter.h"
#include "MoverSimulationTypes.h"
#include "Movement/Project_JMovementQueries.h"
#include "Movement/Project_JMoverComponent.h"
#include "Components/Project_JFoleyComponent.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Interaction/Project_JInteractionQuery.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Animation/Project_JAnimationProfileValidation.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Engine/LocalPlayer.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Project_JGameplayTags.h"
#include "Camera/Project_JCameraComponent.h"
#include "GameFramework/PlayerState.h"
#include "Project_JAbilitySystemComponent.h"
#include "Network/Project_JOwnedPawnScopeComponent.h"
#include "Project_JAbilitySystemOwnerInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Project_JAttributeSet.h"
#include "Components/Project_JReplicatedAnimEventComponent.h"
#include "Components/Project_JReplicatedJumpStateComponent.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Components/Project_JCombatIntroComponent.h"
#include "Components/Project_JCombatAnimationLayerComponent.h"
#include "Components/Project_JMountedAnimationLayerComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JCombatStateComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Components/Project_JSkillInputRouterComponent.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Mount/Project_JMountComponent.h"
#include "Mount/Project_JMountCharacter.h"
#include "Mount/Project_JMountItemDefinition.h"
#include "Interaction/Project_JInteractable.h"
#include "UI/Project_JCharacterUIBindingComponent.h"
#include "UI/Project_JCharacterViewModel.h"
#include "InputCoreTypes.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "Net/Iris/ReplicationSystem/ReplicationSystemUtil.h"
#include "Net/Iris/ReplicationSystem/EngineReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Engine/OverlapResult.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogProjectJPlayer);

namespace
{
TAutoConsoleVariable<int32> CVarProjectJCombatPresentationDebug(
	TEXT("p.ProjectJ.CombatPresentation.Debug"),
	0,
	TEXT("Combat draw presentation replication trace. 0: off, 1: log local start, server receipt, replication, and remote montage playback."));

bool ShouldLogCombatPresentationTrace()
{
	return CVarProjectJCombatPresentationDebug.GetValueOnAnyThread() != 0;
}

void LogCombatPresentationTrace(const AProject_JPlayerCharacter& Character, const TCHAR* Stage)
{
	if (!ShouldLogCombatPresentationTrace())
	{
		return;
	}

	const UWorld* World = Character.GetWorld();
	UE_LOG(LogProjectJPlayer, Log,
		TEXT("[CombatPresentation] Stage=%s Time=%.3f Owner=%s Local=%s Authority=%s LocalRole=%d RemoteRole=%d"),
		Stage,
		World ? World->GetTimeSeconds() : -1.0f,
		*GetNameSafe(&Character),
		Character.IsLocallyControlled() ? TEXT("true") : TEXT("false"),
		Character.HasAuthority() ? TEXT("true") : TEXT("false"),
		static_cast<int32>(Character.GetLocalRole()),
		static_cast<int32>(Character.GetRemoteRole()));
}

int32 GetCharacterLevelForInterfaceObject(const UObject* Object)
{
	return Object ? IProject_JCombatInterface::Execute_GetCharacterLevel(const_cast<UObject*>(Object)) : 1;
}
}

AProject_JPlayerCharacter::AProject_JPlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	RuntimeStateOwnership = EProject_JRuntimeStateOwnership::PlayerStatePreferred;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	PrimaryActorTick.bCanEverTick = true; // Tick ?쒖꽦??

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	GetMesh()->SetAnimInstanceClass(UProject_JCharacterAnimInstance::StaticClass());
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
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
	// Mover trajectory is captured once by the native GASP animation bridge.
	CharacterUIBindingComponent = CreateDefaultSubobject<UProject_JCharacterUIBindingComponent>(TEXT("CharacterUIBindingComponent"));
	PlayerInputBindingComponent = CreateDefaultSubobject<UProject_JPlayerInputBindingComponent>(TEXT("PlayerInputBindingComponent"));
	SkillInputRouterComponent = CreateDefaultSubobject<UProject_JSkillInputRouterComponent>(TEXT("SkillInputRouterComponent"));
	SkillInputExecutionComponent = CreateDefaultSubobject<UProject_JSkillInputExecutionComponent>(TEXT("SkillInputExecutionComponent"));
	AnimationUpdateCoordinator = CreateDefaultSubobject<UProject_JAnimationUpdateCoordinatorComponent>(TEXT("AnimationUpdateCoordinator"));
	ReplicatedAnimEventComponent = CreateDefaultSubobject<UProject_JReplicatedAnimEventComponent>(TEXT("ReplicatedAnimEventComponent"));
	ReplicatedAnimEventComponent->Initialize(LocomotionAnimStateComponent, AnimationUpdateCoordinator);
	ReplicatedJumpStateComponent = CreateDefaultSubobject<UProject_JReplicatedJumpStateComponent>(TEXT("ReplicatedJumpStateComponent"));
	ReplicatedJumpStateComponent->Initialize(LocomotionAnimStateComponent, AnimationUpdateCoordinator);
	CombatStateComponent = CreateDefaultSubobject<UProject_JCombatStateComponent>(TEXT("CombatStateComponent"));
	CombatIntroComponent = CreateDefaultSubobject<UProject_JCombatIntroComponent>(TEXT("CombatIntroComponent"));
	CombatAnimationLayerComponent = CreateDefaultSubobject<UProject_JCombatAnimationLayerComponent>(TEXT("CombatAnimationLayerComponent"));
	WeaponPresentationComponent = CreateDefaultSubobject<UProject_JWeaponPresentationComponent>(TEXT("WeaponPresentationComponent"));
	CombatPresentationComponent = CreateDefaultSubobject<UProject_JCombatPresentationComponent>(TEXT("CombatPresentationComponent"));
	CombatHitValidationComponent = CreateDefaultSubobject<UProject_JCombatHitValidationComponent>(TEXT("CombatHitValidationComponent"));
	MountComponent = CreateDefaultSubobject<UProject_JMountComponent>(TEXT("MountComponent"));
	MountedAnimationLayerComponent = CreateDefaultSubobject<UProject_JMountedAnimationLayerComponent>(TEXT("MountedAnimationLayerComponent"));
}

void AProject_JPlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AProject_JPlayerCharacter, SummonedMount);
	DOREPLIFETIME(AProject_JPlayerCharacter, CurrentCombatStyle);
	DOREPLIFETIME(AProject_JPlayerCharacter, CurrentWeaponPresentationProfile);
	DOREPLIFETIME_CONDITION(AProject_JPlayerCharacter, bReplicatedCombatModePresentation, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AProject_JPlayerCharacter, bReplicatedCombatIntroPresentation, COND_SkipOwner);
}

// Creation-only: retain PlayerState's own steady priority/count-limiter policy.
void AProject_JPlayerCharacter::RefreshPlayerStateCreationDependency(bool bRegistrationStarted)
{
    using namespace UE::Net;
    UProject_JOwnedPawnScopeComponent::Wake(this);
    UWorld* World = GetWorld();
    APlayerState* State = GetPlayerState();
    const UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
    const UReplicationSystem* System = Driver ? Driver->GetReplicationSystem() : nullptr;
    UEngineReplicationBridge* Bridge = IsValid(this) && HasAuthority() && GetIsReplicated() && !bCreationDependencyEnding &&
        !IsActorBeingDestroyed() && World && !World->bIsTearingDown &&
        (HasActorBegunPlay() || IsActorBeginningPlay()) && IsValid(State) && !State->IsActorBeingDestroyed() &&
        State->HasAuthority() && State->GetIsReplicated() && State->GetWorld() == World && State->GetPawn() == this &&
        (State->HasActorBegunPlay() || State->IsActorBeginningPlay()) && System && System->IsServer()
        ? FReplicationSystemUtil::GetActorReplicationBridge(this) : nullptr;
    if (!IsValid(Bridge) || System->GetReplicationBridge() != Bridge ||
        FReplicationSystemUtil::GetActorReplicationBridge(State) != Bridge)
    {
        RetirePlayerStateCreationDependency();
        return;
    }
    const FNetRefHandle Parent = Bridge->GetReplicatedRefHandle(State);
    const FNetRefHandle Pawn = Bridge->GetReplicatedRefHandle(this);
    if (!Parent.IsCompleteHandle() || !Pawn.IsCompleteHandle() || Parent == Pawn ||
        Bridge->GetReplicatedObject(Parent) != State || Bridge->GetReplicatedObject(Pawn) != this)
    {
        // Either actor may register last. Its OnReplicationStarted wakes this
        // path; never force-register an actor or poll for a missing handle.
        RetirePlayerStateCreationDependency();
        return;
    }
    const bool bSamePair = CreationDependencyState.Get() == State && CreationDependencyBridge.Get() == Bridge &&
        FNetRefHandle::FullCompare(CreationDependencyParent, Parent) && FNetRefHandle::FullCompare(CreationDependencyPawn, Pawn);
    if (bSamePair && !bRegistrationStarted) return;
    if (!bSamePair) RetirePlayerStateCreationDependency();
    // OnStop has no driver identity. Root stop removes its graph links in Iris;
    // keep weak keys until revalidation rather than deleting another driver's
    // primary edge. Add is idempotent even if registration reuses the same pair.
    Bridge->AddCreationDependencyLink(Parent, Pawn);
    CreationDependencyState = State;
    CreationDependencyBridge = Bridge;
    CreationDependencyParent = Parent;
    CreationDependencyPawn = Pawn;
}

void AProject_JPlayerCharacter::RetirePlayerStateCreationDependency()
{
    using namespace UE::Net;
    APlayerState* State = CreationDependencyState.Get();
    UEngineReplicationBridge* Bridge = CreationDependencyBridge.Get();
    const FNetRefHandle Parent = CreationDependencyParent, Pawn = CreationDependencyPawn;
    CreationDependencyState.Reset(); CreationDependencyBridge.Reset();
    CreationDependencyParent = {}; CreationDependencyPawn = {};
    UWorld* World = GetWorld();
    const UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
    const UReplicationSystem* System = Driver ? Driver->GetReplicationSystem() : nullptr;
    // Remove rejects stale handles. After root stop/driver teardown the engine
    // owns removal; only a still-registered exact pair may be retired here.
    if (!World || World->bIsTearingDown || !System || !System->IsServer() || !IsValid(Bridge) ||
        System->GetReplicationBridge() != Bridge || !IsValid(State) || State->GetWorld() != World ||
        !Parent.IsCompleteHandle() || !Pawn.IsCompleteHandle() ||
        !FNetRefHandle::FullCompare(Bridge->GetReplicatedRefHandle(State), Parent) ||
        !FNetRefHandle::FullCompare(Bridge->GetReplicatedRefHandle(this), Pawn) ||
        Bridge->GetReplicatedObject(Parent) != State || Bridge->GetReplicatedObject(Pawn) != this) return;
    Bridge->RemoveCreationDependencyLink(Parent, Pawn);
}

void AProject_JPlayerCharacter::OnPlayerStateChanged(APlayerState* NewPlayerState, APlayerState* OldPlayerState)
{
    Super::OnPlayerStateChanged(NewPlayerState, OldPlayerState);
    RefreshPlayerStateCreationDependency();
}

void AProject_JPlayerCharacter::OnReplicationStartedForIris(const FOnReplicationStartedParams& Params)
{
    const TStrongObjectPtr<AProject_JPlayerCharacter> KeepSelf(this);
    if (IsActorBeginningPlay()) bCreationDependencyEnding = false;
    Super::OnReplicationStartedForIris(Params);
    if (IsValid(this)) RefreshPlayerStateCreationDependency(true);
}

void AProject_JPlayerCharacter::OnStopReplicationForIris(const FOnStopReplicationParams& Params)
{
    // Callback has no driver identity. Re-read the primary lease at the safe
    // phase instead of revoking a still-valid primary for a secondary stop.
    UProject_JOwnedPawnScopeComponent::Wake(this, false);
    Super::OnStopReplicationForIris(Params);
}

void AProject_JPlayerCharacter::BeginPlay()
{
	bCreationDependencyEnding = false;
	ObservePlayerStateAbilitySystem();
	Super::BeginPlay();

	if (CombatStateComponent)
	{
		CombatStateComponent->OnCombatStateTagChanged.AddUniqueDynamic(this, &AProject_JPlayerCharacter::OnCombatStateTagChanged);
	}
	if (CombatIntroComponent)
	{
		CombatIntroComponent->OnCombatIntroEnded.AddUniqueDynamic(this, &AProject_JPlayerCharacter::OnCombatIntroMontageEnded);
		CombatIntroComponent->OnCombatOutroEnded.AddUniqueDynamic(this, &AProject_JPlayerCharacter::OnCombatOutroMontageEnded);
		CombatIntroComponent->OnTransitionStateChanged.AddUObject(this, &ThisClass::RefreshCombatTransitionViews);
		RefreshCombatTransitionViews();
	}
	if (ReplicatedAnimEventComponent)
	{
		ReplicatedAnimEventComponent->Initialize(LocomotionAnimStateComponent, AnimationUpdateCoordinator);
	}
	if (ReplicatedJumpStateComponent)
	{
		ReplicatedJumpStateComponent->Initialize(LocomotionAnimStateComponent, AnimationUpdateCoordinator);
	}
	if (MountComponent)
	{
		MountComponent->OnMountChanged.AddUniqueDynamic(this, &AProject_JPlayerCharacter::OnMountChangedForAnimation);
	}

	ApplyLocomotionProfile();
	LogAnimationProfileConfiguration();
	RefreshAbilitySystemDependentComponents();
	ApplyPrototypeStartingEquipment();
	if (CombatAnimationLayerComponent)
	{
		CombatAnimationLayerComponent->PreloadLayerForCurrentWeapon();
		CombatAnimationLayerComponent->RefreshLayer();
	}

	// 諛붿씤?? ?댄깮/?뚰뵾/?쇨꺽 ?쒓렇媛 蹂寃쎈맆 ??利됯컖?곸쑝濡?Sprint ?띾룄瑜?媛깆떊
}

void AProject_JPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bCreationDependencyEnding = true;
	UProject_JOwnedPawnScopeComponent::WithdrawFor(this);
	StopObservingPlayerStateAbilitySystem();
	if (IsValid(MountComponent)) MountComponent->OnRuntimeOwnerPrerequisitesChanged().Remove(MountRuntimeOwnerPrerequisitesChangedHandle);
	MountRuntimeOwnerPrerequisitesChangedHandle.Reset();
	RetirePlayerStateCreationDependency();
#if !UE_BUILD_SHIPPING
	UnregisterMouseTurnCameraTrace();
#endif
	PendingMountItemId.Invalidate();
	if (IsValid(SummonedMount))
	{
		SummonedMount->OnDestroyed.RemoveDynamic(this, &AProject_JPlayerCharacter::OnSummonedMountDestroyed);
	}
	if (PendingMountClassLoadHandle)
	{
		PendingMountClassLoadHandle->CancelHandle();
		PendingMountClassLoadHandle.Reset();
	}
	// Sprint is an InstancedPerActor ability. Explicitly end it before the ASC
	// starts removing specs during PIE shutdown or pawn destruction.
	bSprintInputHeld = false;
	CancelAbilitiesByTag(SprintAbilityTag);

	if (MountComponent)
	{
		MountComponent->OnMountChanged.RemoveDynamic(this, &AProject_JPlayerCharacter::OnMountChangedForAnimation);
	}
	UnregisterCombatStateTagEvents();
	if (CombatStateComponent)
	{
		CombatStateComponent->OnCombatStateTagChanged.RemoveDynamic(this, &AProject_JPlayerCharacter::OnCombatStateTagChanged);
	}
	if (CombatIntroComponent)
	{
		CombatIntroComponent->OnCombatIntroEnded.RemoveDynamic(this, &AProject_JPlayerCharacter::OnCombatIntroMontageEnded);
		CombatIntroComponent->OnCombatOutroEnded.RemoveDynamic(this, &AProject_JPlayerCharacter::OnCombatOutroMontageEnded);
		CombatIntroComponent->OnTransitionStateChanged.RemoveAll(this);
	}

	Super::EndPlay(EndPlayReason);
}

void AProject_JPlayerCharacter::PossessedBy(AController* NewController)
{
    if (HasAuthority()) UProject_JOwnedPawnScopeComponent::Ensure(Cast<APlayerController>(NewController));
	Super::PossessedBy(NewController);

	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->RefreshCachedReferences();
	}

	RefreshAbilitySystemDependentComponents();
	ApplyPrototypeStartingEquipment();
	if (FoleyComponent) { FoleyComponent->PrepareLocalAudio(); }
}

void AProject_JPlayerCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	if (auto* ASC = Cast<UProject_JAbilitySystemComponent>(GetAbilitySystemComponent()))
	{ ASC->RefreshActorInfoForCurrentActor(this); }
	if (FoleyComponent) { FoleyComponent->PrepareLocalAudio(); }
}

void AProject_JPlayerCharacter::UnPossessed()
{
	UProject_JOwnedPawnScopeComponent::WithdrawFor(this);
	StopSprint();
	SprintMoveInput = FVector2D::ZeroVector;
	if (PlayerInputBindingComponent) PlayerInputBindingComponent->UnbindInput();
	Super::UnPossessed();
}

bool AProject_JPlayerCharacter::CanConsumePlayerStateAbilitySystem(const APlayerState* State) const
{
	if (!IsValid(State) || State->IsActorBeingDestroyed() || State->GetWorld() != GetWorld()) return false;
	if (HasAuthority()) return State->GetPawn() == this;
	const auto* OwnerInterface = Cast<IProject_JAbilitySystemOwnerInterface>(State);
	const auto* ASC = OwnerInterface ? OwnerInterface->GetProjectJAbilitySystemComponent() : nullptr;
	return IsValid(ASC) && !ASC->IsBeingDestroyed() && ASC->GetOwner() == State
		&& !ASC->IsActorInfoChanging() && ASC->AbilityActorInfo.IsValid()
		&& ASC->GetOwnerActor() == State && ASC->AbilityActorInfo->OwnerActor.Get() == State
		&& ASC->GetAvatarActor_Direct() == this && ASC->GetAvatarActor() == this;
}

void AProject_JPlayerCharacter::StopObservingPlayerStateAbilitySystem()
{
	if (auto* ASC = ObservedPlayerStateASC.Get()) ASC->OnActorInfoSettled.Remove(PlayerStateActorInfoSettledHandle);
	ObservedPlayerStateASC.Reset();
	PlayerStateActorInfoSettledHandle.Reset();
}

void AProject_JPlayerCharacter::ObservePlayerStateAbilitySystem()
{
	if (bCreationDependencyEnding) return;
	// Authority also observes registration changes on the same possessed mount;
	// those do not produce a new-pawn notification on its controller.
	if (IsValid(MountComponent) && !MountRuntimeOwnerPrerequisitesChangedHandle.IsValid())
		MountRuntimeOwnerPrerequisitesChangedHandle = MountComponent->OnRuntimeOwnerPrerequisitesChanged().AddUObject(this, &ThisClass::OnMountRuntimeOwnerPrerequisitesChanged);
	if (HasAuthority()) return;
	APlayerState* State = GetPlayerState();
	if (!State && IsValid(MountComponent)) State = MountComponent->ResolveRuntimeOwner();
	// APawn clears PlayerState during mounting. Retain only a weak subscription
	// through that gap/ASC.Clear so a later actual publication can wake this Pawn.
	if (!State) return;
	if (!IsValid(State) || State->IsActorBeingDestroyed() || State->GetWorld() != GetWorld())
	{
		StopObservingPlayerStateAbilitySystem();
		return;
	}
	const auto* OwnerInterface = Cast<IProject_JAbilitySystemOwnerInterface>(State);
	auto* ASC = OwnerInterface ? OwnerInterface->GetProjectJAbilitySystemComponent() : nullptr;
	if (!IsValid(ASC) || ASC->IsBeingDestroyed()
		|| ASC->GetOwner() != State || ASC->GetWorld() != GetWorld()) ASC = nullptr;
	if (ObservedPlayerStateASC.Get() == ASC) return;
	StopObservingPlayerStateAbilitySystem();
	ObservedPlayerStateASC = ASC;
	if (ASC) PlayerStateActorInfoSettledHandle = ASC->OnActorInfoSettled.AddUObject(this, &ThisClass::OnPlayerStateActorInfoSettled);
}

void AProject_JPlayerCharacter::OnPlayerStateActorInfoSettled(UProject_JAbilitySystemComponent* Source)
{
	if (bCreationDependencyEnding || IsActorBeingDestroyed() || ObservedPlayerStateASC.Get() != Source) return;
	// Re-read on every callback: an earlier subscriber can Init/Clear reentrantly.
	// The ordinary OnRep path rechecks the exact tuple before APawn can rewrite
	// PlayerState.Pawn. With a null PS (mounted rider), it never writes that link.
	OnRep_PlayerState();
}

void AProject_JPlayerCharacter::OnMountRuntimeOwnerPrerequisitesChanged()
{
	if (bCreationDependencyEnding || IsActorBeingDestroyed()) return;
	if (HasAuthority())
	{
		auto* CurrentMount = IsValid(MountComponent) ? MountComponent->GetMountedMount() : nullptr;
		if (IsValid(CurrentMount) && !CurrentMount->IsActorBeingDestroyed()
			&& CurrentMount->GetWorld() == GetWorld() && CurrentMount->GetRider() == this)
			UProject_JOwnedPawnScopeComponent::Wake(CurrentMount, false);
		return;
	}
	// Same ASC tuple across dismount: topology completion, not a new recipient
	// epoch, wakes the existing pre-Super/back-reference and consumer fences.
	OnRep_PlayerState();
}

void AProject_JPlayerCharacter::OnRep_Controller()
{
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	Super::OnRep_Controller();
	if (!IsValid(this) || IsActorBeingDestroyed() || bCreationDependencyEnding) return;
	OnMountRuntimeOwnerPrerequisitesChanged();
}

void AProject_JPlayerCharacter::OnRep_PlayerState()
{
	ObservePlayerStateAbilitySystem();
	APlayerState* State = GetPlayerState();
	const auto* PublishedMount = State ? Cast<AProject_JMountCharacter>(State->GetPawn()) : nullptr;
	const bool bRetainedMountedRider = PublishedMount && PublishedMount->GetRider() == this;
	if ((!State || CanConsumePlayerStateAbilitySystem(State)) && !bRetainedMountedRider)
	{
		Super::OnRep_PlayerState();
	}
	else
	{
		// APawn::OnRep_PlayerState calls SetPlayerState and unconditionally writes
		// State.Pawn=this. Reject stale/unpublished recipients before that write;
		// an already controlled mount retains its own back-reference.
		InitializeAbilitySystem();
		BindEquipmentRuntimeToResolvedEquipmentManager();
	}
	if (LocomotionAnimStateComponent) LocomotionAnimStateComponent->RefreshCachedReferences();
	RefreshAbilitySystemDependentComponents();
}

void AProject_JPlayerCharacter::ApplyPrototypeStartingEquipment()
{
	if (!HasAuthority() || !PrototypeStartingWeapon || !GetPlayerState())
	{
		return;
	}

	UProject_JEquipmentManagerComponent* ResolvedEquipmentManager = ResolveEquipmentManagerForRuntime();
	if (!ResolvedEquipmentManager)
	{
		UE_LOG(LogProjectJPlayer, Warning, TEXT("Prototype starting weapon was not equipped: no EquipmentManager is available. Owner=%s Weapon=%s"),
			*GetName(), *GetNameSafe(PrototypeStartingWeapon));
		return;
	}

	if (ResolvedEquipmentManager->bPersistentProfileBound || ResolvedEquipmentManager->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon))
	{
		return;
	}

	ResolvedEquipmentManager->EquipItem(PrototypeStartingWeapon);
	UE_LOG(LogProjectJPlayer, Log, TEXT("Prototype starting weapon equipped. Owner=%s Weapon=%s"),
		*GetName(), *GetNameSafe(PrototypeStartingWeapon));
}

void AProject_JPlayerCharacter::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_PlayerCharacterTick);
	Super::Tick(DeltaTime);
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_PlayerCharacterTick_MovementPolicy);
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_PlayerCharacterTick_ApplyCombatRotationMode);
			ApplyCombatRotationMode(IsCombatModeActive());
		}
	}

	// The component owns generation eligibility and same-frame de-duplication.
	// Calling here guarantees locomotion semantics consume the current movement
	// frame even when CharacterMovement did not broadcast an update.


	if (LocomotionAnimStateComponent)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_PlayerCharacterTick_LocomotionState);
		LocomotionAnimStateComponent->UpdateState(DeltaTime);

	}
#if !UE_BUILD_SHIPPING
	RecordMouseTurnFrame(DeltaTime);
#endif
}

AActor* AProject_JPlayerCharacter::GetAbilitySystemOwnerActor() const
{
	if (APlayerState* PS = GetPlayerState(); CanConsumePlayerStateAbilitySystem(PS))
	{
		// A retained mount rider is resolved below using its existing reciprocal
		// mount/session contract, rather than lending an arbitrary stale PS.
		if (const auto* Mount = Cast<AProject_JMountCharacter>(PS->GetPawn()); !Mount || Mount->GetRider() != this)
			return PS;
	}
	// APawn clears the rider's PlayerState during the mount possession hand-off.
	// Resolve only the current reciprocal mount session; never cache an old owner.
	if (IsValid(MountComponent))
	{
		if (auto* PS = MountComponent->ResolveRuntimeOwner()) { return PS; }
	}
	return Super::GetAbilitySystemOwnerActor();
}

void AProject_JPlayerCharacter::OnMountChangedForAnimation(AProject_JMountCharacter* PreviousMount, AProject_JMountCharacter* NewMount)
{
	if (NewMount)
	{
		// Authority rejects active/entering combat before this point. A completed
		// combat exit may still be blending its cosmetic sheathe montage, so mounted
		// presentation terminates either transition and normalizes the weapon socket.
		CancelCombatIntroMontage();
		CancelCombatOutroMontage();
		MoveWeaponToSheathedSocket();
		ApplyCombatRotationMode(false);
	}

	if (CombatAnimationLayerComponent)
	{
		CombatAnimationLayerComponent->PreloadLayerForCurrentWeapon();
		CombatAnimationLayerComponent->RefreshLayer();
	}
}

void AProject_JPlayerCharacter::OnRep_SummonedMount()
{
	// Do not make every client preload every remote player's merely summoned
	// mount. Simulated proxies stream the rider layer only on the actual replicated
	// mount event; the local owner gets the latency-hiding early preload.
	if (IsLocallyControlled() && MountedAnimationLayerComponent)
	{
		MountedAnimationLayerComponent->PreloadLayerForMount(GetSummonedMount());
	}
}

AProject_JMountCharacter* AProject_JPlayerCharacter::GetSummonedMount() const
{
	return IsValid(SummonedMount) && !SummonedMount->IsActorBeingDestroyed() ? SummonedMount.Get() : nullptr;
}

void AProject_JPlayerCharacter::OnSummonedMountDestroyed(AActor* DestroyedActor)
{
	if (SummonedMount != DestroyedActor) return;
	SummonedMount = nullptr;
	OnRep_SummonedMount();
	ForceNetUpdate();
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
		UseMountItemAuthority(ItemInstanceId);
	}
	else
	{
		ServerRequestUseMountItem(ItemInstanceId);
	}
}

void AProject_JPlayerCharacter::ServerRequestUseMountItem_Implementation(FGuid ItemInstanceId)
{
	// Match mount/interaction admission before inventory lookup, load replacement or spawn.
	if (!UProject_JCommandBudgetComponent::AdmitClientCommand(this, NextClientMountItemRequestTime, 0.2)) return;
	UseMountItemAuthority(ItemInstanceId);
}

void AProject_JPlayerCharacter::UseMountItemAuthority(FGuid ItemInstanceId)
{
	if (IsActorBeingDestroyed() || !HasAuthority()) return;
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
	if (!MountItem || !GetWorld())
	{
		return;
	}
	if (PendingMountClassLoadHandle && PendingMountItemId != ItemInstanceId)
	{
		PendingMountItemId.Invalidate();
		PendingMountClassLoadHandle->CancelHandle();
		PendingMountClassLoadHandle.Reset();
	}

	if (GetSummonedMount())
	{
		if (PendingMountClassLoadHandle)
		{
			PendingMountItemId.Invalidate();
			PendingMountClassLoadHandle->CancelHandle();
			PendingMountClassLoadHandle.Reset();
		}
		if (MountItem->bAutoMountAfterSpawn)
		{
			SummonedMount->TryMountRider(this);
		}
		return;
	}
	// Destroy() precedes garbage collection; a non-null UObject pointer is not
	// sufficient evidence that the previous summoned actor is still usable.
	SummonedMount = nullptr;

	const TSubclassOf<AProject_JMountCharacter> MountClass = MountItem->MountClass.Get();
	if (MountClass && PendingMountClassLoadHandle)
	{
		PendingMountItemId.Invalidate();
		PendingMountClassLoadHandle->CancelHandle();
		PendingMountClassLoadHandle.Reset();
	}
	if (!MountClass)
	{
		const FSoftObjectPath MountClassPath = MountItem->MountClass.ToSoftObjectPath();
		if (MountClassPath.IsNull() || (PendingMountClassLoadHandle && PendingMountItemId == ItemInstanceId))
		{
			return;
		}
		if (PendingMountClassLoadHandle)
		{
			PendingMountClassLoadHandle->CancelHandle();
			PendingMountClassLoadHandle.Reset();
		}
		PendingMountItemId = ItemInstanceId;
		const TWeakObjectPtr<AProject_JPlayerCharacter> WeakThis(this);
		const TSoftClassPtr<AProject_JMountCharacter> MountClassRef = MountItem->MountClass;
		PendingMountClassLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
			MountClassPath,
			FStreamableDelegate::CreateLambda([WeakThis, ItemInstanceId, MountClassRef]()
			{
				AProject_JPlayerCharacter* Character = WeakThis.Get();
				if (!Character || Character->PendingMountItemId != ItemInstanceId)
				{
					return;
				}
				TSharedPtr<FStreamableHandle> CompletedHandle = MoveTemp(Character->PendingMountClassLoadHandle);
				Character->PendingMountItemId.Invalidate();
				if (Character->HasAuthority() && MountClassRef.Get())
				{
					// Recheck the inventory, lock state, and current mount after loading.
					Character->UseMountItemAuthority(ItemInstanceId);
				}
				else if (Character->HasAuthority())
				{
					UE_LOG(LogProjectJPlayer, Warning, TEXT("Mount class failed to load: %s"), *MountClassRef.ToSoftObjectPath().ToString());
				}
				CompletedHandle.Reset();
			}));
		if (!PendingMountClassLoadHandle)
		{
			PendingMountItemId.Invalidate();
			UE_LOG(LogProjectJPlayer, Warning, TEXT("Mount class load could not start: %s"), *MountClassPath.ToString());
		}
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.Instigator = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
	const FVector SpawnLocation = GetActorLocation() + GetActorForwardVector() * MountItem->SpawnDistance;
	AProject_JMountCharacter* Mount = GetWorld()->SpawnActor<AProject_JMountCharacter>(MountClass, SpawnLocation, GetActorRotation(), SpawnParameters);
	SummonedMount = IsValid(Mount) && !Mount->IsActorBeingDestroyed() ? Mount : nullptr;
	if (SummonedMount)
	{
		SummonedMount->OnDestroyed.AddUniqueDynamic(this, &AProject_JPlayerCharacter::OnSummonedMountDestroyed);
	}
	OnRep_SummonedMount();
	if (SummonedMount && MountItem->bAutoMountAfterSpawn)
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
 ActionSet.WalkAction = WalkAction;
	ActionSet.ToggleCombatAction = ToggleCombatAction;
	ActionSet.AttackAction = AttackAction;
	ActionSet.HeavyAttackAction = HeavyAttackAction;
	ActionSet.SkillModifierAction = SkillModifierAction;
	ActionSet.SkillInputMappingData = SkillInputRouterComponent ? SkillInputRouterComponent->InputMappingData : nullptr;
	ActionSet.InteractAction = InteractAction;

	if (SkillInputRouterComponent)
	{
		SkillInputRouterComponent->Initialize(this);
	}

	const bool bBoundInput = PlayerInputBindingComponent && PlayerInputBindingComponent->BindInput(PlayerInputComponent, this, ActionSet);
	if (!bBoundInput)
	{
		UE_LOG(LogProjectJPlayer, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AProject_JPlayerCharacter::TryInteract()
{
	if (!HasAuthority())
	{
		ServerTryInteract();
		return;
	}
	Project_J::Interaction::TryInteract(*this);
}

void AProject_JPlayerCharacter::ServerTryInteract_Implementation()
{
	// Client requests pay the existing session budget before collision/reflected
	// work, plus the same domain interval used by mount requests. Trusted server
	// TryInteract calls retain their existing execution path.
	if (!UProject_JCommandBudgetComponent::AdmitClientCommand(this, NextClientInteractionRequestTime, 0.2)) return;
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
	// PlayerState owns the ASC across pawn replacement. An old input callback or
	// old pawn EndPlay must never cancel an ability running on its new avatar.
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (ASC && ASC->GetAvatarActor() == this && AbilityTag.IsValid())
	{
		FGameplayTagContainer AbilityTags(AbilityTag);
		ASC->CancelAbilities(&AbilityTags);
	}
}

void AProject_JPlayerCharacter::RefreshAbilitySystemDependentComponents()
{
	if (CharacterUIBindingComponent)
	{
		CharacterUIBindingComponent->InitializeFromAttributes(Cast<UProject_JAbilitySystemComponent>(GetAbilitySystemComponent()), GetAttributeSet(), GetCharacterLevelForInterfaceObject(this));
	}

	RegisterCombatStateTagEvents();
	if (SkillInputExecutionComponent)
	{
		SkillInputExecutionComponent->Initialize(this);
	}
	if (CameraComponent)
	{
		CameraComponent->RefreshAbilitySystemBinding();
	}
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
		if (HasAuthority())
		{
			bReplicatedCombatModePresentation = bIsCombatModeActive;
			// The gameplay tag is applied after the local draw montage. Clear the
			// earlier cosmetic transition once that authoritative state arrives.
			bReplicatedCombatIntroPresentation = false;
			if (bIsCombatModeActive)
			{
				SetCombatTransitionState(false);
			}
			ForceNetUpdate();
			LogCombatPresentationTrace(*this, TEXT("ServerCombatStateReplicated"));
		}

		ApplyCombatPresentationState(bIsCombatModeActive, false);
	}

	UpdateMaxWalkSpeed();
	ApplySprintAnimationState();
	RefreshSprintAbilityFromInput();
}

void AProject_JPlayerCharacter::OnRep_ReplicatedCombatModePresentation()
{
	LogCombatPresentationTrace(*this, TEXT("RemoteCombatStateOnRep"));
	// Never consult the remote avatar's ASC here. Its replicated tag timing is
	// intentionally independent from this cosmetic state and can otherwise keep
	// a just-sheathed weapon attached to the hand.
	const bool bNeedsFallbackRemoteTransition =
		bReplicatedCombatModePresentation && !bHasReceivedRemoteCombatIntroPresentation;
	ApplyCombatPresentationState(bReplicatedCombatModePresentation, bNeedsFallbackRemoteTransition);
	// The main presentation state is now authoritative. The next combat entry
	// needs a fresh early-transition signal, but this entry must not replay it.
	bHasReceivedRemoteCombatIntroPresentation = false;
	UpdateMaxWalkSpeed();
	ApplySprintAnimationState();
}

void AProject_JPlayerCharacter::OnRep_ReplicatedCombatIntroPresentation()
{
	LogCombatPresentationTrace(
		*this,
		bReplicatedCombatIntroPresentation ? TEXT("RemoteIntroOnRepTrue") : TEXT("RemoteIntroOnRepFalse"));
	// This is intentionally independent from the ASC tag replication: remote
	// observers must see draw immediately, while the server still grants combat
	// gameplay only after the owning player's intro completes.
	if (bReplicatedCombatIntroPresentation && !bReplicatedCombatModePresentation)
	{
		bHasReceivedRemoteCombatIntroPresentation = true;
		if (WeaponPresentationComponent)
		{
			WeaponPresentationComponent->EnterCombatPresentation();
		}
		PlayCosmeticCombatIntroMontage();
	}

	if (CombatAnimationLayerComponent)
	{
		CombatAnimationLayerComponent->RefreshLayer();
	}
}

void AProject_JPlayerCharacter::OnProgressionChanged()
{
	TStrongObjectPtr<ThisClass> PinnedThis(this);
	TWeakObjectPtr<UWorld> OriginalWorld(GetWorld());
	const int32 PreviousRevision = CombatConfiguration.Revision;
	if (GetCombatConfiguration().Revision != PreviousRevision)
	{
		OnRep_CurrentCombatStyle();
	}
	if (IsActorBeingDestroyed() || GetWorld() != OriginalWorld.Get() || (GetWorld() && GetWorld()->bIsTearingDown)) { return; }
	// A level-only update must not clear command sequences or reload animation layers.
	if (CharacterUIBindingComponent) CharacterUIBindingComponent->UpdateCharacterLevel(GetCharacterLevel_Implementation());
}

void AProject_JPlayerCharacter::OnRep_CurrentCombatStyle()
{
	TStrongObjectPtr<ThisClass> PinnedThis(this);
	TWeakObjectPtr<UWorld> OriginalWorld(GetWorld());
	const int32 Revision = GetCombatConfiguration().Revision;
	if (CombatPresentationComponent) { CombatPresentationComponent->RefreshPresentation(); }
	if (IsActorBeingDestroyed() || GetWorld() != OriginalWorld.Get() ||
		(GetWorld() && GetWorld()->bIsTearingDown) || GetCombatConfiguration().Revision != Revision) { return; }
	ApplyCurrentCombatStyleReaction();
}

void AProject_JPlayerCharacter::ApplyCurrentCombatStyleReaction()
{
	// Both private callers retain the owner and validate their world/configuration after retirement.
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		if (auto* Combo = Cast<UProject_JGameplayAbility_Melee>(ASC->GetAnimatingAbility())) { Combo->ReconcileCombatConfiguration(); }
		TArray<FText> Conflicts;
		FProject_JCombatConfiguration::FindInputGrantConflicts(*ASC, Conflicts);
		for (const FText& Conflict : Conflicts) { UE_LOG(LogProjectJPlayer, Verbose, TEXT("%s"), *Conflict.ToString()); }
	}
	if (SkillInputExecutionComponent)
	{
		SkillInputExecutionComponent->ClearCommandInputHistory();
	}
	if (CombatAnimationLayerComponent)
	{
		CombatAnimationLayerComponent->PreloadLayerForCurrentWeapon();
		CombatAnimationLayerComponent->RefreshLayer();
	}
}

void AProject_JPlayerCharacter::OnRep_CurrentWeaponPresentationProfile()
{
	TStrongObjectPtr<ThisClass> PinnedThis(this);
	TWeakObjectPtr<UWorld> OriginalWorld(GetWorld());
	TWeakObjectPtr<UProject_JWeaponPresentationProfile> SelectedProfile(CurrentWeaponPresentationProfile);
	const int32 Revision = GetCombatConfiguration().Revision;
	// Remote profile-only updates obey the same order as the local pair setter:
	// retire old cues before destroying/replacing their weapon attachment.
	if (CombatPresentationComponent) { CombatPresentationComponent->RefreshPresentation(); }
	if (IsActorBeingDestroyed() || GetWorld() != OriginalWorld.Get() ||
		(GetWorld() && GetWorld()->bIsTearingDown) || CurrentWeaponPresentationProfile != SelectedProfile.Get() ||
		GetCombatConfiguration().Revision != Revision) { return; }
	if (WeaponPresentationComponent) { WeaponPresentationComponent->RefreshPresentation(); }
}

void AProject_JPlayerCharacter::ApplyCombatPresentationState(
	const bool bCombatModeActive,
	const bool bPlayRemoteTransitionMontage)
{
	if (bCombatModeActive)
	{
		CancelCombatOutroMontage();
		if (WeaponPresentationComponent)
		{
			WeaponPresentationComponent->EnterCombatPresentation();
		}
		ApplyCombatRotationMode(true);

		// The owning client already played the draw montage before applying its
		// predicted combat state. Simulated proxies need an explicit cosmetic play.
		if (bPlayRemoteTransitionMontage)
		{
			PlayCosmeticCombatIntroMontage();
		}
	}
	else
	{
		CancelCombatIntroMontage();
		ApplyCombatRotationMode(false);
		if (GetEffectiveCombatOutroMontage())
		{
			if (WeaponPresentationComponent)
			{
				WeaponPresentationComponent->BeginSheathePresentation();
			}
			PlayCombatOutroMontage();
		}
		else if (WeaponPresentationComponent)
		{
			WeaponPresentationComponent->ExitCombatPresentation();
		}
	}

	if (CombatAnimationLayerComponent)
	{
		CombatAnimationLayerComponent->RefreshLayer();
	}
}

void AProject_JPlayerCharacter::ApplyLocomotionProfile()
{
	if (const UProject_JLocomotionProfile* EffectiveLocomotionProfile = GetLocomotionProfile())
	{
		if (LocomotionAnimStateComponent)
		{
			LocomotionAnimStateComponent->ApplyTransitionPolicy(EffectiveLocomotionProfile->TransitionPolicy);
			LocomotionAnimStateComponent->HiddenRemoteUpdateInterval = EffectiveLocomotionProfile->AnimStateHiddenRemoteUpdateInterval;
		}

		SignificanceNearDistance = EffectiveLocomotionProfile->AnimationBudget.NearDistance;
		SignificanceMidDistance = EffectiveLocomotionProfile->AnimationBudget.MidDistance;
		SignificanceFarDistance = EffectiveLocomotionProfile->AnimationBudget.FarDistance;
	}

	UpdateMaxWalkSpeed();
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

UAnimMontage* AProject_JPlayerCharacter::GetEffectiveCombatOutroMontage() const
{
	if (const UProject_JWeaponAnimProfile* EffectiveWeaponAnimProfile = GetWeaponAnimProfile())
	{
		return EffectiveWeaponAnimProfile->CombatOutroMontage.Get();
	}

	return nullptr;
}

float AProject_JPlayerCharacter::GetEffectiveCombatOutroMontagePlayRate() const
{
	if (const UProject_JWeaponAnimProfile* EffectiveWeaponAnimProfile = GetWeaponAnimProfile())
	{
		return EffectiveWeaponAnimProfile->CombatOutroMontagePlayRate;
	}

	return 1.0f;
}

bool AProject_JPlayerCharacter::ShouldPlayCombatIntroMontage() const
{
	if (const UProject_JCombatAnimProfile* EffectiveCombatAnimProfile = GetCombatAnimProfile())
	{
		return EffectiveCombatAnimProfile->bPlayIntroMontageWhenEnteringCombat;
	}

	return true;
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





const UProject_JWeaponAnimProfile* AProject_JPlayerCharacter::GetWeaponAnimProfile() const
{
	if (const UProject_JCombatStyleDefinition* CombatStyle = GetCombatConfiguration().AnimationStyle)
	{
		return CombatStyle->WeaponAnimationProfile;
	}
	return nullptr;
}

const UProject_JCombatAnimProfile* AProject_JPlayerCharacter::GetCombatAnimProfile() const
{
	return CharacterAnimProfile ? CharacterAnimProfile->CombatAnimProfile.Get() : nullptr;
}

const FProject_JCombatConfiguration& AProject_JPlayerCharacter::GetCombatConfiguration() const
{
	check(IsInGameThread());
	const auto* Class = GetCharacterClassDefinition();
	const auto* Advancement = GetAdvancementDefinition();
	if (!bCombatConfigurationBuilt || ConfigurationClass.Get() != Class
		|| ConfigurationAdvancement.Get() != Advancement || ConfigurationEquipment.Get() != CurrentCombatStyle)
	{
		auto* Self = const_cast<AProject_JPlayerCharacter*>(this);
		auto Resolved = FProject_JCombatConfiguration::Resolve(Class ? Class->DefaultCombatStyle.Get() : nullptr, Advancement, CurrentCombatStyle);
		Resolved.Revision = CombatConfiguration.Revision == MAX_int32 ? 1 : CombatConfiguration.Revision + 1;
		Self->CombatConfiguration = Resolved;
		Self->ConfigurationClass = Class;
		Self->ConfigurationAdvancement = Advancement;
		Self->ConfigurationEquipment = CurrentCombatStyle;
		Self->bCombatConfigurationBuilt = true;
	}
	return CombatConfiguration;
}

const UProject_JCombatStyleDefinition* AProject_JPlayerCharacter::GetCombatStyleDefinition() const
{
	return GetCombatConfiguration().GameplayStyle;
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
	return (CombatStateComponent && CombatStateComponent->IsCombatModeActive()) || bReplicatedCombatModePresentation;
}

void AProject_JPlayerCharacter::SetCurrentCombatStyle(UProject_JCombatStyleDefinition* InCombatStyle)
{
	SetCurrentEquipmentConfiguration(InCombatStyle, CurrentWeaponPresentationProfile);
}

void AProject_JPlayerCharacter::SetCurrentWeaponPresentationProfile(UProject_JWeaponPresentationProfile* InPresentationProfile)
{
	SetCurrentEquipmentConfiguration(CurrentCombatStyle, InPresentationProfile);
}

void AProject_JPlayerCharacter::SetCurrentEquipmentConfiguration(
	UProject_JCombatStyleDefinition* InCombatStyle, UProject_JWeaponPresentationProfile* InPresentationProfile)
{
	check(IsInGameThread());
	TStrongObjectPtr<ThisClass> PinnedThis(this);
	TWeakObjectPtr<UWorld> OriginalWorld(GetWorld());
	const bool bStyleChanged = CurrentCombatStyle != InCombatStyle;
	const bool bProfileChanged = CurrentWeaponPresentationProfile != InPresentationProfile;
	if (!bStyleChanged && !bProfileChanged)
	{
		return;
	}

	// All consumers observe the new pair, never a new style with the old weapon.
	// Existing replicated properties and Blueprint setter signatures stay intact.
	CurrentCombatStyle = InCombatStyle;
	CurrentWeaponPresentationProfile = InPresentationProfile;
	const auto CanContinue = [this, OriginalWorld, InCombatStyle, InPresentationProfile]
	{
		return !IsActorBeingDestroyed() && GetWorld() == OriginalWorld.Get() &&
			(!GetWorld() || !GetWorld()->bIsTearingDown) && CurrentCombatStyle == InCombatStyle &&
			CurrentWeaponPresentationProfile == InPresentationProfile;
	};
	if (CombatPresentationComponent)
	{
		CombatPresentationComponent->RefreshPresentation();
	}
	if (!CanContinue()) { return; }
	if (bProfileChanged && WeaponPresentationComponent)
	{
		WeaponPresentationComponent->RefreshPresentation();
	}
	if (!CanContinue()) { return; }
	if (bStyleChanged)
	{
		// Cues were already retired before weapon replacement. Do not cancel/re-prefetch the same pair twice.
		ApplyCurrentCombatStyleReaction();
	}
	if (!CanContinue()) { return; }
	if (HasAuthority())
	{
		ForceNetUpdate();
	}
}

void AProject_JPlayerCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	if (UProject_JFoleyComponent* Foley = GetFoleyComponent())
	{
		Foley->PlayJump(GetVelocity().Size2D());
	}

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

void AProject_JPlayerCharacter::DispatchMoveStopAnimationEvent(bool bWasSprintingAtStop)
{
	if (ReplicatedAnimEventComponent)
	{
		ReplicatedAnimEventComponent->DispatchMoveStopped(bWasSprintingAtStop);
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

void AProject_JPlayerCharacter::ToggleCombatMode()
{
	if (!CanToggleCombatMode())
	{
		return;
	}

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
	if (!CanToggleCombatMode())
	{
		return;
	}

	ApplyCombatRotationMode(true);
	PlayCombatIntroMontage();
}

void AProject_JPlayerCharacter::ServerSetCombatIntroPresentation_Implementation(bool bShouldShowIntro)
{
	LogCombatPresentationTrace(*this, bShouldShowIntro ? TEXT("ServerIntroRpcReceivedTrue") : TEXT("ServerIntroRpcReceivedFalse"));
	// This RPC controls only remote visuals. Do not duplicate CanToggleCombatMode
	// here: its stricter airborne/action checks belong to authoritative gameplay
	// activation and would unnecessarily delay a confirmed draw presentation.
	bReplicatedCombatIntroPresentation = bShouldShowIntro &&
		!bReplicatedCombatModePresentation &&
		!(MountComponent && MountComponent->IsMounted());
	SetCombatTransitionState(bReplicatedCombatIntroPresentation);
	ForceNetUpdate();
	LogCombatPresentationTrace(
		*this,
		bReplicatedCombatIntroPresentation ? TEXT("ServerIntroPresentationSetTrue") : TEXT("ServerIntroPresentationSetFalse"));
}

void AProject_JPlayerCharacter::SetCombatTransitionState(bool bTransitionActive)
{
	if (!HasAuthority())
	{
		return;
	}

	UProject_JAbilitySystemComponent* ProjectJASC = Cast<UProject_JAbilitySystemComponent>(GetAbilitySystemComponent());
	if (!ProjectJASC)
	{
		return;
	}

	const FGameplayTag TransitionTag = FProject_JGameplayTags::Get().State_CombatTransition;
	const bool bAlreadyActive = ProjectJASC->HasMatchingGameplayTag(TransitionTag);
	if (bTransitionActive == bAlreadyActive)
	{
		return;
	}

	if (bTransitionActive)
	{
		ProjectJASC->AddProjectJLooseGameplayTag(TransitionTag, true);
	}
	else
	{
		ProjectJASC->RemoveProjectJLooseGameplayTag(TransitionTag, true);
	}
}

void AProject_JPlayerCharacter::RefreshCombatTransitionViews()
{
	bIsPlayingCombatIntro = CombatIntroComponent && CombatIntroComponent->IsPlayingIntro();
	bPendingCombatModeFromIntro = CombatIntroComponent && CombatIntroComponent->IsPendingCombatMode();
}

bool AProject_JPlayerCharacter::IsCombatIntroPlaying() const
{
	return (CombatIntroComponent && CombatIntroComponent->IsPlayingIntro()) || bReplicatedCombatIntroPresentation;
}

bool AProject_JPlayerCharacter::IsCombatOutroPlaying() const
{
	return CombatIntroComponent && CombatIntroComponent->IsPlayingOutro();
}

bool AProject_JPlayerCharacter::CanToggleCombatMode() const
{
	if ((MountComponent && MountComponent->IsMounted()) ||
		(CombatIntroComponent && CombatIntroComponent->IsPlayingIntro()) ||
		IsCombatOutroPlaying() ||
		IsAttacking() ||
		IsDodging() ||
		IsHitReacting())
	{
		return false;
	}

	if (Project_J::Movement::IsInAir(this))
	{
		return false;
	}

	// JumpStart and landing have a short overlap with grounded movement mode.
	// Their animation state is therefore included in addition to IsFalling().
	return !LocomotionAnimStateComponent ||
		(!LocomotionAnimStateComponent->bIsJumping &&
			!LocomotionAnimStateComponent->bIsFallOffStart &&
			!LocomotionAnimStateComponent->bIsLanding &&
			!LocomotionAnimStateComponent->bIsPhysicallyInAir);
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
		// Send the presentation signal only after the local montage really starts.
		// This avoids remote draw visuals for a missing asset or incompatible slot.
		if (IsLocallyControlled())
		{
			LogCombatPresentationTrace(*this, TEXT("LocalIntroMontageStarted"));
			ServerSetCombatIntroPresentation(true);
		}
		if (CombatAnimationLayerComponent)
		{
			// Keep the weapon layer alive beneath the montage so its blend-out
			// resolves directly into armed idle rather than a one-frame base locomotion pose.
			CombatAnimationLayerComponent->RefreshLayer();
		}
	}
}

void AProject_JPlayerCharacter::PlayCosmeticCombatIntroMontage()
{
	UAnimMontage* EffectiveCombatIntroMontage = GetEffectiveCombatIntroMontage();
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!EffectiveCombatIntroMontage || !AnimInstance || AnimInstance->Montage_IsPlaying(EffectiveCombatIntroMontage))
	{
		return;
	}

	// This has no pending-combat side effect. It is only the visual replay used
	// by simulated proxies after the server confirms a remote player's draw.
	const float Duration = PlayAnimMontage(EffectiveCombatIntroMontage, GetEffectiveCombatIntroMontagePlayRate());
	if (ShouldLogCombatPresentationTrace())
	{
		UE_LOG(LogProjectJPlayer, Log,
			TEXT("[CombatPresentation] Stage=RemoteIntroMontagePlay Time=%.3f Owner=%s Duration=%.3f AlreadyCombat=%s"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
			*GetNameSafe(this),
			Duration,
			bReplicatedCombatModePresentation ? TEXT("true") : TEXT("false"));
	}
}

void AProject_JPlayerCharacter::PlayCombatOutroMontage()
{
	UAnimMontage* EffectiveCombatOutroMontage = GetEffectiveCombatOutroMontage();
	if (!EffectiveCombatOutroMontage || !CombatIntroComponent || IsCombatOutroPlaying())
	{
		return;
	}

	if (!CombatIntroComponent->PlayOutro(*this, EffectiveCombatOutroMontage, GetEffectiveCombatOutroMontagePlayRate()))
	{
		// A broken/missing slot must not strand the weapon in the hand.
		MoveWeaponToSheathedSocket();
		return;
	}
}

void AProject_JPlayerCharacter::CancelCombatIntroMontage()
{
	if (IsLocallyControlled())
	{
		ServerSetCombatIntroPresentation(false);
	}

	if (CombatIntroComponent)
	{
		CombatIntroComponent->CancelIntro(*this, GetEffectiveCombatIntroMontage());
	}

	if (CombatAnimationLayerComponent)
	{
		CombatAnimationLayerComponent->RefreshLayer();
	}
}

void AProject_JPlayerCharacter::CancelCombatOutroMontage()
{
	if (CombatIntroComponent) { CombatIntroComponent->CancelOutro(*this); }
}

void AProject_JPlayerCharacter::OnCombatIntroMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	const bool bPendingCombatMode = CombatIntroComponent && CombatIntroComponent->IsPendingCombatMode();
	if (CombatIntroComponent) { CombatIntroComponent->ClearPendingCombatMode(); }
	if (bPendingCombatMode && !bInterrupted && !IsHitReacting())
	{
		// The transition component commits Idle before publishing completion.
		TryActivateAbilityByTag(CombatToggleAbilityTag);
	}
	else if (!IsCombatModeActive())
	{
		if (IsLocallyControlled())
		{
			ServerSetCombatIntroPresentation(false);
		}
		ApplyCombatRotationMode(false);
	}

	if (CombatAnimationLayerComponent)
	{
		CombatAnimationLayerComponent->RefreshLayer();
	}
}

void AProject_JPlayerCharacter::OnCombatOutroMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (!IsCombatModeActive())
	{
		// Fallback for an omitted/misplaced notify; a valid notify simply performs
		// the same operation earlier at the authored hand-to-back frame.
		MoveWeaponToSheathedSocket();
	}
}

void AProject_JPlayerCharacter::MoveWeaponToSheathedSocket()
{
	if (!IsCombatModeActive() && WeaponPresentationComponent)
	{
		WeaponPresentationComponent->AttachWeaponToSheathedSocket();
	}
}

void AProject_JPlayerCharacter::InterruptCombatIntroForHit()
{
	if (!ShouldInterruptCombatIntroOnHit() || !CombatIntroComponent || !CombatIntroComponent->IsPlayingIntro())
	{
		return;
	}

	CombatIntroComponent->CancelIntro(*this, GetEffectiveCombatIntroMontage());
	if (IsLocallyControlled())
	{
		ServerSetCombatIntroPresentation(false);
	}
	if (CombatAnimationLayerComponent)
	{
		CombatAnimationLayerComponent->RefreshLayer();
	}
	if (!IsCombatModeActive())
	{
		ApplyCombatRotationMode(false);
	}
}

void AProject_JPlayerCharacter::Landed(const FHitResult& Hit)
{
	const FVector PreLandingVelocity = GetLastLandingVelocity();
	const FVector PreLandingAcceleration = Project_J::Movement::ReadPhysicalFrame(this).Acceleration;
	const float ImpactFallSpeed = FMath::Abs(PreLandingVelocity.Z);
	const float HorizontalSpeedSquared = FVector(PreLandingVelocity.X, PreLandingVelocity.Y, 0.0f).SizeSquared();
	const float LandingMovingSpeedThreshold = LocomotionAnimStateComponent
		? LocomotionAnimStateComponent->IdleSpeedThreshold
		: 30.0f;
	const bool bWasMovingAtLanding =
		HorizontalSpeedSquared > FMath::Square(LandingMovingSpeedThreshold) ||
		PreLandingAcceleration.SizeSquared2D() > UE_KINDA_SMALL_NUMBER;
	const bool bWasSprintingAtLanding =
		IsSprintLocomotionAllowed() ||
		(LocomotionAnimStateComponent &&
			(LocomotionAnimStateComponent->bUseSprintLocomotion || LocomotionAnimStateComponent->bWantsSprint));
	const bool bWasHeavyLanding = LocomotionAnimStateComponent
		? ImpactFallSpeed >= LocomotionAnimStateComponent->HeavyLandSpeedThreshold
		: false;

	Super::Landed(Hit);

	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->HandleLanded(Hit);

		if (LocomotionAnimStateComponent->ConsumeRealLandingEventRequested())
		{
			K2_OnRealLanded();
		}
	}

	if (HasAuthority() && ReplicatedAnimEventComponent)
	{
		ReplicatedAnimEventComponent->DispatchLandingStarted(
			ImpactFallSpeed,
			bWasMovingAtLanding,
			bWasSprintingAtLanding,
			bWasHeavyLanding);
	}
}

void AProject_JPlayerCharacter::FinishLanding(bool bForceFinish)
{
	if (LocomotionAnimStateComponent)
	{
		LocomotionAnimStateComponent->FinishLanding(bForceFinish);
	}
}

void AProject_JPlayerCharacter::HandleSkillInputTagPressed(FGameplayTag InputTag)
{
	if (SkillInputExecutionComponent)
	{
		SkillInputExecutionComponent->HandleInputTagPressed(InputTag);
	}
}

void AProject_JPlayerCharacter::ReceiveAuthorityCombatInput(const FGameplayTag& InputTag, double ClientTimestamp, uint32 InputSequence)
{
	if (SkillInputExecutionComponent)
	{ SkillInputExecutionComponent->ReceiveAuthorityCombatInput(InputTag, ClientTimestamp, InputSequence); }
}

void AProject_JPlayerCharacter::HandleSkillInputTagReleased(FGameplayTag InputTag)
{
	if (SkillInputExecutionComponent)
	{
		SkillInputExecutionComponent->HandleInputTagReleased(InputTag);
	}
}

void AProject_JPlayerCharacter::HandleSkillInputTagPressedFromSource(FGameplayTag InputTag, const UObject* Source, uint64 Receipt)
{
	if (SkillInputExecutionComponent) SkillInputExecutionComponent->HandleInputTagPressedFromSource(InputTag, Source, Receipt);
}

void AProject_JPlayerCharacter::HandleSkillInputTagReleasedFromSource(FGameplayTag InputTag, const UObject* Source, uint64 Receipt)
{
	if (SkillInputExecutionComponent) SkillInputExecutionComponent->HandleInputTagReleasedFromSource(InputTag, Source, Receipt);
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
	TryApplyHandoverSnapshot(InData);
}

bool AProject_JPlayerCharacter::TryApplyHandoverSnapshot(const TArray<uint8>& InData)
{
	if (!HasAuthority() || IsActorBeingDestroyed() || !GetRootComponent())
	{
		return false;
	}
	const TStrongObjectPtr<AProject_JPlayerCharacter> PinnedThis(this);
	const TStrongObjectPtr<AActor> StateOwner(GetAbilitySystemOwnerActor());
	const TStrongObjectPtr<UProject_JProgressionComponent> Progression(GetProgressionComponent());

	FProject_JPlayerHandoverSnapshot Snapshot;
	// Version 1 uses fixed-width archive operators. Check the complete size before
	// reading: FMemoryReader can assert on a truncated primitive read.
	TArray<uint8> Layout;
	FMemoryWriter LayoutWriter(Layout, true);
	LayoutWriter << Snapshot.Version << Snapshot.Level << Snapshot.Location << Snapshot.Rotation;
	if (InData.Num() != Layout.Num()) { return false; }
	TArray<uint8> LocalData = InData;
	FMemoryReader Reader(LocalData, true);

	Reader << Snapshot.Version;
	Reader << Snapshot.Level;
	Reader << Snapshot.Location;
	Reader << Snapshot.Rotation;

	if (Reader.IsError() || !Reader.AtEnd() || Snapshot.Version != 1 || Snapshot.Level < 1 ||
		Snapshot.Location.ContainsNaN() || Snapshot.Rotation.ContainsNaN())
	{
		return false;
	}
	// A portable snapshot requests an exact level. Preserve the scalar setter's
	// class-floor normalization, but never acknowledge a different level or pose-only apply.
	if (!CanApplyCharacterLevelExactly(Snapshot.Level)) { return false; }

	if (!CastChecked<UProject_JMoverComponent>(GetMover())->ApplyAuthorityHandoverTransform(FTransform(Snapshot.Rotation,Snapshot.Location)))
	{
		return false;
	}
	// The transform has changed even if a subsequent progression callback rejects
	// the remaining handover. No old capsule or weapon geometry may survive it.
	ResetCombatSpatialHistory();

	if (IsActorBeingDestroyed() || GetAbilitySystemOwnerActor() != StateOwner.Get()
		|| GetProgressionComponent() != Progression.Get() || !TrySetCharacterLevel(Snapshot.Level)
		|| GetCharacterLevel_Implementation() != Snapshot.Level) { return false; }

	ForceNetUpdate();
	return !IsActorBeingDestroyed() && IsValid(StateOwner.Get()) && !StateOwner->IsActorBeingDestroyed()
		&& GetAbilitySystemOwnerActor() == StateOwner.Get() && GetProgressionComponent() == Progression.Get()
		&& (!Progression.Get() || !Progression->IsBeingDestroyed()) && GetCharacterLevel_Implementation() == Snapshot.Level;
}

void AProject_JPlayerCharacter::SetCharacterLevel(int32 NewLevel)
{
	Super::SetCharacterLevel(NewLevel);
	// Base's compatibility adapter dispatches the checked override once.
}

bool AProject_JPlayerCharacter::TrySetCharacterLevel(int32 NewLevel)
{
	const TStrongObjectPtr<AProject_JPlayerCharacter> PinnedThis(this);
	const TStrongObjectPtr<AActor> StateOwner(GetAbilitySystemOwnerActor());
	const TStrongObjectPtr<UProject_JProgressionComponent> Progression(GetProgressionComponent());
	if (!Super::TrySetCharacterLevel(NewLevel)) { return false; }
	RefreshAbilitySystemDependentComponents();
	return !IsActorBeingDestroyed() && IsValid(StateOwner.Get()) && !StateOwner->IsActorBeingDestroyed()
		&& GetAbilitySystemOwnerActor() == StateOwner.Get() && GetProgressionComponent() == Progression.Get()
		&& (!Progression.Get() || !Progression->IsBeingDestroyed());
}

void AProject_JPlayerCharacter::LogAnimationProfileConfiguration() const
{
 TArray<FString> Warnings;
 Project_J::AnimationProfileValidation::ValidatePlayerAnimationConfiguration(*this,GetLocomotionProfile(),GetWeaponAnimProfile(),GetCombatAnimProfile(),LocomotionAnimStateComponent,Warnings);
 for(const auto& Warning:Warnings) UE_LOG(LogProjectJPlayer,Warning,TEXT("%s"),*Warning);
}
