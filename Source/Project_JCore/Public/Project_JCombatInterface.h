// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Project_JCombatInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UProject_JCombatInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface for characters or actors that can participate in combat.
 */
class PROJECT_JCORE_API IProject_JCombatInterface
{
	GENERATED_BODY()

public:

	// Returns the combat participant's current character level.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat")
	int32 GetCharacterLevel() const;

	// Returns the world location of the named combat socket.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat")
	FVector GetCombatSocketLocation(const FName& SocketName);

	// Returns whether this combat participant is dead.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat")
	bool IsDead() const;
};
