#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Backend/Project_JProfileRules.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/Project_JPlayerState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProfileRuleCompatibilityTest, "ProjectJ.MMO.Persistence.QuestRuleCompatibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJProfileRuleCompatibilityTest::RunTest(const FString&)
{
    using namespace ProjectJ::LocalProfileRules;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Player = World->SpawnActor<AProject_JPlayerState>();
    auto* Progress = Player->FindComponentByClass<UProject_JProgressionComponent>();
    auto* Quests = Player->FindComponentByClass<UProject_JQuestComponent>();
    auto* Existing = NewObject<UProject_JQuestDefinition>(Player);
    Existing->QuestId = TEXT("RuleCompatibility.Existing"); Existing->ObjectiveEvent = TEXT("ExistingObjective");
    Existing->RequiredCount = 2; Existing->RewardExperience = 7;
    auto* Additional = NewObject<UProject_JQuestDefinition>(Player);
    Additional->QuestId = TEXT("RuleCompatibility.Additional"); Additional->ObjectiveEvent = TEXT("AdditionalObjective");
    Quests->Definitions = {Existing};
    FProject_JLocalProfileSnapshot Saved;
    Saved.Format = 2; // Preserve this test's genuine legacy-format contract.
    Saved.Account = FGuid::NewGuid(); Saved.Character = FGuid::NewGuid();
    Saved.Progression = Progress->CaptureSnapshot();
    FProject_JQuestState State; State.QuestId = Existing->QuestId; State.Count = 2; State.bClaimed = true; State.bTracked = false;
    Saved.Quests.States = {State};
    FProject_JQuestDefinitionIndex Index; Quests->BuildDefinitionIndex(Index);
    Saved.RulesFingerprint = Fingerprint(*Progress, Index, nullptr, {}, nullptr);
    TestTrue(TEXT("Legacy all-catalog rule payload still matches its exact definitions"), Matches(Saved, *Progress, Index, nullptr, {}));
    const FString Legacy = Saved.RulesFingerprint;
    Quests->Definitions.Add(Additional); Quests->BuildDefinitionIndex(Index);
    TestFalse(TEXT("Legacy digest cannot guess a missing historical catalog after new content deployment"), Matches(Saved, *Progress, Index, nullptr, {}));
    Quests->Definitions = {Existing}; Quests->BuildDefinitionIndex(Index);
    Saved.RulesFingerprint = Fingerprint(*Progress, Index, nullptr, {}, &Saved.Quests);
    TestTrue(TEXT("New scoped form is domain-separated from legacy even for identical selected IDs"), Saved.RulesFingerprint != Legacy);
    Quests->Definitions = {Additional, Existing}; Quests->BuildDefinitionIndex(Index);
    TestTrue(TEXT("Unaccepted content addition and definition order do not invalidate saved claimed progress"), Matches(Saved, *Progress, Index, nullptr, {}));
    TArray<uint8> Bytes; FProject_JLocalProfileSnapshot Decoded;
    TestTrue(TEXT("Existing Format2 codec round-trips the scoped rule digest without new document fields"),
        Saved.Encode(Bytes) && FProject_JLocalProfileSnapshot::Decode(Bytes, Decoded) && Decoded.Format == 2 && Decoded.RulesFingerprint == Saved.RulesFingerprint);
    auto Active = Saved; Active.Quests.States[0].bClaimed = false; Active.Quests.States[0].Count = 1;
    TestTrue(TEXT("Progress/claim status does not change which quest contract is protected"), Matches(Active, *Progress, Index, nullptr, {}));
    FQuestGuard Guard; TestTrue(TEXT("Live guard captures accepted quest contracts"), Guard.Capture(Index, Saved.Quests));
    Additional->RewardExperience = 999; ++Additional->SchemaVersion; Quests->BuildDefinitionIndex(Index);
    TestTrue(TEXT("Unaccepted rule changes cannot poison the established character scope"), Guard.IsCurrent(Index) && Matches(Saved, *Progress, Index, nullptr, {}));
    Quests->Definitions = {Existing}; Quests->BuildDefinitionIndex(Index);
    TestTrue(TEXT("Unaccepted definition removal remains compatible"), Matches(Saved, *Progress, Index, nullptr, {}));
    auto CheckChanged = [&] {
        Quests->BuildDefinitionIndex(Index);
        TestFalse(TEXT("Changed claimed quest semantics require explicit migration"), Matches(Saved, *Progress, Index, nullptr, {}));
        TestFalse(TEXT("The same changed contract also rejects active progress"), Matches(Active, *Progress, Index, nullptr, {}));
        TestFalse(TEXT("Same live change cannot be adopted by a checkpoint or reward command"), Guard.IsCurrent(Index));
    };
    ++Existing->SchemaVersion; CheckChanged(); --Existing->SchemaVersion;
    ++Existing->RewardExperience; CheckChanged(); --Existing->RewardExperience;
    ++Existing->RequiredCount; CheckChanged(); --Existing->RequiredCount;
    Existing->ObjectiveEvent = TEXT("ChangedObjective"); CheckChanged(); Existing->ObjectiveEvent = TEXT("ExistingObjective");
    Existing->bTravelObjective = true; CheckChanged(); Existing->bTravelObjective = false;
    Quests->Definitions.Reset(); CheckChanged();
    Quests->Definitions = {Existing, Additional}; Quests->BuildDefinitionIndex(Index);
    FProject_JQuestState Next; Next.QuestId = Additional->QuestId; Saved.Quests.States.Add(Next);
    TestTrue(TEXT("Newly accepted quest can extend a still-current guard"), Guard.IsCurrent(Index) && Guard.Capture(Index, Saved.Quests));
    ++Additional->RewardExperience; Quests->BuildDefinitionIndex(Index);
    TestFalse(TEXT("Newly accepted scope is now protected against later semantic changes"), Guard.IsCurrent(Index));
    Saved.Quests.States.Add(Next);
    TestTrue(TEXT("Duplicate accepted IDs cannot produce a scoped digest"), Fingerprint(*Progress, Index, nullptr, {}, &Saved.Quests).IsEmpty());
    auto* Compound = NewObject<UProject_JQuestDefinition>(Player);
    Compound->QuestId = TEXT("RuleCompatibility.Compound"); Compound->RewardExperience = 9;
    auto& Gather = Compound->Objectives.AddDefaulted_GetRef(); Gather.ObjectiveId = TEXT("Collect");
    Gather.Kind = EProject_JQuestObjectiveKind::GatherItem; Gather.TargetId = TEXT("HealthPotion"); Gather.RequiredCount = 2;
    auto& Craft = Compound->Objectives.AddDefaulted_GetRef(); Craft.ObjectiveId = TEXT("Brew");
    Craft.Kind = EProject_JQuestObjectiveKind::CraftRecipe; Craft.TargetId = TEXT("Prototype.Alchemy.HealthToMana");
    Quests->Definitions = {Existing, Compound}; Quests->BuildDefinitionIndex(Index);
    TestTrue(TEXT("Existing scoped schema1 hash survives unused compound content"), Matches(Active, *Progress, Index, nullptr, {}));
    auto OldGlobal = Active; OldGlobal.RulesFingerprint = Legacy;
    TestFalse(TEXT("Legacy whole-catalog history is not guessed after compound catalog expansion"), Matches(OldGlobal, *Progress, Index, nullptr, {}));
    FProject_JLocalProfileSnapshot CompoundSaved;
    CompoundSaved.Format = 2; // Nested quest schema2 already existed in Format2.
    CompoundSaved.Account = FGuid::NewGuid(); CompoundSaved.Character = FGuid::NewGuid(); CompoundSaved.Progression = Progress->CaptureSnapshot();
    TestTrue(TEXT("Compound acceptance creates typed stable objective counters"), Quests->PlanAcceptance(Compound->QuestId, CompoundSaved.Quests));
    CompoundSaved.RulesFingerprint = Fingerprint(*Progress, Index, nullptr, {}, &CompoundSaved.Quests);
    FQuestGuard CompoundGuard; TestTrue(TEXT("Compound guard captures all AND semantics"), CompoundGuard.Capture(Index, CompoundSaved.Quests));
    const auto Rules = CompoundSaved.RulesFingerprint;
    Swap(Compound->Objectives[0], Compound->Objectives[1]); Compound->Title = FText::FromString(TEXT("Cosmetic quest title"));
    Compound->Objectives[0].Title = FText::FromString(TEXT("Cosmetic objective title")); Quests->BuildDefinitionIndex(Index);
    TestTrue(TEXT("Stable ID canonicalization ignores display text and authored objective order"),
        Fingerprint(*Progress, Index, nullptr, {}, &CompoundSaved.Quests) == Rules && CompoundGuard.IsCurrent(Index));
    ++Compound->Objectives[0].TargetVersion; Quests->BuildDefinitionIndex(Index);
    TestFalse(TEXT("Target version change requires explicit saved rule migration"), Matches(CompoundSaved, *Progress, Index, nullptr, {}) || CompoundGuard.IsCurrent(Index));
    --Compound->Objectives[0].TargetVersion; Quests->BuildDefinitionIndex(Index);
    TestTrue(TEXT("Format2 carries nested schema2 with bounded objective values"), CompoundSaved.Encode(Bytes) && FProject_JLocalProfileSnapshot::Decode(Bytes, Decoded) && Decoded.Quests.Version == 2 && Decoded.Quests.States == CompoundSaved.Quests.States);
    auto Bad = CompoundSaved; Bad.Quests.States[0].Objectives[0].ObjectiveId = TEXT("Unknown");
    TestFalse(TEXT("Unknown objective IDs cannot restore against current canonical definitions"), Quests->CanRestoreSnapshot(Bad.Quests));
    Bad = CompoundSaved;
    const FProject_JQuestObjectiveState DuplicateObjective = Bad.Quests.States[0].Objectives[0];
    Bad.Quests.States[0].Objectives.Add(DuplicateObjective);
    TestFalse(TEXT("Worker codec rejects duplicate objective IDs before any publication"), Bad.Encode(Bytes));
    Bad = CompoundSaved; Bad.Quests.States[0].Count = 1;
    TestFalse(TEXT("Compound scalar Count cannot become a second progress writer"), Bad.Encode(Bytes));
    Bad = CompoundSaved;
    for (int32 I = 0; I < 7; ++I) { auto& O = Bad.Quests.States[0].Objectives.AddDefaulted_GetRef(); O.ObjectiveId = FName(*FString::Printf(TEXT("Overflow%d"), I)); }
    TestFalse(TEXT("Worker codec rejects more than eight objectives before allocating live state"), Bad.Encode(Bytes));
    Compound->Objectives.SetNum(1); Quests->BuildDefinitionIndex(Index);
    TestNull(TEXT("Compound authoring requires at least two objectives"), Index.FindRef(Compound->QuestId));
    return true;
}
#endif
