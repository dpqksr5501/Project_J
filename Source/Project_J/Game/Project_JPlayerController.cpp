// Copyright Epic Games, Inc. All Rights Reserved.


#include "Project_JPlayerController.h"
#include "Game/Project_JInputLeaseSubsystem.h"
#include "Testing/Project_JEquipmentClientTestComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Project_J.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Project_JGameState.h"
#include "Project_JPlayerState.h"
#include "Network/Project_JNetObjectFilter_Distance.h"
#include "Network/Project_JNetObjectPrioritizer_Combat.h"
#include "Project_JNPCCharacter.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Game/Project_JProfilingCrowdComponent.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "UI/Project_JPlayerUIComponent.h"
#include "InputKeyEventArgs.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
const TCHAR* ToDebugString(ENetRole Role)
{
	switch (Role)
	{
	case ROLE_Authority:
		return TEXT("Authority");
	case ROLE_AutonomousProxy:
		return TEXT("Autonomous");
	case ROLE_SimulatedProxy:
		return TEXT("Simulated");
	case ROLE_None:
	default:
		return TEXT("None");
	}
}

const TCHAR* ToDebugString(EProject_JAnimBudgetTier Tier)
{
	switch (Tier)
	{
	case EProject_JAnimBudgetTier::Local:
		return TEXT("Local");
	case EProject_JAnimBudgetTier::Near:
		return TEXT("Near");
	case EProject_JAnimBudgetTier::Mid:
		return TEXT("Mid");
	case EProject_JAnimBudgetTier::Far:
		return TEXT("Far");
	case EProject_JAnimBudgetTier::Hidden:
		return TEXT("Hidden");
	default:
		return TEXT("Unknown");
	}
}

int32 GetBudgetTierIndex(EProject_JAnimBudgetTier Tier)
{
	switch (Tier)
	{
	case EProject_JAnimBudgetTier::Local:
		return 0;
	case EProject_JAnimBudgetTier::Near:
		return 1;
	case EProject_JAnimBudgetTier::Mid:
		return 2;
	case EProject_JAnimBudgetTier::Far:
		return 3;
	case EProject_JAnimBudgetTier::Hidden:
		return 4;
	default:
		return 4;
	}
}
}

AProject_JPlayerController::AProject_JPlayerController()
{
	PlayerUI = CreateDefaultSubobject<UProject_JPlayerUIComponent>(TEXT("PlayerUI"));
#if WITH_EDITOR
	EquipmentClientTestComponent = CreateEditorOnlyDefaultSubobject<UProject_JEquipmentClientTestComponent>(TEXT("EquipmentClientTest"), true);
#endif
#if WITH_EDITOR
	ProfilingCrowdComponent = CreateEditorOnlyDefaultSubobject<UProject_JProfilingCrowdComponent>(TEXT("ProfilingCrowdComponent"), true);
#endif
}

void AProject_JPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// only spawn touch controls on local player controllers
	if (ShouldUseTouchControls() && IsLocalPlayerController())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogProject_J, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void AProject_JPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if WITH_EDITOR
	StopSprintInputTest();
#endif
	if (InputLeaseSubsystem.IsValid()) { InputLeaseSubsystem->Release(this); }
	InputLeaseSubsystem.Reset();
	if (MobileControlsWidget) { MobileControlsWidget->RemoveFromParent(); MobileControlsWidget = nullptr; }
	Super::EndPlay(EndPlayReason);
}

void AProject_JPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (IsLocalPlayerController() && PlayerUI) PlayerUI->BindMenuInput(InputComponent);

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UProject_JInputLeaseSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UProject_JInputLeaseSubsystem>(GetLocalPlayer()))
		{
			if (InputLeaseSubsystem.IsValid() && InputLeaseSubsystem.Get() != Subsystem) { InputLeaseSubsystem->Release(this); }
			InputLeaseSubsystem = Subsystem;
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->Acquire(this, CurrentContext);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->Acquire(this, CurrentContext);
				}
			}
		}
	}
}

void AProject_JPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState(); if (PlayerUI) PlayerUI->RefreshSources();
}
void AProject_JPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer(); if (PlayerUI) PlayerUI->RefreshSources();
}

bool AProject_JPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

