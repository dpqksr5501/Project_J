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
