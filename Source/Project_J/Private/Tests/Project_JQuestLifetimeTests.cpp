#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JQuestComponent.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Project_JAbilitySystemComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJQuestLifetimeTest, "ProjectJ.RuntimeOwnership.QuestRetirement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJQuestLifetimeTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* State = World->SpawnActor<AProject_JPlayerState>();
    State->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(State, State);
    auto* Progression = State->FindComponentByClass<UProject_JProgressionComponent>();
    Progression->InitializeDefaults(nullptr, nullptr, 1);
    auto* Definition = LoadObject<UProject_JQuestDefinition>(nullptr,
        TEXT("/Game/UI/Gameplay/DA_ProjectJFirstSteps.DA_ProjectJFirstSteps"));
    if (!TestNotNull(TEXT("Authored travel quest"), Definition)) { return false; }
    auto* Quest = State->FindComponentByClass<UProject_JQuestComponent>();
    Quest->Definitions = {Definition};
    TestTrue(TEXT("Actual data accepts before BeginPlay"), Quest->Accept(Definition->QuestId));
    TestTrue(TEXT("Authored objective owns a travel timer"), World->GetTimerManager().TimerExists(Quest->TravelTimer));
    Quest->RecordObjective(Definition->ObjectiveEvent, Definition->RequiredCount);
    Quest->DestroyComponent();
    TestFalse(TEXT("Destroy before BeginPlay retires the owned timer"), World->GetTimerManager().TimerExists(Quest->TravelTimer));
    TestFalse(TEXT("Retired quest cannot award XP"), Quest->Claim(Definition->QuestId));
    TestEqual(TEXT("Retired claim preserves progression"), Progression->GetState().Level, 1);
    auto* Retired = NewObject<UProject_JQuestComponent>(State); Retired->RegisterComponent();
    Retired->Definitions = {Definition}; Retired->DestroyComponent();
    TestFalse(TEXT("Late accept cannot restart a destroyed component"), Retired->Accept(Definition->QuestId));
    FProject_JQuestSnapshot Snapshot; FProject_JQuestState Entry; Entry.QuestId = Definition->QuestId;
    Snapshot.States.Add(Entry);
    TestFalse(TEXT("Late restore cannot create resources on a destroyed component"), Retired->RestoreSnapshot(Snapshot));
    auto* Pending = NewObject<UProject_JQuestComponent>(State); Pending->RegisterComponent();
    Pending->Definitions = {Definition}; Pending->Accept(Definition->QuestId);
    TestTrue(TEXT("Incomplete travel objective owns its sampling timer"), World->GetTimerManager().TimerExists(Pending->TravelTimer));
    Pending->DestroyComponent();
    TestFalse(TEXT("Incomplete objective's timer retires on component destruction"), World->GetTimerManager().TimerExists(Pending->TravelTimer));
    Pending->RecordObjective(Definition->ObjectiveEvent, 1);
    TestEqual(TEXT("Late objective cannot mutate retired progress"), Pending->States[0].Count, 0);
    return true;
}
#endif
