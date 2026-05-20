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

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

	// ?덉떆: 罹먮┃???덈꺼 諛섑솚
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat")
	int32 GetCharacterLevel() const;

	// ?덉떆: 臾닿린??怨듦꺽 ?댄럺?몃? ?ㅽ룿???뚯폆 ?꾩튂 諛섑솚
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat")
	FVector GetCombatSocketLocation(const FName& SocketName);

	// ?덉떆: ?щ쭩 ?곹깭 ?뺤씤
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat")
	bool IsDead() const;
};