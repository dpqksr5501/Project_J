// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "Project_JAbilitySystemComponent.generated.h"

struct FProject_JAbilityGrantRecord
{
	TArray<FGameplayAbilitySpecHandle> AbilityHandles;
	TArray<FActiveGameplayEffectHandle> EffectHandles;
};

/**
 * Custom Ability System Component for Project J.
 * Can be extended with custom logic for the MMORPG structure.
 */
UCLASS()
class PROJECT_JGAS_API UProject_JAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()
	
public:
	UProject_JAbilitySystemComponent();

	void AddProjectJLooseGameplayTag(const FGameplayTag& GameplayTag, bool bReplicateOnAuthority = true);
	void RemoveProjectJLooseGameplayTag(const FGameplayTag& GameplayTag, bool bReplicateOnAuthority = true);
	bool TryActivateAbilitiesByInputTag(const FGameplayTag& InputTag);
	bool AbilityInputTagPressed(const FGameplayTag& InputTag);
	bool AbilityInputTagReleased(const FGameplayTag& InputTag);
	bool ReserveAbilityGrantSource(FName SourceId);
	void RegisterGrantedAbility(FName SourceId, FGameplayAbilitySpecHandle Handle);
	void RegisterGrantedEffect(FName SourceId, FActiveGameplayEffectHandle Handle);
	bool RemoveAbilityGrantSource(FName SourceId);
	bool HasAbilityGrantSource(FName SourceId) const;

private:
	bool HasGameplayTagReplicationAuthority() const;
	TMap<FName, FProject_JAbilityGrantRecord> AbilityGrantRecords;
};
