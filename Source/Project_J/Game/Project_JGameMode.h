// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Project_JMMOTypes.h"
#include "Project_JGameMode.generated.h"

class AProject_JPlayerState;

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

protected:
	virtual FProject_JWorldInstanceId CreatePrototypeWorldInstanceId() const;
	virtual void AssignPrototypeIdentity(AProject_JPlayerState& ProjectPlayerState) const;
};



