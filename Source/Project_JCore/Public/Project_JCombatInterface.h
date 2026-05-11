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

	// ?ˆì‹œ: ìºë¦­???ˆë²¨ ë°˜í™˜
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat")
	int32 GetCharacterLevel() const;

	// ?ˆì‹œ: ë¬´ê¸°??ê³µê²© ?´í™?¸ê? ?¤í°???Œì¼“???„ì¹˜ ë°˜í™˜
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat")
	FVector GetCombatSocketLocation(const FName& SocketName);

	// ?ˆì‹œ: ?¬ë§ ?íƒœ ?•ì¸
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Combat")
	bool IsDead() const;
};
