// Copyright Epic Games, Inc. All Rights Reserved.


#include "Project_JPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Project_J.h"
#include "Project_JGameState.h"
#include "Project_JPlayerState.h"
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
