#include "UI/Project_JPlayerUIComponent.h"
#include "UI/Project_JPlayerHUDWidget.h"
#include "UI/Project_JInventoryViewModel.h"
#include "UI/Project_JUILayoutSettings.h"
#include "Game/Project_JQuestComponent.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Inventory/Project_JItemDefinition.h"
#include "Engine/LocalPlayer.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JGameplayTags.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/World.h"
#include "EnhancedPlayerInput.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputSubsystems.h"
#include "UI/Project_JStatusEffects.h"
#include "AbilitySystemInterface.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CsvProfiler.h"
CSV_DEFINE_CATEGORY(ProjectJUI, true);

UProject_JPlayerUIComponent::UProject_JPlayerUIComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	ScreenClass = UProject_JPlayerHUDWidget::StaticClass();
}
void UProject_JPlayerUIComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!ProfileCatalog && Cast<APlayerController>(GetOwner()) &&
		Cast<APlayerController>(GetOwner())->IsLocalPlayerController())
		ProfileCatalog = LoadObject<UProject_JUIProfileCatalog>(
			nullptr, TEXT("/Game/UI/DA_ProjectJUIProfiles.DA_ProjectJUIProfiles"));
	RefreshSources();
#if !UE_BUILD_SHIPPING
	if (GetWorld() && GetNetMode() == NM_Standalone && FParse::Param(FCommandLine::Get(), TEXT("ProjectJUIRuntimeSmoke")))
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJUIProfile")))
			GetWorld()->GetTimerManager().SetTimer(RuntimeSmokeTimer, this, &ThisClass::PrepareRuntimeProfile, 2.f, false);
		else
		GetWorld()->GetTimerManager().SetTimer(RuntimeSmokeTimer, this, &ThisClass::RunRuntimeSmoke, 8.f, false);
	}
