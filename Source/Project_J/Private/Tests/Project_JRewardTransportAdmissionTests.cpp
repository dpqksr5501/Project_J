#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Project_JSourceAuditPhase2UIReceiver.h"
#include "Project_JMMOArchitectureReceiver.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Components/Project_JInventoryComponent.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JQuestComponent.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Backend/Project_JHandoverManager.h"
#include "Backend/Project_JHandoverTransport.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJQuestRewardRollbackTest, "ProjectJ.Continuation.QuestRewardRollback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJQuestRewardRollbackTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* State = World->SpawnActor<AProject_JPlayerState>();
    State->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(State, State);
    auto* Progression = State->FindComponentByClass<UProject_JProgressionComponent>();
    Progression->InitializeDefaults(nullptr, nullptr, 1);
    TestTrue(TEXT("Existing XP is authoritative"), Progression->GrantExperience(1));
    auto* Quest = State->FindComponentByClass<UProject_JQuestComponent>();
    auto* Definition = NewObject<UProject_JQuestDefinition>(State);
    Definition->QuestId = TEXT("RollbackReward"); Definition->ObjectiveEvent = TEXT("TrustedObjective");
    Definition->RequiredCount = 1; Definition->RewardExperience = MAX_int64;
    Quest->Definitions = {Definition};
    TestTrue(TEXT("Valid definition is accepted"), Quest->Accept(Definition->QuestId));
    Quest->RecordObjective(Definition->ObjectiveEvent, 1);
    // The owner previously chose to stop tracking. Reward rejection must not
    // turn that independent presentation choice back on.
    Quest->RequestTracking(Definition->QuestId, false);
    TestFalse(TEXT("Owner's tracking choice was applied"), Quest->States[0].bTracked);
    const auto Before = Quest->CaptureSnapshot();
    TestFalse(TEXT("Actual progression overflow rejects this reward"), Quest->Claim(Definition->QuestId));
    TestEqual(TEXT("Failed claim preserves tracking"), Quest->States[0].bTracked, Before.States[0].bTracked);
    TestEqual(TEXT("Failed claim preserves claim state"), Quest->States[0].bClaimed, Before.States[0].bClaimed);
    TestEqual(TEXT("Failed claim preserves objective count"), Quest->States[0].Count, Before.States[0].Count);
    TestEqual(TEXT("Rejected reward changes no XP"), Progression->GetExperience(), int64(1));
    Definition->RewardExperience = 100;
    TestTrue(TEXT("Subsequent valid reward can be claimed"), Quest->Claim(Definition->QuestId));
    TestFalse(TEXT("Committed reward cannot replay"), Quest->Claim(Definition->QuestId));
    TestEqual(TEXT("Valid reward was paid once"), Progression->GetState().Level, 2);
    // A real reward delegate may clear authoring/state, retire the quest and
    // collect it while Claim still owns a mutation guard on that component.
    int32 RetiredQuestPublications = 0, RewardCallbacks = 0;
    const int64 BeforeRetirementXP = Progression->GetExperience();
    auto* Retiring = NewObject<UProject_JQuestComponent>(State); Retiring->RegisterComponent();
    auto* RetiringDefinition = NewObject<UProject_JQuestDefinition>(Retiring);
    RetiringDefinition->QuestId = TEXT("RetiringReward"); RetiringDefinition->ObjectiveEvent = TEXT("RetiringObjective");
    RetiringDefinition->RequiredCount = 1; RetiringDefinition->RewardExperience = 1;
    Retiring->Definitions = {RetiringDefinition};
    const FName RetiringId = RetiringDefinition->QuestId;
    TestTrue(TEXT("Retirement reward is admitted"), Retiring->Accept(RetiringId));
    Retiring->RecordObjective(RetiringDefinition->ObjectiveEvent, 1);
    const auto Callback = Progression->OnChanged.AddLambda([&] {
        ++RewardCallbacks;
        Retiring->States.Reset(); Retiring->Definitions.Reset();
        Retiring->DestroyComponent();
        // Retire clears delegates. A late subscriber must not bypass CanPublish.
        Retiring->OnChanged.AddLambda([&] { ++RetiredQuestPublications; });
        CollectGarbage(RF_NoFlags, true);
    });
    const bool ClaimedDuringRetirement = Retiring->Claim(RetiringId);
    Progression->OnChanged.Remove(Callback);
    TestTrue(TEXT("Already paid XP remains successful when its reward callback retires the quest"), ClaimedDuringRetirement);
    TestEqual(TEXT("The actual progression callback ran once"), RewardCallbacks, 1);
    TestEqual(TEXT("Retired quest publishes no late change"), RetiredQuestPublications, 0);
    TestEqual(TEXT("Retirement does not replay or undo paid XP"), Progression->GetExperience(), BeforeRetirementXP + 1);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNativeQuestRewardBundleTest, "ProjectJ.Foundation.Quests.NativeRewardBundle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNativeQuestRewardBundleTest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> Game(NewObject<UGameInstance>(GEngine));
    Game->InitializeStandalone();
    UWorld* World = Game->GetWorld();
    ON_SCOPE_EXIT { if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit); Game->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Data = Game->GetSubsystem<UProject_JCharacterDataSubsystem>();
    auto* Health = LoadObject<UProject_JConsumableDefinition>(nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"));
    auto* Mana = LoadObject<UProject_JConsumableDefinition>(nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJManaPotion.DA_ProjectJManaPotion"));
    if (!TestTrue(TEXT("Native bundle uses actual canonical player item definitions"), Data && Data->EnsurePrimaryAssetDefinitions()
        && Health && Mana && Data->GetItemDefinition(Health->ItemId, Health->SchemaVersion) == Health
        && Data->GetItemDefinition(Mana->ItemId, Mana->SchemaVersion) == Mana && Health->MaxStackCount >= 3)) return false;
    const auto MakeOwner = [&]
    {
        auto* Player = World->SpawnActor<AProject_JPlayerState>();
        // A real normal progression state can exist before an Avatar/ActorInfo is bound. Do not synthesize an epoch.
        Player->FindComponentByClass<UProject_JProgressionComponent>()->InitializeDefaults(nullptr, nullptr, 1);
        return Player;
    };
    const auto MakeQuest = [&](AProject_JPlayerState* Player, FName Id, int64 XP, int32 HealthCount, int32 ManaCount)
    {
        auto* Quests = Player->FindComponentByClass<UProject_JQuestComponent>();
        auto* Reward = NewObject<UProject_JQuestDefinition>(Quests);
        Reward->QuestId = Id; Reward->ObjectiveEvent = Id; Reward->RewardExperience = XP;
        if (HealthCount) { auto& Line = Reward->RewardItems.AddDefaulted_GetRef(); Line.Definition = Health->ItemId; Line.DefinitionVersion = Health->SchemaVersion; Line.Count = HealthCount; }
        if (ManaCount) { auto& Line = Reward->RewardItems.AddDefaulted_GetRef(); Line.Definition = Mana->ItemId; Line.DefinitionVersion = Mana->SchemaVersion; Line.Count = ManaCount; }
        Quests->Definitions = {Reward};
        TestTrue(TEXT("Actual native reward definition accepts"), Quests->Accept(Id));
        Quests->RecordObjective(Id);
        return Reward;
    };
    auto* ItemOnlyPlayer = World->SpawnActor<AProject_JPlayerState>();
    auto* ItemOnlyProgression = ItemOnlyPlayer->FindComponentByClass<UProject_JProgressionComponent>();
    auto* ItemOnlyQuests = ItemOnlyPlayer->FindComponentByClass<UProject_JQuestComponent>();
    auto* ItemOnlyReward = MakeQuest(ItemOnlyPlayer, TEXT("NativeItemOnlyUninitialized"), 0, 1, 0);
    TestEqual(TEXT("Item-only control really has no initialized progression revision"), ItemOnlyProgression->GetState().Revision, 0);
    TestTrue(TEXT("Item-only reward retains native uninitialized progression compatibility"), ItemOnlyQuests->Claim(ItemOnlyReward->QuestId));
    TestTrue(TEXT("Item-only commit does not initialize or publish XP"), ItemOnlyProgression->GetState().Revision == 0
        && ItemOnlyProgression->GetExperience() == 0 && ItemOnlyQuests->HasCompleted(ItemOnlyReward->QuestId)
        && ItemOnlyPlayer->GetInventoryComponent()->GetItemInstances().Num() == 1);
    auto* Player = MakeOwner();
    auto* Quests = Player->FindComponentByClass<UProject_JQuestComponent>();
    auto* Progression = Player->FindComponentByClass<UProject_JProgressionComponent>();
    auto* Inventory = Player->GetInventoryComponent();
    const auto Locked = Inventory->AddItemDefinition(Health, 1);
    const auto Merge = Inventory->AddItemDefinition(Health, Health->MaxStackCount - 1);
    TestTrue(TEXT("Existing native transient lock is real"), Inventory->SetItemInstanceLocked(Locked.InstanceId, true, false));
    TestFalse(TEXT("Persistence validation remains strict about transient non-equipped locks"), Inventory->ValidatePersistentItems(Inventory->GetItemInstances(), true));
    auto* Reward = MakeQuest(Player, TEXT("NativeBundle"), 9, 3, 1);
    const auto CountItem = [&](UProject_JInventoryComponent* Bag, const UProject_JItemDefinition* Item)
    { int64 Count = 0; for (const auto& Entry : Bag->GetItemInstances()) if (Entry.ItemDef == Item) Count += Entry.StackCount; return Count; };
    const auto SeesWholeBundle = [&]
    {
        return Progression->GetExperience() == 9 && Quests->HasCompleted(Reward->QuestId) && Quests->States.IsEmpty()
            && CountItem(Inventory, Health) == int64(Health->MaxStackCount) + 3 && CountItem(Inventory, Mana) == 1;
    };
    int32 ProgressionNotifications = 0, ItemNotifications = 0, QuestNotifications = 0;
    bool WholeAtProgression = false, WholeAtItem = false, NestedRejected = false;
    const auto ProgressionHandle = Progression->OnChanged.AddLambda([&] { ++ProgressionNotifications; WholeAtProgression = SeesWholeBundle(); });
    const auto QuestHandle = Quests->OnChanged.AddLambda([&] { ++QuestNotifications; });
    TStrongObjectPtr<UProject_JSourceAuditPhase2UIReceiver> Receiver(NewObject<UProject_JSourceAuditPhase2UIReceiver>());
    Receiver->OnConsumption = [&]
    {
        ++ItemNotifications; Receiver->bConsumedCallback = false;
        WholeAtItem = SeesWholeBundle();
        NestedRejected = !Quests->Claim(Reward->QuestId) && !Progression->GrantExperience(1)
            && !Inventory->SetItemStackCount(Merge.InstanceId, 1);
    };
    Inventory->OnItemAdded.AddDynamic(Receiver.Get(), &UProject_JSourceAuditPhase2UIReceiver::OnItem);
    Inventory->OnItemChanged.AddDynamic(Receiver.Get(), &UProject_JSourceAuditPhase2UIReceiver::OnItem);
    ON_SCOPE_EXIT { Receiver->OnConsumption = {}; Inventory->OnItemAdded.RemoveDynamic(Receiver.Get(), &UProject_JSourceAuditPhase2UIReceiver::OnItem);
        Inventory->OnItemChanged.RemoveDynamic(Receiver.Get(), &UProject_JSourceAuditPhase2UIReceiver::OnItem);
        Progression->OnChanged.Remove(ProgressionHandle); Quests->OnChanged.Remove(QuestHandle); };
    TestTrue(TEXT("Unbound native ASC/lease does not require fake identity initialization"), Player->GetProjectJAbilitySystemComponent()->GetActorInfoRevision() == 0);
    TestTrue(TEXT("One actual native Claim installs the item/XP/history bundle"), Quests->Claim(Reward->QuestId));
    TestTrue(TEXT("The first XP and item callbacks already see all committed values"), WholeAtProgression && WholeAtItem);
    TestTrue(TEXT("Existing native guards reject reentrant XP, item and duplicate quest edits"), NestedRejected);
    TestEqual(TEXT("XP publishes once"), ProgressionNotifications, 1);
    TestEqual(TEXT("Quest publishes once after bundle observers"), QuestNotifications, 1);
    TestEqual(TEXT("One merged and two new item rows publish actual deltas"), ItemNotifications, 3);
    FProject_JItemInstanceData KeptLock;
    TestTrue(TEXT("Existing transient lock and its stack survive without reward merging"), Inventory->FindItemInstance(Locked.InstanceId, KeptLock)
        && KeptLock.bIsLocked && !KeptLock.bIsEquipped && KeptLock.StackCount == 1);
    TestFalse(TEXT("Paid native bundle cannot replay"), Quests->Claim(Reward->QuestId));

    auto* FullPlayer = MakeOwner(); auto* FullBag = FullPlayer->GetInventoryComponent();
    auto* FullQuests = FullPlayer->FindComponentByClass<UProject_JQuestComponent>();
    auto* FullProgression = FullPlayer->FindComponentByClass<UProject_JProgressionComponent>();
    TArray<FProject_JItemInstanceData> FullItems;
    for (int32 Index = 0; Index < 4096; ++Index)
    { auto& Item = FullItems.AddDefaulted_GetRef(); Item.InstanceId = FGuid::NewGuid(); Item.ItemDef = Health;
      Item.StackCount = Health->MaxStackCount; Item.BagOrder = Index; }
    FullBag->InstallPersistentItems(FullItems);
    auto* FullReward = MakeQuest(FullPlayer, TEXT("NativeFull"), 7, 1, 1);
    const auto FullBefore = FullQuests->CaptureSnapshot();
    TestFalse(TEXT("A full actual bag rejects the entire bundle"), FullQuests->Claim(FullReward->QuestId));
    TestTrue(TEXT("Full bag failure preserves every original item"), FullBag->GetItemInstances() == FullItems);
    TestTrue(TEXT("Full bag failure preserves current quest/history and XP"), FullQuests->States == FullBefore.States
        && FullQuests->GetCompletedCount() == 0 && FullProgression->GetExperience() == 0);

    auto* OverflowPlayer = MakeOwner();
    auto* OverflowProgression = OverflowPlayer->FindComponentByClass<UProject_JProgressionComponent>();
    auto* OverflowQuests = OverflowPlayer->FindComponentByClass<UProject_JQuestComponent>();
    TestTrue(TEXT("XP overflow baseline is a real public grant"), OverflowProgression->GrantExperience(1));
    auto* OverflowReward = MakeQuest(OverflowPlayer, TEXT("NativeOverflow"), MAX_int64, 1, 1);
    const auto OverflowBefore = OverflowQuests->CaptureSnapshot();
    TestFalse(TEXT("XP overflow rejects items and completion together"), OverflowQuests->Claim(OverflowReward->QuestId));
    TestTrue(TEXT("Overflow changes no owned state"), OverflowPlayer->GetInventoryComponent()->GetItemInstances().IsEmpty()
        && OverflowProgression->GetExperience() == 1 && OverflowQuests->States == OverflowBefore.States && !OverflowQuests->HasCompleted(OverflowReward->QuestId));

    auto* StalePlayer = MakeOwner();
    auto* StaleQuests = StalePlayer->FindComponentByClass<UProject_JQuestComponent>();
    auto* StaleProgression = StalePlayer->FindComponentByClass<UProject_JProgressionComponent>();
    auto* StaleReward = MakeQuest(StalePlayer, TEXT("NativeRemainingState"), 5, 1, 1);
    auto* OtherQuest = NewObject<UProject_JQuestDefinition>(StaleQuests);
    OtherQuest->QuestId = TEXT("NativeOtherActive"); OtherQuest->ObjectiveEvent = OtherQuest->QuestId; OtherQuest->RequiredCount = 2;
    StaleQuests->Definitions.Add(OtherQuest);
    TestTrue(TEXT("Another real active quest can progress before a definition edit"), StaleQuests->Accept(OtherQuest->QuestId));
    StaleQuests->RecordObjective(OtherQuest->ObjectiveEvent, 2);
    OtherQuest->RequiredCount = 1; // Public authoring edit in memory; its existing progress is now out of range.
    const auto StaleBefore = StaleQuests->CaptureSnapshot();
    TestFalse(TEXT("An invalid remaining quest state rejects the whole proposed bundle before any install"), StaleQuests->Claim(StaleReward->QuestId));
    TestTrue(TEXT("Other-state rejection preserves all quest, item and XP ownership"), StaleQuests->States == StaleBefore.States
        && StaleQuests->GetCompletedCount() == 0 && StaleProgression->GetExperience() == 0
        && StalePlayer->GetInventoryComponent()->GetItemInstances().IsEmpty());
    OtherQuest->RequiredCount = 2;
    TestTrue(TEXT("Restoring valid remaining state permits the original unpaid claim"), StaleQuests->Claim(StaleReward->QuestId));

    auto* QueryPlayer = World->SpawnActor<AProject_JQuestRewardQueryFixture>();
    auto* QueryProgression = QueryPlayer->FindComponentByClass<UProject_JProgressionComponent>();
    QueryProgression->InitializeDefaults(nullptr, nullptr, 1);
    auto* QueryQuests = QueryPlayer->FindComponentByClass<UProject_JQuestComponent>();
    auto* QueryReward = MakeQuest(QueryPlayer, TEXT("NativeFinalRulesFence"), 5, 1, 1);
    bool AtReservedGetter = false;
    int32 Mutation = 0, NewWriterCalls = 0;
    const auto RejectPersistent = [&](const FProject_JPersistentEdit&, FProject_JPersistentCompletion) { ++NewWriterCalls; return false; };
    TFunction<void()> LateQuery;
    LateQuery = [&]
    {
        int32 Resolved = 0;
        // Public admission can observe the real mutation guard without reading or seeding a protected flag.
        if (!QueryProgression->CanSetLevel(QueryProgression->GetState().Level, Resolved))
        {
            AtReservedGetter = true;
            if (Mutation == 1) ++QueryReward->SchemaVersion;
            else if (Mutation == 2) QueryQuests->SubmitPersistentEdit = RejectPersistent;
            else if (Mutation == 3) QueryPlayer->GetInventoryComponent()->SubmitPersistentEdit = RejectPersistent;
            else if (Mutation == 4) QueryProgression->SubmitPersistentEdit = RejectPersistent;
            else ++QueryReward->RequiredCount;
            CollectGarbage(RF_NoFlags, true);
        }
        else QueryPlayer->OnASCQuery = LateQuery;
    };
    ON_SCOPE_EXIT { QueryPlayer->OnASCQuery = {}; LateQuery = {}; };
    for (int32 Case : {0, 1, 2, 3, 4})
    {
        Mutation = Case; AtReservedGetter = false;
        QueryPlayer->OnASCQuery = LateQuery;
        TestFalse(TEXT("The final real virtual ASC getter cannot change rules or writer policy and receive the old bundle"), QueryQuests->Claim(QueryReward->QuestId));
        TestTrue(TEXT("Negative control reached the real reserved final getter window"), AtReservedGetter);
        TestTrue(TEXT("Late rule changes publish no item, XP or completion"), QueryPlayer->GetInventoryComponent()->GetItemInstances().IsEmpty()
            && QueryProgression->GetExperience() == 0 && QueryQuests->GetCompletedCount() == 0 && QueryQuests->States.Num() == 1);
        QueryReward->RequiredCount = 1; QueryReward->SchemaVersion = 1; QueryPlayer->OnASCQuery = {};
        QueryQuests->SubmitPersistentEdit = {}; QueryPlayer->GetInventoryComponent()->SubmitPersistentEdit = {}; QueryProgression->SubmitPersistentEdit = {};
    }
    TestEqual(TEXT("A retired native intent is never redirected into the replacement persistent writer"), NewWriterCalls, 0);
    TestTrue(TEXT("A fresh unchanged-rules retry remains usable"), QueryQuests->Claim(QueryReward->QuestId));

    auto* RebindingPlayer = MakeOwner();
    auto* RebindingBag = RebindingPlayer->GetInventoryComponent();
    auto* RebindingQuests = RebindingPlayer->FindComponentByClass<UProject_JQuestComponent>();
    auto* RebindingProgression = RebindingPlayer->FindComponentByClass<UProject_JProgressionComponent>();
    auto* RebindingReward = MakeQuest(RebindingPlayer, TEXT("NativeSourceFence"), 4, 1, 1);
    bool Rebound = false;
    RebindingBag->PersistentAdmission = [&]
    {
        if (!Rebound) { Rebound = true; RebindingPlayer->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(RebindingPlayer, RebindingPlayer); CollectGarbage(RF_NoFlags, true); }
        return true;
    };
    ON_SCOPE_EXIT { RebindingBag->PersistentAdmission = {}; };
    TestFalse(TEXT("A real preflight admission callback replacing the ASC epoch rejects the old bundle"), RebindingQuests->Claim(RebindingReward->QuestId));
    RebindingBag->PersistentAdmission = {};
    TestTrue(TEXT("Source replacement publishes no partial items, XP or completion"), Rebound && RebindingBag->GetItemInstances().IsEmpty()
        && RebindingProgression->GetExperience() == 0 && !RebindingQuests->HasCompleted(RebindingReward->QuestId));

    int32 RebindingItemNotifications = 0, RebindingQuestNotifications = 0;
    const auto RebindingQuestHandle = RebindingQuests->OnChanged.AddLambda([&] { ++RebindingQuestNotifications; });
    TStrongObjectPtr<UProject_JSourceAuditPhase2UIReceiver> RebindingReceiver(NewObject<UProject_JSourceAuditPhase2UIReceiver>());
    RebindingReceiver->OnConsumption = [&]
    {
        ++RebindingItemNotifications; RebindingReceiver->bConsumedCallback = false;
        RebindingPlayer->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(RebindingPlayer, RebindingPlayer);
        CollectGarbage(RF_NoFlags, true);
    };
    RebindingBag->OnItemAdded.AddDynamic(RebindingReceiver.Get(), &UProject_JSourceAuditPhase2UIReceiver::OnItem);
    ON_SCOPE_EXIT { RebindingReceiver->OnConsumption = {}; RebindingBag->OnItemAdded.RemoveDynamic(RebindingReceiver.Get(), &UProject_JSourceAuditPhase2UIReceiver::OnItem);
        RebindingQuests->OnChanged.Remove(RebindingQuestHandle); };
    TestTrue(TEXT("A valid retry installs once before callback replacement"), RebindingQuests->Claim(RebindingReward->QuestId));
    TestEqual(TEXT("First item callback source replacement suppresses later item deltas"), RebindingItemNotifications, 1);
    TestEqual(TEXT("Old bundle has no late quest publication after source replacement"), RebindingQuestNotifications, 0);
    TestTrue(TEXT("Already installed bundle is not rolled back after callback/GC"), RebindingProgression->GetExperience() == 4
        && RebindingQuests->HasCompleted(RebindingReward->QuestId) && CountItem(RebindingBag, Health) == 1 && CountItem(RebindingBag, Mana) == 1);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLoopbackFiniteClockTest, "ProjectJ.Continuation.LoopbackFiniteClock",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJLoopbackFiniteClockTest::RunTest(const FString&)
{
    FProject_JHandoverTransportRequest Request; Request.Envelope.TransferId = FGuid::NewGuid();
    for (float Invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        auto Transport = MakeShared<FProject_JLoopbackHandoverTransport>(); Transport->SetResponseDelay(1);
        int32 Replies = 0;
        Transport->Send(Request, [&](const auto&) { ++Replies; });
        Transport->Tick(Invalid);
        TestEqual(TEXT("Invalid elapsed sample cannot manufacture an ack"), Replies, 0);
        Transport->Tick(1);
        TestEqual(TEXT("Invalid elapsed sample cannot poison later valid time"), Replies, 1);
        Transport = MakeShared<FProject_JLoopbackHandoverTransport>(); Transport->SetResponseDelay(1);
        Transport->SetResponseDelay(Invalid); Replies = 0;
        Transport->Send(Request, [&](const auto&) { ++Replies; });
        Transport->Tick(0.25f);
        TestEqual(TEXT("Invalid tuning preserves the last finite delay"), Replies, 0);
        Transport->Tick(0.75f);
        TestEqual(TEXT("Preserved finite delay still completes"), Replies, 1);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJHandoverConfigurationTest, "ProjectJ.Continuation.HandoverConfiguration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJHandoverConfigurationTest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
    for (int32 Field = 0; Field < 4; ++Field) {
        for (float Invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.f}) {
            TStrongObjectPtr<UProject_JHandoverManager> Manager(NewObject<UProject_JHandoverManager>(GI.Get()));
            auto Transport = MakeShared<FProject_JLoopbackHandoverTransport>(); Transport->SetDropResponses(true);
            Manager->SetTransport(Transport);
            float* Value = Field == 0 ? &Manager->MaxEnvelopeAgeSeconds : Field == 1 ? &Manager->AckTimeoutSeconds
                : Field == 2 ? &Manager->RetryDelaySeconds : &Manager->TerminalRecordTtlSeconds;
            *Value = Invalid;
            FProject_JHandoverEnvelope Envelope;
            Envelope.TransferId = FGuid::NewGuid(); Envelope.Timestamp = FDateTime::UtcNow();
            Envelope.ActorClassPath = TEXT("/Script/Engine.Actor"); Envelope.SourceNodeId = TEXT("Source"); Envelope.TargetNodeId = TEXT("Target");
            Envelope.PayloadChecksum = Manager->CalculatePayloadChecksum(Envelope.Payload);
            TestEqual(TEXT("Bad tuning is a typed configuration failure, not a malformed timestamp"),
                Manager->ValidateEnvelope(Envelope), EProject_JHandoverValidationFailure::InvalidConfiguration);
            TestEqual(TEXT("Invalid server tuning is rejected before transport publication"),
                Manager->StartEnvelopeTransferChecked(Envelope), EProject_JHandoverAdmission::Invalid);
            TestEqual(TEXT("Bad config publishes no request record"), Manager->GetHandoverState(Envelope.TransferId), EProject_JHandoverState::None);
            Manager->Deinitialize();
        }
    }
    TStrongObjectPtr<UProject_JHandoverManager> Manager(NewObject<UProject_JHandoverManager>(GI.Get()));
    Manager->MaxEnvelopeAgeSeconds = 0; // Audit closure now requires bounded envelope validity.
    Manager->AckTimeoutSeconds = 0.05f; Manager->RetryDelaySeconds = 0; Manager->TerminalRecordTtlSeconds = 1;
    FProject_JHandoverEnvelope Envelope;
    Envelope.TransferId = FGuid::NewGuid(); Envelope.Timestamp = FDateTime::UtcNow() - FTimespan::FromHours(1);
    Envelope.ActorClassPath = TEXT("/Script/Engine.Actor"); Envelope.SourceNodeId = TEXT("Source"); Envelope.TargetNodeId = TEXT("Target");
    Envelope.PayloadChecksum = Manager->CalculatePayloadChecksum(Envelope.Payload);
    TestNotEqual(TEXT("Zero age cannot publish an envelope without persistent replay protection"),
        Manager->ValidateEnvelope(Envelope), EProject_JHandoverValidationFailure::None);
    Manager->MaxEnvelopeAgeSeconds = 30;
    Envelope.Timestamp = FDateTime::UtcNow();
    auto Transport = MakeShared<FProject_JLoopbackHandoverTransport>(); Transport->SetDropResponses(true);
    Manager->SetTransport(Transport);
    TestTrue(TEXT("Valid boundary configuration can publish"), Manager->StartEnvelopeTransfer(Envelope));
    Manager->AckTimeoutSeconds = std::numeric_limits<float>::infinity();
    Manager->TickHandover(1);
    FProject_JHandoverRecord Record; Manager->GetHandoverRecord(Envelope.TransferId, Record);
    TestEqual(TEXT("Invalid runtime config cannot change a live timer"), Record.StateElapsedTime, 0.f);
    Manager->AckTimeoutSeconds = 5; Manager->TickHandover(0.25f);
    Manager->GetHandoverRecord(Envelope.TransferId, Record);
    TestEqual(TEXT("Restored valid config resumes from the original finite elapsed value"), Record.StateElapsedTime, 0.25f);
    Manager->Deinitialize();
    return !HasAnyErrors();
}
#endif
