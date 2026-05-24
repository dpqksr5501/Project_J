// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JGameDataSubsystem.generated.h"

/**
 * A global subsystem to manage high-level game data.
 * This subsystem's lifetime is tied to the UGameInstance (it lives as long as the game is running).
 * It prevents the GameInstance from becoming a "God Class" by handling its own specific data.
 */
UCLASS()
class PROJECT_JCORE_API UProject_JGameDataSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// End USubsystem

	// Example functionality: Check if system is ready
	UFUNCTION(BlueprintCallable, Category = "Project_J|System")
	bool IsGameDataReady() const { return bIsReady; }

private:
	bool bIsReady = false;
};
