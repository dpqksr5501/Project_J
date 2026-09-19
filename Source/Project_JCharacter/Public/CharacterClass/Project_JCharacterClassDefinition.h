#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Project_JCharacterClassDefinition.generated.h"

class UProject_JAbilitySet;
class UProject_JDefaultAttributeSetData;
class UProject_JCombatStyleDefinition;
class UProject_JCombatPresentationSet;

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
	int32 SchemaVersion = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Class", meta = (ClampMin = "1"))
	int32 StartingLevel = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attributes")
	TObjectPtr<UProject_JDefaultAttributeSetData> DefaultAttributeData = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	TArray<TObjectPtr<UProject_JAbilitySet>> AbilitySets;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UProject_JCombatStyleDefinition> DefaultCombatStyle = nullptr;
};

UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JCharacterAdvancementDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Advancement")
	FName AdvancementId = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Advancement", meta = (ClampMin = "1"))
	int32 SchemaVersion = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Advancement")
	TObjectPtr<UProject_JCharacterClassDefinition> BaseClass = nullptr;

	/** All IDs must have been acquired. Registry validation rejects missing IDs and cycles. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirements")
	TArray<FName> RequiredAdvancementIds;

	/** Nonempty equal keys identify mutually exclusive active branches. Replace retires old grants. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Requirements")
	FName ExclusiveBranch;

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UProject_JCombatStyleDefinition> CombatStyleOverride = nullptr;

	/** Opt-in: advancement supplies combo/commands/VFX while equipment still supplies weapon animation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat")
	bool bOverrideEquippedGameplay = false;

	/**
	 * Optional per-attack cosmetic overrides for this advancement. Use this when
	 * mechanics remain the same but, for example, a demon and angel advancement
	 * need different trails or impacts. Do not duplicate a CombatStyle just for VFX.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TObjectPtr<UProject_JCombatPresentationSet> CombatPresentationOverrideSet = nullptr;
};

namespace ProjectJ
{
	PROJECT_JCHARACTER_API bool ValidateAdvancementGraph(const TArray<UProject_JCharacterClassDefinition*>& Classes,
		const TArray<UProject_JCharacterAdvancementDefinition*>& Advancements, TArray<FText>& Errors);
}
