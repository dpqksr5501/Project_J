#include "UI/Project_JCharacterUIBindingComponent.h"

#include "AbilitySystemComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "UI/Project_JCharacterViewModel.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/World.h"

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
	bEndingPlay = false;
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
	if (!CanPublishPresentation()) { return nullptr; }
	// Preserve existing BP/C++ callers. New remote panels should use scoped demand.
	const auto* Pawn = Cast<APawn>(GetOwner());
	const bool bLocalHUD = Pawn && Cast<APlayerController>(Pawn->GetController()) && Pawn->IsLocallyControlled();
	if (!bLocalHUD) { bLegacyViewModelRequested = true; }
	RefreshPresentationBinding();
	return CharacterViewModel;
}

UProject_JCharacterViewModel* UProject_JCharacterUIBindingComponent::AcquireCharacterViewModel(UObject* Consumer)
{
	if (!IsValid(Consumer) || !CanPublishPresentation()) { return nullptr; }
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
	if (!CanPublishPresentation()) { return; }
	SourceLevel = CharacterLevel;
	if (CharacterViewModel)
	{
		TStrongObjectPtr<UProject_JCharacterUIBindingComponent> CallbackOwner(this);
		TStrongObjectPtr<UProject_JCharacterViewModel> Model(CharacterViewModel.Get());
		Model->SetLevel(SourceLevel);
	}
}

void UProject_JCharacterUIBindingComponent::InitializeFromAttributes(UProject_JAbilitySystemComponent* InAbilitySystemComponent, UProject_JAttributeSet* InAttributeSet, int32 CharacterLevel)
{
	if (!CanPublishPresentation()) { return; }
	SourceAbilitySystem = InAbilitySystemComponent;
	SourceAttributes = InAttributeSet;
	SourceLevel = CharacterLevel;
	RefreshPresentationBinding();
}

void UProject_JCharacterUIBindingComponent::RefreshPresentationBinding()
{
	check(IsInGameThread());
	if (!CanPublishPresentation()) { ClearAttributeBindings(); CharacterViewModel = nullptr; return; }
	TStrongObjectPtr<UProject_JCharacterUIBindingComponent> CallbackOwner(this);
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
		TStrongObjectPtr<UProject_JCharacterViewModel> Model(CharacterViewModel.Get());
		Model->SetLevel(SourceLevel);
		return;
	}
	ClearAttributeBindings();
	if (!CharacterViewModel)
	{
		CharacterViewModel = NewObject<UProject_JCharacterViewModel>(this);
	}
	BoundAbilitySystemComponent = SourceAbilitySystem.Get();
	BoundAttributeSet = SourceAttributes.Get();
	const uint64 Revision = BindingRevision;
	TStrongObjectPtr<UProject_JCharacterViewModel> Model(CharacterViewModel.Get());
	TStrongObjectPtr<UProject_JAbilitySystemComponent> ASC(BoundAbilitySystemComponent.Get());
	TStrongObjectPtr<UProject_JAttributeSet> Attributes(BoundAttributeSet.Get());
	if (!Model.IsValid()) { return; }
	const bool bHasSource = ASC.IsValid() && !ASC->IsBeingDestroyed() && Attributes.IsValid();
	// Publish the complete delegate lease before FieldNotify can reenter. A nested
	// binding then removes exactly these handles; the outer call cannot register again.
	if (bHasSource)
	{
		HealthChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(Attributes->GetHealthAttribute()).AddUObject(this, &ThisClass::OnHealthChanged, Revision);
		MaxHealthChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(Attributes->GetMaxHealthAttribute()).AddUObject(this, &ThisClass::OnMaxHealthChanged, Revision);
		ManaChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(Attributes->GetManaAttribute()).AddUObject(this, &ThisClass::OnManaChanged, Revision);
		MaxManaChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(Attributes->GetMaxManaAttribute()).AddUObject(this, &ThisClass::OnMaxManaChanged, Revision);
	}
	const auto IsCurrent = [&]()
	{
		return CanPublishPresentation() && BindingRevision == Revision && CharacterViewModel == Model.Get()
			&& (!bHasSource || !ASC->IsBeingDestroyed());
	};
	Model->SetHealth(bHasSource ? Attributes->GetHealth() : 0);
	if (!IsCurrent()) { return; }
	Model->SetMaxHealth(bHasSource ? Attributes->GetMaxHealth() : 0);
	if (!IsCurrent()) { return; }
	Model->SetMana(bHasSource ? Attributes->GetMana() : 0);
	if (!IsCurrent()) { return; }
	Model->SetMaxMana(bHasSource ? Attributes->GetMaxMana() : 0);
	if (!IsCurrent()) { return; }
	Model->SetLevel(SourceLevel);
}

