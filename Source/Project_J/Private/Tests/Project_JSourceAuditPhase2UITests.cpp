#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Game/Project_JPlayerState.h"
#include "UI/Project_JPlayerUIComponent.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Combat/Project_JGameplayAbility_Sprint.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Components/Project_JSkillInputRouterComponent.h"
#include "Components/Project_JPlayerInputBindingComponent.h"
#include "Components/Project_JCombatStateComponent.h"
#include "InputAction.h"
#include "Tests/Project_JSourceAuditPhase2UIReceiver.h"
#include "UI/Project_JInventoryViewModel.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Project_JAttributeSet.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAudit2SharedSkillHoldTest,
    "ProjectJ.SourceAudit2.SharedSkillHold", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAudit2SharedSkillHoldTest::RunTest(const FString&)
{
    for (int32 Mode = 0; Mode < 4; ++Mode)
    {
        auto* World = UWorld::CreateWorld(EWorldType::Game, false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
        auto* State = World->SpawnActor<AProject_JPlayerState>();
        auto* Controller = World->SpawnActor<APlayerController>();
        auto* Player = World->SpawnActor<AProject_JGreatswordCharacter>();
        State->SetOwner(Controller); Controller->SetPlayerState(State); Player->SetPlayerState(State);
        Controller->SetAsLocalPlayerController(); Controller->Possess(Player);
        auto* ASC = State->GetProjectJAbilitySystemComponent(); ASC->InitAbilityActorInfo(State, Player);
        auto* Input = Player->FindComponentByClass<UProject_JSkillInputExecutionComponent>(); Input->Initialize(Player);
        Player->FindComponentByClass<UProject_JCombatStateComponent>()->BindToAbilitySystem(ASC);
        const auto Tag = FProject_JGameplayTags::Get().InputTag_Weapon_LMB;
        FGameplayAbilitySpec Spec(UProject_JGameplayAbility_Sprint::StaticClass()); Spec.GetDynamicSpecSourceTags().AddTag(Tag);
        const auto Handle = ASC->GiveAbility(Spec);
        auto* UI = NewObject<UProject_JPlayerUIComponent>(Controller); UI->RegisterComponent();
        FProject_JUISkillSlot Slot; Slot.InputTag = Tag; UI->PresentedSkills.Add(Slot);
        UI->HUDPreferences.Slots.SetNum(2);
        for (auto& Binding : UI->HUDPreferences.Slots) { Binding.Kind = EProject_JQuickSlotKind::Skill; Binding.InputTag = Tag; }
        auto* Router = Player->FindComponentByClass<UProject_JSkillInputRouterComponent>(); Router->Initialize(Player);
        auto* Bindings = Player->FindComponentByClass<UProject_JPlayerInputBindingComponent>();
        auto* Mapping = NewObject<UProject_JSkillInputMappingData>(Player);
        auto* FirstAction = NewObject<UInputAction>(Player); auto* SecondAction = NewObject<UInputAction>(Player);
        ON_SCOPE_EXIT { UI->ReleaseAllSkills(); Router->CancelMouseInput(); Bindings->UnbindInput(); ASC->CancelAllAbilities(); };
        if (Mode < 2)
        {
            if (Mode == 1) Player->HandleSkillInputTagPressed(Tag);
            UI->PressSkill(0);
            if (Mode == 0) UI->PressSkill(1);
        }
        else if (Mode == 2)
        {
            auto& L = Mapping->Chords.AddDefaulted_GetRef(); L.bRequiresLMB = true; L.InputTag = Tag;
            auto& R = Mapping->Chords.AddDefaulted_GetRef(); R.bRequiresLMB = false; R.bRequiresRMB = true; R.InputTag = Tag;
            Router->InputMappingData = Mapping;
            Router->HandleButtonPressed(EProject_JSkillInputButton::LMB); Router->HandleButtonPressed(EProject_JSkillInputButton::RMB);
        }
        else
        {
            auto& A = Mapping->DirectSkillBindings.AddDefaulted_GetRef(); A.InputAction = FirstAction; A.InputTag = Tag;
            auto& B = Mapping->DirectSkillBindings.AddDefaulted_GetRef(); B.InputAction = SecondAction; B.InputTag = Tag;
            Bindings->BoundPlayerCharacter = Player; Bindings->ActiveSkillInputMappingData = Mapping;
            Bindings->HandleDirectSkillActionPressed(FirstAction); Bindings->HandleDirectSkillActionPressed(SecondAction);
        }
        TestTrue(TEXT("Two sources reach a held real GAS ability"), ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
        if (Mode == 0) UI->ReleaseSkill(0);
        else if (Mode == 1) UI->ReleaseAllSkills();
        else if (Mode == 2) Router->HandleButtonReleased(EProject_JSkillInputButton::LMB);
        else Bindings->HandleDirectSkillActionReleased(FirstAction);
        TestTrue(FString::Printf(TEXT("Releasing one source preserves the other hold (mode %d)"), Mode), ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
        if (Mode == 0) UI->ReleaseSkill(1);
        else if (Mode == 1) Player->HandleSkillInputTagReleased(Tag);
        else if (Mode == 2) Router->CancelMouseInput();
        else Bindings->UnbindInput();
        TestFalse(TEXT("Last source release/teardown releases actual GAS hold"), ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAudit2ConsumedUnavailableUITest,
    "ProjectJ.SourceAudit2.ConsumedUnavailableUI", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAudit2ConsumedUnavailableUITest::RunTest(const FString&)
{
    for (int32 Count : {1, 2})
    {
        auto* World = UWorld::CreateWorld(EWorldType::Game, false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
        ++GFrameCounter; World->GetTimerManager().Tick(0.f);
        auto* State = World->SpawnActor<AProject_JPlayerState>();
        auto* ASC = State->GetProjectJAbilitySystemComponent(); ASC->InitAbilityActorInfo(State, State);
        ASC->AddAttributeSetSubobject(NewObject<UProject_JAttributeSet>(State));
        ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 100.f);
        ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 40.f);
        ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxManaAttribute(), 100.f);
        auto* OtherAvatar = World->SpawnActor<AActor>();
        auto* Inventory = State->GetInventoryComponent();
        auto* Definition = NewObject<UProject_JConsumableDefinition>(State); Definition->ItemId = TEXT("Audit2Potion"); Definition->MaxStackCount = 10; Definition->RestoreHealth = 20;
        const auto Item = Inventory->AddItemDefinition(Definition, Count);
        auto* Receiver = NewObject<UProject_JSourceAuditPhase2UIReceiver>(State);
        Receiver->OnConsumption = [=] { ASC->InitAbilityActorInfo(State, OtherAvatar); };
        Inventory->OnItemChanged.AddDynamic(Receiver, &UProject_JSourceAuditPhase2UIReceiver::OnItem);
        Inventory->OnItemRemoved.AddDynamic(Receiver, &UProject_JSourceAuditPhase2UIReceiver::OnItem);
        Inventory->OnUseCompleted.AddDynamic(Receiver, &UProject_JSourceAuditPhase2UIReceiver::OnReply);
        auto* Model = NewObject<UProject_JInventoryViewModel>(State); Model->Bind(Inventory, State->GetEquipmentManagerComponent());
        ON_SCOPE_EXIT { Model->Unbind(); };
        TestTrue(TEXT("UI submits one explicit use request"), Model->Use(Item.InstanceId));
        TestEqual(TEXT("Actual completion reports consumed-but-unavailable"), Receiver->LastResult, EProject_JItemUseResult::Unavailable);
        TestFalse(TEXT("Unavailable clears pending without an automatic request"), Model->bPending);
        TestTrue(TEXT("UI explicitly asks the player to check quantity after Unavailable"), Model->Status.ToString().Contains(TEXT("수량")));
        TestEqual(TEXT("UI projects actual remaining quantity immediately"), Model->FindItemSummary(Definition->ItemId) ? Model->FindItemSummary(Definition->ItemId)->Quantity : 0, Count - 1);
        for (float Step : {.1f, 6.f}) { ++GFrameCounter; World->GetTimerManager().Tick(Step); }
        TestEqual(TEXT("Refresh/old timeout does not submit a fresh request"), Receiver->Replies, 1);
        TestEqual(TEXT("UI preserves authoritative consumption after queued refresh"), Model->FindItemSummary(Definition->ItemId) ? Model->FindItemSummary(Definition->ItemId)->Quantity : 0, Count - 1);
        TestEqual(TEXT("No recovery is applied"), ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), 40.f);
    }
    return true;
}
#endif
