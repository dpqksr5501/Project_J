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

void UProject_JAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}

	ABILITYLIST_SCOPE_LOCK();
	for (FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (!AbilitySpec.Ability || !AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			continue;
		}

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
}

void UProject_JAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}

	ABILITYLIST_SCOPE_LOCK();
	for (FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
	{
		if (!AbilitySpec.Ability || !AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			continue;
		}

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