void UProject_JCharacterUIBindingComponent::ClearAttributeBindings()
{
	++BindingRevision;
	const auto PreviousASC = BoundAbilitySystemComponent;
	const auto PreviousAttributes = BoundAttributeSet;
	const auto Health = HealthChangedHandle, MaxHealth = MaxHealthChangedHandle, Mana = ManaChangedHandle, MaxMana = MaxManaChangedHandle;
	HealthChangedHandle.Reset(); MaxHealthChangedHandle.Reset(); ManaChangedHandle.Reset(); MaxManaChangedHandle.Reset();
	BoundAbilitySystemComponent = nullptr; BoundAttributeSet = nullptr;
	if (PreviousASC && PreviousAttributes)
	{
		PreviousASC->GetGameplayAttributeValueChangeDelegate(PreviousAttributes->GetHealthAttribute()).Remove(Health);
		PreviousASC->GetGameplayAttributeValueChangeDelegate(PreviousAttributes->GetMaxHealthAttribute()).Remove(MaxHealth);
		PreviousASC->GetGameplayAttributeValueChangeDelegate(PreviousAttributes->GetManaAttribute()).Remove(Mana);
		PreviousASC->GetGameplayAttributeValueChangeDelegate(PreviousAttributes->GetMaxManaAttribute()).Remove(MaxMana);
	}
}
bool UProject_JCharacterUIBindingComponent::CanPublishPresentation() const
{
	return !bEndingPlay && !IsBeingDestroyed() && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed()
		&& GetWorld() && !GetWorld()->bIsTearingDown && GetNetMode() != NM_DedicatedServer;
}
void UProject_JCharacterUIBindingComponent::PublishAttribute(float Value, uint64 ExpectedRevision, void (UProject_JCharacterViewModel::*Setter)(float))
{
	if (!CanPublishPresentation() || BindingRevision != ExpectedRevision || !CharacterViewModel) { return; }
	TStrongObjectPtr<UProject_JCharacterUIBindingComponent> CallbackOwner(this);
	TStrongObjectPtr<UProject_JCharacterViewModel> Model(CharacterViewModel.Get());
	(Model.Get()->*Setter)(Value);
}
void UProject_JCharacterUIBindingComponent::OnHealthChanged(const FOnAttributeChangeData& Data, uint64 Revision)
{ PublishAttribute(Data.NewValue, Revision, &UProject_JCharacterViewModel::SetHealth); }
void UProject_JCharacterUIBindingComponent::OnMaxHealthChanged(const FOnAttributeChangeData& Data, uint64 Revision)
{ PublishAttribute(Data.NewValue, Revision, &UProject_JCharacterViewModel::SetMaxHealth); }
void UProject_JCharacterUIBindingComponent::OnManaChanged(const FOnAttributeChangeData& Data, uint64 Revision)
{ PublishAttribute(Data.NewValue, Revision, &UProject_JCharacterViewModel::SetMana); }
void UProject_JCharacterUIBindingComponent::OnMaxManaChanged(const FOnAttributeChangeData& Data, uint64 Revision)
{ PublishAttribute(Data.NewValue, Revision, &UProject_JCharacterViewModel::SetMaxMana); }
