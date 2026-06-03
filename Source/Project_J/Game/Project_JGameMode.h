// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Project_JGameMode.generated.h"

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class AProject_JGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	
	/** Constructor */
	AProject_JGameMode();

	virtual void InitGameState() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
};