#endif
}
void UProject_JPlayerUIComponent::QueueSkillRefresh()
{
	if (GetWorld() && !bEnding && !SkillsRefreshTimer.IsValid())
		SkillsRefreshTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(
			[WeakThis = TWeakObjectPtr<UProject_JPlayerUIComponent>(this)]()
			{
				if (auto *Self = WeakThis.Get())
				{
					Self->SkillsRefreshTimer.Invalidate();
					Self->RefreshPresentation();
				}
			});
}
void UProject_JPlayerUIComponent::RefreshSources()
{
	if (bEnding || !HasBegunPlay())
		return;
	if (auto *PC = Cast<APlayerController>(GetOwner());
		PC && PC->IsLocalPlayerController() && GetNetMode() != NM_DedicatedServer)
	{
		if (!Screen)
		{
			PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::OnPawnChanged);
			InventoryModel = NewObject<UProject_JInventoryViewModel>(this);
			StatusEffects = NewObject<UProject_JStatusEffectModel>(this);
			StatusEffects->OnChanged.AddUObject(this, &ThisClass::OnStatusEffectsChanged);
			if (FSlateApplication::IsInitialized())
				ActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddWeakLambda(this, [this](bool bActive)
				{
					if (!bActive) { ReleaseAllSkills(); if (auto *Controller = Cast<APlayerController>(GetOwner()); Controller && Controller->PlayerInput) Controller->PlayerInput->FlushPressedKeys(); }
				});
			InventoryModel->OnChanged.AddDynamic(this, &ThisClass::OnInventoryPresentationChanged);
			Screen = CreateWidget<UProject_JPlayerHUDWidget>(PC, ScreenClass);
			if (Screen)
			{
				Screen->AddToPlayerScreen(20);
				Screen->InitializeScreen(this);
			}
			if (SkillSlots.IsEmpty())
			{
				const auto &Tags = FProject_JGameplayTags::Get();
				SkillSlots = {{Tags.InputTag_Weapon_LMB, FText::FromString(TEXT("LMB"))},
							  {Tags.InputTag_Weapon_RMB, FText::FromString(TEXT("RMB"))},
							  {Tags.InputTag_Skill_Q, FText::FromString(TEXT("Q"))},
							  {Tags.InputTag_Skill_R, FText::FromString(TEXT("R"))},
							  {Tags.InputTag_Skill_T, FText::FromString(TEXT("T"))}};
			}
		}
	}
	auto *PC = Cast<APlayerController>(GetOwner());
	if (bEnding || !Screen || !InventoryModel || !PC || !PC->IsLocalPlayerController())
		return;
	auto *State = PC->GetPlayerState<AProject_JPlayerState>();
	auto *ASC = State ? State->GetProjectJAbilitySystemComponent() : nullptr;
	if (BoundASC.Get() != ASC || BoundState.Get() != State)
	{
		ReleaseAllSkills();
		ClearSources();
		BoundASC = ASC;
		if (StatusEffects) StatusEffects->Bind(ASC);
		BoundState = State;
		if (State)
		{
			State->OnCharacterIdentityChanged.AddUObject(this, &ThisClass::QueueSkillRefresh);
			BoundQuests = State->FindComponentByClass<UProject_JQuestComponent>();
			if (BoundQuests.IsValid())
				BoundQuests->OnChanged.AddUObject(this, &ThisClass::OnQuestsChanged);
		}
		if (ASC)
		{
			ASC->AbilitySpecDirtiedCallbacks.AddWeakLambda(this, [this](const FGameplayAbilitySpec &)
														   { QueueSkillRefresh(); });
			ASC->AbilityActivatedCallbacks.AddWeakLambda(this, [this](UGameplayAbility *) { QueueSkillRefresh(); });
			ASC->AbilityCommittedCallbacks.AddWeakLambda(this, [this](UGameplayAbility *) { QueueSkillRefresh(); });
			ASC->AbilityEndedCallbacks.AddWeakLambda(this, [this](UGameplayAbility *) { QueueSkillRefresh(); });
			ASC->RegisterGenericGameplayTagEvent().AddWeakLambda(this,
																 [this](FGameplayTag, int32) { QueueSkillRefresh(); });
			BoundProgression = State->FindComponentByClass<UProject_JProgressionComponent>();
			if (BoundProgression.IsValid())
				BoundProgression->OnChanged.AddUObject(this, &ThisClass::QueueSkillRefresh);
		}
	}
	InventoryModel->Bind(State ? State->GetInventoryComponent() : nullptr,
						 State ? State->GetEquipmentManagerComponent() : nullptr);
	RefreshPresentation();
	Screen->RefreshQuests();
}
void UProject_JPlayerUIComponent::ReleaseAllSkills()
{
	for (const auto &Press : PressedSkills)
		if (PressedPawn.IsValid())
			PressedPawn->HandleSkillInputTagReleased(Press.Value);
	PressedSkills.Reset();
	PressedPawn.Reset();
}
void UProject_JPlayerUIComponent::BindResourceAttributes()
{
	if (!BoundASC.IsValid())
		return;
	for (const auto &Binding : AttributeHandles)
		BoundASC->GetGameplayAttributeValueChangeDelegate(Binding.Key).Remove(Binding.Value);
	AttributeHandles.Reset();
	TSet<FGameplayAttribute> Attributes = {UProject_JAttributeSet::GetAttackPowerAttribute(),
										   UProject_JAttributeSet::GetDefenseAttribute()};
	for (const auto &Resource : PresentedResources)
	{
		Attributes.Add(Resource.Current);
		Attributes.Add(Resource.Maximum);
	}
	for (const auto &Attribute : Attributes)
		if (Attribute.IsValid() && BoundASC->HasAttributeSetForAttribute(Attribute))
			AttributeHandles.Add(Attribute, BoundASC->GetGameplayAttributeValueChangeDelegate(Attribute).AddUObject(
												this, &ThisClass::UpdateAttributes));
}
void UProject_JPlayerUIComponent::RefreshPresentation()
{
	if (!Screen || bEnding)
		return;
	UIContext = {};
	if (BoundState.IsValid())
		UIContext.ClassId = BoundState->GetPublicClassId();
	if (BoundProgression.IsValid())
	{
		const auto &Progression = BoundProgression->GetState();
		if (Progression.ClassDefinition)
			UIContext.ClassId = Progression.ClassDefinition->ClassId;
		if (Progression.Advancement)
			UIContext.AdvancementId = Progression.Advancement->AdvancementId;
	}
	if (BoundASC.IsValid())
		BoundASC->GetOwnedGameplayTags(UIContext.OwnedTags);
	auto *NextProfile = ProfileCatalog ? ProfileCatalog->Resolve(UIContext) : nullptr;
	if (!bPresentationInitialized || NextProfile != ActiveProfile)
	{
		ReleaseAllSkills();
		ActiveProfile = NextProfile;
		bPresentationInitialized = true;
		PresentedSkills = ActiveProfile && ActiveProfile->bOverrideSkills ? ActiveProfile->Skills : SkillSlots;
		PresentedResources =
			ActiveProfile && ActiveProfile->bOverrideResources
				? ActiveProfile->Resources
				: TArray<FProject_JUIResourceDefinition>{
					  {FName(TEXT("Health")), NSLOCTEXT("ProjectJUI", "Health", "체력"),
					   UProject_JAttributeSet::GetHealthAttribute(), UProject_JAttributeSet::GetMaxHealthAttribute(),
					   FLinearColor(0.75f, 0.12f, 0.15f)},
					  {FName(TEXT("Mana")), NSLOCTEXT("ProjectJUI", "Mana", "마나"),
					   UProject_JAttributeSet::GetManaAttribute(), UProject_JAttributeSet::GetMaxManaAttribute(),
					   FLinearColor(0.1f, 0.4f, 0.85f)}};
		BindResourceAttributes();
		Screen->ApplyProfile(ActiveProfile);
	}
	Screen->SetLayoutIdentity(BoundState.IsValid() ? BoundState->GetCharacterId().Value : FGuid(),
							  ActiveProfile ? ActiveProfile->ProfileId : FName(TEXT("Default")));
	LoadPreferences();
	UpdateHUD();
	UpdateSkills();
}
void UProject_JPlayerUIComponent::BindMenuInput(UInputComponent *Input)
{
	if (!Input) return;
	if (MenuInput.IsValid()) MenuInput->KeyBindings.RemoveAll([this](const FInputKeyBinding &B) { return B.KeyDelegate.IsBoundToObject(this); });
	MenuInput = Input;
	if (auto *PC = Cast<APlayerController>(GetOwner()); PC && PC->GetLocalPlayer())
	{
		InputKeys = PC->GetLocalPlayer()->GetSubsystem<UProject_JUILayoutSettings>()->ReadKeys();
		if (auto *Enhanced = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			Enhanced->ControlMappingsRebuiltDelegate.AddUniqueDynamic(this, &ThisClass::RebuildKeyBindings);
	}
	RebuildKeyBindings();
}
void UProject_JPlayerUIComponent::RebuildKeyBindings()
{
	if (!MenuInput.IsValid()) return;
	MenuInput->KeyBindings.RemoveAll([this](const FInputKeyBinding &B) { return B.KeyDelegate.IsBoundToObject(this); });
	for (int32 Index = 0; Index < InputKeys.Keys.Num(); ++Index)
	{
		if (IsKeyReserved(InputKeys.Keys[Index])) continue;
		for (EInputEvent Event : {IE_Pressed, IE_Released})
		{
			if (Index >= 10 && Event == IE_Released) continue;
			FInputKeyBinding Binding(FInputChord(InputKeys.Keys[Index]), Event);
			Binding.bConsumeInput = true;
			Binding.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this, Index, Event]()
			{
				if (Index >= 10) ToggleWindow(FProject_JUIKeys::WindowAt(Index));
				else if (Event == IE_Pressed) PressSkill(Index);
				else ReleaseSkill(Index);
			});
			MenuInput->KeyBindings.Add(MoveTemp(Binding));
		}
	}
}
bool UProject_JPlayerUIComponent::IsKeyReserved(FKey Key) const
{
	const auto *PC = Cast<APlayerController>(GetOwner());
	if (!PC) return true;
	if (const auto *Enhanced = Cast<UEnhancedPlayerInput>(PC->PlayerInput))
		for (const auto &Mapping : Enhanced->GetEnhancedActionMappingsView())
			if (Mapping.Key == Key) return true;
	for (const auto *Input : {MenuInput.Get(), PC->GetPawn() ? PC->GetPawn()->InputComponent.Get() : nullptr})
		if (Input) for (const auto &Binding : Input->KeyBindings)
			if (Binding.Chord.Key == Key && !Binding.KeyDelegate.IsBoundToObject(this)) return true;
	return false;
}
bool UProject_JPlayerUIComponent::RebindKey(int32 Index, FKey Key, FText &Reason)
{
	if (!InputKeys.Keys.IsValidIndex(Index) || !FProject_JUIKeys::IsAllowed(Key))
	{
		Reason = NSLOCTEXT("ProjectJUI", "KeyUnsupported", "숫자, I/K/O/U, F1~F12 키를 사용하세요. Escape는 취소입니다.");
		return false;
	}
	if (IsKeyReserved(Key))
	{
		Reason = NSLOCTEXT("ProjectJUI", "KeyReserved", "현재 이동·전투 또는 다른 기능이 사용하는 키입니다.");
		return false;
	}
	ReleaseAllSkills();
	if (auto *PC = Cast<APlayerController>(GetOwner()); PC && PC->PlayerInput) PC->PlayerInput->FlushPressedKeys();
	InputKeys.Rebind(Index, Key);
	RebuildKeyBindings();
	if (auto *PC = Cast<APlayerController>(GetOwner()); PC && PC->GetLocalPlayer())
		PC->GetLocalPlayer()->GetSubsystem<UProject_JUILayoutSettings>()->WriteKeys(InputKeys);
	UpdateSkills();
	Reason = NSLOCTEXT("ProjectJUI", "KeyChanged", "키 설정 적용됨 · 겹치는 UI 키는 서로 교환됩니다.");
	return true;
}
void UProject_JPlayerUIComponent::ResetKeys()
{
	ReleaseAllSkills();
	if (auto *PC = Cast<APlayerController>(GetOwner()); PC && PC->PlayerInput) PC->PlayerInput->FlushPressedKeys();
	InputKeys = FProject_JUIKeys();
	RebuildKeyBindings();
	if (auto *PC = Cast<APlayerController>(GetOwner()); PC && PC->GetLocalPlayer())
		PC->GetLocalPlayer()->GetSubsystem<UProject_JUILayoutSettings>()->WriteKeys(InputKeys);
	UpdateSkills();
}
bool UProject_JPlayerUIComponent::HandleMenuKey(FKey Key)
{
	const int32 Index = InputKeys.Keys.IndexOfByKey(Key);
	if (Index < 10 || IsKeyReserved(Key)) return false;
	ToggleWindow(FProject_JUIKeys::WindowAt(Index));
	return true;
}
void UProject_JPlayerUIComponent::OnPawnChanged(APawn *, APawn *)
{
	ReleaseAllSkills();
	SetObservedTarget(nullptr);
	RebuildKeyBindings();
	RefreshSources();
}
void UProject_JPlayerUIComponent::ClearSources()
{
	if (StatusEffects) StatusEffects->Unbind();
	if (BoundASC.IsValid())
	{
		for (const auto &Binding : AttributeHandles)
			BoundASC->GetGameplayAttributeValueChangeDelegate(Binding.Key).Remove(Binding.Value);
		BoundASC->AbilitySpecDirtiedCallbacks.RemoveAll(this);
		BoundASC->AbilityActivatedCallbacks.RemoveAll(this);
		BoundASC->AbilityCommittedCallbacks.RemoveAll(this);
		BoundASC->AbilityEndedCallbacks.RemoveAll(this);
		BoundASC->RegisterGenericGameplayTagEvent().RemoveAll(this);
	}
	if (BoundQuests.IsValid())
		BoundQuests->OnChanged.RemoveAll(this);
	BoundQuests.Reset();
	if (BoundProgression.IsValid())
		BoundProgression->OnChanged.RemoveAll(this);
	if (BoundState.IsValid())
		BoundState->OnCharacterIdentityChanged.RemoveAll(this);
	AttributeHandles.Reset();
	BoundASC.Reset();
	BoundProgression.Reset();
	BoundState.Reset();
	bPresentationInitialized = false;
}
void UProject_JPlayerUIComponent::UpdateAttributes(const FOnAttributeChangeData &)
{
	UpdateHUD();
	QueueSkillRefresh();
}
void UProject_JPlayerUIComponent::UpdateHUD()
{
	if (!Screen)
		return;
	auto *PC = Cast<APlayerController>(GetOwner());
	auto *State = PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
	const auto *Attributes = State ? State->GetProjectJAttributeSet() : nullptr;
	if (!Attributes)
	{
		const FText Connecting = NSLOCTEXT("ProjectJUI", "Connecting", "캐릭터 연결 중…");
		Screen->UpdateAttributes(Connecting);
		Screen->UpdateCharacter(UIContext, {}, Connecting);
		return;
	}
	const int32 Level =
		BoundProgression.IsValid() ? BoundProgression->GetState().Level : State->GetPublicCharacterLevel();
	Screen->UpdateAttributes(FText::Format(NSLOCTEXT("ProjectJUI", "CharacterHUD", "Lv.{0}"), FText::AsNumber(Level),
										   ActiveProfile ? ActiveProfile->DisplayName : FText::GetEmpty()));
	Screen->UpdateExperience(BoundProgression.IsValid() ? BoundProgression->GetExperience() : 0,
							 BoundProgression.IsValid() ? BoundProgression->GetNextLevelExperience() : 0);
	Screen->UpdateCharacter(
		UIContext, ProjectJUI::ReadResources(BoundASC.Get(), PresentedResources),
		FText::Format(NSLOCTEXT("ProjectJUI", "CharacterStats", "Lv.{0}  ·  공격력 {1}  ·  방어력 {2}  ·  {3}"),
					  FText::AsNumber(Level), FText::AsNumber(Attributes->GetAttackPower()),
					  FText::AsNumber(Attributes->GetDefense()),
					  ActiveProfile ? ActiveProfile->DisplayName : FText::GetEmpty()));
}
void UProject_JPlayerUIComponent::UpdateSkills()
{
	CSV_SCOPED_TIMING_STAT(ProjectJUI, QuickSlotPresentation);
	if (!Screen || !GetWorld() || bEnding)
		return;
	TArray<FProject_JQuickSlotState> States;
	bool bCountdown = false;
	for (int32 Index = 0; Index < HUDPreferences.Slots.Num(); ++Index)
	{
		FProject_JQuickSlotState State;
		State.Binding = HUDPreferences.Slots[Index];
		State.KeyHint = InputKeys.Keys.IsValidIndex(Index) ? InputKeys.Keys[Index].GetDisplayName() : FText::GetEmpty();
		if (State.Binding.Kind == EProject_JQuickSlotKind::Skill)
		{
			const auto *Slot = PresentedSkills.FindByPredicate(
				[&](const auto &Candidate) { return Candidate.InputTag == State.Binding.InputTag; });
			if (Slot)
			{
				State.Name = Slot->Label;
				State.Icon = Slot->Icon;
			}
			if (Slot && BoundASC.IsValid())
			{
				FScopedAbilityListLock Lock(*BoundASC.Get());
				float Duration = 0;
				for (const auto &Spec : BoundASC->GetActivatableAbilities())
					if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(Slot->InputTag))
					{
						State.bAvailable |=
							Spec.Ability->CanActivateAbility(Spec.Handle, BoundASC->AbilityActorInfo.Get());
						float Time = 0, Total = 0;
						Spec.Ability->GetCooldownTimeRemainingAndDuration(Spec.Handle, BoundASC->AbilityActorInfo.Get(),
																		  Time, Total);
						State.Cooldown = FMath::Max(State.Cooldown, Time);
						Duration = FMath::Max(Duration, Total);
					}
				State.CooldownFraction = Duration > 0 ? FMath::Clamp(State.Cooldown / Duration, 0.f, 1.f) : 0;
			}
		}
		else if (State.Binding.Kind == EProject_JQuickSlotKind::Item && InventoryModel)
		{
			const auto *Inventory = BoundState.IsValid() ? BoundState->GetInventoryComponent() : nullptr;
			if (const auto *Summary = InventoryModel->FindItemSummary(State.Binding.ItemId); Summary && Summary->Definition.IsValid())
			{
				const auto *Definition = Summary->Definition.Get();
				State.Name = Definition->ItemName;
				State.Icon = Definition->Icon;
				State.Quantity = Summary->Quantity;
				State.bAvailable = Summary->UsableInstance.IsValid() && !InventoryModel->bPending;
				if (const auto *Def = Cast<UProject_JConsumableDefinition>(Definition); Def && Inventory)
				{
					State.Cooldown = Inventory->GetUseCooldownRemaining();
					State.CooldownFraction = Def->CooldownSeconds > 0 ? FMath::Clamp(State.Cooldown / Def->CooldownSeconds, 0.f, 1.f) : 0;
				}
			}
			State.bAvailable &= State.Cooldown <= 0;
		}
		bCountdown |= State.Cooldown > 0;
		if (!State.bAvailable && State.Binding.Kind != EProject_JQuickSlotKind::Empty)
			State.UnavailableReason = State.Cooldown > 0 ? NSLOCTEXT("ProjectJUI", "SlotCooldownReason", "재사용 대기 중")
				: State.Binding.Kind == EProject_JQuickSlotKind::Item
				? NSLOCTEXT("ProjectJUI", "SlotItemReason", "보유량·잠금·서버 처리 상태를 확인하세요")
				: NSLOCTEXT("ProjectJUI", "SlotSkillReason", "습득·자원·전투 상태 조건을 확인하세요");
		States.Add(State);
	}
	Screen->UpdateQuickSlots(States);
	if (bCountdown)
		GetWorld()->GetTimerManager().SetTimer(SkillsTimer, this, &ThisClass::UpdateSkills, 0.2f, false);
	else
		GetWorld()->GetTimerManager().ClearTimer(SkillsTimer);
}
void UProject_JPlayerUIComponent::PressSkill(int32 Index)
{
	if (bMenuOpen || !HUDPreferences.Slots.IsValidIndex(Index) || PressedSkills.Contains(Index))
		return;
	const auto &Binding = HUDPreferences.Slots[Index];
	if (Binding.Kind == EProject_JQuickSlotKind::Item)
	{
		if (InventoryModel && !InventoryModel->bPending) InventoryModel->UseByItemId(Binding.ItemId);
	}
	else if (Binding.Kind == EProject_JQuickSlotKind::Skill &&
			 PresentedSkills.ContainsByPredicate([&](const auto &Slot) { return Slot.InputTag == Binding.InputTag; }))
	{
		auto *PC = Cast<APlayerController>(GetOwner());
		auto *Pawn = PC ? Cast<AProject_JPlayerCharacter>(PC->GetPawn()) : nullptr;
		if (Pawn)
		{
			PressedPawn = Pawn;
			PressedSkills.Add(Index, Binding.InputTag);
			Pawn->HandleSkillInputTagPressed(Binding.InputTag);
			QueueSkillRefresh();
		}
	}
}
void UProject_JPlayerUIComponent::ReleaseSkill(int32 Index)
{
	if (const FGameplayTag *Tag = PressedSkills.Find(Index); Tag && PressedPawn.IsValid())
		PressedPawn->HandleSkillInputTagReleased(*Tag);
	PressedSkills.Remove(Index);
	UpdateSkills();
}
void UProject_JPlayerUIComponent::OnInventoryPresentationChanged()
{
	if (Screen)
	{
		Screen->RefreshInventory();
		UpdateSkills();
	}
}
void UProject_JPlayerUIComponent::ToggleMenu()
{
	ToggleWindow(TEXT("Bag"));
}
void UProject_JPlayerUIComponent::ToggleWindow(FName Id)
{
	if (Screen)
		Screen->ToggleWindow(Id);
}
void UProject_JPlayerUIComponent::SetMenuOpen(bool bOpen)
{
	auto *PC = Cast<APlayerController>(GetOwner());
	if (!PC || !Screen || bMenuOpen == bOpen)
		return;
	bMenuOpen = bOpen;
	if (PC->PlayerInput)
		PC->PlayerInput->FlushPressedKeys();
	if (bOpen)
	{
		ReleaseAllSkills();
		bPreviousCursor = PC->bShowMouseCursor;
		PC->bShowMouseCursor = true;
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
		if (auto *Pawn = Cast<AProject_JPlayerCharacter>(PC->GetPawn()))
			Pawn->StopSprint();
		RefreshSources();
		Screen->SetMenuOpen(true);
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Screen->TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}
	else
	{
		ReleaseAllSkills();
		Screen->SetMenuOpen(false);
		PC->bShowMouseCursor = bPreviousCursor;
		PC->SetIgnoreMoveInput(false);
		PC->SetIgnoreLookInput(false);
		PC->SetInputMode(FInputModeGameOnly());
	}
}
void UProject_JPlayerUIComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	SetMenuOpen(false);
	ReleaseAllSkills();
	bEnding = true;
	if (FSlateApplication::IsInitialized()) FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle);
	if (MenuInput.IsValid()) MenuInput->KeyBindings.RemoveAll([this](const FInputKeyBinding &Binding) { return Binding.KeyDelegate.IsBoundToObject(this); });
	MenuInput.Reset();
	if (auto *PC = Cast<APlayerController>(GetOwner()))
	{
		PC->OnPossessedPawnChanged.RemoveAll(this);
		if (PC->GetLocalPlayer()) if (auto *Enhanced = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			Enhanced->ControlMappingsRebuiltDelegate.RemoveAll(this);
	}
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(SkillsTimer);
		GetWorld()->GetTimerManager().ClearTimer(SkillsRefreshTimer);
		GetWorld()->GetTimerManager().ClearTimer(TargetTimer);
		GetWorld()->GetTimerManager().ClearTimer(RuntimeSmokeTimer);
	}
	ClearSources();
	if (InventoryModel)
	{
		InventoryModel->Unbind();
		InventoryModel->OnChanged.RemoveAll(this);
	}
	if (Screen)
		Screen->RemoveFromParent();
	Screen = nullptr;
	InventoryModel = nullptr;
	StatusEffects = nullptr;
	ObservedTarget.Reset();
	Super::EndPlay(Reason);
}

