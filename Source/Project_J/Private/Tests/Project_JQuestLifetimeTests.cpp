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
#include "AIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

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
    auto* Controller=World->SpawnActor<AAIController>();Controller->SetPlayerState(State);
    auto* Pawn=World->SpawnActor<ACharacter>();Controller->Possess(Pawn);
    Pawn->GetCharacterMovement()->Velocity=FVector(200,0,0);
    Pending->SampleTravelAt(0);const auto Committed=Pending->CaptureSnapshot();
    bool bAdmission=false;Pending->PersistentAdmission=[&]() { return bAdmission; };
    for(int32 Step=1;Step<=3;++Step) { Pawn->SetActorLocation(FVector(Step*100,0,0));Pending->SampleTravelAt(Step*0.5); }
    TestEqual(TEXT("Pending travel does not mutate a durable command snapshot"),Pending->States[0].Count,0);
    Pending->InstallCommittedQuests(Committed);bAdmission=true;Pending->SampleTravelAt(2.0);
    TestEqual(TEXT("Travel sampled during commit is published after snapshot installation"),Pending->States[0].Count,FMath::Min(3,Definition->RequiredCount));
    Pending->States[0].Count=0;
    bool bProgressAccepted = false;
    Pending->SubmitPersistentProgress = [&](const TArray<FProject_JQuestProgressInput>& Inputs) {
        TestTrue(TEXT("Travel uses actual canonical objective input"), !Inputs.IsEmpty() && Inputs[0].Amount > 0);
        return bProgressAccepted;
    };
    Pawn->SetActorLocation(FVector(400,0,0)); Pending->SampleTravelAt(2.5);
    TestTrue(TEXT("Unadmitted travel retains the integer remainder"), Pending->TravelRemainder >= 1.);
    bProgressAccepted = true; Pending->SampleTravelAt(3.0);
    TestTrue(TEXT("Accepted travel transfers the integer and preserves its fraction"), Pending->TravelRemainder < 1.);
    TestEqual(TEXT("Owned submission does not change live travel before commit"), Pending->States[0].Count, 0);
    Pending->SubmitPersistentProgress = {};
    Pending->DestroyComponent();
    TestFalse(TEXT("Incomplete objective's timer retires on component destruction"), World->GetTimerManager().TimerExists(Pending->TravelTimer));
    TestFalse(TEXT("Retired producer gets an explicit rejection"), Pending->TryRecordObjective(Definition->ObjectiveEvent, 1));
    TestEqual(TEXT("Late objective cannot mutate retired progress"), Pending->States[0].Count, 0);
    {
        // Drive the REAL owned timer. Existing admission is a consumer callback,
        // so its count observes repeated component work without test-only state.
        auto* TimerOwner = World->SpawnActor<AProject_JPlayerState>();
        auto* Timed = TimerOwner->FindComponentByClass<UProject_JQuestComponent>();
        auto* TimerDefinition = NewObject<UProject_JQuestDefinition>(Timed);
        TimerDefinition->QuestId = TEXT("TimerTravel"); TimerDefinition->ObjectiveEvent = TEXT("TimerMetres");
        TimerDefinition->bTravelObjective = true; TimerDefinition->RequiredCount = 10000;
        Timed->Definitions = {TimerDefinition};
        TestTrue(TEXT("Timer-backed travel accepts normally"), Timed->Accept(TimerDefinition->QuestId));
        auto* TimerController = World->SpawnActor<AAIController>(); TimerController->SetPlayerState(TimerOwner);
        auto* TimerPawn = World->SpawnActor<ACharacter>(); TimerController->Possess(TimerPawn);
        TimerPawn->GetCharacterMovement()->Velocity = FVector(200, 0, 0);
        World->TimeSeconds = 0;
        Timed->SampleTravelAt(0);
        int32 Admissions = 0, Publications = 0;
        bool bAdmit = true;
        Timed->PersistentAdmission = [&] { ++Admissions; return bAdmit; };
        Timed->OnChanged.AddLambda([&] { ++Publications; });
        const auto AdvanceTimer = [&](float Delta)
        {
            World->TimeSeconds += Delta;
            ++GFrameCounter;
            World->GetTimerManager().Tick(Delta);
        };
        AdvanceTimer(0); // Activate the real pending timer.
        AdvanceTimer(0.001f); // Engine expiry is strictly greater than the due time.
        Admissions = Publications = 0;
        TimerPawn->SetActorLocation(FVector(300, 0, 0)); AdvanceTimer(1.5f);
        TestEqual(TEXT("One hitch performs one actual admission/absolute movement sample"), Admissions, 1);
        TestEqual(TEXT("Hitch retains the observed three metres once"), Timed->States[0].Count, 3);
        TestEqual(TEXT("Hitch publishes one coherent quest state"), Publications, 1);
        Admissions = 0;
        TimerPawn->SetActorLocation(FVector(400, 0, 0)); AdvanceTimer(0.5f);
        TestEqual(TEXT("The next interval has no leftover catch-up callback debt"), Admissions, 1);
        TestEqual(TEXT("Normal cadence resumes with the next metre"), Timed->States[0].Count, 4);
        const auto CommittedTimer = Timed->CaptureSnapshot();
        bAdmit = false; Admissions = 0;
        TimerPawn->SetActorLocation(FVector(700, 0, 0)); AdvanceTimer(1.5f);
        TestEqual(TEXT("Pending admission is evaluated once on a hitch"), Admissions, 1);
        TestEqual(TEXT("Pending commit still protects its snapshot"), Timed->States[0].Count, 4);
        Timed->InstallCommittedQuests(CommittedTimer); bAdmit = true;
        TimerPawn->SetActorLocation(FVector(800, 0, 0)); AdvanceTimer(0.5f);
        TestEqual(TEXT("Bounded pending distance applies after commit using the real timer"), Timed->States[0].Count, 8);
        // Observe the owned progress consumer with real unadmitted distance.
        // A zero integer remainder correctly skips both admission paths.
        int32 OwnedProgressCalls = 0;
        Timed->SubmitPersistentProgress = [&](const TArray<FProject_JQuestProgressInput>& Inputs) {
            ++OwnedProgressCalls;
            TestTrue(TEXT("Owned timer submits the same canonical pending metre"), Inputs.Num() == 1 &&
                Inputs[0].Quest == TimerDefinition->QuestId && Inputs[0].Objective.IsNone() &&
                Inputs[0].Current == 8 && Inputs[0].Required == TimerDefinition->RequiredCount && Inputs[0].Amount == 1);
            return false; // Explicitly unadmitted: keep the real integer remainder.
        };
        TimerPawn->SetActorLocation(FVector(900, 0, 0)); AdvanceTimer(0.5f);
        TestEqual(TEXT("A real metre reaches the owned progress consumer once"), OwnedProgressCalls, 1);
        const double PendingMetre = Timed->TravelRemainder;
        TestTrue(TEXT("Rejected native ingress retains a whole metre"), PendingMetre >= 1. && PendingMetre < 2.);
        OwnedProgressCalls = 0;
        TimerPawn->SetActorLocation(FVector(1500, 0, 0)); AdvanceTimer(3.0f);
        TestEqual(TEXT("Long gaps are still not extrapolated into quest progress"), Timed->States[0].Count, 8);
        TestEqual(TEXT("A long gap performs one actual owned progress attempt"), OwnedProgressCalls, 1);
        TestEqual(TEXT("A rejected long-gap attempt preserves only the prior pending metre"), Timed->TravelRemainder, PendingMetre);
        Timed->SubmitPersistentProgress = {};
        auto* NextPawn = World->SpawnActor<ACharacter>();
        NextPawn->SetActorLocation(FVector(10000, 0, 0));
        TimerController->Possess(NextPawn); NextPawn->GetCharacterMovement()->Velocity = FVector(200, 0, 0);
        AdvanceTimer(1.5f);
        TestEqual(TEXT("Actual Pawn replacement does not inherit old-pawn distance"), Timed->States[0].Count, 8);
        NextPawn->SetActorLocation(FVector(10100, 0, 0)); AdvanceTimer(0.5f);
        TestEqual(TEXT("The replacement's own normal sample progresses"), Timed->States[0].Count, 9);
        Timed->DestroyComponent(); Admissions = 0;
        AdvanceTimer(2.0f);
        TestEqual(TEXT("Retirement prevents late hitch work"), Admissions, 0);
        TestEqual(TEXT("Retirement preserves final progress"), Timed->States[0].Count, 9);
    }
    return true;
}
#endif
