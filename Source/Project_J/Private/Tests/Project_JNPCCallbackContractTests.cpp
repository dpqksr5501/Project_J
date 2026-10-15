#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Project_JNPCCallbackFixture.h"
#include "Components/Project_JNPCActivationComponent.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "Project_JAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "System/Project_JTargetScoringSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

namespace ProjectJNPCCallbackTests
{
struct FWorld
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    AProject_JNPCCallbackFixture* NPC;
    UProject_JTargetScoringComponent* Scoring;
    UProject_JNPCActionComponent* Action;
    UProject_JNPCActivationComponent* Activation;
    UProject_JNPCDecisionSubsystem* Scheduler;
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        NPC = World->SpawnActor<AProject_JNPCCallbackFixture>();
        World->SpawnActor<AAIController>()->Possess(NPC);
        NPC->GetAbilitySystemComponent()->InitAbilityActorInfo(NPC, NPC);
        Scoring = NewObject<UProject_JTargetScoringComponent>(NPC); NPC->AddInstanceComponent(Scoring); Scoring->RegisterComponent();
        Action = NewObject<UProject_JNPCActionComponent>(NPC); NPC->AddInstanceComponent(Action); Action->RegisterComponent();
        Activation = NewObject<UProject_JNPCActivationComponent>(NPC); NPC->AddInstanceComponent(Activation); Activation->RegisterComponent();
        World->InitializeActorsForPlay(FURL());
        World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
        NPC->GetAttributeSet()->InitMaxHealth(100); NPC->GetAttributeSet()->InitHealth(100);
        Scheduler = World->GetSubsystem<UProject_JNPCDecisionSubsystem>();
    }
    ~FWorld()
    {
        World->EndPlay(EEndPlayReason::LevelTransition);
        World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    }
    AProject_JNPCCallbackFixture* Target(double X, int32 Team = 2)
    {
        auto* Target = World->SpawnActor<AProject_JNPCCallbackFixture>();
        Target->SetActorLocation(FVector(X, 0, 0)); Scheduler->RegisterTarget(Target, Team); return Target;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCCommitSupersededTest, "ProjectJ.Continuation.NPCCommitSuperseded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCCommitSupersededTest::RunTest(const FString&)
{
    ProjectJNPCCallbackTests::FWorld F;
    auto* A = F.Target(100); auto* B = F.Target(150);
    TestTrue(TEXT("Server registration"), F.Scoring->StartBatchedNPCDecisions(1));
    TestTrue(TEXT("Movement consumer"), F.Action->StartActions(F.Scoring, {}));
    F.Scoring->OnQueryCompleted.Broadcast(A, 1.0);
    TestTrue(TEXT("Original target can commit"), F.Action->CanCommitToTarget(A));
    bool Fired = false;
    F.NPC->OnLifeQuery = [&] { Fired = true; F.Scoring->OnQueryCompleted.Broadcast(B, 1.0); };
    TestFalse(TEXT("An older target cannot commit after IsDead switches intent"), F.Action->CanCommitToTarget(A));
    TestTrue(TEXT("Actual interface event callback"), Fired);
    TestEqual(TEXT("Newer intent remains installed"), F.Action->GetIntentTarget(), static_cast<AActor*>(B));
    TestTrue(TEXT("Fresh commit to the newer target remains supported"), F.Action->CanCommitToTarget(B));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCLifeRetiredTest, "ProjectJ.Continuation.NPCLifeRetired",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCLifeRetiredTest::RunTest(const FString&)
{
    ProjectJNPCCallbackTests::FWorld F;
    auto* Target = F.Target(100);
    TestTrue(TEXT("Server registration"), F.Scoring->StartBatchedNPCDecisions(1));
    bool Fired = false;
    Target->OnLifeQuery = [&] { Fired = true; Target->Destroy(); };
    TestFalse(TEXT("A target retired during its life event is ineligible"), F.Scheduler->CanActOnTarget(F.Scoring, Target));
    TestTrue(TEXT("Actual target life callback"), Fired);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCGrantRegistrationTest, "ProjectJ.Continuation.NPCGrantRegistrationSuperseded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCGrantRegistrationTest::RunTest(const FString&)
{
    ProjectJNPCCallbackTests::FWorld F;
    auto* Definition = NewObject<UProject_JNPCActivationDefinition>();
    Definition->AttackAbility = UProject_JNPCGrantCallbackFixture::StaticClass();
    const int32 Baseline = F.NPC->GetAbilitySystemComponent()->GetActivatableAbilities().Num();
    bool Fired = false, Restarted = false;
    F.NPC->OnGrant = [&]
    {
        Fired = true; F.Scoring->StopBatchedNPCDecisions(); Restarted = F.Scoring->StartBatchedNPCDecisions(2);
    };
    TestFalse(TEXT("Grant callback registration change supersedes old activation"), F.Activation->ActivateNPC(Definition, F.Scoring, F.Action, 1));
    TestTrue(TEXT("Actual GAS grant event ran"), Fired);
    TestTrue(TEXT("Newer registration succeeded"), Restarted);
    TestFalse(TEXT("Old activation cannot own the new registration's action"), F.Activation->HasOwnedActivation());
    TestEqual(TEXT("No leaked old activation grant"), F.NPC->GetAbilitySystemComponent()->GetActivatableAbilities().Num(), Baseline);
    TestEqual(TEXT("Old activation cannot start pursuit"), F.Action->GetActionState(), EProjectJNPCActionState::Disabled);
    auto* Target = F.Target(100, 1);
    TestTrue(TEXT("Rollback preserves the newer team2 registration"), F.Scheduler->CanActOnTarget(F.Scoring, Target));
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCCallbackGCTest, "ProjectJ.Continuation.NPCCallbackGC",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCCallbackGCTest::RunTest(const FString&)
{
    {
        ProjectJNPCCallbackTests::FWorld F;
        auto* Target = F.Target(100);
        F.Scoring->StartBatchedNPCDecisions(1); F.Action->StartActions(F.Scoring, {});
        F.Scoring->OnQueryCompleted.Broadcast(Target, 1.0);
        F.NPC->OnLifeQuery = [&] { F.Action->DestroyComponent(); CollectGarbage(RF_NoFlags, true); };
        TestFalse(TEXT("Actual action retirement and GC rejects a pending commit"), F.Action->CanCommitToTarget(Target));
    }
    {
        ProjectJNPCCallbackTests::FWorld F;
        auto* Definition = NewObject<UProject_JNPCActivationDefinition>();
        Definition->AttackAbility = UProject_JNPCGrantCallbackFixture::StaticClass();
        const int32 Before = F.NPC->GetAbilitySystemComponent()->GetActivatableAbilities().Num();
        F.NPC->OnGrant = [&] { F.Activation->DestroyComponent(); CollectGarbage(RF_NoFlags, true); };
        TestFalse(TEXT("Grant callback retirement and GC cannot acknowledge activation"), F.Activation->ActivateNPC(Definition, F.Scoring, F.Action, 1));
        TestEqual(TEXT("Retired activation releases its grant"), F.NPC->GetAbilitySystemComponent()->GetActivatableAbilities().Num(), Before);
        TestFalse(TEXT("Retired activation releases its registration"), F.Scoring->IsBatchedNPCDecisionRegistered());
    }
    {
        ProjectJNPCCallbackTests::FWorld F;
        auto* Definition = NewObject<UProject_JNPCActivationDefinition>();
        Definition->AttackAbility = UProject_JNPCGrantCallbackFixture::StaticClass();
        auto* ASC = F.NPC->GetAbilitySystemComponent();
        const auto Foreign = ASC->GiveAbility(FGameplayAbilitySpec(Definition->AttackAbility));
        TestTrue(TEXT("Normal grant activation"), F.Activation->ActivateNPC(Definition, F.Scoring, F.Action, 1));
        auto* Target = F.Target(100);
        F.Scoring->OnQueryCompleted.Broadcast(Target, 1.0); F.Action->UpdateAction();
        const auto Owned = F.Action->GetAttackAbilityHandle();
        TestTrue(TEXT("Real owned GAS attack is active"), ASC->FindAbilitySpecFromHandle(Owned)->IsActive());
        bool Fired = false, Restarted = false;
        const auto Callback = ASC->OnAbilityEnded.AddLambda([&](const FAbilityEndedData& Data)
        {
            if (Data.AbilitySpecHandle != Owned) { return; }
            Fired = true;
            F.Scoring->StopBatchedNPCDecisions(); Restarted = F.Scoring->StartBatchedNPCDecisions(2);
            F.Activation->DestroyComponent(); CollectGarbage(RF_NoFlags, true);
        });
        F.Activation->DeactivateNPC();
        TestTrue(TEXT("Actual Cancel/End callback executed"), Fired);
        TestTrue(TEXT("Newer callback registration remains active"), Restarted && F.Scoring->IsBatchedNPCDecisionRegistered());
        TestNull(TEXT("Only old activation grant is removed"), ASC->FindAbilitySpecFromHandle(Owned));
        TestNotNull(TEXT("Foreign grant survives callback GC cleanup"), ASC->FindAbilitySpecFromHandle(Foreign));
        ASC->OnAbilityEnded.Remove(Callback);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCResultGCTest, "ProjectJ.Continuation.NPCResultBroadcastGC",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCResultGCTest::RunTest(const FString&)
{
    ProjectJNPCCallbackTests::FWorld F;
    auto* Target = F.Target(100);
    const FName OriginalName = Target->GetFName();
    const TStrongObjectPtr<UProject_JNPCResultCallbackFixture> A(NewObject<UProject_JNPCResultCallbackFixture>());
    const TStrongObjectPtr<UProject_JNPCResultCallbackFixture> B(NewObject<UProject_JNPCResultCallbackFixture>());
    int32 Calls = 0; bool RemainingArgumentReadable = false;
    const auto Callback = [&](AActor* Result)
    {
        ++Calls;
        if (Calls == 1) { Target->Destroy(); F.Scoring->DestroyComponent(); CollectGarbage(RF_NoFlags, true); }
        else { RemainingArgumentReadable = Result && Result->GetFName() == OriginalName; }
    };
    A->OnResult = Callback; B->OnResult = Callback;
    F.Scoring->OnQueryCompleted.AddDynamic(A.Get(), &UProject_JNPCResultCallbackFixture::Receive);
    F.Scoring->OnQueryCompleted.AddDynamic(B.Get(), &UProject_JNPCResultCallbackFixture::Receive);
    F.Scoring->Execution = EProject_JTargetScoringExecution::Serial;
    TestTrue(TEXT("Actual Core scoring request"), F.Scoring->RequestTargets({Target}));
    F.World->GetSubsystem<UProject_JTargetScoringSubsystem>()->Tick(0);
    TestEqual(TEXT("Both actual multicast listeners ran"), Calls, 2);
    TestTrue(TEXT("Broadcast argument is held for remaining listeners across GC"), RemainingArgumentReadable);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPCPreparedBatchTest, "ProjectJ.Continuation.NPCPreparedBatchSuperseded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPCPreparedBatchTest::RunTest(const FString&)
{
    ProjectJNPCCallbackTests::FWorld F;
    F.Target(100);
    TestTrue(TEXT("First collector registration"), F.Scoring->StartBatchedNPCDecisions(1));
    auto* Peer = F.World->SpawnActor<AProject_JNPCCallbackFixture>();
    auto* PeerScoring = NewObject<UProject_JTargetScoringComponent>(Peer);
    Peer->AddInstanceComponent(PeerScoring); PeerScoring->RegisterComponent();
    TestTrue(TEXT("Second collector registration"), PeerScoring->StartBatchedNPCDecisions(1));
    bool Fired = false, Restarted = false;
    Peer->OnLifeQuery = [&]
    {
        Fired = true; F.Scoring->StopBatchedNPCDecisions(); Restarted = F.Scoring->StartBatchedNPCDecisions(2);
    };
    F.Scheduler->Tick(0);
    TestTrue(TEXT("Later agent's real interface callback ran"), Fired && Restarted);
    TestEqual(TEXT("Retired prepared query is removed before actual Core dispatch"), F.Scheduler->GetStats().SubmittedDecisions, uint64(1));
    TestEqual(TEXT("Only current peer query owns an outstanding decision"), F.Scheduler->GetOutstandingCount(), 1);
    TestEqual(TEXT("Restart preserves exactly two current registry agents"), F.Scheduler->GetAgentCount(), 2);
    return !HasAnyErrors();
}
#endif
