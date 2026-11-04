#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Backend/Project_JProfileRules.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JQuestComponent.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJQuestRewardRulesTest, "ProjectJ.MMO.Persistence.QuestItemRewardRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJQuestRewardRulesTest::RunTest(const FString&)
{
    using namespace ProjectJ::LocalProfileRules;
    const TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
    Instance->InitializeStandalone();
    auto* World = Instance->GetWorld();
    ON_SCOPE_EXIT
    {
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        Instance->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    };
    auto* Data = Instance->GetSubsystem<UProject_JCharacterDataSubsystem>();
    if (!TestTrue(TEXT("Existing canonical registry loads for reward rule capture"), Data && Data->LoadPrimaryAssetDefinitions())) return false;
    auto* Health = LoadObject<UProject_JConsumableDefinition>(nullptr,
        TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"));
    if (!TestTrue(TEXT("Reward uses the exact canonical registered item/version"), Health &&
        Data->GetItemDefinition(Health->ItemId, Health->SchemaVersion) == Health)) return false;
    auto* Owner = World->SpawnActor<AProject_JPlayerState>();
    auto* Progress = Owner->FindComponentByClass<UProject_JProgressionComponent>();
    auto* Quests = Owner->FindComponentByClass<UProject_JQuestComponent>();
    auto* Definition = NewObject<UProject_JQuestDefinition>(Quests);
    Definition->QuestId = TEXT("RewardRules.Canonical"); Definition->ObjectiveEvent = TEXT("RewardRules.Event");
    Definition->RewardExperience = 7; Quests->Definitions = {Definition};
    FProject_JQuestSnapshot Scope; Scope.States.Add({Definition->QuestId, 0, false, true});
    FProject_JQuestDefinitionIndex Index; Quests->BuildDefinitionIndex(Index);
    const FString EmptyDigest = Fingerprint(*Progress, Index, nullptr, {}, &Scope);
    FQuestGuard EmptyGuard;
    TestTrue(TEXT("Empty bundles retain their no-registry legacy guard contract"),
        !EmptyDigest.IsEmpty() && EmptyGuard.Capture(Index, Scope) && EmptyGuard.IsCurrent(Index) && EmptyGuard.IsCurrent(Index, Data));
    TArray<ProjectJ::Crafting::FResolvedLine> Frozen;
    TestTrue(TEXT("Empty rewards capture without any registry or loading"), Definition->CaptureRewardItems(nullptr, Frozen) && Frozen.IsEmpty());

    FProject_JQuestRewardItem Reward;
    Reward.Definition = Health->ItemId; Reward.DefinitionVersion = Health->SchemaVersion; Reward.Count = 3;
    Definition->RewardItems = {Reward}; Quests->BuildDefinitionIndex(Index);
    TestTrue(TEXT("A valid item bundle freezes quantity/level/version and canonical stackability"),
        Definition->CaptureRewardItems(Data, Frozen) && Frozen.Num() == 1 && Frozen[0].Definition == Reward.Definition &&
        Frozen[0].DefinitionVersion == Reward.DefinitionVersion && Frozen[0].Level == 1 && Frozen[0].CountPerBatch == 3 &&
        Frozen[0].MaxStackCount == Health->MaxStackCount);
    FQuestGuard BundleGuard;
    const FString BundleDigest = Fingerprint(*Progress, Index, nullptr, {}, &Scope);
    TestTrue(TEXT("Nonempty rewards extend the accepted rule digest and require canonical capture"),
        !BundleDigest.IsEmpty() && BundleDigest != EmptyDigest && !BundleGuard.Capture(Index, Scope) &&
        BundleGuard.Capture(Index, Scope, Data) && BundleGuard.IsCurrent(Index, Data) && !EmptyGuard.IsCurrent(Index, Data));
    auto Sentinel = Frozen; Sentinel[0].Definition = TEXT("UnchangedOnFailure");
    TestFalse(TEXT("Unresolved reward definitions fail before changing the capture output"), Definition->CaptureRewardItems(nullptr, Sentinel));
    TestTrue(TEXT("Failed capture preserves the supplied values"), Sentinel.Num() == 1 && Sentinel[0].Definition == FName(TEXT("UnchangedOnFailure")));
    ++Definition->RewardItems[0].Count;
    TestFalse(TEXT("Quantity edits invalidate an already accepted bundle rule"), BundleGuard.IsCurrent(Index, Data));
    --Definition->RewardItems[0].Count;
    {
        // Simulate a public canonical authoring edit in memory only; no package is saved.
        const int32 ChangedStack = Health->MaxStackCount == MAX_int32 ? MAX_int32 - 1 : Health->MaxStackCount + 1;
        TGuardValue<int32> StackEdit(Health->MaxStackCount, ChangedStack);
        TestFalse(TEXT("Canonical stack-rule edits also invalidate the frozen bundle contract"), BundleGuard.IsCurrent(Index, Data));
    }
    TestTrue(TEXT("Restoring authoring terms restores the exact original bundle digest"),
        BundleGuard.IsCurrent(Index, Data) && BundleDigest == Fingerprint(*Progress, Index, nullptr, {}, &Scope));
    Definition->RewardItems.Add(Reward);
    TestFalse(TEXT("Duplicate item IDs cannot select ambiguous grant terms"), Definition->IsRuntimeValid());
    Definition->RewardItems.SetNum(1);
    Definition->RewardItems[0].DefinitionVersion = MAX_int32;
    TestFalse(TEXT("A missing canonical item version cannot be paid from a fallback definition"), Definition->CaptureRewardItems(Data, Sentinel));
    Definition->RewardItems[0] = Reward;

    ProjectJ::QuestHistory::FViewPtr History; FString Failure;
    if (!TestTrue(TEXT("A confirmed completion can be represented independently of current reward rules"),
        ProjectJ::QuestHistory::Append({}, Definition->QuestId, History, Failure))) return false;
    FProject_JQuestSnapshot Completed; Completed.Version = 3; Completed.History = History;
    Completed.HistoryCount = 1; Completed.HistoryRootDigest = ProjectJ::QuestHistory::RootDigest(History);
    FQuestGuard CompletedGuard;
    TestTrue(TEXT("Archived completions require neither a reward catalog nor their old definition"), CompletedGuard.Capture(Index, Completed));
    Quests->Definitions.Reset(); Quests->BuildDefinitionIndex(Index);
    TestTrue(TEXT("Removing a paid definition cannot reopen or invalidate its completion fact"), CompletedGuard.IsCurrent(Index));
    Definition->RewardItems.Reset(); Quests->Definitions = {Definition}; Quests->BuildDefinitionIndex(Index);
    TestEqual(TEXT("An empty bundle preserves the pre-bundle legacy rule bytes/hash"), Fingerprint(*Progress, Index, nullptr, {}, &Scope), EmptyDigest);
    return true;
}
#endif
