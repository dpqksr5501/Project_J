// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JCombatComponent.generated.h"

/**
 * Base component class for character combat capabilities.
 * Concrete jobs (e.g. Warrior, Mage) inherit from this to implement specific combat styles.
 */
UCLASS(Abstract, BlueprintType, Blueprintable)
class PROJECT_JCHARACTER_API UProject_JCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UProject_JCombatComponent();

public:
	/** Equip job-specific weapons */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	virtual void EquipWeapon() {}

	/** Unequip job-specific weapons */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	virtual void UnequipWeapon() {}

	/** Trigger basic attack or combo sequence */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	virtual void Attack() {}
};
