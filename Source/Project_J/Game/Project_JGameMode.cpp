// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JGameMode.h"
#include "Project_JGameState.h"
#include "Project_JPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"

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
