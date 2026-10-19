// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JGameMode.h"
#include "Engine/World.h"
#include "Project_JGameState.h"
#include "Project_JPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "Engine/GameInstance.h"

AProject_JGameMode::AProject_JGameMode()
{
	GameStateClass = AProject_JGameState::StaticClass();
	PlayerStateClass = AProject_JPlayerState::StaticClass();
}

void AProject_JGameMode::InitGameState()
{
	Super::InitGameState();

	if (AProject_JGameState* ProjectGameState = GetGameState<AProject_JGameState>())
	{
		ProjectGameState->SetWorldInstanceId(CreatePrototypeWorldInstanceId());
	}
}

void AProject_JGameMode::PostLogin(APlayerController* NewPlayer)
{
	FString LocalProfile;
	if (FParse::Value(FCommandLine::Get(), TEXT("ProjectJLocalProfile="), LocalProfile))
	{
		auto* State = NewPlayer ? NewPlayer->GetPlayerState<AProject_JPlayerState>() : nullptr;
		auto* Profiles = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>() : nullptr;
		TWeakObjectPtr<AProject_JGameMode> WeakThis(this); TWeakObjectPtr<APlayerController> WeakPlayer(NewPlayer);
		auto Ready = [WeakThis, WeakPlayer](bool Success)
		{
			auto* Mode = WeakThis.Get(); auto* PC = WeakPlayer.Get();
			if (!IsValid(Mode) || Mode->IsActorBeingDestroyed() || !IsValid(PC) || PC->IsActorBeingDestroyed()) return;
			if (Success) Mode->HandleStartingNewPlayer(PC);
			else
			{
				auto* Status=Mode->GetGameInstance()?Mode->GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>():nullptr;
				const FText Reason=Status?Status->GetStatusText(PC->GetPlayerState<AProject_JPlayerState>()):FText::GetEmpty();
				PC->ClientReturnToMainMenuWithTextReason(Reason.IsEmpty()?NSLOCTEXT("LocalProfile", "LoadFailed", "Local profile could not be loaded. Existing saved data was preserved."):Reason);
			}
		};
#if UE_BUILD_SHIPPING
		Ready(false); // Local names never establish production account authority.
#else
		if (Profiles && State) Profiles->OpenProfile(State, LocalProfile, MoveTemp(Ready));
		else Ready(false);
#endif
		Super::PostLogin(NewPlayer); // Starting-player override holds spawn until restoration completes.
		return;
	}
	Super::PostLogin(NewPlayer);

	AProject_JPlayerState* ProjectPlayerState = NewPlayer ? NewPlayer->GetPlayerState<AProject_JPlayerState>() : nullptr;
	if (!ProjectPlayerState)
	{
		return;
	}

	if (!ProjectPlayerState->GetAccountId().IsValid() || !ProjectPlayerState->GetCharacterId().IsValid())
	{
		AssignPrototypeIdentity(*ProjectPlayerState);
	}
}

void AProject_JGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	FString LocalProfile;
	if (FParse::Value(FCommandLine::Get(), TEXT("ProjectJLocalProfile="), LocalProfile))
	{
		auto* Profiles = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>() : nullptr;
		if (!Profiles || !NewPlayer || !Profiles->IsReady(NewPlayer->GetPlayerState<AProject_JPlayerState>())) return;
	}
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}
void AProject_JGameMode::Logout(AController* Exiting)
{
	if (auto* Profiles = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>() : nullptr)
	{
		auto* State = Exiting ? Exiting->GetPlayerState<AProject_JPlayerState>() : nullptr;
		Profiles->RequestCheckpoint(State);
		Profiles->Detach(State);
	}
	Super::Logout(Exiting);
}

FProject_JWorldInstanceId AProject_JGameMode::CreatePrototypeWorldInstanceId() const
{
	const FString MapName = GetWorld() ? GetWorld()->GetMapName() : FString(TEXT("UnknownMap"));

	FProject_JWorldInstanceId WorldInstanceId;
	WorldInstanceId.WorldId = FName(TEXT("PrototypeWorld"));
	WorldInstanceId.ZoneId = FName(*FPaths::GetBaseFilename(MapName));
	WorldInstanceId.InstanceId = FName(*FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
	WorldInstanceId.ChannelId = FName(TEXT("Default"));
	return WorldInstanceId;
}

void AProject_JGameMode::AssignPrototypeIdentity(AProject_JPlayerState& ProjectPlayerState) const
{
	ProjectPlayerState.SetIdentity(FProject_JAccountId::NewId(), FProject_JCharacterId::NewId());
}