#if WITH_EDITOR
void AProject_JPlayerController::StartProfilingVisualCrowd(int32 Count)
{
#if UE_BUILD_SHIPPING
	return;
#else
	const AProject_JPlayerCharacter* SourceCharacter = Cast<AProject_JPlayerCharacter>(GetPawn());
	if (!ProfilingCrowdComponent || !SourceCharacter)
	{
		ClientMessage(TEXT("Profiling visual crowd unavailable: possess an AProject_JPlayerCharacter first."));
		return;
	}

	const bool bStarted = ProfilingCrowdComponent->Start(
		SourceCharacter->GetClass(),
		SourceCharacter->GetActorLocation(),
		SourceCharacter->GetActorForwardVector(),
		Count);
	const FString Message = bStarted
		? FString::Printf(TEXT("Profiling visual crowd started: %d local non-replicated clones."), ProfilingCrowdComponent->GetSpawnedCount())
		: TEXT("Profiling visual crowd did not start. Check the Output Log for the spawn failure.");
	ClientMessage(Message);
	UE_LOG(LogProject_J, Display, TEXT("%s"), *Message);
#endif
}

void AProject_JPlayerController::StopProfilingVisualCrowd()
{
#if UE_BUILD_SHIPPING
	return;
#else
	if (ProfilingCrowdComponent)
	{
		ProfilingCrowdComponent->Stop();
	}
	ClientMessage(TEXT("Profiling visual crowd stopped."));
	UE_LOG(LogProject_J, Display, TEXT("Profiling visual crowd stopped."));
#endif
}

void AProject_JPlayerController::DumpProfilingVisualCrowd()
{
#if UE_BUILD_SHIPPING
	return;
#else
	const int32 Count = ProfilingCrowdComponent ? ProfilingCrowdComponent->GetSpawnedCount() : 0;
	const int32 MovingCount = ProfilingCrowdComponent ? ProfilingCrowdComponent->GetMovingCharacterCount() : 0;
	const FString Message = FString::Printf(
		TEXT("ProfilingVisualCrowd Spawned=%d Moving=%d Replicated=false Purpose=VisualAnimationCpuOnly"),
		Count,
		MovingCount);
	ClientMessage(Message);
	UE_LOG(LogProject_J, Display, TEXT("%s"), *Message);
#endif
}

void AProject_JPlayerController::StartProfilingReplicatedMovementCrowd(int32 Count)
{
#if UE_BUILD_SHIPPING
	return;
#else
	if (HasAuthority())
	{
		ServerStartProfilingReplicatedMovementCrowd_Implementation(Count);
	}
	else
	{
		ServerStartProfilingReplicatedMovementCrowd(Count);
		ClientMessage(TEXT("Requested server-side replicated movement crowd. Check the server Output Log for its start confirmation."));
	}
#endif
}

void AProject_JPlayerController::StopProfilingReplicatedMovementCrowd()
{
#if UE_BUILD_SHIPPING
	return;
#else
	if (HasAuthority())
	{
		ServerStopProfilingReplicatedMovementCrowd_Implementation();
	}
	else
	{
		ServerStopProfilingReplicatedMovementCrowd();
	}
#endif
}

void AProject_JPlayerController::DumpProfilingReplicatedMovementCrowd()
{
#if UE_BUILD_SHIPPING
	return;
#else
	if (HasAuthority())
	{
		ServerDumpProfilingReplicatedMovementCrowd_Implementation();
	}
	else
	{
		ServerDumpProfilingReplicatedMovementCrowd();
	}
#endif
}

void AProject_JPlayerController::ServerStartProfilingReplicatedMovementCrowd_Implementation(int32 Count)
{
#if !UE_BUILD_SHIPPING
	const AProject_JPlayerCharacter* SourceCharacter = Cast<AProject_JPlayerCharacter>(GetPawn());
	if (!ProfilingCrowdComponent || !SourceCharacter)
	{
		UE_LOG(LogProject_J, Warning, TEXT("Profiling replicated movement crowd unavailable: no possessed AProject_JPlayerCharacter on the server."));
		return;
	}

	const bool bStarted = ProfilingCrowdComponent->StartReplicatedMovement(
		SourceCharacter->GetClass(),
		SourceCharacter->GetActorLocation(),
		SourceCharacter->GetActorForwardVector(),
		Count);
	if (bStarted)
	{
		UE_LOG(
			LogProject_J,
			Display,
			TEXT("ProfilingReplicatedMovementCrowd started Requested=%d Spawned=%d Moving=%d NetUpdateHz=30 Purpose=ServerToClientMovementOnly"),
			Count,
			ProfilingCrowdComponent->GetSpawnedCount(),
			ProfilingCrowdComponent->GetMovingCharacterCount());
	}
	else
	{
		UE_LOG(LogProject_J, Warning, TEXT("ProfilingReplicatedMovementCrowd failed to start Requested=%d."), Count);
	}
#endif
}

