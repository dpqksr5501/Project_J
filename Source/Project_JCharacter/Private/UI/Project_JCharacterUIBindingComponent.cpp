#include "UI/Project_JCharacterUIBindingComponent.h"

#include "AbilitySystemComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "UI/Project_JCharacterViewModel.h"

UProject_JCharacterUIBindingComponent::UProject_JCharacterUIBindingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JCharacterUIBindingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearAttributeBindings();
	Super::EndPlay(EndPlayReason);
}

void UProject_JCharacterUIBindingComponent::InitializeFromAttributes(UProject_JAbilitySystemComponent* InAbilitySystemComponent, UProject_JAttributeSet* InAttributeSet, int32 CharacterLevel)
{
	ClearAttributeBindings();

	BoundAbilitySystemComponent = InAbilitySystemComponent;
	BoundAttributeSet = InAttributeSet;
	if (!CharacterViewModel)
	{
		CharacterViewModel = NewObject<UProject_JCharacterViewModel>(this);
	}

	if (!BoundAbilitySystemComponent || !BoundAttributeSet || !CharacterViewModel)
	{
		return;
	}

	CharacterViewModel->SetHealth(BoundAttributeSet->GetHealth());
	CharacterViewModel->SetMaxHealth(BoundAttributeSet->GetMaxHealth());
	CharacterViewModel->SetMana(BoundAttributeSet->GetMana());
	CharacterViewModel->SetMaxMana(BoundAttributeSet->GetMaxMana());
	CharacterViewModel->SetLevel(CharacterLevel);

	HealthChangedHandle = BoundAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(BoundAttributeSet->GetHealthAttribute()).AddUObject(this, &UProject_JCharacterUIBindingComponent::OnHealthChanged);
	MaxHealthChangedHandle = BoundAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(BoundAttributeSet->GetMaxHealthAttribute()).AddUObject(this, &UProject_JCharacterUIBindingComponent::OnMaxHealthChanged);
	ManaChangedHandle = BoundAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(BoundAttributeSet->GetManaAttribute()).AddUObject(this, &UProject_JCharacterUIBindingComponent::OnManaChanged);
	MaxManaChangedHandle = BoundAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(BoundAttributeSet->GetMaxManaAttribute()).AddUObject(this, &UProject_JCharacterUIBindingComponent::OnMaxManaChanged);
}

void UProject_JCharacterUIBindingComponent::ClearAttributeBindings()
{
	if (BoundAbilitySystemComponent && BoundAttributeSet)
	{
		if (HealthChangedHandle.IsValid())
		{
			BoundAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(BoundAttributeSet->GetHealthAttribute()).Remove(HealthChangedHandle);
		}
		if (MaxHealthChangedHandle.IsValid())
		{
			BoundAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(BoundAttributeSet->GetMaxHealthAttribute()).Remove(MaxHealthChangedHandle);
		}
		if (ManaChangedHandle.IsValid())
		{
			BoundAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(BoundAttributeSet->GetManaAttribute()).Remove(ManaChangedHandle);
		}
		if (MaxManaChangedHandle.IsValid())
		{
			BoundAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(BoundAttributeSet->GetMaxManaAttribute()).Remove(MaxManaChangedHandle);
		}
	}

	HealthChangedHandle.Reset();
	MaxHealthChangedHandle.Reset();
	ManaChangedHandle.Reset();
	MaxManaChangedHandle.Reset();
	BoundAbilitySystemComponent = nullptr;
	BoundAttributeSet = nullptr;
}

void UProject_JCharacterUIBindingComponent::OnHealthChanged(const FOnAttributeChangeData& Data)
{
	if (CharacterViewModel)
	{
		CharacterViewModel->SetHealth(Data.NewValue);
	}
}

void UProject_JCharacterUIBindingComponent::OnMaxHealthChanged(const FOnAttributeChangeData& Data)
{
	if (CharacterViewModel)
	{
		CharacterViewModel->SetMaxHealth(Data.NewValue);
	}
}

void UProject_JCharacterUIBindingComponent::OnManaChanged(const FOnAttributeChangeData& Data)
{
	if (CharacterViewModel)
	{
		CharacterViewModel->SetMana(Data.NewValue);
	}
}

void UProject_JCharacterUIBindingComponent::OnMaxManaChanged(const FOnAttributeChangeData& Data)
{
	if (CharacterViewModel)
	{
		CharacterViewModel->SetMaxMana(Data.NewValue);
	}
}
