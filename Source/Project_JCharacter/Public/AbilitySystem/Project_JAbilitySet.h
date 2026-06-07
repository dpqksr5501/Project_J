#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayEffectTypes.h"
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
	TArray<TSubclassOf<UGameplayAbility>> GrantedGameplayAbilities;

	UPROPERTY(EditDefaultsOnly, Category = "Gameplay Effects")
	TArray<TSubclassOf<UGameplayEffect>> GrantedGameplayEffects;

	/** Grants the abilities and effects to the specified ASC, and stores the handles to remove them later. */
	void GiveToAbilitySystem(UAbilitySystemComponent* ASC, FProject_JAbilitySet_GrantedHandles* OutGrantedHandles, UObject* SourceObject = nullptr) const;

	/** Removes the abilities and effects previously granted. */
	void TakeFromAbilitySystem(UAbilitySystemComponent* ASC, FProject_JAbilitySet_GrantedHandles* GrantedHandles) const;
};