void AProject_JPlayerController::ServerStopProfilingReplicatedMovementCrowd_Implementation()
{
#if !UE_BUILD_SHIPPING
	if (ProfilingCrowdComponent)
	{
		ProfilingCrowdComponent->Stop();
	}
	UE_LOG(LogProject_J, Display, TEXT("ProfilingReplicatedMovementCrowd stopped."));
#endif
}

void AProject_JPlayerController::ServerDumpProfilingReplicatedMovementCrowd_Implementation()
{
#if !UE_BUILD_SHIPPING
	const int32 Count = ProfilingCrowdComponent ? ProfilingCrowdComponent->GetSpawnedCount() : 0;
	const int32 MovingCount = ProfilingCrowdComponent ? ProfilingCrowdComponent->GetMovingCharacterCount() : 0;
	const bool bReplicated = ProfilingCrowdComponent && ProfilingCrowdComponent->IsReplicatedMovementProfile();
	UE_LOG(
		LogProject_J,
		Display,
		TEXT("ProfilingReplicatedMovementCrowd Spawned=%d Moving=%d Replicated=%s NetUpdateHz=30 Purpose=ServerToClientMovementOnly"),
		Count,
		MovingCount,
		bReplicated ? TEXT("true") : TEXT("false"));
#endif
}

void AProject_JPlayerController::DumpAnimationExecutionPolicy()
{
#if UE_BUILD_SHIPPING
	return;
#else
	auto ReadCVar = [](const TCHAR* Name)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable ? Variable->GetInt() : INDEX_NONE;
	};

	const ACharacter* ProfiledCharacter = Cast<ACharacter>(GetPawn());
	const USkeletalMeshComponent* Mesh = ProfiledCharacter ? ProfiledCharacter->GetMesh() : nullptr;
	const UAnimInstance* AnimInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	const UEngine* EngineDefaults = GetDefault<UEngine>();
	const FString Message = FString::Printf(
		TEXT("AnimationExecutionPolicy ParallelEval=%d ParallelUpdate=%d ForceParallelUpdate=%d ParallelInterpolation=%d EngineAllowMT=%d AnimAllowMT=%d CanRunParallel=%d RootMotionMode=%d Mesh=%s AnimInstance=%s"),
		ReadCVar(TEXT("a.ParallelAnimEvaluation")),
		ReadCVar(TEXT("a.ParallelAnimUpdate")),
		ReadCVar(TEXT("a.ForceParallelAnimUpdate")),
		ReadCVar(TEXT("a.ParallelAnimInterpolation")),
		EngineDefaults && EngineDefaults->bAllowMultiThreadedAnimationUpdate ? 1 : 0,
		AnimInstance && AnimInstance->bUseMultiThreadedAnimationUpdate ? 1 : 0,
		AnimInstance && AnimInstance->CanRunParallelWork() ? 1 : 0,
		AnimInstance ? static_cast<int32>(AnimInstance->RootMotionMode.GetValue()) : INDEX_NONE,
		*GetNameSafe(Mesh),
		*GetNameSafe(AnimInstance));
	ClientMessage(Message);
	UE_LOG(LogProject_J, Display, TEXT("%s"), *Message);
#endif
}

void AProject_JPlayerController::DumpMMOState()
{
#if UE_BUILD_SHIPPING
	return;
#else
	const AProject_JPlayerState* ProjectPlayerState = GetPlayerState<AProject_JPlayerState>();
	const AProject_JGameState* ProjectGameState = GetWorld() ? GetWorld()->GetGameState<AProject_JGameState>() : nullptr;

	const FString PlayerLine = ProjectPlayerState
		? FString::Printf(
			TEXT("PlayerState Account=%s Character=%s Class=%s Level=%d"),
			*ProjectPlayerState->GetAccountId().ToString(),
			*ProjectPlayerState->GetCharacterId().ToString(),
			*ProjectPlayerState->GetPublicClassId().ToString(),
			ProjectPlayerState->GetPublicCharacterLevel())
		: FString(TEXT("PlayerState unavailable"));

	const FString WorldLine = ProjectGameState
		? FString::Printf(
			TEXT("GameState %s PublicEvent=%s"),
			*ProjectGameState->GetWorldInstanceId().ToDebugString(),
			*ProjectGameState->GetPublicEventId().ToString())
		: FString(TEXT("GameState unavailable"));

	ClientMessage(PlayerLine);
	ClientMessage(WorldLine);
	UE_LOG(LogProject_J, Display, TEXT("%s"), *PlayerLine);
	UE_LOG(LogProject_J, Display, TEXT("%s"), *WorldLine);
#endif
}

