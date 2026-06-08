#include "Components/Project_JCombatStateComponent.h"

#include "AbilitySystemComponent.h"
#include "Project_JGameplayTags.h"

UProject_JCombatStateComponent::UProject_JCombatStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JCombatStateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearAbilitySystemBinding();
	Super::EndPlay(EndPlayReason);
}

void UProject_JCombatStateComponent::BindToAbilitySystem(UAbilitySystemComponent* InAbilitySystemComponent)
{
	if (BoundAbilitySystemComponent.Get() == InAbilitySystemComponent && !StateTagEventHandles.IsEmpty())
	{
		return;
	}

	ClearAbilitySystemBinding();
	BoundAbilitySystemComponent = InAbilitySystemComponent;
	RegisterStateTagEvents();
}

void UProject_JCombatStateComponent::ClearAbilitySystemBinding()
{
	UnregisterStateTagEvents();
	BoundAbilitySystemComponent.Reset();
}

bool UProject_JCombatStateComponent::HasStateTag(const FGameplayTag& StateTag) const
{
	const UAbilitySystemComponent* ASC = BoundAbilitySystemComponent.Get();
	return ASC && StateTag.IsValid() && ASC->HasMatchingGameplayTag(StateTag);
}

bool UProject_JCombatStateComponent::TryActivateAbilityByTag(const FGameplayTag& AbilityTag) const
{
	UAbilitySystemComponent* ASC = BoundAbilitySystemComponent.Get();
	if (!ASC || !AbilityTag.IsValid())
	{
		return false;
	}

	return ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(AbilityTag));
}

void UProject_JCombatStateComponent::CancelAbilitiesByTag(const FGameplayTag& AbilityTag) const
{
	UAbilitySystemComponent* ASC = BoundAbilitySystemComponent.Get();
	if (!ASC || !AbilityTag.IsValid())
	{
		return;
	}

	FGameplayTagContainer TagContainer(AbilityTag);
	ASC->CancelAbilities(&TagContainer);
}

bool UProject_JCombatStateComponent::IsCombatModeActive() const
{
	return HasStateTag(FProject_JGameplayTags::Get().State_CombatMode);
}

bool UProject_JCombatStateComponent::IsAttacking() const
{
	return HasStateTag(FProject_JGameplayTags::Get().State_Attacking);
}

bool UProject_JCombatStateComponent::IsDodging() const
{
	return HasStateTag(FProject_JGameplayTags::Get().State_Dodging);
}

bool UProject_JCombatStateComponent::IsHitReacting() const
{
	return HasStateTag(FProject_JGameplayTags::Get().State_HitReacting);
}

bool UProject_JCombatStateComponent::IsSprintTagActive() const
{
	return HasStateTag(FProject_JGameplayTags::Get().State_Movement_Sprinting);
}

void UProject_JCombatStateComponent::RegisterStateTagEvents()
{
	UAbilitySystemComponent* ASC = BoundAbilitySystemComponent.Get();
	if (!ASC)
	{
		return;
	}

	const FProject_JGameplayTags& GameplayTags = FProject_JGameplayTags::Get();
	const FGameplayTag StateTags[] =
	{
		GameplayTags.State_Attacking,
		GameplayTags.State_Dodging,
		GameplayTags.State_HitReacting,
		GameplayTags.State_CombatMode,
		GameplayTags.State_Movement_Sprinting
	};

	StateTagEventHandles.Reserve(UE_ARRAY_COUNT(StateTags));
	for (const FGameplayTag& StateTag : StateTags)
	{
		StateTagEventHandles.Add(ASC->RegisterGameplayTagEvent(StateTag, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &UProject_JCombatStateComponent::HandleStateTagChanged));
	}
}

void UProject_JCombatStateComponent::UnregisterStateTagEvents()
{
	UAbilitySystemComponent* ASC = BoundAbilitySystemComponent.Get();
	if (!ASC)
	{
		StateTagEventHandles.Reset();
		return;
	}

	const FProject_JGameplayTags& GameplayTags = FProject_JGameplayTags::Get();
	const FGameplayTag StateTags[] =
	{
		GameplayTags.State_Attacking,
		GameplayTags.State_Dodging,
		GameplayTags.State_HitReacting,
		GameplayTags.State_CombatMode,
		GameplayTags.State_Movement_Sprinting
	};

	for (int32 Index = 0; Index < StateTagEventHandles.Num() && Index < UE_ARRAY_COUNT(StateTags); ++Index)
	{
		ASC->RegisterGameplayTagEvent(StateTags[Index], EGameplayTagEventType::NewOrRemoved).Remove(StateTagEventHandles[Index]);
	}

	StateTagEventHandles.Reset();
}

void UProject_JCombatStateComponent::HandleStateTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
	OnCombatStateTagChanged.Broadcast(CallbackTag, NewCount);
}
