// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JGreatswordCharacter.generated.h"

class UProject_JWarriorComponent;

/**
 * Native foundation for the greatsword job family.
 *
 * Class/advancement data, visual mesh, weapon profile, and AnimBP remain
 * authored in BP_GreatswordCharacter and Data Assets. This class only owns
 * greatsword-specific runtime extension points.
 */
UCLASS(Blueprintable)
class PROJECT_JCHARACTER_API AProject_JGreatswordCharacter : public AProject_JPlayerCharacter
{
	GENERATED_BODY()

public:
	AProject_JGreatswordCharacter();

	UFUNCTION(BlueprintPure, Category = "Greatsword")
	UProject_JWarriorComponent* GetGreatswordCombatComponent() const { return GreatswordCombatComponent; }

protected:
	/**
	 * Uses the current generic melee combat implementation initially. A future
	 * greatsword-only charge/guard component can replace this without changing
	 * the common player character class.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Greatsword", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProject_JWarriorComponent> GreatswordCombatComponent = nullptr;
};