void AProject_JPlayerController::DumpAnimBudget()
{
#if UE_BUILD_SHIPPING
	return;
#else
	const APawn* ControlledPawn = GetPawn();
	const ACharacter* ControlledCharacter = Cast<ACharacter>(ControlledPawn);
	const USkeletalMeshComponent* Mesh = ControlledCharacter ? ControlledCharacter->GetMesh() : nullptr;
	const UProject_JCharacterAnimInstance* AnimInstance = Mesh ? Cast<UProject_JCharacterAnimInstance>(Mesh->GetAnimInstance()) : nullptr;

	const FString AnimLine = AnimInstance
		? AnimInstance->GetAnimationDebugSummary()
		: FString(TEXT("Animation budget unavailable: possessed pawn does not use UProject_JCharacterAnimInstance."));

	ClientMessage(AnimLine);
	UE_LOG(LogProject_J, Display, TEXT("%s"), *AnimLine);
#endif
}

void AProject_JPlayerController::DumpMotionMatchingTrace()
{
#if UE_BUILD_SHIPPING
	return;
#else
	const ACharacter* ControlledCharacter = Cast<ACharacter>(GetPawn());
	const USkeletalMeshComponent* Mesh = ControlledCharacter ? ControlledCharacter->GetMesh() : nullptr;
	const UProject_JCharacterAnimInstance* AnimInstance = Mesh ? Cast<UProject_JCharacterAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	const FString Trace = AnimInstance
		? AnimInstance->GetMotionMatchingTraceSummary()
		: FString(TEXT("Motion Matching trace unavailable: possessed pawn does not use UProject_JCharacterAnimInstance."));
	ClientMessage(TEXT("Motion Matching trace written to Output Log."));
	UE_LOG(LogProject_J, Display, TEXT("%s"), *Trace);
#endif
}

void AProject_JPlayerController::DumpLocomotionKinematics()
{
#if UE_BUILD_SHIPPING
	return;
#else
	const AProject_JPlayerCharacter* PlayerCharacter = Cast<AProject_JPlayerCharacter>(GetPawn());
	const UProject_JLocomotionAnimStateComponent* LocomotionState = PlayerCharacter
		? PlayerCharacter->GetLocomotionAnimStateComponent()
		: nullptr;
	const FString Summary = LocomotionState
		? LocomotionState->GetDebugSummary()
		: FString(TEXT("Locomotion kinematics unavailable: possessed pawn does not use AProject_JPlayerCharacter."));
	ClientMessage(TEXT("Locomotion kinematics written to Output Log."));
	UE_LOG(LogProject_J, Display, TEXT("%s"), *Summary);
#endif
}

void AProject_JPlayerController::DumpMotionMatchingPivotTrace()
{
#if UE_BUILD_SHIPPING
	return;
#else
	const ACharacter* ControlledCharacter = Cast<ACharacter>(GetPawn());
	const USkeletalMeshComponent* Mesh = ControlledCharacter ? ControlledCharacter->GetMesh() : nullptr;
	const UProject_JCharacterAnimInstance* AnimInstance = Mesh ? Cast<UProject_JCharacterAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	const FString Trace = AnimInstance
		? AnimInstance->GetMotionMatchingPivotTraceSummary()
		: FString(TEXT("Motion Matching Pivot trace unavailable: possessed pawn does not use UProject_JCharacterAnimInstance."));
	ClientMessage(TEXT("Motion Matching Pivot trace written to Output Log."));
	UE_LOG(LogProject_J, Display, TEXT("%s"), *Trace);
#endif
}

void AProject_JPlayerController::DumpMotionMatchingTransitionTrace()
{
#if UE_BUILD_SHIPPING
	return;
#else
	const ACharacter* ControlledCharacter = Cast<ACharacter>(GetPawn());
	const USkeletalMeshComponent* Mesh = ControlledCharacter ? ControlledCharacter->GetMesh() : nullptr;
	const UProject_JCharacterAnimInstance* AnimInstance = Mesh ? Cast<UProject_JCharacterAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	const FString Trace = AnimInstance
		? AnimInstance->GetMotionMatchingPivotTraceSummary()
		: FString(TEXT("Motion Matching transition trace unavailable: possessed pawn does not use UProject_JCharacterAnimInstance."));
	ClientMessage(TEXT("Motion Matching transition trace written to Output Log."));
	UE_LOG(LogProject_J, Display, TEXT("%s"), *Trace);
#endif
}

