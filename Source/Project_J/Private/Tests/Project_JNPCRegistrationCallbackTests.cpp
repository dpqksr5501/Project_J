#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAttributeSet.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

namespace ProjectJNPCRegistrationTests
{
struct FWorld
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AProject_JNPCCharacter* NPC;
    UProject_JTargetScoringComponent* Scoring;
    UProject_JNPCDecisionSubsystem* Scheduler;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        NPC=World->SpawnActor<AProject_JNPCCharacter>();
        NPC->GetAttributeSet()->InitMaxHealth(100); NPC->GetAttributeSet()->InitHealth(100);
        Scoring=NewObject<UProject_JTargetScoringComponent>(NPC);
        NPC->AddInstanceComponent(Scoring); Scoring->RegisterComponent();
        World->InitializeActorsForPlay(FURL());
        Scheduler=World->GetSubsystem<UProject_JNPCDecisionSubsystem>();
    }
    ~FWorld() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCRegistrationRetiredTest,"ProjectJ.Continuation.NPCRegistrationRetired",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJNPCRegistrationRetiredTest::RunTest(const FString&)
{
    ProjectJNPCRegistrationTests::FWorld F;
    // Keep the fixture readable after destruction; no GC is involved in this admission repro.
    const TStrongObjectPtr<UProject_JTargetScoringComponent> FixturePin(F.Scoring);
    bool Fired=false;
    const auto Callback=F.Scoring->OnContextInvalidated.AddLambda([&]
    {
        if (!Fired) { Fired=true; F.Scoring->DestroyComponent(); }
    });
    TestFalse(TEXT("Registration retired by actual context invalidation cannot succeed"),F.Scoring->StartBatchedNPCDecisions(1));
    TestTrue(TEXT("Real invalidation delegate executed"),Fired);
    TestFalse(TEXT("Retired component cannot publish registered ownership"),F.Scoring->IsBatchedNPCDecisionRegistered());
    TestEqual(TEXT("Retired admission leaves no scheduler entry"),F.Scheduler->GetAgentCount(),0);
    F.Scoring->OnContextInvalidated.Remove(Callback);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCRegistrationStoppedTest,"ProjectJ.Continuation.NPCRegistrationStopped",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJNPCRegistrationStoppedTest::RunTest(const FString&)
{
    ProjectJNPCRegistrationTests::FWorld F;
    if (!TestTrue(TEXT("Normal original registration"),F.Scoring->StartBatchedNPCDecisions(1))) return false;
    bool Fired=false;
    const auto Callback=F.Scoring->OnContextInvalidated.AddLambda([&]
    {
        if (!Fired) { Fired=true; F.Scoring->StopBatchedNPCDecisions(); }
    });
    TestFalse(TEXT("Team update stopped during invalidation cannot acknowledge registration"),F.Scoring->StartBatchedNPCDecisions(2));
    TestTrue(TEXT("Actual team-change invalidation ran"),Fired);
    TestFalse(TEXT("Stop keeps registration closed"),F.Scoring->IsBatchedNPCDecisionRegistered());
    TestEqual(TEXT("Stopped entry remains absent"),F.Scheduler->GetAgentCount(),0);
    F.Scoring->OnContextInvalidated.Remove(Callback);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCRegistrationSupersededTest,"ProjectJ.Continuation.NPCRegistrationSuperseded",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJNPCRegistrationSupersededTest::RunTest(const FString&)
{
    ProjectJNPCRegistrationTests::FWorld F;
    bool Fired=false, Inner=false;
    const auto Callback=F.Scoring->OnContextInvalidated.AddLambda([&]
    {
        if (!Fired) { Fired=true; Inner=F.Scoring->StartBatchedNPCDecisions(2); }
    });
    TestFalse(TEXT("Older admission cannot acknowledge a newer callback registration"),F.Scoring->StartBatchedNPCDecisions(1));
    TestTrue(TEXT("Latest callback admission remains valid"),Inner);
    TestTrue(TEXT("Latest registration stays installed"),F.Scoring->IsBatchedNPCDecisionRegistered());
    TestEqual(TEXT("One component owns exactly one scheduler agent"),F.Scheduler->GetAgentCount(),1);
    auto* Target=F.World->SpawnActor<AActor>();
    TestTrue(TEXT("Actual target registry"),F.Scheduler->RegisterTarget(Target,1));
    TestTrue(TEXT("Target eligibility uses the latest team2"),F.Scheduler->CanActOnTarget(F.Scoring,Target));
    F.Scoring->OnContextInvalidated.Remove(Callback);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCRegistrationGCTest,"ProjectJ.Continuation.NPCRegistrationCallbackGC",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJNPCRegistrationGCTest::RunTest(const FString&)
{
    {
        ProjectJNPCRegistrationTests::FWorld F;
        bool Fired=false;
        const TWeakObjectPtr<UProject_JTargetScoringComponent> Weak(F.Scoring);
        F.Scoring->OnContextInvalidated.AddLambda([&]
        {
            if (!Fired) { Fired=true; F.Scoring->DestroyComponent(); CollectGarbage(RF_NoFlags); }
        });
        // No fixture pin: the real operation must protect itself through the callback.
        TestFalse(TEXT("Actual retirement plus GC rejects admission safely"),F.Scoring->StartBatchedNPCDecisions(1));
        TestTrue(TEXT("GC callback ran"),Fired);
        TestFalse(TEXT("Retired object cannot remain a usable registration"),Weak.IsValid());
        TestEqual(TEXT("GC-retired registration leaks no agent"),F.Scheduler->GetAgentCount(),0);
    }
    {
        ProjectJNPCRegistrationTests::FWorld F;
        TestTrue(TEXT("Original registration succeeds"),F.Scoring->StartBatchedNPCDecisions(1));
        bool Fired=false, NewStarted=false;
        const auto Callback=F.Scoring->OnContextInvalidated.AddLambda([&]
        {
            if (!Fired) { Fired=true; NewStarted=F.Scoring->StartBatchedNPCDecisions(2); CollectGarbage(RF_NoFlags); }
        });
        F.Scoring->StopBatchedNPCDecisions();
        TestTrue(TEXT("Latest callback registration survives old Stop completion"),NewStarted && F.Scoring->IsBatchedNPCDecisionRegistered());
        TestEqual(TEXT("Old stop cannot remove or duplicate the new agent"),F.Scheduler->GetAgentCount(),1);
        F.Scoring->OnContextInvalidated.Remove(Callback);
        F.Scoring->StopBatchedNPCDecisions();
        TestEqual(TEXT("Explicit final release closes the agent"),F.Scheduler->GetAgentCount(),0);
    }
    {
        ProjectJNPCRegistrationTests::FWorld F;
        TestTrue(TEXT("Valid normal registration"),F.Scoring->StartBatchedNPCDecisions(1));
        int32 Notifications=0;
        const auto Callback=F.Scoring->OnContextInvalidated.AddLambda([&]
        {
            ++Notifications; F.Scoring->StopBatchedNPCDecisions();
        });
        TestFalse(TEXT("Recursive stop during team invalidation cancels that request"),F.Scoring->StartBatchedNPCDecisions(2));
        TestEqual(TEXT("Nested invalidation does not recursively redeliver"),Notifications,1);
        TestEqual(TEXT("Nested stop still retires the scheduler entry"),F.Scheduler->GetAgentCount(),0);
        F.Scoring->OnContextInvalidated.Remove(Callback);
    }
    return !HasAnyErrors();
}
#endif
