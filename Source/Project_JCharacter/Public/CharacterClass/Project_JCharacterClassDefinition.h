#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Project_JCharacterClassDefinition.generated.h"

class UProject_JAbilitySet;
class UProject_JDefaultAttributeSetData;

UENUM(BlueprintType)
enum class EProject_JAdvancementAbilityGrantPolicy : uint8
{
	Additive,
	ReplacePreviousAdvancement
};

UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JCharacterClassDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Class")
	FName ClassId = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Class", meta = (ClampMin = "1"))
	int32 StartingLevel = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attributes")
	TObjectPtr<UProject_JDefaultAttributeSetData> DefaultAttributeData = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	TArray<TObjectPtr<UProject_JAbilitySet>> AbilitySets;
};

UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JCharacterAdvancementDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Advancement")
	FName AdvancementId = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Advancement")
	TObjectPtr<UProject_JCharacterClassDefinition> BaseClass = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirements", meta = (ClampMin = "1"))
	int32 RequiredLevel = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirements")
	FGameplayTagContainer RequiredTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirements")
	FGameplayTagContainer BlockedTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attributes")
	TObjectPtr<UProject_JDefaultAttributeSetData> OverrideAttributeData = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	TArray<TObjectPtr<UProject_JAbilitySet>> AdditionalAbilitySets;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	EProject_JAdvancementAbilityGrantPolicy AbilityGrantPolicy = EProject_JAdvancementAbilityGrantPolicy::Additive;
};