void AProject_JPlayerController::DumpReplicationPolicy()
{
#if UE_BUILD_SHIPPING
	return;
#else
	const AActor* TargetActor = GetPawn();
	if (!TargetActor)
	{
		ClientMessage(TEXT("Replication policy unavailable: no possessed pawn."));
		return;
	}

	const FVector ViewerLocation = PlayerCameraManager ? PlayerCameraManager->GetCameraLocation() : TargetActor->GetActorLocation();
	const UProject_JNetObjectFilter_Distance* DistanceFilter = NewObject<UProject_JNetObjectFilter_Distance>(GetTransientPackage());
	const UProject_JNetObjectPrioritizer_Combat* CombatPrioritizer = NewObject<UProject_JNetObjectPrioritizer_Combat>(GetTransientPackage());

	FProject_JReplicationPolicyDecision Decision = DistanceFilter->BuildReplicationDecision(TargetActor, ViewerLocation, GetPawn());
	Decision = CombatPrioritizer->ApplyCombatPriority(TargetActor, Decision);

	const FString PolicyLine = FString::Printf(TEXT("ReplicationPolicy Actor=%s %s"), *GetNameSafe(TargetActor), *Decision.ToDebugString());
	ClientMessage(PolicyLine);
	UE_LOG(LogProject_J, Display, TEXT("%s"), *PolicyLine);
#endif
}

void AProject_JPlayerController::DumpCharacterComponents()
{
#if UE_BUILD_SHIPPING
	return;
#else
	const AProject_JPlayerCharacter* ProjectCharacter = Cast<AProject_JPlayerCharacter>(GetPawn());
	if (!ProjectCharacter)
	{
		ClientMessage(TEXT("Character component dump unavailable: possessed pawn is not AProject_JPlayerCharacter."));
		return;
	}

	const FString ComponentLine = FString::Printf(
		TEXT("CharacterComponents WeaponPresentation=%s HitValidation=%s Locomotion=%s Trajectory=%s ViewModel=%s"),
		ProjectCharacter->GetWeaponPresentationComponent() ? TEXT("yes") : TEXT("no"),
		ProjectCharacter->GetCombatHitValidationComponent() ? TEXT("yes") : TEXT("no"),
		ProjectCharacter->GetLocomotionAnimStateComponent() ? TEXT("yes") : TEXT("no"),
		ProjectCharacter->GetMotionMatchingTrajectoryComponent() ? TEXT("yes") : TEXT("no"),
		ProjectCharacter->GetCharacterViewModel() ? TEXT("yes") : TEXT("no"));

	ClientMessage(ComponentLine);
	UE_LOG(LogProject_J, Display, TEXT("%s"), *ComponentLine);
#endif
}

void AProject_JPlayerController::DumpCombatState()
{
#if UE_BUILD_SHIPPING
	return;
#else
	const AProject_JPlayerCharacter* ProjectCharacter = Cast<AProject_JPlayerCharacter>(GetPawn());
	if (!ProjectCharacter)
	{
		ClientMessage(TEXT("Combat state dump unavailable: possessed pawn is not AProject_JPlayerCharacter."));
		return;
	}

	const FString CombatLine = FString::Printf(
		TEXT("CombatState Combat=%s Attack=%s Dodge=%s HitReact=%s SprintAllowed=%s JumpAllowed=%s GroundStart=%s GroundStop=%s Overlay=%s AimAlpha=%.2f"),
		ProjectCharacter->IsCombatModeActive() ? TEXT("true") : TEXT("false"),
		ProjectCharacter->IsAttacking() ? TEXT("true") : TEXT("false"),
		ProjectCharacter->IsDodging() ? TEXT("true") : TEXT("false"),
		ProjectCharacter->IsHitReacting() ? TEXT("true") : TEXT("false"),
		ProjectCharacter->IsSprintLocomotionAllowed() ? TEXT("true") : TEXT("false"),
		ProjectCharacter->IsJumpLocomotionAllowed() ? TEXT("true") : TEXT("false"),
		ProjectCharacter->IsGroundStartAllowed() ? TEXT("true") : TEXT("false"),
		ProjectCharacter->IsGroundStopAllowed() ? TEXT("true") : TEXT("false"),
		ProjectCharacter->IsCombatLocomotionOverlayAllowed() ? TEXT("true") : TEXT("false"),
		ProjectCharacter->GetEffectiveCombatAimAlpha());

	ClientMessage(CombatLine);
	UE_LOG(LogProject_J, Display, TEXT("%s"), *CombatLine);
#endif
}

