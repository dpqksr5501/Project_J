// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Project_JCombatInterface.h"
#include "Project_JBaseCharacter.generated.h"

class UProject_JAbilitySystemComponent;
class UProject_JAttributeSet;

UCLASS(Abstract)
class PROJECT_JCHARACTER_API AProject_JBaseCharacter : public ACharacter, public IAbilitySystemInterface, public IProject_JCombatInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AProject_JBaseCharacter();

	// Implement IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// IProject_JCombatInterface implementations (can be overridden in BP if NativeEvent is used, or implemented in CPP here)
	virtual int32 GetCharacterLevel_Implementation() const override;
	virtual FVector GetCombatSocketLocation_Implementation(const FName& SocketName) override;
	virtual bool IsDead_Implementation() const override;

	FORCEINLINE UProject_JAttributeSet* GetAttributeSet() const { return AttributeSet; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	UFUNCTION(BlueprintPure, Category = "Significance")
	float GetSignificance() const { return CurrentSignificance; }

protected:
	UPROPERTY(Transient)
	float CurrentSignificance = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Significance")
	float SignificanceNearDistance = 2500.0f;

	UPROPERTY(EditAnywhere, Category = "Significance")
	float SignificanceMidDistance = 6000.0f;

	UPROPERTY(EditAnywhere, Category = "Significance")
	float SignificanceFarDistance = 12000.0f;

	// The Ability System Component for this character
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	UProject_JAbilitySystemComponent* AbilitySystemComponent;

	// The Attribute Set for this character
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	UProject_JAttributeSet* AttributeSet;

	// Character Level
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Class Defaults")
	int32 CharacterLevel;

	// Helper function to initialize attributes (e.g. from a gameplay effect or table)
	virtual void InitializeDefaultAttributes() const;
};
