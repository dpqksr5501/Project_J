#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Project_JNPCCallbackFixture.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "System/Project_JTargetScoringSubsystem.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "AIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "UObject/GarbageCollection.h"

namespace ProjectJNPCRegistryTests
{
struct FWorld
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    UProject_JNPCDecisionSubsystem* Scheduler = nullptr;
    UProject_JTargetScoringSubsystem* ScoringService = nullptr;
    AProject_JNPCCallbackFixture* FollowOwner = nullptr;
    TArray<AProject_JNPCCallbackFixture*> NPCs;
    TArray<UProject_JTargetScoringComponent*> Scoring;
    TArray<UProject_JNPCActionComponent*> Actions;
    TMap<AActor*, int32> FixtureIndices;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
        Scheduler = World->GetSubsystem<UProject_JNPCDecisionSubsystem>();
        ScoringService = World->GetSubsystem<UProject_JTargetScoringSubsystem>();
        FollowOwner = SpawnNPC();
    }
    ~FWorld()
    {
        for (auto* NPC : NPCs) if (IsValid(NPC)) NPC->OnLifeQuery = nullptr;
        World->EndPlay(EEndPlayReason::LevelTransition);
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    }
    AProject_JNPCCallbackFixture* SpawnNPC()
    {
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* NPC = World->SpawnActor<AProject_JNPCCallbackFixture>(AProject_JNPCCallbackFixture::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
        World->SpawnActor<AAIController>()->Possess(NPC);
        NPC->GetAbilitySystemComponent()->InitAbilityActorInfo(NPC, NPC);
        NPC->GetAttributeSet()->InitMaxHealth(100); NPC->GetAttributeSet()->InitHealth(100);
        return NPC;
    }
    void Add()
    {
        auto* NPC = SpawnNPC();
        FixtureIndices.Add(NPC, NPCs.Add(NPC));
        auto* Source = NewObject<UProject_JTargetScoringComponent>(NPC);
        NPC->AddInstanceComponent(Source); Source->RegisterComponent(); Scoring.Add(Source);
        auto* Action = NewObject<UProject_JNPCActionComponent>(NPC);
        NPC->AddInstanceComponent(Action); Action->RegisterComponent(); Actions.Add(Action);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCRegistryChurnTest, "ProjectJ.NPCDecision.RegistryChurnFairness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCRegistryChurnTest::RunTest(const FString&)
{
    constexpr int32 Count = 128;
    // Admissions, cancellation, current-recipient checks and scheduling use the real public APIs.
    // Private access selects the adversarial cursor/dense victims and makes admitted action deadlines due.
    // No worker, life result, gameplay intent or registry entry is fabricated.
    for (bool bAction : {false, true})
    {
        ProjectJNPCRegistryTests::FWorld F;
        for (int32 Index = 0; Index < Count; ++Index) F.Add();
        const auto Admit = [&](int32 Index)
        {
            if (!bAction) return F.Scoring[Index]->StartBatchedNPCDecisions(1);
            const bool Started = F.Actions[Index]->StartFollowing(F.FollowOwner, F.FollowOwner->GetAbilitySystemComponent(), 100);
            if (Started)
            {
                const int32 Slot = F.Scheduler->ActionIndices.FindChecked(F.Actions[Index]);
                F.Scheduler->Actions[Slot].NextDue = -1;
            }
            return Started;
        };
        const auto Retire = [&](int32 Index)
        {
            if (bAction) F.Actions[Index]->StopActions();
            else F.Scoring[Index]->StopBatchedNPCDecisions();
        };
        const auto CheckIndices = [&]
        {
            if (bAction)
            {
                TestEqual(TEXT("Action lookup and dense registry retain equal cardinality"), F.Scheduler->ActionIndices.Num(), F.Scheduler->GetActionCount());
                TSet<int32> Slots;
                for (const auto& Pair : F.Scheduler->ActionIndices)
                {
                    if (!TestTrue(TEXT("Action index resolves one unique current dense slot"),
                        F.Scheduler->Actions.IsValidIndex(Pair.Value) && !Slots.Contains(Pair.Value))) continue;
                    Slots.Add(Pair.Value);
                    const auto& Entry = F.Scheduler->Actions[Pair.Value];
                    TestTrue(TEXT("Action index preserves component identity"), Entry.Component == Pair.Key);
                    const int32* Previous = F.Scheduler->ActionIndices.Find(Entry.Previous);
                    const int32* Next = F.Scheduler->ActionIndices.Find(Entry.Next);
                    TestTrue(TEXT("Action ring neighbours remain current and reciprocal"), Previous && Next
                        && F.Scheduler->Actions[*Previous].Next == Entry.Component && F.Scheduler->Actions[*Next].Previous == Entry.Component);
                }
                TestTrue(TEXT("Action cursor resolves a current consumer or is empty"), F.Scheduler->Actions.IsEmpty()
                    ? F.Scheduler->ActionCursor.IsExplicitlyNull() : F.Scheduler->ActionIndices.Contains(F.Scheduler->ActionCursor));
            }
            else
            {
                TestEqual(TEXT("Agent lookup and dense registry retain equal cardinality"), F.Scheduler->AgentIndices.Num(), F.Scheduler->GetAgentCount());
                TSet<int32> Slots;
                for (const auto& Pair : F.Scheduler->AgentIndices)
                {
                    TestTrue(TEXT("Agent index resolves one unique current dense slot"),
                        F.Scheduler->Agents.IsValidIndex(Pair.Value) && !Slots.Contains(Pair.Value));
                    Slots.Add(Pair.Value);
                    EProject_JNPCUpdateBudgetTier Tier; double Interval = 0;
                    TestTrue(TEXT("Current agent budget remains reachable through the production query"),
                        F.Scheduler->GetAgentDecisionBudget(Pair.Key.Get(), Tier, Interval));
                }
                TestTrue(TEXT("Agent cursor resolves a current consumer or is empty"), F.Scheduler->Agents.IsEmpty()
                    ? F.Scheduler->AgentCursor.IsExplicitlyNull() : F.Scheduler->AgentIndices.Contains(F.Scheduler->AgentCursor));
            }
        };
        for (int32 Index = 0; Index < Count; ++Index)
            if (!TestTrue(TEXT("Actual bounded native admission succeeds"), Admit(Index))) return false;
        if (bAction) F.Scheduler->ActionCursor = F.Actions[1];
        else F.Scheduler->AgentCursor = F.Scoring[1];
        TArray<bool> Original; Original.Init(true, Count);
        TArray<bool> Visited; Visited.Init(false, Count);
        int32 ChurnSteps = 0, TotalPreviousVisits = 0, ProbeVisit = 0, LastOriginalOrder = -1;
        bool bProbeVisited = false, bCollected = false;
        TFunction<void(int32)> Arm;
        Arm = [&](int32 Index)
        {
            F.NPCs[Index]->OnLifeQuery = [&, Index]
            {
                if (Original[Index])
                {
                    TestFalse(TEXT("Each original surviving lease is observed once in its current round"), Visited[Index]);
                    Visited[Index] = true;
                    const int32 Order = (Index - 1 + Count) % Count;
                    TestTrue(TEXT("Original survivors preserve their admission-order frontier"), Order > LastOriginalOrder);
                    LastOriginalOrder = Order;
                }
                else TestTrue(TEXT("A newly admitted tail lease cannot overtake the protected old survivor"), bProbeVisited);
                if (Index == 0)
                {
                    bProbeVisited = true;
                    const auto& Stats = F.Scheduler->GetStats();
                    ProbeVisit = TotalPreviousVisits + (bAction ? Stats.LastTickActionVisits : Stats.LastTickAgentVisits);
                    TestTrue(TEXT("Protected survivor is serviced within one original registry traversal despite continuous churn"), ProbeVisit <= Count);
                }
                if (bProbeVisited || HasAnyErrors()) return;
                ++ChurnSteps;
                const int32 VictimSlot = ChurnSteps % 2 ? Count - 1 : 1;
                AActor* VictimOwner = nullptr;
                if (bAction) VictimOwner = F.Scheduler->Actions[VictimSlot].Component->GetOwner();
                else for (const auto& Pair : F.Scheduler->AgentIndices)
                    if (Pair.Value == VictimSlot) { VictimOwner = Pair.Key->GetOwner(); break; }
                const int32 Victim = F.FixtureIndices.FindChecked(VictimOwner);
                if (!TestTrue(TEXT("The adversarial dense victim never removes the protected survivor"), Victim != 0)) return;
                Original[Victim] = false;
                Retire(Victim);
                if (!TestTrue(TEXT("Actual callback-driven retirement readmits only a fresh tail lease"), Admit(Victim))) return;
                Arm(Victim);
                if (!bCollected) { bCollected = true; CollectGarbage(RF_NoFlags, true); }
            };
        };
        for (int32 Index = 0; Index < Count; ++Index) Arm(Index);
        const double Deadline = FPlatformTime::Seconds() + 5;
        while (!bProbeVisited && !HasAnyErrors() && FPlatformTime::Seconds() < Deadline)
        {
            F.Scheduler->Tick(0);
            const auto& Stats = F.Scheduler->GetStats();
            TestTrue(TEXT("Churn retains the action visit and update limits"), Stats.LastTickActionVisits <= F.Scheduler->MaxActionVisitsPerTick
                && Stats.LastTickActionUpdates <= F.Scheduler->MaxActionUpdatesPerTick);
            TestTrue(TEXT("Churn retains the agent visit and outstanding limits"), Stats.LastTickAgentVisits <= F.Scheduler->MaxAgentVisitsPerTick
                && F.Scheduler->GetOutstandingCount() <= F.Scheduler->MaxOutstandingDecisions);
            TotalPreviousVisits += bAction ? Stats.LastTickActionVisits : Stats.LastTickAgentVisits;
            CheckIndices(); F.ScoringService->Tick(0);
            if (!bProbeVisited) FPlatformProcess::SleepNoStats(.001f);
        }
        TestTrue(TEXT("Continuous callback churn and GC cannot starve an old live survivor"), bProbeVisited && ChurnSteps > 8 && bCollected);
        for (auto* NPC : F.NPCs) NPC->OnLifeQuery = nullptr;
        for (int32 Index = 0; Index < Count; ++Index) { Retire(Index); Retire(Index); }
        CheckIndices();
        TestEqual(TEXT("Repeated release returns all agent capacity"), F.Scheduler->GetAgentCount(), 0);
        TestEqual(TEXT("Repeated release returns all action capacity"), F.Scheduler->GetActionCount(), 0);
        TestEqual(TEXT("Final agent retirement releases shared decision reservations"), F.Scheduler->GetOutstandingCount(), 0);
        AddInfo(FString::Printf(TEXT("NPCRegistryChurn action=%d admitted=%d churn_steps=%d original_bound=%d actual_probe_visit=%d"), bAction, Count, ChurnSteps, Count, ProbeVisit));
    }
    return !HasAnyErrors();
}
#endif