void AProject_JPlayerController::DumpMMOProfilingSnapshot(int32 MaxDetailedCharacters)
{
#if UE_BUILD_SHIPPING
	return;
#else
	UWorld* World = GetWorld();
	if (!World)
	{
		ClientMessage(TEXT("MMOProfilingSnapshot unavailable: no world."));
		return;
	}

	MaxDetailedCharacters = FMath::Max(0, MaxDetailedCharacters);

	const FVector ViewerLocation = PlayerCameraManager
		? PlayerCameraManager->GetCameraLocation()
		: (GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector);

	const UProject_JNetObjectFilter_Distance* DistanceFilter = NewObject<UProject_JNetObjectFilter_Distance>(GetTransientPackage());
	const UProject_JNetObjectPrioritizer_Combat* CombatPrioritizer = NewObject<UProject_JNetObjectPrioritizer_Combat>(GetTransientPackage());

	int32 PlayerCharacterCount = 0;
	int32 NPCCharacterCount = 0;
	int32 AuthorityCount = 0;
	int32 AutonomousCount = 0;
	int32 SimulatedCount = 0;
	int32 AnimInstanceCount = 0;
	int32 FullChooserCount = 0;
	int32 FarChooserOnlyCount = 0;
	int32 AnimationDataUpdateCount = 0;
	int32 TierCounts[5] = { 0, 0, 0, 0, 0 };
	float TotalMotionMatchingInterval = 0.0f;
	int32 MotionMatchingIntervalCount = 0;
	int32 DetailedLinesPrinted = 0;

	UE_LOG(LogProject_J, Display, TEXT("==== MMO Profiling Snapshot ===="));

	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ACharacter* IterCharacter = *It;
		if (!IterCharacter)
		{
			continue;
		}

		const bool bIsPlayerCharacter = IterCharacter->IsA<AProject_JPlayerCharacter>();
		const bool bIsNPCCharacter = IterCharacter->IsA<AProject_JNPCCharacter>();
		if (!bIsPlayerCharacter && !bIsNPCCharacter)
		{
			continue;
		}

		PlayerCharacterCount += bIsPlayerCharacter ? 1 : 0;
		NPCCharacterCount += bIsNPCCharacter ? 1 : 0;

		switch (IterCharacter->GetLocalRole())
		{
		case ROLE_Authority:
			++AuthorityCount;
			break;
		case ROLE_AutonomousProxy:
			++AutonomousCount;
			break;
		case ROLE_SimulatedProxy:
			++SimulatedCount;
			break;
		default:
			break;
		}

		UProject_JCharacterAnimInstance* AnimInstance = nullptr;
		if (USkeletalMeshComponent* Mesh = IterCharacter->GetMesh())
		{
			AnimInstance = Cast<UProject_JCharacterAnimInstance>(Mesh->GetAnimInstance());
		}

		FProject_JReplicationPolicyDecision Decision = DistanceFilter->BuildReplicationDecision(IterCharacter, ViewerLocation, GetPawn());
		Decision = CombatPrioritizer->ApplyCombatPriority(IterCharacter, Decision);

		FString AnimSummary = TEXT("Anim=None");
		if (AnimInstance)
		{
			const FProject_JAnimMotionMatchingThreadSafeData MotionMatchingData =
				AnimInstance->GetMotionMatchingDebugSnapshot();
			++AnimInstanceCount;
			const FProject_JAnimOptimizationPolicy& Policy = AnimInstance->CurrentOptimizationPolicy;
			++TierCounts[GetBudgetTierIndex(Policy.Tier)];
			FullChooserCount += Policy.bUseFullChooserRows ? 1 : 0;
			FarChooserOnlyCount += Policy.bUseFarChooserRowsOnly ? 1 : 0;
			AnimationDataUpdateCount += Policy.bUpdateAnimationData ? 1 : 0;
			TotalMotionMatchingInterval += Policy.MotionMatchingUpdateInterval;
			++MotionMatchingIntervalCount;

			AnimSummary = FString::Printf(
				TEXT("AnimTier=%s UpdateData=%s FullChooser=%s FarOnly=%s MMInterval=%.3f ActivePSD=%s MMRev=%d ForceReselect=%s TrajectorySamples=%d"),
				ToDebugString(Policy.Tier),
				Policy.bUpdateAnimationData ? TEXT("true") : TEXT("false"),
				Policy.bUseFullChooserRows ? TEXT("true") : TEXT("false"),
				Policy.bUseFarChooserRowsOnly ? TEXT("true") : TEXT("false"),
				Policy.MotionMatchingUpdateInterval,
				*GetNameSafe(AnimInstance->CurrentActivePoseSearchDatabase.Get()),
				MotionMatchingData.SelectionRevision,
				MotionMatchingData.bForceReselect ? TEXT("true") : TEXT("false"),
				MotionMatchingData.TrajectorySampleCount);
		}

		if (DetailedLinesPrinted < MaxDetailedCharacters)
		{
			const FString DetailLine = FString::Printf(
				TEXT("MMOProfile Actor=%s Type=%s Role=%s Distance=%.0f %s Rep=%s"),
				*GetNameSafe(IterCharacter),
				bIsPlayerCharacter ? TEXT("Player") : TEXT("NPC"),
				ToDebugString(IterCharacter->GetLocalRole()),
				FMath::Sqrt(Decision.DistanceSquared),
				*AnimSummary,
				*Decision.ToDebugString());
			UE_LOG(LogProject_J, Display, TEXT("%s"), *DetailLine);
			++DetailedLinesPrinted;
		}
	}

	const float AverageMotionMatchingInterval = MotionMatchingIntervalCount > 0
		? TotalMotionMatchingInterval / static_cast<float>(MotionMatchingIntervalCount)
		: 0.0f;

	const FString SummaryLine = FString::Printf(
		TEXT("MMOProfileSummary Players=%d NPCs=%d Authority=%d Autonomous=%d Simulated=%d AnimInstances=%d Tiers(Local/Near/Mid/Far/Hidden)=%d/%d/%d/%d/%d UpdateData=%d FullChooser=%d FarOnly=%d AvgMMInterval=%.3f Detailed=%d"),
		PlayerCharacterCount,
		NPCCharacterCount,
		AuthorityCount,
		AutonomousCount,
		SimulatedCount,
		AnimInstanceCount,
		TierCounts[0],
		TierCounts[1],
		TierCounts[2],
		TierCounts[3],
		TierCounts[4],
		AnimationDataUpdateCount,
		FullChooserCount,
		FarChooserOnlyCount,
		AverageMotionMatchingInterval,
		DetailedLinesPrinted);

	ClientMessage(SummaryLine);
	UE_LOG(LogProject_J, Display, TEXT("%s"), *SummaryLine);
