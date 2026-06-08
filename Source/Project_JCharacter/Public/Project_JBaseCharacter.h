// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystem/Project_JAbilitySet.h"
#include "Project_JCombatInterface.h"
#include "Project_JBaseCharacter.generated.h"

class UProject_JAbilitySystemComponent;
class UProject_JAttributeSet;
class UProject_JDefaultAttributeSetData;
class UProject_JEquipmentRuntimeComponent;
class UProject_JCharacterClassDefinition;
class UProject_JCharacterAdvancementDefinition;

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

	virtual UProject_JAttributeSet* GetAttributeSet() const;

	UFUNCTION(BlueprintPure, Category = "Character Class")
	FName GetCharacterClassId() const;

	UFUNCTION(BlueprintPure, Category = "Character Class")
	FName GetAdvancementId() const;

	UFUNCTION(BlueprintPure, Category = "Character Class")
	const UProject_JCharacterClassDefinition* GetCharacterClassDefinition() const { return CharacterClassDefinition; }

	UFUNCTION(BlueprintPure, Category = "Character Class")
	const UProject_JCharacterAdvancementDefinition* GetAdvancementDefinition() const { return AdvancementDefinition; }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Character Class")
	bool CanApplyAdvancementDefinition(const UProject_JCharacterAdvancementDefinition* NewAdvancementDefinition) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Character Class")
	bool ApplyAdvancementDefinition(UProject_JCharacterAdvancementDefinition* NewAdvancementDefinition);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;

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

	UPROPERTY(EditAnywhere, Category = "Significance|Tick", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float NearSignificanceTickInterval = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Significance|Tick", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MidSignificanceTickInterval = 0.033f;

	UPROPERTY(EditAnywhere, Category = "Significance|Tick", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FarSignificanceTickInterval = 0.083f;

	UPROPERTY(EditAnywhere, Category = "Significance|Tick", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HiddenSignificanceTickInterval = 0.15f;

	// The Ability System Component for this character
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	UProject_JAbilitySystemComponent* AbilitySystemComponent;

	// The Attribute Set for this character
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	UProject_JAttributeSet* AttributeSet;

	// Equipment Manager for modular meshes
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment", meta = (AllowPrivateAccess = "true"))
	class UProject_JEquipmentManagerComponent* EquipmentManager;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment", meta = (AllowPrivateAccess = "true"))
	UProject_JEquipmentRuntimeComponent* EquipmentRuntime;

	// Character Level
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character Class Defaults")
	int32 CharacterLevel;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Class Defaults")
	TObjectPtr<UProject_JDefaultAttributeSetData> DefaultAttributeData = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Class Defaults")
	TObjectPtr<UProject_JCharacterClassDefinition> CharacterClassDefinition = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character Class Defaults")
	TObjectPtr<UProject_JCharacterAdvancementDefinition> AdvancementDefinition = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	TArray<TSubclassOf<class UGameplayAbility>> DefaultAbilities;

	UPROPERTY(Transient)
	bool bDefaultAbilitiesGranted = false;

	UPROPERTY(Transient)
	FProject_JAbilitySet_GrantedHandles AdvancementGrantedHandles;

	// Helper function to initialize attributes (e.g. from a gameplay effect or table)
	virtual void InitializeDefaultAttributes() const;
	void InitializeAbilitySystem();
	const UProject_JDefaultAttributeSetData* GetEffectiveDefaultAttributeData() const;
	void GiveDefaultAbilitySets(UAbilitySystemComponent& ASC, UObject* AbilitySourceObject);
	void GiveAdvancementAbilitySets(UAbilitySystemComponent& ASC, UObject* AbilitySourceObject, FProject_JAbilitySet_GrantedHandles& OutGrantedHandles) const;
	void RemoveAdvancementAbilitySets(UAbilitySystemComponent& ASC);
	void BindEquipmentRuntimeToEquipmentManager();
	virtual AActor* GetAbilitySystemOwnerActor() const;
};
