// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JAbilitySystemComponent.h"

#include "GameFramework/Actor.h"

UProject_JAbilitySystemComponent::UProject_JAbilitySystemComponent()
{
	// Set to true if you want to tick the ASC
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JAbilitySystemComponent::AddProjectJLooseGameplayTag(const FGameplayTag& GameplayTag, bool bReplicateOnAuthority)
{
	if (!GameplayTag.IsValid())
	{
		return;
	}

	const EGameplayTagReplicationState ReplicationState = (bReplicateOnAuthority && HasGameplayTagReplicationAuthority())
		? EGameplayTagReplicationState::TagAndCountToAll
		: EGameplayTagReplicationState::None;

	AddLooseGameplayTag(GameplayTag, 1, ReplicationState);
}

void UProject_JAbilitySystemComponent::RemoveProjectJLooseGameplayTag(const FGameplayTag& GameplayTag, bool bReplicateOnAuthority)
{
	if (!GameplayTag.IsValid())
	{
		return;
	}

	const EGameplayTagReplicationState ReplicationState = (bReplicateOnAuthority && HasGameplayTagReplicationAuthority())
		? EGameplayTagReplicationState::TagAndCountToAll
		: EGameplayTagReplicationState::None;

	RemoveLooseGameplayTag(GameplayTag, 1, ReplicationState);
}

bool UProject_JAbilitySystemComponent::TryActivateAbilitiesByInputTag(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return false;
	}

	bool bActivatedAnyAbility = false;

	ABILITYLIST_SCOPE_LOCK();
	for (FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (!AbilitySpec.Ability || !AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			continue;
		}

		bActivatedAnyAbility |= TryActivateAbility(AbilitySpec.Handle);
	}

	return bActivatedAnyAbility;
}

bool UProject_JAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return false;
	}

	bool bHandledAnyAbility = false;

	ABILITYLIST_SCOPE_LOCK();
	for (FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (!AbilitySpec.Ability || !AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			continue;
		}

		bHandledAnyAbility = true;
		AbilitySpec.InputPressed = true;
		if (AbilitySpec.IsActive())
		{
			if (AbilitySpec.Ability->bReplicateInputDirectly && !IsOwnerActorAuthoritative())
			{
				ServerSetInputPressed(AbilitySpec.Handle);
			}

			AbilitySpecInputPressed(AbilitySpec);

PRAGMA_DISABLE_DEPRECATION_WARNINGS
			TArray<UGameplayAbility*> Instances = AbilitySpec.GetAbilityInstances();
			const FGameplayAbilityActivationInfo& ActivationInfo = Instances.IsEmpty() ? AbilitySpec.ActivationInfo : Instances.Last()->GetCurrentActivationInfoRef();
PRAGMA_ENABLE_DEPRECATION_WARNINGS
			InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, AbilitySpec.Handle, ActivationInfo.GetActivationPredictionKey());
		}
		else
		{
			TryActivateAbility(AbilitySpec.Handle);
		}
	}

	return bHandledAnyAbility;
}

bool UProject_JAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return false;
	}

	bool bHandledAnyAbility = false;

	ABILITYLIST_SCOPE_LOCK();
	for (FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (!AbilitySpec.Ability || !AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			continue;
		}

		bHandledAnyAbility = true;
		AbilitySpec.InputPressed = false;
		if (AbilitySpec.IsActive())
		{
			if (AbilitySpec.Ability->bReplicateInputDirectly && !IsOwnerActorAuthoritative())
			{
				ServerSetInputReleased(AbilitySpec.Handle);
			}

			AbilitySpecInputReleased(AbilitySpec);

PRAGMA_DISABLE_DEPRECATION_WARNINGS
			TArray<UGameplayAbility*> Instances = AbilitySpec.GetAbilityInstances();
			const FGameplayAbilityActivationInfo& ActivationInfo = Instances.IsEmpty() ? AbilitySpec.ActivationInfo : Instances.Last()->GetCurrentActivationInfoRef();
PRAGMA_ENABLE_DEPRECATION_WARNINGS
			InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, AbilitySpec.Handle, ActivationInfo.GetActivationPredictionKey());
		}
	}

	return bHandledAnyAbility;
}

bool UProject_JAbilitySystemComponent::HasGameplayTagReplicationAuthority() const
{
	const AActor* Avatar = GetAvatarActor();
	if (Avatar && Avatar->HasAuthority())
	{
		return true;
	}

	const AActor* Owner = GetOwnerActor();
	return Owner && Owner->HasAuthority();
}

bool UProject_JAbilitySystemComponent::ReserveAbilityGrantSource(const FName SourceId)
{
	if (!IsOwnerActorAuthoritative() || SourceId.IsNone() || AbilityGrantRecords.Contains(SourceId))
	{
		return false;
	}
	AbilityGrantRecords.Add(SourceId);
	return true;
}

void UProject_JAbilitySystemComponent::RegisterGrantedAbility(const FName SourceId, const FGameplayAbilitySpecHandle Handle)
{
	if (FProject_JAbilityGrantRecord* Record = AbilityGrantRecords.Find(SourceId); Record && Handle.IsValid())
	{
		Record->AbilityHandles.Add(Handle);
	}
}

void UProject_JAbilitySystemComponent::RegisterGrantedEffect(const FName SourceId, const FActiveGameplayEffectHandle Handle)
{
	if (FProject_JAbilityGrantRecord* Record = AbilityGrantRecords.Find(SourceId); Record && Handle.IsValid())
	{
		Record->EffectHandles.Add(Handle);
	}
}

bool UProject_JAbilitySystemComponent::RemoveAbilityGrantSource(const FName SourceId)
{
	FProject_JAbilityGrantRecord Record;
	if (!IsOwnerActorAuthoritative() || SourceId.IsNone() || !AbilityGrantRecords.RemoveAndCopyValue(SourceId, Record))
	{
		return false;
	}

	for (const FGameplayAbilitySpecHandle Handle : Record.AbilityHandles)
	{
		if (Handle.IsValid())
		{
			CancelAbilityHandle(Handle);
			ClearAbility(Handle);
		}
	}
	for (const FActiveGameplayEffectHandle Handle : Record.EffectHandles)
	{
		if (Handle.IsValid())
		{
			RemoveActiveGameplayEffect(Handle);
		}
	}
	return true;
}

bool UProject_JAbilitySystemComponent::HasAbilityGrantSource(const FName SourceId) const
{
	return !SourceId.IsNone() && AbilityGrantRecords.Contains(SourceId);
}
