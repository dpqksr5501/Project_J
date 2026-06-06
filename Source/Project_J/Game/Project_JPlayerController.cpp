// Copyright Epic Games, Inc. All Rights Reserved.


#include "Project_JPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Project_J.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Project_JGameState.h"
#include "Project_JPlayerState.h"
#include "Network/Project_JNetObjectFilter_Distance.h"
#include "Network/Project_JNetObjectPrioritizer_Combat.h"
#include "Project_JPlayerCharacter.h"
#include "Widgets/Input/SVirtualJoystick.h"

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

	FProject_JReplicationPolicyDecision Decision = DistanceFilter->BuildReplicationDecision(TargetActor, ViewerLocation);
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
		TEXT("CharacterComponents Combat=%s Locomotion=%s Trajectory=%s ViewModel=%s"),
		ProjectCharacter->GetActiveCombatComponent() ? TEXT("yes") : TEXT("no"),
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
