#include "UI/Project_JPlayerUIComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "UI/Project_JPlayerHUDWidget.h"
#include "Components/Project_JCraftingJobsComponent.h"
#include "UI/Project_JInventoryViewModel.h"
#include "UI/Project_JUILayoutSettings.h"
#include "Game/Project_JQuestComponent.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Inventory/Project_JItemDefinition.h"
#include "Engine/LocalPlayer.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JPlayerCharacter.h"
#include "Components/Project_JSkillInputRouterComponent.h"
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
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateUser.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Widgets/SViewport.h"
#include "UI/Project_JCursorInput.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Misc/ScopeExit.h"
CSV_DEFINE_CATEGORY(ProjectJUI, true);

namespace
{
class FProject_JCursorInputProcessor final : public IInputProcessor
{
public:
	explicit FProject_JCursorInputProcessor(UProject_JPlayerUIComponent* InOwner) : Owner(InOwner) {}
	void Tick(float, FSlateApplication& App, TSharedRef<ICursor>) override
	{
		auto* PC = Owner.IsValid() ? Cast<APlayerController>(Owner->GetOwner()) : nullptr;
		auto* Local = PC ? PC->GetLocalPlayer() : nullptr;
		if (!Local || !Local->GetSlateUser() || !IsOurViewport(App, Local->GetSlateUser()->GetUserIndex())) Gesture.Reset();
	}
	bool IsOurViewport(FSlateApplication& App, int32 UserIndex) const
	{
		auto* UI = Owner.Get();
		auto* PC = UI ? Cast<APlayerController>(UI->GetOwner()) : nullptr;
		auto* Local = PC ? PC->GetLocalPlayer() : nullptr;
		if (!Local || !Local->ViewportClient || !Local->GetSlateUser() ||
			Local->GetSlateUser()->GetUserIndex() != UserIndex ||
			Local->ViewportClient->GetWindow() != App.GetActiveTopLevelWindow()) return false;
		const auto Viewport = Local->ViewportClient->GetGameViewportWidget();
		return Viewport && Viewport->HasAnyUserFocusOrFocusedDescendants();
	}
	bool HandleKeyDownEvent(FSlateApplication& App, const FKeyEvent& Event) override
	{
		if (IsOurViewport(App, Event.GetUserIndex()))
			Gesture.KeyDown(Event.GetKey(), Event.IsRepeat(), Event.IsControlDown() || Event.IsShiftDown() || Event.IsCommandDown() || Event.IsRightAltDown());
		else Gesture.Reset();
		return false; // Enhanced Input and widget modifier shortcuts still receive the event.
	}
	bool HandleKeyUpEvent(FSlateApplication& App, const FKeyEvent& Event) override
	{
		const bool bOurViewport = IsOurViewport(App, Event.GetUserIndex());
		if (Gesture.KeyUp(Event.GetKey()) && bOurViewport && Owner.IsValid()) Owner->ToggleCursorMode();
		return false;
	}
	bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent&) override { Gesture.MouseDown(); return false; }
	void Reset() { Gesture.Reset(); }
private:
	TWeakObjectPtr<UProject_JPlayerUIComponent> Owner;
	FProject_JCursorGesture Gesture;
};
}

