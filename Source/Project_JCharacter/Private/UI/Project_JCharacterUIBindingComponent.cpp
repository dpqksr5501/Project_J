#include "UI/Project_JCharacterUIBindingComponent.h"

#include "AbilitySystemComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "UI/Project_JCharacterViewModel.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

UProject_JCharacterUIBindingComponent::UProject_JCharacterUIBindingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JCharacterUIBindingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopPresentation();
	Super::EndPlay(EndPlayReason);
}

void UProject_JCharacterUIBindingComponent::BeginPlay()
{
	Super::BeginPlay();
	if (auto* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::OnControllerChanged);
	}
	RefreshPresentationBinding();
}

void UProject_JCharacterUIBindingComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	StopPresentation();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UProject_JCharacterUIBindingComponent::StopPresentation()
{
	bEndingPlay = true;
	if (auto* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::OnControllerChanged);
	}
	ClearAttributeBindings();
	Consumers.Reset();
	SourceAbilitySystem.Reset();
	SourceAttributes.Reset();
	CharacterViewModel = nullptr;
}

void UProject_JCharacterUIBindingComponent::OnControllerChanged(APawn*, AController*, AController*)
{
	RefreshPresentationBinding();
}

UProject_JCharacterViewModel* UProject_JCharacterUIBindingComponent::GetCharacterViewModel()
{
	// Preserve existing BP/C++ callers. New remote panels should use scoped demand.
	const auto* Pawn = Cast<APawn>(GetOwner());
	const bool bLocalHUD = Pawn && Cast<APlayerController>(Pawn->GetController()) && Pawn->IsLocallyControlled();
	if (!bLocalHUD) { bLegacyViewModelRequested = true; }
	RefreshPresentationBinding();
	return CharacterViewModel;
}

UProject_JCharacterViewModel* UProject_JCharacterUIBindingComponent::AcquireCharacterViewModel(UObject* Consumer)
{
	if (!IsValid(Consumer) || bEndingPlay || GetNetMode() == NM_DedicatedServer) { return nullptr; }
	Consumers.Add(Consumer);
	RefreshPresentationBinding();
	return CharacterViewModel;
}

void UProject_JCharacterUIBindingComponent::ReleaseCharacterViewModel(UObject* Consumer)
{
	Consumers.Remove(Consumer);
	RefreshPresentationBinding();
}

void UProject_JCharacterUIBindingComponent::ReleaseLegacyViewModelRequest()
{
	bLegacyViewModelRequested = false;
	RefreshPresentationBinding();
}

void UProject_JCharacterUIBindingComponent::UpdateCharacterLevel(int32 CharacterLevel)
{
	SourceLevel = CharacterLevel;
	if (CharacterViewModel) { CharacterViewModel->SetLevel(SourceLevel); }
}

void UProject_JCharacterUIBindingComponent::InitializeFromAttributes(UProject_JAbilitySystemComponent* InAbilitySystemComponent, UProject_JAttributeSet* InAttributeSet, int32 CharacterLevel)
{
	SourceAbilitySystem = InAbilitySystemComponent;
	SourceAttributes = InAttributeSet;
	SourceLevel = CharacterLevel;
	RefreshPresentationBinding();
}

void UProject_JCharacterUIBindingComponent::RefreshPresentationBinding()
{
	check(IsInGameThread());
	for (auto It = Consumers.CreateIterator(); It; ++It)
	{
		if (!It->IsValid()) { It.RemoveCurrent(); }
	}
	const auto* Pawn = Cast<APawn>(GetOwner());
	const bool bLocalHUD = Pawn && Cast<APlayerController>(Pawn->GetController()) && Pawn->IsLocallyControlled();
	if (bEndingPlay || GetNetMode() == NM_DedicatedServer || (!bLocalHUD && !bLegacyViewModelRequested && Consumers.IsEmpty()))
	{
		ClearAttributeBindings();
		CharacterViewModel = nullptr;
		return;
	}
	if (CharacterViewModel && BoundAbilitySystemComponent == SourceAbilitySystem.Get()
		&& BoundAttributeSet == SourceAttributes.Get() && HealthChangedHandle.IsValid())
	{
		CharacterViewModel->SetLevel(SourceLevel);
		return;
	}
	ClearAttributeBindings();
	if (!CharacterViewModel)
	{
		CharacterViewModel = NewObject<UProject_JCharacterViewModel>(this);
	}
	BoundAbilitySystemComponent = SourceAbilitySystem.Get();
	BoundAttributeSet = SourceAttributes.Get();

	if (!BoundAbilitySystemComponent || !BoundAttributeSet || !CharacterViewModel)
	{
		if (CharacterViewModel)
		{
			CharacterViewModel->SetHealth(0);
			CharacterViewModel->SetMaxHealth(0);
			CharacterViewModel->SetMana(0);
			CharacterViewModel->SetMaxMana(0);
			CharacterViewModel->SetLevel(SourceLevel);
		}
		return;
	}

	CharacterViewModel->SetHealth(BoundAttributeSet->GetHealth());
	CharacterViewModel->SetMaxHealth(BoundAttributeSet->GetMaxHealth());
	CharacterViewModel->SetMana(BoundAttributeSet->GetMana());
	CharacterViewModel->SetMaxMana(BoundAttributeSet->GetMaxMana());
	CharacterViewModel->SetLevel(SourceLevel);

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