#endif
}

void AProject_JPlayerController::EquipmentClientTest(const FString& Action)
{
	if (EquipmentClientTestComponent) { EquipmentClientTestComponent->Execute(Action); }
}
#endif

#if WITH_EDITOR
#include "Inventory/Project_JConsumableDefinition.h"
#include "Testing/Project_JUIValidationEffect.h"
#include "Components/Project_JInventoryComponent.h"
#include "Game/Project_JQuestComponent.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JAbilitySystemComponent.h"
void AProject_JPlayerController::SprintInputTest()
{
	if (!IsLocalPlayerController() || !GetWorld() || GetWorld()->WorldType != EWorldType::PIE ||
		IsMoveInputIgnored() || !GetPawn()) return;
	StopSprintInputTest();
	SprintInputTestPawn = GetPawn();
	SprintInputTestStart = GetWorld()->GetTimeSeconds();
	SprintInputTestPhase = INDEX_NONE;
	GetWorld()->GetTimerManager().SetTimer(SprintInputTestTimer, this, &ThisClass::TickSprintInputTest, 0.05f, true);
	UE_LOG(LogProject_J, Display, TEXT("SprintInputTest Begin Pawn=%s LocalRole=%d"), *GetNameSafe(GetPawn()), int32(GetPawn()->GetLocalRole()));
}
void AProject_JPlayerController::StopSprintInputTest()
{
	const bool bHadInput = SprintInputTestPhase != INDEX_NONE || SprintInputTestPawn.IsValid() || SprintInputTestDirection.IsValid();
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(SprintInputTestTimer);
	if (!bHadInput) return;
	for (const FKey Key : {EKeys::W, EKeys::S, EKeys::LeftShift})
		InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Key, IE_Released, 0.f, false, 0));
	if (auto* TestAvatar = Cast<AProject_JPlayerCharacter>(SprintInputTestPawn.Get())) TestAvatar->StopSprint();
	SprintInputTestPawn.Reset();
	SprintInputTestDirection = FKey();
	SprintInputTestPhase = INDEX_NONE;
}
void AProject_JPlayerController::TickSprintInputTest()
{
	if (!GetWorld() || GetPawn() != SprintInputTestPawn.Get() || IsMoveInputIgnored()) { StopSprintInputTest(); return; }
	const float Elapsed = GetWorld()->GetTimeSeconds() - SprintInputTestStart;
	if (Elapsed >= 8.f)
	{
		UE_LOG(LogProject_J, Display, TEXT("SprintInputTest End Pawn=%s"), *GetNameSafe(GetPawn()));
		StopSprintInputTest(); return;
	}
	const int32 Phase = FMath::FloorToInt(Elapsed / 2.f);
	if (Phase != SprintInputTestPhase)
	{
		SprintInputTestPhase = Phase;
		InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::LeftShift,
			Phase % 2 ? IE_Pressed : IE_Released, Phase % 2 ? 1.f : 0.f, false, 0));
		UE_LOG(LogProject_J, Display, TEXT("SprintInputTest Phase=%d Time=%.3f"), Phase, GetWorld()->GetTimeSeconds());
	}
	const FKey Direction = FMath::FloorToInt(Elapsed) % 2 ? EKeys::S : EKeys::W;
	if (Direction != SprintInputTestDirection)
	{
		if (SprintInputTestDirection.IsValid()) InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), SprintInputTestDirection, IE_Released, 0.f, false, 0));
		SprintInputTestDirection = Direction;
		InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Direction, IE_Pressed, 1.f, false, 0));
	}
	if (const auto* TestAvatar = Cast<AProject_JPlayerCharacter>(GetPawn()))
		UE_LOG(LogProject_J, Display, TEXT("SprintInputTest Sample T=%.3f Pawn=%s Sprint=%d Speed=%.1f Max=%.1f Location=%s"),
			GetWorld()->GetTimeSeconds(), *TestAvatar->GetName(), TestAvatar->IsSprintLocomotionAllowed() ? 1 : 0,
			TestAvatar->GetVelocity().Size2D(), TestAvatar->GetCharacterMovement()->MaxWalkSpeed, *TestAvatar->GetActorLocation().ToCompactString());
}
void AProject_JPlayerController::UIPrototypeTest(const FString& Action)
{
	if (IsLocalController() && GetWorld() && GetWorld()->WorldType == EWorldType::PIE)
	{
		ServerUIPrototypeTest(Action);
	}
}
void AProject_JPlayerController::ServerUIPrototypeTest_Implementation(const FString& Action)
{
	if (!GetWorld() || GetWorld()->WorldType != EWorldType::PIE || GetWorld()->bIsTearingDown ||
		GetWorld()->GetTimeSeconds() - LastUIPrototypeRequest < 1)
	{
		return;
	}
	LastUIPrototypeRequest = GetWorld()->GetTimeSeconds();
	auto* PS = GetPlayerState<AProject_JPlayerState>();
	if (!PS) { return; }
	auto* Inventory = PS->GetInventoryComponent();
	auto* ASC = PS->GetProjectJAbilitySystemComponent();
	auto* Quests = PS->FindComponentByClass<UProject_JQuestComponent>();
	auto* Progression = PS->FindComponentByClass<UProject_JProgressionComponent>();
	if (Action == TEXT("prepare") && UIPrototypeItems.IsEmpty())
	{
		for (const TCHAR* Path : {TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"),
			TEXT("/Game/UI/Gameplay/DA_ProjectJManaPotion.DA_ProjectJManaPotion")})
		{
			if (auto* Def = LoadObject<UProject_JConsumableDefinition>(nullptr, Path))
			{
				const auto Item = Inventory->AddItemDefinition(Def, 10);
				if (Item.IsValid()) { UIPrototypeItems.Add(Item.InstanceId); }
			}
		}
		ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 50);
		ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetManaAttribute(), 30);
	}
	// Bounded, owned fixture entries for exercising virtualized scrolling, never durable inventory.
	if (Action == TEXT("fill") && UIPrototypeItems.Num() < 80)
	{
		if (auto* Def = LoadObject<UProject_JConsumableDefinition>(nullptr,
			TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion")))
		{
			for (int32 Index = UIPrototypeItems.Num(); Index < 80; ++Index)
			{
				const auto Item = Inventory->AddItemDefinition(Def, Index + 1);
				if (Item.IsValid()) { UIPrototypeItems.Add(Item.InstanceId); }
			}
		}
	}
	if (Action == TEXT("finish") && Quests) { Quests->RecordObjective(TEXT("TravelMetres"), 20); }
	if (Action == TEXT("buff"))
	{
		for (int32 Index = 0; Index < 3; ++Index) ASC->ApplyGameplayEffectToSelf(GetDefault<UProject_JUIValidationEffect>(), 1.f, ASC->MakeEffectContext());
	}
	if (Action == TEXT("stop"))
	{
		for (FGuid Id : UIPrototypeItems) { Inventory->RemoveItemInstance(Id); }
		UIPrototypeItems.Reset();
	}
	UE_LOG(LogTemp, Display, TEXT("PROJECT_J_UI_PROTOTYPE: HP=%.0f MP=%.0f Lv=%d XP=%lld/%lld UseCD=%.1f Items=%d"),
		PS->GetProjectJAttributeSet()->GetHealth(), PS->GetProjectJAttributeSet()->GetMana(),
		Progression ? Progression->GetState().Level : 0, Progression ? Progression->GetExperience() : 0,
		Progression ? Progression->GetNextLevelExperience() : 0, Inventory->GetUseCooldownRemaining(),
		Inventory->GetItemInstances().Num());
}
#endif