UProject_JPlayerUIComponent::UProject_JPlayerUIComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	ScreenClass = UProject_JPlayerHUDWidget::StaticClass();
}
void UProject_JPlayerUIComponent::BeginPlay()
{
	bEnding = false;
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
	++SkillsPresentationRevision;
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
			// A failed creation must own no input processor, model or subscription.
			// A later retry publishes those resources only after a screen exists.
			Screen = CreateWidget<UProject_JPlayerHUDWidget>(PC, ScreenClass);
			if (!Screen) { return; }
			PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &ThisClass::OnPawnChanged);
			InventoryModel = NewObject<UProject_JInventoryViewModel>(this);
			StatusEffects = NewObject<UProject_JStatusEffectModel>(this);
			StatusEffects->OnChanged.AddUObject(this, &ThisClass::OnStatusEffectsChanged);
			if (FSlateApplication::IsInitialized())
			{
				CursorInputProcessor = MakeShared<FProject_JCursorInputProcessor>(this);
				FSlateApplication::Get().RegisterInputPreProcessor(CursorInputProcessor);
				ActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddWeakLambda(this, [this](bool bActive)
				{
					if (!bActive)
					{
						StaticCastSharedPtr<FProject_JCursorInputProcessor>(CursorInputProcessor)->Reset();
						ReleaseAllSkills();
						if (auto *Controller = Cast<APlayerController>(GetOwner()))
						{
							if (auto* Pawn = Cast<AProject_JPlayerCharacter>(Controller->GetPawn())) Pawn->StopSprint();
							if (Controller->PlayerInput) Controller->PlayerInput->FlushPressedKeys();
						}
					}
				});
			}
			InventoryModel->OnChanged.AddDynamic(this, &ThisClass::OnInventoryPresentationChanged);
			Screen->AddToPlayerScreen(20);
			Screen->InitializeScreen(this);
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
	const FGuid CharacterId = State ? State->GetCharacterId().Value : FGuid();
	if (BoundASC.Get() != ASC || BoundState.Get() != State || BoundCharacterId != CharacterId)
	{
		ResetInteraction();
		if (InventoryModel) InventoryModel->Unbind();
		ClearSources();
		BoundCharacterId = CharacterId;
		BoundASC = ASC;
		if (StatusEffects) StatusEffects->Bind(ASC);
		BoundState = State;
		if (State)
		{
			State->OnCharacterIdentityChanged.AddUObject(this, &ThisClass::OnCharacterIdentityChanged);
			State->OnPartyRosterChanged.AddUObject(this, &ThisClass::OnPartyRosterChanged);
			BoundQuests = State->FindComponentByClass<UProject_JQuestComponent>();
			if (BoundQuests.IsValid())
            {
				BoundQuests->OnChanged.AddUObject(this, &ThisClass::OnQuestsChanged);
                BoundQuests->OnHistoryPageChanged.AddUObject(this, &ThisClass::OnQuestsChanged);
            }
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
	BindCraftingSource(State ? State->GetCraftingJobsComponent() : nullptr);
	InventoryModel->Bind(State ? State->GetInventoryComponent() : nullptr,
						 State ? State->GetEquipmentManagerComponent() : nullptr);
	RefreshPresentation();
	Screen->RefreshQuests();
	OnPartyRosterChanged();
}
void UProject_JPlayerUIComponent::ReleaseAllSkills()
{
	const TStrongObjectPtr<UProject_JPlayerUIComponent> KeepSelf(this);
	const TStrongObjectPtr<AProject_JPlayerCharacter> OriginalPawn(PressedPawn.Get());
	const auto Released = MoveTemp(PressedSkills);
	PressedSkills.Reset();
	PressedPawn.Reset();
	// End this receipt before GAS callbacks. Reentrant presses own the new map
	// and pawn; this operation only releases its captured original inputs.
	for (const auto& Press : Released)
		if (IsValid(OriginalPawn.Get()) && !OriginalPawn->IsActorBeingDestroyed())
			OriginalPawn->HandleSkillInputTagReleasedFromSource(Press.Value.InputTag, this, Press.Value.Receipt);
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
	++KeyBindingRevision;
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
	const uint64 Revision = ++KeyBindingRevision;
	if (bEnding || !MenuInput.IsValid()) return;
	MenuInput->KeyBindings.RemoveAll([this](const FInputKeyBinding &B) { return B.KeyDelegate.IsBoundToObject(this); });
	for (int32 Index = 0; Index < InputKeys.Keys.Num(); ++Index)
	{
		if (IsKeyReserved(InputKeys.Keys[Index])) continue;
		const TSharedRef<uint64> KeyReceipt = MakeShared<uint64>(0);
		for (EInputEvent Event : {IE_Pressed, IE_Released})
		{
			if (Index >= 10 && Event == IE_Released) continue;
			FInputKeyBinding Binding(FInputChord(InputKeys.Keys[Index]), Event);
			Binding.bConsumeInput = true;
			Binding.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this, Index, Event, KeyReceipt, Revision]()
			{
				if (bEnding || Revision != KeyBindingRevision) return;
				if (Index >= 10) ToggleWindow(FProject_JUIKeys::WindowAt(Index));
				else if (Event == IE_Pressed) PressSkillWithReceipt(Index, &KeyReceipt.Get());
				else
				{
					const uint64 Receipt = KeyReceipt.Get(); KeyReceipt.Get() = 0;
					if (Receipt) ReleaseSkillReceipt(Index, Receipt);
				}
			});
			MenuInput->KeyBindings.Add(MoveTemp(Binding));
		}
	}
	// Publish the replacement bindings before returning the retired inputs. A
	// callback may press through the new bindings; old cleanup owns only its batch.
	ReleaseAllSkills();
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
	ResetInteraction();
	RebuildKeyBindings();
	RefreshSources();
}
void UProject_JPlayerUIComponent::OnCharacterIdentityChanged()
{
	RefreshSources();
}
void UProject_JPlayerUIComponent::ResetInteraction()
{
	if (CursorInputProcessor) StaticCastSharedPtr<FProject_JCursorInputProcessor>(CursorInputProcessor)->Reset();
	bCursorMode = false;
	ReleaseAllSkills();
	SetObservedTarget(nullptr);
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(SkillsTimer);
		GetWorld()->GetTimerManager().ClearTimer(SkillsRefreshTimer);
	}
	SkillsRefreshTimer.Invalidate();
	if (Screen) Screen->ResetTransientInteraction();
	SetMenuOpen(false);
	ApplyInputMode();
}
void UProject_JPlayerUIComponent::ClearSources()
{
    const TStrongObjectPtr<UProject_JPlayerHUDWidget> PreviousScreen(Screen);
	ClearCraftingSource();
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
    {
		BoundQuests->OnChanged.RemoveAll(this);
        BoundQuests->OnHistoryPageChanged.RemoveAll(this);
    }
	BoundQuests.Reset();
	if (BoundProgression.IsValid())
		BoundProgression->OnChanged.RemoveAll(this);
	if (BoundState.IsValid())
	{
		BoundState->OnCharacterIdentityChanged.RemoveAll(this);
		BoundState->OnPartyRosterChanged.RemoveAll(this);
	}
	AttributeHandles.Reset();
	BoundASC.Reset();
	BoundProgression.Reset();
	BoundState.Reset();
	BoundCharacterId.Invalidate();
	bPresentationInitialized = false;
    // All old binding writes are complete before focus/widget callbacks can install a newer source.
    if (PreviousScreen.IsValid()) PreviousScreen->ClearQuestPresentation(this);
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
		Screen->UpdateExperience(0, 1);
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
	check(IsInGameThread());
	CSV_SCOPED_TIMING_STAT(ProjectJUI, QuickSlotPresentation);
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_UIQuickSlots);
	const uint64 Revision = ++SkillsPresentationRevision;
	if (!IsValid(Screen) || !GetWorld() || bEnding || IsBeingDestroyed())
		return;
	if (bUpdatingSkills) { QueueSkillRefresh(); return; }
	TGuardValue<bool> Updating(bUpdatingSkills, true);
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<AActor> Owner(GetOwner());
	const TStrongObjectPtr<UProject_JPlayerHUDWidget> View(Screen);
	const TStrongObjectPtr<UProject_JAbilitySystemComponent> ASC(BoundASC.Get());
	const TStrongObjectPtr<UProject_JInventoryViewModel> Model(InventoryModel);
	const TWeakObjectPtr<AProject_JPlayerState> StateOwner = BoundState;
	const TWeakObjectPtr<UWorld> World = GetWorld();
	const auto ActorInfo = ASC.IsValid() ? ASC->AbilityActorInfo : nullptr;
	const uint64 ActorInfoRevision = ASC.IsValid() ? ASC->GetActorInfoRevision() : 0;
	const int32 AbilityCount = ASC.IsValid() ? ASC->GetActivatableAbilities().Num() : 0;
	const TWeakObjectPtr<AActor> Avatar = ActorInfo ? ActorInfo->AvatarActor : nullptr;
	const TWeakObjectPtr<AActor> AbilityOwner = ActorInfo ? ActorInfo->OwnerActor : nullptr;
	const bool bHadAvatar = Avatar.IsValid(), bHadAbilityOwner = AbilityOwner.IsValid(), bHadState = StateOwner.IsValid();
	const uint64 KeyRevision = KeyBindingRevision;
	const auto IsCurrent = [&]
	{
		return !bEnding && !IsBeingDestroyed() && Owner.IsValid() && !Owner->IsActorBeingDestroyed()
			&& World.IsValid() && GetWorld() == World.Get() && !World->bIsTearingDown
			&& Revision == SkillsPresentationRevision && KeyRevision == KeyBindingRevision && Screen == View.Get()
			&& BoundASC.Get() == ASC.Get() && BoundState == StateOwner && InventoryModel == Model.Get()
			&& (!bHadState || (StateOwner.IsValid() && !StateOwner->IsActorBeingDestroyed()))
			&& (!bHadAvatar || (Avatar.IsValid() && !Avatar->IsActorBeingDestroyed()))
			&& (!bHadAbilityOwner || (AbilityOwner.IsValid() && !AbilityOwner->IsActorBeingDestroyed()))
			&& (!ASC.IsValid() || (ASC->GetActivatableAbilities().Num() == AbilityCount &&
				ASC->GetActorInfoRevision() == ActorInfoRevision && ASC->AbilityActorInfo == ActorInfo && (!ActorInfo ||
				(ActorInfo->AvatarActor == Avatar && ActorInfo->OwnerActor == AbilityOwner))));
	};
	const auto Abandon = [&]
	{
		CSV_CUSTOM_STAT(ProjectJUI, AbortedQuickSlotSnapshots, 1, ECsvCustomStatOp::Accumulate);
		if (!bEnding && !IsBeingDestroyed() && Owner.IsValid() && !Owner->IsActorBeingDestroyed()
			&& World.IsValid() && !World->bIsTearingDown) QueueSkillRefresh();
	};
	LastSkillSpecVisits = LastSkillAbilityEvaluations = 0;
	if (!IsCurrent()) return;
	ON_SCOPE_EXIT
	{
		CSV_CUSTOM_STAT(ProjectJUI, QuickSlotSpecVisits, LastSkillSpecVisits, ECsvCustomStatOp::Set);
		CSV_CUSTOM_STAT(ProjectJUI, QuickSlotAbilityEvaluations, LastSkillAbilityEvaluations, ECsvCustomStatOp::Set);
	};
	struct FSkillState { bool bAvailable = false; float Cooldown = 0, Duration = 0; };
	TMap<FGameplayTag, FSkillState> Skills;
	const auto Bindings = HUDPreferences.Slots;
	const auto Keys = InputKeys.Keys;
	TArray<FProject_JQuickSlotState> States;
	States.Reserve(Bindings.Num());
	for (int32 Index = 0; Index < Bindings.Num(); ++Index)
	{
		auto& State = States.AddDefaulted_GetRef();
		State.Binding = Bindings[Index];
		State.KeyHint = Keys.IsValidIndex(Index) ? Keys[Index].GetDisplayName() : FText::GetEmpty();
		if (State.Binding.Kind == EProject_JQuickSlotKind::Skill)
		{
			const auto *Slot = PresentedSkills.FindByPredicate(
				[&](const auto &Candidate) { return Candidate.InputTag == State.Binding.InputTag; });
			if (Slot)
			{
				State.Name = Slot->Label;
				State.Icon = Slot->Icon;
				Skills.FindOrAdd(State.Binding.InputTag);
			}
		}
	}
	if (ASC.IsValid() && ActorInfo && !Skills.IsEmpty())
	{
		FScopedAbilityListLock Lock(*ASC.Get());
		for (const auto& Spec : ASC->GetActivatableAbilities())
		{
			++LastSkillSpecVisits;
			if (!Spec.Ability || Spec.PendingRemove) continue;
			TArray<FGameplayTag, TInlineAllocator<10>> Matches;
			for (const auto& Tag : Spec.GetDynamicSpecSourceTags()) if (Skills.Contains(Tag)) Matches.Add(Tag);
			if (Matches.IsEmpty()) continue;
			const TStrongObjectPtr<UGameplayAbility> Ability(Spec.Ability);
			const auto Handle = Spec.Handle;
			++LastSkillAbilityEvaluations;
			const bool bAvailable = Ability->CanActivateAbility(Handle, ActorInfo.Get());
			// Native and Blueprint condition/cooldown queries may replace the view,
			// clear sources, change ActorInfo in place, or collect garbage.
			if (!IsCurrent() || Spec.PendingRemove) { Abandon(); return; }
			float Time = 0, Total = 0;
			Ability->GetCooldownTimeRemainingAndDuration(Handle, ActorInfo.Get(), Time, Total);
			if (!IsCurrent() || Spec.PendingRemove) { Abandon(); return; }
			for (const auto& Tag : Matches)
			{
				auto& Skill = Skills.FindChecked(Tag);
				Skill.bAvailable |= bAvailable;
				Skill.Cooldown = FMath::Max(Skill.Cooldown, Time);
				Skill.Duration = FMath::Max(Skill.Duration, Total);
			}
		}
	}
	// Unlocking GAS can itself deliver deferred ability changes.
	if (!IsCurrent()) { Abandon(); return; }
	bool bCountdown = false;
	for (auto& State : States)
	{
		if (State.Binding.Kind == EProject_JQuickSlotKind::Skill)
		{
			if (const auto* Skill = Skills.Find(State.Binding.InputTag))
			{
				State.bAvailable = Skill->bAvailable; State.Cooldown = Skill->Cooldown;
				State.CooldownFraction = Skill->Duration > 0 ? FMath::Clamp(Skill->Cooldown / Skill->Duration, 0.f, 1.f) : 0;
			}
		}
		else if (State.Binding.Kind == EProject_JQuickSlotKind::Item && Model.IsValid())
		{
			const auto *Inventory = StateOwner.IsValid() ? StateOwner->GetInventoryComponent() : nullptr;
			if (const auto *Summary = Model->FindItemSummary(State.Binding.ItemId); Summary && Summary->Definition.IsValid())
			{
				const auto *Definition = Summary->Definition.Get();
				State.Name = Definition->ItemName;
				State.Icon = Definition->Icon;
				State.Quantity = Summary->Quantity;
				State.bAvailable = Summary->UsableInstance.IsValid() && !Model->bPending;
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
	}
	View->UpdateQuickSlots(States);
	if (!IsCurrent()) { Abandon(); return; }
	if (bCountdown)
		World->GetTimerManager().SetTimer(SkillsTimer, this, &ThisClass::UpdateSkills, 0.2f, false);
	else
		World->GetTimerManager().ClearTimer(SkillsTimer);
}
void UProject_JPlayerUIComponent::PressSkill(int32 Index)
{
	PressSkillWithReceipt(Index, nullptr);
}
void UProject_JPlayerUIComponent::PressSkillWithReceipt(int32 Index, uint64* OutReceipt)
{
	const TStrongObjectPtr<UProject_JPlayerUIComponent> KeepSelf(this);
	if (bEnding || bMenuOpen || !HUDPreferences.Slots.IsValidIndex(Index) || PressedSkills.Contains(Index))
		return;
	const auto Binding = HUDPreferences.Slots[Index];
	if (Binding.Kind == EProject_JQuickSlotKind::Item)
	{
		if (InventoryModel && !InventoryModel->bPending) InventoryModel->UseByItemId(Binding.ItemId);
	}
	else if (Binding.Kind == EProject_JQuickSlotKind::Skill &&
			 PresentedSkills.ContainsByPredicate([&](const auto &Slot) { return Slot.InputTag == Binding.InputTag; }))
	{
		auto *PC = Cast<APlayerController>(GetOwner());
		const TStrongObjectPtr<AProject_JPlayerCharacter> Pawn(PC ? Cast<AProject_JPlayerCharacter>(PC->GetPawn()) : nullptr);
		if (Pawn.IsValid() && !Pawn->IsActorBeingDestroyed())
		{
			if (++NextSkillReceipt == 0) ++NextSkillReceipt;
			PressedPawn = Pawn.Get();
			PressedSkills.Add(Index, {Binding.InputTag, NextSkillReceipt});
			// Publish the source's receipt before activation can recycle its widget
			// or key binding. Never write into that source again after callbacks.
			if (OutReceipt) *OutReceipt = NextSkillReceipt;
			Pawn->HandleSkillInputTagPressedFromSource(Binding.InputTag, this, NextSkillReceipt);
			QueueSkillRefresh();
		}
	}
}
void UProject_JPlayerUIComponent::ReleaseSkill(int32 Index)
{
	ReleaseSkillReceipt(Index, 0); // Preserve the public/BP explicit release facade.
}
bool UProject_JPlayerUIComponent::IsSkillReceiptCurrent(int32 Index, uint64 Receipt) const
{
	const auto* Press = PressedSkills.Find(Index);
	return Receipt && Press && Press->Receipt == Receipt;
}
void UProject_JPlayerUIComponent::ReleaseSkillReceipt(int32 Index, uint64 ExpectedReceipt)
{
	const TStrongObjectPtr<UProject_JPlayerUIComponent> KeepSelf(this);
	if (const auto* Press = PressedSkills.Find(Index))
	{
		if (ExpectedReceipt && Press->Receipt != ExpectedReceipt) return;
		const FGameplayTag ReleasedTag = Press->InputTag;
		const uint64 ReleasedReceipt = Press->Receipt;
		const TStrongObjectPtr<AProject_JPlayerCharacter> OriginalPawn(PressedPawn.Get());
		PressedSkills.Remove(Index);
		if (PressedSkills.IsEmpty()) { PressedPawn.Reset(); }
		if (IsValid(OriginalPawn.Get()) && !OriginalPawn->IsActorBeingDestroyed())
			OriginalPawn->HandleSkillInputTagReleasedFromSource(ReleasedTag, this, ReleasedReceipt);
	}
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
		if (auto *Pawn = Cast<AProject_JPlayerCharacter>(PC->GetPawn()))
			Pawn->StopSprint();
		RefreshSources();
		Screen->SetMenuOpen(true);
	}
	else
	{
		ReleaseAllSkills();
		Screen->SetMenuOpen(false);
	}
	ApplyInputMode();
}
void UProject_JPlayerUIComponent::ToggleCursorMode()
{
	if (bEnding || bMenuOpen || !Screen) return;
	bCursorMode = !bCursorMode;
	ReleaseAllSkills();
	ApplyInputMode();
}
void UProject_JPlayerUIComponent::ApplyInputMode()
{
	auto* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalPlayerController()) return;
	const bool bLookIgnore = bMenuOpen || bCursorMode;
	if (bLookIgnore && !bOwnsLookIgnore)
		if (APawn* CurrentPawn = PC->GetPawn())
			if (auto* Router = CurrentPawn->FindComponentByClass<UProject_JSkillInputRouterComponent>()) Router->CancelMouseInput();
	// Ignore flags are counters. Release only our own contribution, once per transition.
	if (bOwnsMoveIgnore != bMenuOpen) { PC->SetIgnoreMoveInput(bMenuOpen); bOwnsMoveIgnore = bMenuOpen; }
	if (bOwnsLookIgnore != bLookIgnore) { PC->SetIgnoreLookInput(bLookIgnore); bOwnsLookIgnore = bLookIgnore; }
	PC->bShowMouseCursor = bLookIgnore;
	if (bMenuOpen && Screen)
	{
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Screen->TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}
	else if (bCursorMode && Screen)
	{
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(Screen->TakeWidget());
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}
	else PC->SetInputMode(FInputModeGameOnly());
}
void UProject_JPlayerUIComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	const TStrongObjectPtr<UProject_JPlayerUIComponent> KeepSelf(this);
	// Close admission before menu/input cleanup can invoke gameplay callbacks.
	bEnding = true;
	++KeyBindingRevision;
	bCursorMode = false;
	SetMenuOpen(false);
	ApplyInputMode();
	ReleaseAllSkills();
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(CursorInputProcessor);
		FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle);
	}
	CursorInputProcessor.Reset();
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
		GetWorld()->GetTimerManager().ClearTimer(DynamicProfileTimer);
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
		!InventoryModel || !InventoryModel->IsCurrentEntry(Entry) || !Entry->Item.ItemDef || Entry->bEquipmentEntry ||
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

