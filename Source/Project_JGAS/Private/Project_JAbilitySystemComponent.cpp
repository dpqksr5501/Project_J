// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JAbilitySystemComponent.h"

#include "GameFramework/Actor.h"
#include "Project_JCombatInterface.h"

void UProject_JAbilitySystemComponent::InitAbilityActorInfo(AActor* Owner,AActor* Avatar)
{
	check(IsInGameThread());if(++ActorInfoRevision==0)++ActorInfoRevision;
	Super::InitAbilityActorInfo(Owner,Avatar);
}
void UProject_JAbilitySystemComponent::ClearActorInfo()
{
	check(IsInGameThread());if(++ActorInfoRevision==0)++ActorInfoRevision;
	Super::ClearActorInfo();
}

UProject_JAbilitySystemComponent::UProject_JAbilitySystemComponent()
{
	// Preserve GAS/GameplayTasks tick capability. The engine's GetShouldTick /
	// UpdateShouldTick owns activation for ticking tasks, replicated montages and
	// tickable attributes, then sleeps when idle. bCanEverTick=false prevents all
	// of those systems from waking the registered component.
}

void UProject_JAbilitySystemComponent::ServerSendCombatInputEvent_Implementation(AActor* ExpectedAvatar, FGameplayTag InputTag, float ClientTimestamp, uint32 InputSequence)
{
	// A reliable packet queued before pawn replacement belongs to that old avatar.
	// GAS activation and intent share this ASC's actor channel and preserve order.
	if (!IsValid(ExpectedAvatar) || ExpectedAvatar->IsActorBeingDestroyed() || !ExpectedAvatar->HasAuthority()
		|| GetAvatarActor() != ExpectedAvatar) { return; }
	if (auto* Receiver = Cast<IProject_JCombatInterface>(ExpectedAvatar))
	{ Receiver->ReceiveAuthorityCombatInput(InputTag, ClientTimestamp, InputSequence); }
}

bool UProject_JAbilitySystemComponent::RefreshActorInfoForCurrentActor(AActor* Requester)
{
	check(IsInGameThread());
	if (!IsValid(Requester) || Requester->IsActorBeingDestroyed() || !AbilityActorInfo.IsValid()) { return false; }
	auto* Owner = AbilityActorInfo->OwnerActor.Get();
	auto* Avatar = AbilityActorInfo->AvatarActor.Get();
	if (!IsValid(Owner) || Owner->IsActorBeingDestroyed() || GetOwnerActor() != Owner || GetAvatarActor() != Avatar
		|| (Avatar && Avatar->IsActorBeingDestroyed()) || (Requester != Owner && Requester != Avatar)) { return false; }
	// RefreshAbilityActorInfo updates the cached PlayerController and components.
	// It does not run OnAvatarSet, grant abilities, or reset an active ability.
	RefreshAbilityActorInfo();
	return true;
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

bool UProject_JAbilitySystemComponent::ReserveAbilityGrantSource(const FName SourceId, uint64& OutGeneration)
{
	OutGeneration = 0;
	if (AbilityGrantRecords.Contains(SourceId)) { return false; }
	bool bNeedsGrant = false;
	return AcquireAbilityGrantSource(SourceId, bNeedsGrant, OutGeneration);
}

bool UProject_JAbilitySystemComponent::AcquireAbilityGrantSource(FName SourceId, bool& bOutNeedsGrant, uint64& OutGeneration)
{
	check(IsInGameThread());
	bOutNeedsGrant = false;
	OutGeneration = 0;
	if (!IsOwnerActorAuthoritative() || SourceId.IsNone()) return false;
	if (auto* Record = AbilityGrantRecords.Find(SourceId))
	{
		if (Record->LeaseCount == MAX_int32) return false;
		++Record->LeaseCount;
		OutGeneration = Record->Generation;
		return true;
	}
	if (NextAbilityGrantGeneration == MAX_uint64) { return false; }
	OutGeneration = ++NextAbilityGrantGeneration;
	AbilityGrantRecords.Add(SourceId).Generation = OutGeneration;
	bOutNeedsGrant = true;
	return true;
}

bool UProject_JAbilitySystemComponent::RegisterGrantedAbility(const FName SourceId, uint64 Generation, const FGameplayAbilitySpecHandle Handle)
{
	if (FProject_JAbilityGrantRecord* Record = AbilityGrantRecords.Find(SourceId); Record && Record->Generation == Generation && Handle.IsValid())
	{
		Record->AbilityHandles.Add(Handle);
		return true;
	}
	return false;
}

bool UProject_JAbilitySystemComponent::RegisterGrantedEffect(const FName SourceId, uint64 Generation, const FActiveGameplayEffectHandle Handle)
{
	if (FProject_JAbilityGrantRecord* Record = AbilityGrantRecords.Find(SourceId); Record && Record->Generation == Generation && Handle.IsValid())
	{
		Record->EffectHandles.Add(Handle);
		return true;
	}
	return false;
}

bool UProject_JAbilitySystemComponent::RemoveAbilityGrantSource(const FName SourceId, uint64 Generation)
{
	check(IsInGameThread());
	if (!IsOwnerActorAuthoritative() || !IsAbilityGrantSourceCurrent(SourceId, Generation)) return false;
	if (auto* Existing = AbilityGrantRecords.Find(SourceId); Existing && Existing->LeaseCount > 1)
	{
		--Existing->LeaseCount;
		return true;
	}
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
bool UProject_JAbilitySystemComponent::IsAbilityGrantSourceCurrent(FName SourceId, uint64 Generation) const
{
	const auto* Record = AbilityGrantRecords.Find(SourceId);
	return Generation != 0 && Record && Record->Generation == Generation;
}
