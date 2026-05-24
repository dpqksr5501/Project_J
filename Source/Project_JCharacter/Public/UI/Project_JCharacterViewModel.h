// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "Project_JCharacterViewModel.generated.h"

/**
 * ViewModel for character attributes like Health, Mana, Level.
 * This acts as the Data Model for the UI. Updates to properties here will
 * automatically notify bound UI elements without the need for Event Ticks.
 */
UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JCharacterViewModel : public UMVVMViewModelBase
{
	GENERATED_BODY()

public:

	// -------------------------------------------------------------------------
	// Properties (State)
	// -------------------------------------------------------------------------

	UPROPERTY(BlueprintReadWrite, FieldNotify, Setter, Getter, Category = "Character|Attributes")
	float Health;

	UPROPERTY(BlueprintReadWrite, FieldNotify, Setter, Getter, Category = "Character|Attributes")
	float MaxHealth;

	UPROPERTY(BlueprintReadWrite, FieldNotify, Setter, Getter, Category = "Character|Attributes")
	float Mana;

	UPROPERTY(BlueprintReadWrite, FieldNotify, Setter, Getter, Category = "Character|Attributes")
	float MaxMana;

	UPROPERTY(BlueprintReadWrite, FieldNotify, Setter, Getter, Category = "Character|Attributes")
	int32 Level;

	// -------------------------------------------------------------------------
	// Accessors (Getters & Setters)
	// -------------------------------------------------------------------------

	float GetHealth() const { return Health; }
	void SetHealth(float NewHealth);

	float GetMaxHealth() const { return MaxHealth; }
	void SetMaxHealth(float NewMaxHealth);

	float GetMana() const { return Mana; }
	void SetMana(float NewMana);

	float GetMaxMana() const { return MaxMana; }
	void SetMaxMana(float NewMaxMana);

	int32 GetLevel() const { return Level; }
	void SetLevel(int32 NewLevel);
};
