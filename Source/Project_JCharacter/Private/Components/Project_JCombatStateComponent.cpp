#include "Components/Project_JCombatStateComponent.h"

#include "AbilitySystemComponent.h"
#include "Project_JGameplayTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJCombatState, Log, All);

UProject_JCombatStateComponent::UProject_JCombatStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JCombatStateComponent::BeginPlay()
{
	bEnding = false;
	Super::BeginPlay();
}

void UProject_JCombatStateComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	bEnding = true;
	ClearAbilitySystemBinding();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UProject_JCombatStateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEnding = true;
	ClearAbilitySystemBinding();
	Super::EndPlay(EndPlayReason);
}

void UProject_JCombatStateComponent::BindToAbilitySystem(UAbilitySystemComponent* InAbilitySystemComponent)
{
	if (bEnding || IsBeingDestroyed() ||
		(InAbilitySystemComponent && InAbilitySystemComponent->GetAvatarActor() != GetOwner()))
	{
		InAbilitySystemComponent = nullptr;
	}
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
	const UAbilitySystemComponent* ASC = GetCurrentAvatarAbilitySystem();
	return ASC && StateTag.IsValid() && ASC->HasMatchingGameplayTag(StateTag);
}

bool UProject_JCombatStateComponent::TryActivateAbilityByTag(const FGameplayTag& AbilityTag) const
{
	UAbilitySystemComponent* ASC = GetCurrentAvatarAbilitySystem();
	if (!ASC || !AbilityTag.IsValid())
	{
		return false;
	}

	return ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(AbilityTag));
}

void UProject_JCombatStateComponent::CancelAbilitiesByTag(const FGameplayTag& AbilityTag) const
{
	UAbilitySystemComponent* ASC = GetCurrentAvatarAbilitySystem();
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

UAbilitySystemComponent* UProject_JCombatStateComponent::GetCurrentAvatarAbilitySystem() const
{
	// PlayerState's ASC can survive this pawn. Binding validity is an avatar
	// ownership contract, not just a live UObject pointer.
	auto* ASC = BoundAbilitySystemComponent.Get();
	const auto* Owner = GetOwner();
	return !bEnding && !IsBeingDestroyed() && IsValid(Owner) && !Owner->IsActorBeingDestroyed() && ASC && ASC->GetAvatarActor() == Owner ? ASC : nullptr;
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
		StateTagEventHandles.Add(StateTag, ASC->RegisterGameplayTagEvent(StateTag, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &UProject_JCombatStateComponent::HandleStateTagChanged));
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

	// Remove the exact tag/handle pairs acquired at registration. There is one
	// observed-tag list, so extending it cannot drift from the release list.
	for (const auto& Entry : StateTagEventHandles)
	{
		ASC->RegisterGameplayTagEvent(Entry.Key, EGameplayTagEventType::NewOrRemoved).Remove(Entry.Value);
	}

	StateTagEventHandles.Reset();
}

void UProject_JCombatStateComponent::HandleStateTagChanged(const FGameplayTag CallbackTag, int32 NewCount)
{
	if (!GetCurrentAvatarAbilitySystem()) { return; }
	UE_LOG(LogProjectJCombatState, Log, TEXT("State tag changed. Owner=%s Tag=%s Count=%d"), *GetNameSafe(GetOwner()), *CallbackTag.ToString(), NewCount);
	OnCombatStateTagChanged.Broadcast(CallbackTag, NewCount);
}
