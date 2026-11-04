#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UI/Project_JPlayerHUDWidget.h"
#include "UI/Project_JPlayerUIComponent.h"
#include "UI/Project_JInventoryViewModel.h"
#include "UObject/UnrealType.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JQuestComponent.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "UI/Project_JHUDWindow.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ScrollBox.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "HAL/PlatformProcess.h"
#include "Components/TextBlock.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIModelOwnershipTest,
	"ProjectJ.Continuation.UIModelOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUIModelOwnershipTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Controller = World->SpawnActor<APlayerController>();
	auto* First = NewObject<UProject_JPlayerUIComponent>(Controller);
	auto* Second = NewObject<UProject_JPlayerUIComponent>(Controller);
	Controller->AddInstanceComponent(First); Controller->AddInstanceComponent(Second);
	auto* A = NewObject<UProject_JInventoryViewModel>(First);
	auto* B = NewObject<UProject_JInventoryViewModel>(Second);
	auto* Property = FindFProperty<FObjectPropertyBase>(UProject_JPlayerUIComponent::StaticClass(), TEXT("InventoryModel"));
	if (!TestNotNull(TEXT("Reflected model binding"), Property)) { return false; }
	Property->SetObjectPropertyValue_InContainer(First, A);
	Property->SetObjectPropertyValue_InContainer(Second, B);
	auto* HUD = NewObject<UProject_JPlayerHUDWidget>(Controller);
	int32 OtherSubscriberCalls = 0;
	const auto Other = A->OnSplitRequested.AddLambda([&](UProject_JInventoryEntry*) { ++OtherSubscriberCalls; });
	ON_SCOPE_EXIT { A->OnSplitRequested.Remove(Other); };
	HUD->InitializeScreen(First);
	TestTrue(TEXT("First model owns its screen subscription"), A->OnSplitRequested.IsBoundToObject(HUD));
	HUD->InitializeScreen(First);
	HUD->InitializeScreen(Second);
	TestFalse(TEXT("Rebinding retires the previous model subscription"), A->OnSplitRequested.IsBoundToObject(HUD));
	TestTrue(TEXT("Current model owns the screen subscription"), B->OnSplitRequested.IsBoundToObject(HUD));
	A->OnSplitRequested.Broadcast(nullptr);
	TestEqual(TEXT("Unrelated subscribers are preserved"), OtherSubscriberCalls, 1);
	TestTrue(TEXT("Retired model cannot publish guidance into the new model"), B->Status.IsEmpty());
	HUD->InitializeScreen(nullptr);
	TestFalse(TEXT("Clearing the screen binding returns its subscription"), B->OnSplitRequested.IsBoundToObject(HUD));
	B->Status = FText::FromString(TEXT("server request pending")); B->bPending = true;
	B->SetLocalStatus(FText::GetEmpty());
	TestEqual(TEXT("Local cancel cannot erase an in-flight server status"), B->Status.ToString(), FString(TEXT("server request pending")));
	B->bPending = false;
	B->SetLocalStatus(FText::FromString(TEXT("choose merge target")));
	TestEqual(TEXT("Idle model accepts local guidance"), B->Status.ToString(), FString(TEXT("choose merge target")));
	B->SetLocalStatus(FText::GetEmpty());
	TestTrue(TEXT("Idle guidance can be cleared"), B->Status.IsEmpty());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUIQuestProjectionTest, "ProjectJ.UI.Quests.BoundedJournalProjection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUIQuestProjectionTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Controller = World->SpawnActor<APlayerController>();
    auto* FirstState = World->SpawnActor<AProject_JPlayerState>();
    auto* SecondState = World->SpawnActor<AProject_JPlayerState>();
    auto* FirstQuest = FirstState->FindComponentByClass<UProject_JQuestComponent>();
    auto* SecondQuest = SecondState->FindComponentByClass<UProject_JQuestComponent>();
    auto* FirstProgress = FirstState->FindComponentByClass<UProject_JProgressionComponent>();
    auto* SecondProgress = SecondState->FindComponentByClass<UProject_JProgressionComponent>();
    FirstState->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(FirstState, FirstState);
    SecondState->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(SecondState, SecondState);
    FirstProgress->InitializeDefaults(nullptr, nullptr, 1); SecondProgress->InitializeDefaults(nullptr, nullptr, 1);
    const TStrongObjectPtr<UProject_JQuestDefinition> Main(NewObject<UProject_JQuestDefinition>());
    const TStrongObjectPtr<UProject_JQuestDefinition> Other(NewObject<UProject_JQuestDefinition>());
    Main->QuestId = TEXT("JournalMain"); Main->Title = FText::FromString(TEXT("Main quest"));
    Main->ObjectiveEvent = TEXT("JournalProgress"); Main->RequiredCount = 1; Main->RewardExperience = 17;
    Other->QuestId = TEXT("JournalOther"); Other->Title = FText::FromString(TEXT("Other quest"));
    Other->ObjectiveEvent = TEXT("OtherProgress"); Other->RequiredCount = 2;
    FirstQuest->Definitions = {Main.Get(), Other.Get()}; SecondQuest->Definitions = FirstQuest->Definitions;
    if (!TestTrue(TEXT("Real quest owners accept the same authored IDs independently"), FirstQuest->Accept(Main->QuestId)
        && FirstQuest->Accept(Other->QuestId) && SecondQuest->Accept(Main->QuestId) && SecondQuest->Accept(Other->QuestId))) return false;
    // Acceptance tracks by default. Establish the intended untracked control through the real public request.
    FirstQuest->RequestTracking(Main->QuestId, false); SecondQuest->RequestTracking(Main->QuestId, false);
    if (!TestTrue(TEXT("Both real owners start the button test untracked"), !FirstQuest->States[0].bTracked && !SecondQuest->States[0].bTracked)) return false;
    auto* FirstUI = NewObject<UProject_JPlayerUIComponent>(Controller);
    auto* SecondUI = NewObject<UProject_JPlayerUIComponent>(Controller);
    Controller->AddInstanceComponent(FirstUI); Controller->AddInstanceComponent(SecondUI);
    const TStrongObjectPtr<UProject_JPlayerHUDWidget> View(NewObject<UProject_JPlayerHUDWidget>(Controller));
    View->WidgetTree = NewObject<UWidgetTree>(View.Get());
    auto* JournalRoot = View->WidgetTree->ConstructWidget<UVerticalBox>(); View->WidgetTree->RootWidget = JournalRoot;
    View->QuestHistoryCommands = View->WidgetTree->ConstructWidget<UHorizontalBox>(); JournalRoot->AddChildToVerticalBox(View->QuestHistoryCommands);
    View->QuestHistoryStatus = View->WidgetTree->ConstructWidget<UTextBlock>(); JournalRoot->AddChildToVerticalBox(View->QuestHistoryStatus);
    View->QuestJournal = View->WidgetTree->ConstructWidget<UScrollBox>(); JournalRoot->AddChildToVerticalBox(View->QuestJournal);
    View->QuestHistoryList = View->WidgetTree->ConstructWidget<UScrollBox>(); JournalRoot->AddChildToVerticalBox(View->QuestHistoryList);
    View->QuestTracker = View->WidgetTree->ConstructWidget<UTextBlock>(); View->WindowStack.Add(TEXT("Quests"));
    const auto Bind = [&](UProject_JPlayerUIComponent* UI, AProject_JPlayerState* State, UProject_JQuestComponent* Quest)
    {
        UI->Screen = View.Get(); UI->BoundState = State; UI->BoundQuests = Quest;
        Quest->OnChanged.AddUObject(UI, &UProject_JPlayerUIComponent::OnQuestsChanged);
        Quest->OnHistoryPageChanged.AddUObject(UI, &UProject_JPlayerUIComponent::OnQuestsChanged);
        View->InitializeScreen(UI); View->RefreshQuests();
    };
    int32 FirstPublications = 0, SecondPublications = 0;
    const auto FirstObserver = FirstQuest->OnChanged.AddLambda([&] { ++FirstPublications; });
    const auto SecondObserver = SecondQuest->OnChanged.AddLambda([&] { ++SecondPublications; });
    ON_SCOPE_EXIT
    {
        FirstUI->ClearSources(); SecondUI->ClearSources();
        FirstUI->Screen = nullptr; SecondUI->Screen = nullptr;
        FirstQuest->OnChanged.Remove(FirstObserver); SecondQuest->OnChanged.Remove(SecondObserver);
        View->InitializeScreen(nullptr);
    };
    Bind(FirstUI, FirstState, FirstQuest);
    if (!TestEqual(TEXT("Visible actual journal creates two rows"), View->QuestJournalRows.Num(), 2)) return false;
    const auto Initial = View->QuestJournalRows.FindChecked(Main->QuestId);
    const auto InitialOther = View->QuestJournalRows.FindChecked(Other->QuestId);
    const TStrongObjectPtr<UProject_JUICommandButton> OldAction(Initial.Action), RemovedAction(InitialOther.Action);
    const TStrongObjectPtr<UVerticalBox> MainColumn(Initial.Column), OtherColumn(InitialOther.Column);
    const TStrongObjectPtr<UWidget> ActionText(OldAction->GetContent());
    auto* InitialSlot = MainColumn->Slot.Get();
    // The native getter returns zero until the actual Slate scroll widget is built.
    const auto LiveJournal = View->QuestJournal->TakeWidget();
    View->QuestJournal->SetScrollOffset(9.f);
    if (!TestEqual(TEXT("Realized Slate scroll box observes the control offset"), View->QuestJournal->GetScrollOffset(), 9.f)) return false;
    for (int32 Refresh = 0; Refresh < 12; ++Refresh) FirstUI->OnQuestsChanged();
    const auto Retained = View->QuestJournalRows.FindChecked(Main->QuestId);
    TestTrue(TEXT("Unchanged rows, label, button, button text and panel slot are reused"), Retained.Column == MainColumn.Get()
        && Retained.Label == Initial.Label && Retained.Action == OldAction.Get() && OldAction->GetContent() == ActionText.Get()
        && MainColumn->Slot == InitialSlot && View->QuestJournal->GetChildrenCount() == 2);
    TestEqual(TEXT("Repeated projection preserves scroll"), View->QuestJournal->GetScrollOffset(), 9.f);
    FirstPublications = 0; OldAction->OnClicked.Broadcast();
    TestTrue(TEXT("Actual retained button toggles tracking once, without duplicate Execute binding"), FirstQuest->States[0].bTracked && FirstPublications == 1);
    FirstQuest->RecordObjective(Main->ObjectiveEvent, 1);
    TestTrue(TEXT("Actual event projection updates the retained label and action"), View->QuestJournalRows.FindChecked(Main->QuestId).Action == OldAction.Get()
        && Retained.Label->GetText().ToString().Contains(TEXT("1/1")) && OldAction->GetIsEnabled());
    auto Reordered = FirstQuest->CaptureSnapshot(); Swap(Reordered.States[0], Reordered.States[1]);
    if (!TestTrue(TEXT("Valid authoritative snapshot can reorder the same quest rows"), FirstQuest->ValidateCommittedQuests(Reordered))) return false;
    FirstQuest->InstallCommittedQuests(Reordered); FirstQuest->PublishCommittedQuests();
    TestTrue(TEXT("Data order changes without replacing either row"), View->QuestJournal->GetChildAt(0) == OtherColumn.Get()
        && View->QuestJournal->GetChildAt(1) == MainColumn.Get() && View->QuestJournalRows.Num() == 2);
    auto Reduced = FirstQuest->CaptureSnapshot(); Reduced.States.RemoveAt(0);
    if (!TestTrue(TEXT("Valid source projection removes an obsolete quest"), FirstQuest->ValidateCommittedQuests(Reduced))) return false;
    FirstQuest->InstallCommittedQuests(Reduced); FirstQuest->PublishCommittedQuests();
    TestTrue(TEXT("Removed row is absent, unparented and its real command is disabled"), View->QuestJournalRows.Num() == 1
        && !View->QuestJournalRows.Contains(Other->QuestId) && !OtherColumn->GetParent() && !RemovedAction->GetIsEnabled());
    FirstPublications = 0; OldAction->OnClicked.Broadcast();
    TestTrue(TEXT("Actual Claim pays once, archives membership and retires the current row/command"),
        FirstQuest->GetCompletionStatus(Main->QuestId) == EProject_JQuestCompletionStatus::Completed
        && FirstQuest->States.IsEmpty() && FirstProgress->GetExperience() == 17 && FirstPublications == 1
        && View->QuestJournalRows.IsEmpty() && !OldAction->GetIsEnabled() && !MainColumn->GetParent());
    OldAction->OnClicked.Broadcast();
    TestEqual(TEXT("A retained claimed button cannot pay again"), FirstProgress->GetExperience(), int64(17));
    FirstUI->ClearSources();
    Bind(FirstUI, SecondState, SecondQuest);
    const TStrongObjectPtr<UProject_JUICommandButton> CurrentAction(View->QuestJournalRows.FindChecked(Main->QuestId).Action);
    SecondPublications = 0;
    CollectGarbage(RF_NoFlags, true);
    OldAction->OnClicked.Broadcast(); RemovedAction->OnClicked.Broadcast();
    TestTrue(TEXT("Retired buttons cannot affect a new owner with the same quest ID even via synthetic click"), SecondPublications == 0
        && !SecondQuest->States[0].bTracked && !SecondQuest->States[0].bClaimed && SecondProgress->GetExperience() == 0
        && CurrentAction.Get() != OldAction.Get() && !OldAction->GetIsEnabled());
    CurrentAction->OnClicked.Broadcast();
    TestTrue(TEXT("The new owner's actual current button still works once"), SecondQuest->States[0].bTracked && SecondPublications == 1);
    // Public owner replacement must retire a still-live row; do not pre-clear or hide the old screen reference.
    Bind(SecondUI, SecondState, SecondQuest);
    const TStrongObjectPtr<UProject_JUICommandButton> NewOwnerAction(View->QuestJournalRows.FindChecked(Main->QuestId).Action);
    SecondPublications = 0; CurrentAction->OnClicked.Broadcast();
    TestTrue(TEXT("InitializeScreen owner replacement itself retires the old real command"), SecondPublications == 0 && !CurrentAction->GetIsEnabled());
    FirstUI->ClearSources(); // It deliberately still has Screen==View after the public handoff.
    TestTrue(TEXT("Old owner cleanup cannot clear the new live owner's rows"), FirstUI->Screen == View.Get()
        && View->QuestJournalRows.Num() == 2 && View->QuestJournalRows.FindChecked(Main->QuestId).Action == NewOwnerAction.Get()
        && NewOwnerAction->GetIsEnabled());
    const bool TrackedBeforeNewOwnerClick = SecondQuest->States[0].bTracked;
    NewOwnerAction->OnClicked.Broadcast();
    TestTrue(TEXT("New owner's actual button still works once after old owner cleanup"), SecondPublications == 1
        && SecondQuest->States[0].bTracked != TrackedBeforeNewOwnerClick);
    // One real validated projection reaches the existing128-state contract; no ever-seen row history is kept.
    FProject_JQuestSnapshot Full;
    SecondQuest->Definitions.Reset();
    for (int32 Index = 0; Index < 128; ++Index)
    {
        auto* Def = NewObject<UProject_JQuestDefinition>(SecondState);
        Def->QuestId = FName(*FString::Printf(TEXT("JournalBound%d"), Index)); Def->ObjectiveEvent = TEXT("JournalBoundProgress");
        SecondQuest->Definitions.Add(Def); auto& State = Full.States.AddDefaulted_GetRef(); State.QuestId = Def->QuestId;
    }
    if (!TestTrue(TEXT("The full current-state projection is natively valid"), SecondQuest->ValidateCommittedQuests(Full))) return false;
    SecondQuest->InstallCommittedQuests(Full); SecondQuest->PublishCommittedQuests();
    TestTrue(TEXT("Journal cache and children are bounded to current128 IDs"), View->QuestJournalRows.Num() == 128 && View->QuestJournal->GetChildrenCount() == 128);
    Full.States.SetNum(1); SecondQuest->InstallCommittedQuests(Full); SecondQuest->PublishCommittedQuests();
    TestTrue(TEXT("Obsolete history is released after a smaller projection"), View->QuestJournalRows.Num() == 1 && View->QuestJournal->GetChildrenCount() == 1);

    // Legacy claimed rows remain visible through the real history consumer without adding current-detail widgets.
    auto Historical = SecondQuest->CaptureSnapshot();
    for (int32 Index = 0; Index < 65; ++Index)
    {
        auto* Def = NewObject<UProject_JQuestDefinition>(SecondState);
        Def->QuestId = FName(*FString::Printf(TEXT("CompletedJournal%03d"), Index)); Def->ObjectiveEvent = TEXT("HistoricalProgress");
        SecondQuest->Definitions.Add(Def);
        auto& Entry = Historical.States.AddDefaulted_GetRef(); Entry.QuestId = Def->QuestId;
        Entry.Count = Def->RequiredCount; Entry.bClaimed = true; Entry.bTracked = false;
    }
    if (!TestTrue(TEXT("Actual legacy claimed snapshot is valid before normalization"), SecondQuest->ValidateCommittedQuests(Historical))) return false;
    SecondQuest->InstallCommittedQuests(Historical); SecondQuest->PublishCommittedQuests();
    TestTrue(TEXT("Legacy completions contribute to count while current rows stay current-only"), SecondQuest->GetHistorySummary().Count == 65
        && View->QuestJournalRows.Num() == 1 && View->QuestHistoryButtons.Num() == 5);
    View->QuestHistoryButtons[1]->OnClicked.Broadcast();
    auto Page = SecondQuest->GetHistoryPage();
    if (!TestTrue(TEXT("Actual History button queries and consumes exactly32 IDs"), Page.Status == EProject_JQuestHistoryPageStatus::Fresh
        && Page.bHasPage && Page.Ordinal == 0 && Page.Ids.Num() == 32 && View->QuestHistoryRows.Num() == 32)) return false;
    const TStrongObjectPtr<UTextBlock> FirstHistoryLabel(View->QuestHistoryRows[0]);
    const FGuid ConfirmedRequest = Page.Request;
    for (int32 Refresh = 0; Refresh < 12; ++Refresh) SecondUI->OnQuestsChanged();
    TestTrue(TEXT("UI refresh neither sends another request nor recreates bounded history labels"),
        SecondQuest->GetHistoryPage().Request == ConfirmedRequest && View->QuestHistoryRows[0] == FirstHistoryLabel.Get());
    View->QuestHistoryButtons[3]->OnClicked.Broadcast(); // Same-domain throttle can silently refuse this immediate request.
    TestTrue(TEXT("An unconfirmed request preserves the last page and leaves manual retry enabled"),
        SecondQuest->GetHistoryPage().Ordinal == 0 && SecondQuest->GetHistoryPage().Ids.Num() == 32 && View->QuestHistoryButtons[4]->GetIsEnabled());
    FPlatformProcess::Sleep(0.51f); // Real production monotonic throttle; no injected clock/window state.
    View->QuestHistoryButtons[3]->OnClicked.Broadcast();
    Page = SecondQuest->GetHistoryPage();
    if (!TestTrue(TEXT("Retry obtains the next real page and reuses its32 labels"), Page.Status == EProject_JQuestHistoryPageStatus::Fresh
        && Page.Ordinal == 1 && Page.Ids.Num() == 32 && View->QuestHistoryRows.Num() == 32 && View->QuestHistoryRows[0] == FirstHistoryLabel.Get())) return false;
    FPlatformProcess::Sleep(0.51f);
    View->QuestHistoryButtons[3]->OnClicked.Broadcast();
    Page = SecondQuest->GetHistoryPage();
    TestTrue(TEXT("Last page has one ID while retained widget count remains bounded32"), Page.Status == EProject_JQuestHistoryPageStatus::Fresh
        && Page.Ordinal == 2 && Page.Ids.Num() == 1 && View->QuestHistoryRows.Num() == 32 && !View->QuestHistoryButtons[3]->GetIsEnabled());
    const TStrongObjectPtr<UProject_JUICommandButton> OldHistoryCommand(View->QuestHistoryButtons[1]);
    Bind(FirstUI, SecondState, SecondQuest); // Same component, different screen-owner lease: old toolbar objects cannot revive.
    const TStrongObjectPtr<UProject_JUICommandButton> NewHistoryCommand(View->QuestHistoryButtons[1]);
    const auto BeforeObsoleteClick = SecondQuest->GetHistoryPage().Request;
    CollectGarbage(RF_NoFlags, true);
    OldHistoryCommand->OnClicked.Broadcast();
    TestTrue(TEXT("Old toolbar is inert after same-component UI handover and GC"), !OldHistoryCommand->GetIsEnabled()
        && NewHistoryCommand.Get() != OldHistoryCommand.Get() && SecondQuest->GetHistoryPage().Request == BeforeObsoleteClick);
    SecondUI->ClearSources(); // Still retains Screen==View, but cannot clear FirstUI's current toolbar.
    TestTrue(TEXT("Different UI cleanup cannot retire the new owner's history command"),
        View->QuestHistoryButtons.Num() == 5 && View->QuestHistoryButtons[1] == NewHistoryCommand.Get() && NewHistoryCommand->GetIsEnabled());
    NewHistoryCommand->OnClicked.Broadcast();
    TestTrue(TEXT("New current toolbar still issues its own fresh request"), SecondQuest->GetHistoryPage().Request != BeforeObsoleteClick);
    const TStrongObjectPtr<UProject_JUICommandButton> TeardownHistoryCommand(View->QuestHistoryButtons[4]);
    const TStrongObjectPtr<UProject_JUICommandButton> TeardownAction(View->QuestJournalRows.FindChecked(Full.States[0].QuestId).Action);

    View->NativeDestruct(); SecondPublications = 0; TeardownAction->OnClicked.Broadcast(); TeardownHistoryCommand->OnClicked.Broadcast(); FirstUI->OnQuestsChanged();
    TestTrue(TEXT("Native teardown retires old commands and refuses subsequent refresh on the retired view"), View->QuestJournalRows.IsEmpty()
        && View->QuestJournal->GetChildrenCount() == 0 && !TeardownAction->GetIsEnabled() && !TeardownHistoryCommand->GetIsEnabled()
        && View->QuestHistoryRows.IsEmpty() && View->QuestHistoryButtons.IsEmpty() && SecondPublications == 0);
    return !HasAnyErrors();
}
#endif
