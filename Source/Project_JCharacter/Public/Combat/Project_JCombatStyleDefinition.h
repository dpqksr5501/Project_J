#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Project_JCombatStyleDefinition.generated.h"

class UProject_JAbilitySet;
class UProject_JAttackSet;
class UProject_JCombatCommandSet;
class UProject_JComboDefinition;
class UProject_JWeaponAnimProfile;

/**
 * Stable aggregation root for one job/weapon combat style. Gameplay systems
 * depend on this asset rather than on animation profiles or concrete job classes.
 */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JCombatStyleDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FGameplayTag CombatStyleTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity", meta = (ClampMin = "1"))
	int32 SchemaVersion = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UProject_JWeaponAnimProfile> WeaponAnimationProfile = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UProject_JComboDefinition> ComboDefinition = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UProject_JCombatCommandSet> CommandSet = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UProject_JAttackSet> AttackSet = nullptr;

	/** Abilities granted while this style is equipped. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	TArray<TObjectPtr<UProject_JAbilitySet>> AbilitySets;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
