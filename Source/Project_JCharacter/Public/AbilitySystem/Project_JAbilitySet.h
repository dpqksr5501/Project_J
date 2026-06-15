#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayEffectTypes.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "Project_JAbilitySet.generated.h"

class UGameplayAbility;
class UGameplayEffect;
class UAbilitySystemComponent;

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAbilitySet_GrantedHandles
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FGameplayAbilitySpecHandle> AbilitySpecHandles;

	UPROPERTY()
	TArray<FActiveGameplayEffectHandle> GameplayEffectHandles;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAbilitySet_GameplayAbility
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	TSubclassOf<UGameplayAbility> Ability = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Ability", meta = (ClampMin = "1"))
	int32 AbilityLevel = 1;

	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	FGameplayTag InputTag;

	UPROPERTY(EditDefaultsOnly, Category = "Ability")
	int32 InputID = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAbilitySet_GameplayEffect
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category = "Effect")
	TSubclassOf<UGameplayEffect> Effect = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Effect", meta = (ClampMin = "1"))
	float EffectLevel = 1.0f;
};

/**
 * DataAsset containing a set of GameplayAbilities and GameplayEffects.
 * Used to grant multi-class skills or weapon-specific abilities dynamically.
 */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JAbilitySet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category = "Gameplay Abilities")
	TArray<FProject_JAbilitySet_GameplayAbility> GrantedAbilityEntries;

	UPROPERTY(EditDefaultsOnly, Category = "Gameplay Effects")
	TArray<FProject_JAbilitySet_GameplayEffect> GrantedEffectEntries;

	UPROPERTY(EditDefaultsOnly, Category = "Gameplay Abilities")
	TArray<TSubclassOf<UGameplayAbility>> GrantedGameplayAbilities;

	UPROPERTY(EditDefaultsOnly, Category = "Gameplay Effects")
	TArray<TSubclassOf<UGameplayEffect>> GrantedGameplayEffects;

	/** Grants the abilities and effects to the specified ASC, and stores the handles to remove them later. */
	void GiveToAbilitySystem(UAbilitySystemComponent* ASC, FProject_JAbilitySet_GrantedHandles* OutGrantedHandles, UObject* SourceObject = nullptr) const;

	/** Removes the abilities and effects previously granted. */
	void TakeFromAbilitySystem(UAbilitySystemComponent* ASC, FProject_JAbilitySet_GrantedHandles* GrantedHandles) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
