#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AbilitySystem/Project_JAbilitySet.h"
#include "GameplayTagContainer.h"
#include "Project_JCombatStyleDefinition.generated.h"

class UProject_JAbilitySet;
class UProject_JAttackSet;
class UProject_JAttackDefinition;
class UProject_JCombatCommandSet;
class UProject_JComboDefinition;
class UProject_JWeaponAnimProfile;
class UProject_JCombatPresentationSet;

/**
 * Stable aggregation root for one job/weapon combat style. Gameplay systems
 * depend on this asset rather than on animation profiles or concrete job classes.
 */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JCombatStyleDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Legacy melee defaults remain enabled. GAS-only styles can opt out of each requirement. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Capabilities")
	bool bUsesCombo = true;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Capabilities")
	bool bRequiresWeaponAnimation = true;

	/** Optional local authoring. No separate AbilitySet asset is needed for these entries. */
	UPROPERTY(EditDefaultsOnly, Category="Abilities")
	TArray<FProject_JAbilitySet_GameplayAbility> InlineAbilities;
	UPROPERTY(EditDefaultsOnly, Category="Abilities")
	TArray<FProject_JAbilitySet_GameplayEffect> InlineEffects;

	/** Derive the catalog from ComboDefinition + AdditionalAttacks instead of maintaining an AttackSet. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat")
	bool bDeriveAttackCatalog = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat", meta=(EditCondition="bDeriveAttackCatalog"))
	TArray<TObjectPtr<UProject_JAttackDefinition>> AdditionalAttacks;

	const TArray<TObjectPtr<UProject_JAbilitySet>>& GetRuntimeAbilitySets() const;
	const UProject_JAttackSet* GetRuntimeAttackSet() const;
	/** Pure validation of authored references, usable in shipping code as well as the editor. */
	bool ValidateDefinition(TArray<FText>& Errors) const;
	void InvalidateRuntimeData();
	bool IsRuntimeReady() const;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FGameplayTag CombatStyleTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity", meta = (ClampMin = "1"))
	int32 SchemaVersion = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UProject_JWeaponAnimProfile> WeaponAnimationProfile = nullptr;

	/**
	 * Client presentation selected by this combat style. It maps stable AttackTag
	 * identities to cosmetic VFX profiles and is never consulted by authority.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TObjectPtr<UProject_JCombatPresentationSet> CombatPresentationSet = nullptr;

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
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
private:
	void BuildRuntimeData();
	UPROPERTY(Transient, DuplicateTransient) TArray<TObjectPtr<UProject_JAbilitySet>> RuntimeAbilitySets;
	UPROPERTY(Transient, DuplicateTransient) TObjectPtr<UProject_JAttackSet> RuntimeAttackSet;
	UPROPERTY(Transient, DuplicateTransient) bool bRuntimeDataBuilt = false;
	UPROPERTY(Transient, DuplicateTransient) bool bRuntimeDataValid = false;
};