void UProject_JPlayerUIComponent::OnPartyRosterChanged()
{
	if (bEnding || !Screen) return;
	const auto* PC = Cast<APlayerController>(GetOwner());
	const auto* State = BoundState.Get();
	if (!PC || !PC->IsLocalPlayerController() || !State || State->IsActorBeingDestroyed()
		|| PC->GetPlayerState<AProject_JPlayerState>() != State || State->GetCharacterId().Value != BoundCharacterId)
	{
		Screen->PresentPartyRoster({}); return;
	}
	Screen->PresentPartyRoster(State->GetPartyRoster());
}

int32 UProject_JPlayerUIComponent::GetPresentedPartyMemberCount() const
{
	return !bEnding && Screen ? Screen->GetPresentedPartyMemberCount() : 0;
}

UProject_JCraftingJobsComponent* UProject_JPlayerUIComponent::GetCraftingJobsComponent() const
{
	const auto* PC = Cast<APlayerController>(GetOwner());
	const auto* State = BoundState.Get();
	auto* Jobs = BoundCraftingJobs.Get();
	return !bEnding && Screen && Screen->IsInViewport() && PC && PC->IsLocalPlayerController()
		&& State && !State->IsActorBeingDestroyed() && PC->GetPlayerState<AProject_JPlayerState>() == State
		&& BoundCharacterId.IsValid() && State->GetCharacterId().Value == BoundCharacterId
		&& Jobs && !Jobs->IsBeingDestroyed() && Jobs->IsRegistered() && Jobs->GetWorld() == GetWorld()
		&& Jobs->GetOwner() == State && State->GetCraftingJobsComponent() == Jobs
		&& Jobs->GetPresentationCharacterId() == BoundCharacterId ? Jobs : nullptr;
}
void UProject_JPlayerUIComponent::ClearCraftingSource()
{
	const TStrongObjectPtr<UProject_JPlayerUIComponent> KeepSelf(this);
	const TStrongObjectPtr<UProject_JCraftingJobsComponent> Old(BoundCraftingJobs.Get());
	const FDelegateHandle OldJobs = CraftingJobsHandle, OldRequest = CraftingRequestHandle;
	// Retire the binding before any widget/focus callbacks can reenter.
	BoundCraftingJobs.Reset(); CraftingJobsHandle.Reset(); CraftingRequestHandle.Reset();
	++CraftingBindingRevision;
	if (Old.IsValid()) { Old->OnChanged.Remove(OldJobs); Old->OnRequestChanged.Remove(OldRequest); }
	if (Screen) Screen->PresentCraftingJobs(nullptr);
}
void UProject_JPlayerUIComponent::BindCraftingSource(UProject_JCraftingJobsComponent* Source)
{
	if (BoundCraftingJobs.Get() == Source) return;
	const TStrongObjectPtr<UProject_JPlayerUIComponent> KeepSelf(this);
	const TStrongObjectPtr<UProject_JCraftingJobsComponent> KeepSource(Source);
	const uint64 Previous = CraftingBindingRevision;
	ClearCraftingSource();
	// A new source installed from a teardown callback owns its own handles.
	if (CraftingBindingRevision != Previous + 1 || bEnding || !Screen) return;
	BoundCraftingJobs = Source;
	// Subscribe before owner-scope replication arrives. Scope/identity agreement gates presentation,
	// not this structural subscription; OnChanged will publish when the atomic scope is current.
	const auto* PC = Cast<APlayerController>(GetOwner());
	const auto* State = BoundState.Get();
	if (!Source || !Screen->IsInViewport() || !PC || !PC->IsLocalPlayerController()
		|| !State || State->IsActorBeingDestroyed() || PC->GetPlayerState<AProject_JPlayerState>() != State
		|| Source->IsBeingDestroyed() || !Source->IsRegistered() || Source->GetWorld() != GetWorld()
		|| Source->GetOwner() != State || State->GetCraftingJobsComponent() != Source)
	{ BoundCraftingJobs.Reset(); return; }
	const uint64 Revision = CraftingBindingRevision;
	const TWeakObjectPtr<UProject_JCraftingJobsComponent> Expected(Source);
	CraftingJobsHandle = Source->OnChanged.AddWeakLambda(this, [this, Expected, Revision]
	{
		if (Revision != CraftingBindingRevision || BoundCraftingJobs != Expected || !Screen) return;
		Screen->PresentCraftingJobs(GetCraftingJobsComponent());
	});
	CraftingRequestHandle = Source->OnRequestChanged.AddWeakLambda(this, [this, Expected, Revision]
	{
		if (Revision != CraftingBindingRevision || BoundCraftingJobs != Expected || !Screen) return;
		Screen->RefreshCraftingRequest(GetCraftingJobsComponent());
	});
	Screen->PresentCraftingJobs(GetCraftingJobsComponent());
}
void UProject_JPlayerUIComponent::ReleaseCraftingScreen(const UProject_JPlayerHUDWidget* ExpectedScreen)
{
	if (Screen == ExpectedScreen) ClearCraftingSource();
}
void UProject_JPlayerUIComponent::RequestCraftingJobAction(UProject_JCraftingJobsComponent* ExpectedSource,
	FGuid ExpectedCharacterId, uint64 ExpectedGeneration, FGuid JobId, EProject_JCraftingJobAction Action)
{
	const TStrongObjectPtr<UProject_JPlayerUIComponent> KeepSelf(this);
	const TStrongObjectPtr<UProject_JCraftingJobsComponent> Source(GetCraftingJobsComponent());
	if (!Source.IsValid() || Source.Get() != ExpectedSource || !JobId.IsValid()
		|| !ExpectedCharacterId.IsValid() || ExpectedCharacterId != BoundCharacterId
		|| ExpectedGeneration == 0 || ExpectedGeneration != Source->GetPresentationGeneration()) return;
	const auto Rows = Source->GetJobs();
	if (!Rows.ContainsByPredicate([JobId, ExpectedCharacterId, ExpectedGeneration](const FProject_JCraftingJobSummary& Row) { return Row.JobId == JobId && Row.OwnerCharacterId == ExpectedCharacterId && Row.OwnerGeneration == ExpectedGeneration; })
		|| Source->GetRequestState().Stage == EProject_JCraftingRequestStage::Pending) return;
	if (Action == EProject_JCraftingJobAction::Collect)
	{
		if (Source->GetRemainingSeconds(JobId) != 0) return;
		Source->RequestCollectJob(JobId);
	}
	else if (Action == EProject_JCraftingJobAction::Cancel) Source->RequestCancelJob(JobId);
}
int32 UProject_JPlayerUIComponent::GetPresentedCraftingJobCount() const
{
	return !bEnding && Screen ? Screen->GetPresentedCraftingJobCount() : 0;
}
