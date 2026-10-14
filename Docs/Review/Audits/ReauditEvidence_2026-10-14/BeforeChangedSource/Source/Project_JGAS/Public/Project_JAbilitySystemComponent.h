// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "Project_JAbilitySystemComponent.generated.h"

struct FProject_JAbilityGrantRecord
{
	uint64 Generation = 0;
	int32 LeaseCount = 1;
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
	/** Refresh cached controller/component metadata only for the current owner/avatar. */
	bool RefreshActorInfoForCurrentActor(AActor* Requester);
	UProject_JAbilitySystemComponent();

	void AddProjectJLooseGameplayTag(const FGameplayTag& GameplayTag, bool bReplicateOnAuthority = true);
	void RemoveProjectJLooseGameplayTag(const FGameplayTag& GameplayTag, bool bReplicateOnAuthority = true);
	bool TryActivateAbilitiesByInputTag(const FGameplayTag& InputTag);
	bool AbilityInputTagPressed(const FGameplayTag& InputTag);
	bool AbilityInputTagReleased(const FGameplayTag& InputTag);
	/** Discrete intent uses the same reliable actor channel as GAS activation. */
	UFUNCTION(Server, Reliable)
	void ServerSendCombatInputEvent(AActor* ExpectedAvatar, FGameplayTag InputTag, float ClientTimestamp, uint32 InputSequence);
	bool ReserveAbilityGrantSource(FName SourceId, uint64& OutGeneration);
	/** Acquire one provider lease. Only the first provider creates specs/effects. */
	bool AcquireAbilityGrantSource(FName SourceId, bool& bOutNeedsGrant, uint64& OutGeneration);
	bool RegisterGrantedAbility(FName SourceId, uint64 Generation, FGameplayAbilitySpecHandle Handle);
	bool RegisterGrantedEffect(FName SourceId, uint64 Generation, FActiveGameplayEffectHandle Handle);
	bool RemoveAbilityGrantSource(FName SourceId, uint64 Generation);
	bool HasAbilityGrantSource(FName SourceId) const;
	bool IsAbilityGrantSourceCurrent(FName SourceId, uint64 Generation) const;

private:
	bool HasGameplayTagReplicationAuthority() const;
	TMap<FName, FProject_JAbilityGrantRecord> AbilityGrantRecords;
	uint64 NextAbilityGrantGeneration = 0;
};