void UProject_JPlayerUIComponent::LoadPreferences()
{
	const FString Key =
		UProject_JUILayoutSave::MakeKey(BoundState.IsValid() ? BoundState->GetCharacterId().Value : FGuid(),
										ActiveProfile ? ActiveProfile->ProfileId : FName(TEXT("Default")));
	if (PreferencesKey == Key && HUDPreferences.Slots.Num() == 10)
		return;
	ReleaseAllSkills();
	PreferencesKey = Key;
	HUDPreferences = FProject_JHUDPreferences();
	HUDPreferences.Slots.SetNum(10);
	for (int32 I = 0; I < FMath::Min(10, PresentedSkills.Num()); ++I)
	{
		HUDPreferences.Slots[I].Kind = EProject_JQuickSlotKind::Skill;
		HUDPreferences.Slots[I].InputTag = PresentedSkills[I].InputTag;
	}
	if (auto *PC = Cast<APlayerController>(GetOwner()); PC && PC->GetLocalPlayer())
		PC->GetLocalPlayer()->GetSubsystem<UProject_JUILayoutSettings>()->ReadHUD(Key, HUDPreferences);
	Screen->ApplyPreferences(HUDPreferences);
}
void UProject_JPlayerUIComponent::StorePreferences()
{
	if (auto *PC = Cast<APlayerController>(GetOwner()); PC && PC->GetLocalPlayer())
		PC->GetLocalPlayer()->GetSubsystem<UProject_JUILayoutSettings>()->WriteHUD(PreferencesKey, HUDPreferences);
	if (Screen)
		Screen->ApplyPreferences(HUDPreferences);
	UpdateSkills();
	UpdateHUD();
}
FProject_JQuickSlotBinding UProject_JPlayerUIComponent::GetQuickBinding(int32 Index) const
{
	if (HUDPreferences.Slots.IsValidIndex(Index))
		return HUDPreferences.Slots[Index];
	FProject_JQuickSlotBinding Binding;
	if (Index >= 10 && PresentedSkills.IsValidIndex(Index - 10))
	{
		Binding.Kind = EProject_JQuickSlotKind::Skill;
		Binding.InputTag = PresentedSkills[Index - 10].InputTag;
	}
	return Binding;
}
bool UProject_JPlayerUIComponent::BindItemSlot(int32 Index, const UProject_JInventoryEntry *Entry)
{
	if (HUDPreferences.bLocked || !HUDPreferences.Slots.IsValidIndex(Index) || !Entry ||
		Entry->Model != InventoryModel || !Entry->Item.ItemDef || Entry->bEquipmentEntry ||
		!Cast<UProject_JConsumableDefinition>(Entry->Item.ItemDef) || Entry->Item.ItemDef->ItemId.IsNone())
		return false;
	FProject_JItemInstanceData Current;
	if (!BoundState.IsValid() ||
		!BoundState->GetInventoryComponent()->FindItemInstance(Entry->Item.InstanceId, Current) ||
		Current.ItemDef != Entry->Item.ItemDef || Current.bIsLocked || Current.bIsEquipped)
		return false;
	ReleaseAllSkills();
	auto &Binding = HUDPreferences.Slots[Index];
	Binding = FProject_JQuickSlotBinding();
	Binding.Kind = EProject_JQuickSlotKind::Item;
	Binding.ItemId = Current.ItemDef->ItemId;
	StorePreferences();
	return true;
}
bool UProject_JPlayerUIComponent::MoveQuickSlot(int32 From, int32 To, const FString &Identity,
												const FProject_JQuickSlotBinding &Original)
{
	if (HUDPreferences.bLocked || Identity != PreferencesKey || From < 0 || !HUDPreferences.Slots.IsValidIndex(To))
		return false;
	const auto Current = GetQuickBinding(From);
	if (Current.Kind != Original.Kind || Current.InputTag != Original.InputTag || Current.ItemId != Original.ItemId)
		return false;
	if (Current.Kind == EProject_JQuickSlotKind::Empty)
		return false;
	ReleaseAllSkills();
	if (From >= 10)
		HUDPreferences.Slots[To] = Current;
	else
		Swap(HUDPreferences.Slots[From], HUDPreferences.Slots[To]);
	StorePreferences();
	return true;
}
void UProject_JPlayerUIComponent::ClearQuickSlot(int32 Index)
{
	if (!HUDPreferences.bLocked && HUDPreferences.Slots.IsValidIndex(Index))
	{
		ReleaseAllSkills();
		HUDPreferences.Slots[Index] = FProject_JQuickSlotBinding();
		StorePreferences();
	}
}
void UProject_JPlayerUIComponent::ChangePreferences(FName Command)
{
	if (Command == TEXT("Lock"))
		HUDPreferences.bLocked = !HUDPreferences.bLocked;
	else if (Command == TEXT("Values"))
		HUDPreferences.bShowResourceValues = !HUDPreferences.bShowResourceValues;
	else if (Command == TEXT("ScaleUp"))
		HUDPreferences.Scale = FMath::Min(1.5f, HUDPreferences.Scale + 0.1f);
	else if (Command == TEXT("ScaleDown"))
		HUDPreferences.Scale = FMath::Max(0.75f, HUDPreferences.Scale - 0.1f);
	else if (Command == TEXT("Reset"))
	{
		ReleaseAllSkills();
		HUDPreferences = FProject_JHUDPreferences();
		HUDPreferences.Slots.SetNum(10);
		for (int32 I = 0; I < FMath::Min(10, PresentedSkills.Num()); ++I)
		{
			HUDPreferences.Slots[I].Kind = EProject_JQuickSlotKind::Skill;
			HUDPreferences.Slots[I].InputTag = PresentedSkills[I].InputTag;
		}
	}
	StorePreferences();
}
UProject_JQuestComponent *UProject_JPlayerUIComponent::GetQuestComponent() const
{
	return BoundQuests.Get();
}

