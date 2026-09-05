// Copyright Epic Games, Inc. All Rights Reserved.


#include "Project_JPlayerController.h"
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
	ProfilingCrowdComponent = CreateDefaultSubobject<UProject_JProfilingCrowdComponent>(TEXT("ProfilingCrowdComponent"));
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

void AProject_JPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
}

bool AProject_JPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

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