UProject_JHUDStyle *UProject_JPlayerUIComponent::GetHUDStyle() const
{
	return Screen ? Screen->HUDStyle.Get() : nullptr;
}

void UProject_JPlayerUIComponent::OnQuestsChanged()
{
	if (Screen)
		Screen->RefreshQuests();
}

void UProject_JPlayerUIComponent::OnStatusEffectsChanged()
{
	if (Screen && StatusEffects) Screen->PresentStatusEffects(StatusEffects->States);
}
void UProject_JPlayerUIComponent::SetObservedTarget(AActor *Target)
{
	const auto *PC = Cast<APlayerController>(GetOwner());
	ObservedTarget = PC && PC->IsLocalPlayerController() && IsValid(Target) && Target->GetWorld() == GetWorld() ? Target : nullptr;
	UpdateObservedTarget();
}
void UProject_JPlayerUIComponent::UpdateObservedTarget()
{
	if (!Screen || !GetWorld() || bEnding) return;
	auto *PC = Cast<APlayerController>(GetOwner());
	const APawn *Pawn = PC ? PC->GetPawn().Get() : nullptr;
	const auto *Interface = Cast<IAbilitySystemInterface>(ObservedTarget.Get());
	const auto *ASC = Interface ? Interface->GetAbilitySystemComponent() : nullptr;
	const bool bVisible = Pawn && ObservedTarget.IsValid() && !ObservedTarget->IsActorBeingDestroyed() &&
		Pawn->GetDistanceTo(ObservedTarget.Get()) <= 3000.f && PC->LineOfSightTo(ObservedTarget.Get()) &&
		ASC && ASC->HasAttributeSetForAttribute(UProject_JAttributeSet::GetHealthAttribute()) &&
		ASC->HasAttributeSetForAttribute(UProject_JAttributeSet::GetMaxHealthAttribute());
	if (!bVisible)
	{
		ObservedTarget.Reset();
		Screen->PresentTarget(FText::GetEmpty(), 0, 0);
		GetWorld()->GetTimerManager().ClearTimer(TargetTimer);
		return;
	}
	const float Current = ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
	const float Maximum = ASC->GetNumericAttribute(UProject_JAttributeSet::GetMaxHealthAttribute());
	if (!FMath::IsFinite(Current) || !FMath::IsFinite(Maximum) || Current <= 0 || Maximum <= 0) { SetObservedTarget(nullptr); return; }
	const auto *TargetPawn = Cast<APawn>(ObservedTarget.Get());
	const auto *State = TargetPawn ? TargetPawn->GetPlayerState() : Cast<APlayerState>(ObservedTarget.Get());
	Screen->PresentTarget(State && !State->GetPlayerName().IsEmpty() ? FText::FromString(State->GetPlayerName()) : NSLOCTEXT("ProjectJUI", "Target", "대상"), Current, Maximum);
	GetWorld()->GetTimerManager().SetTimer(TargetTimer, this, &ThisClass::UpdateObservedTarget, .2f, false);
}
