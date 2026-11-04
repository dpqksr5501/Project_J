#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Backend/Project_JHandoverManager.h"
#include "Backend/Project_JHandoverTransport.h"
#include "Tests/Project_JHandoverContractActor.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include <limits>
#include "Async/Async.h"
#include <atomic>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJHandoverParticipantTest, "ProjectJ.Continuation.HandoverParticipantLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJHandoverParticipantTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
    TStrongObjectPtr<UProject_JHandoverManager> Manager(NewObject<UProject_JHandoverManager>(GI.Get()));
    auto* Source = World->SpawnActor<AProject_JHandoverContractActor>();
    bool Destroyed = false;
    Source->OnSerialize = [&] { Destroyed = Source->Destroy(); CollectGarbage(RF_NoFlags, true); };
    FProject_JHandoverEnvelope Out; Out.TransferId = FGuid::NewGuid(); Out.Payload = {77};
    const FGuid Before = Out.TransferId;
    TestFalse(TEXT("A retired source cannot publish an envelope after its callback"), Manager->BuildEnvelope(Source, TEXT("LocalNode"), Out));
    TestTrue(TEXT("Serialization really destroyed the source actor"), Destroyed);
    TestEqual(TEXT("Rejected serialization leaves caller's envelope unchanged"), Out.TransferId, Before);
    auto* ClosingSource = World->SpawnActor<AProject_JHandoverContractActor>();
    ClosingSource->OnSerialize = [&] { Manager->Deinitialize(); CollectGarbage(RF_NoFlags, true); };
    TestFalse(TEXT("A serialize callback cannot publish through a closed manager"), Manager->BuildEnvelope(ClosingSource, TEXT("LocalNode"), Out));
    Manager.Reset(NewObject<UProject_JHandoverManager>(GI.Get()));
    auto* Destination = World->SpawnActor<AProject_JHandoverContractActor>();
    FProject_JHandoverEnvelope Apply;
    TestTrue(TEXT("Valid source can build an envelope"), Manager->BuildEnvelope(Destination, TEXT("LocalNode"), Apply));
    Destination->OnApply = [&](const TArray<uint8>&) { Destination->Destroy(); CollectGarbage(RF_NoFlags, true); return true; };
    TestEqual(TEXT("Success callback cannot acknowledge an actor retired during apply"), Manager->ApplyEnvelopeChecked(Destination, Apply), EProject_JHandoverApplyResult::Rejected);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJHandoverPayloadSnapshotTest, "ProjectJ.Continuation.HandoverPayloadSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJHandoverPayloadSnapshotTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
    TStrongObjectPtr<UProject_JHandoverManager> Manager(NewObject<UProject_JHandoverManager>(GI.Get()));
    auto* Destination = World->SpawnActor<AProject_JHandoverContractActor>();
    FProject_JHandoverEnvelope Envelope;
    TestTrue(TEXT("Valid checked envelope"), Manager->BuildEnvelope(Destination, TEXT("LocalNode"), Envelope));
    int32 ObservedBytes = -1;
    Destination->OnApply = [&](const TArray<uint8>& Data) {
        Envelope.Payload.Reset(); ObservedBytes = Data.Num(); return true;
    };
    TestEqual(TEXT("Checked apply consumes a value snapshot"), Manager->ApplyEnvelopeChecked(Destination, Envelope), EProject_JHandoverApplyResult::Applied);
    TestEqual(TEXT("Callback mutation of caller's storage cannot replace validated bytes"), ObservedBytes, 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJHandoverFiniteTickTest, "ProjectJ.Continuation.HandoverFiniteTick",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJHandoverFiniteTickTest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
    TStrongObjectPtr<UProject_JHandoverManager> Manager(NewObject<UProject_JHandoverManager>(GI.Get()));
    auto Transport = MakeShared<FProject_JLoopbackHandoverTransport>(); Transport->SetDropResponses(true);
    Manager->SetTransport(Transport);
    FProject_JHandoverEnvelope Envelope;
    Envelope.TransferId = FGuid::NewGuid(); Envelope.Timestamp = FDateTime::UtcNow();
    Envelope.ActorClassPath = TEXT("/Script/Engine.Actor"); Envelope.SourceNodeId = TEXT("Source"); Envelope.TargetNodeId = TEXT("Target");
    Envelope.PayloadChecksum = Manager->CalculatePayloadChecksum(Envelope.Payload);
    TestTrue(TEXT("A valid transfer awaits its actual transport ack"), Manager->StartEnvelopeTransfer(Envelope));
    Manager->TickHandover(std::numeric_limits<float>::infinity());
    TestEqual(TEXT("Invalid elapsed time cannot manufacture timeout/retry"), Manager->GetHandoverState(Envelope.TransferId), EProject_JHandoverState::AwaitingAck);
    FProject_JHandoverRecord Record;
    TestTrue(TEXT("Tracked record survives invalid tick"), Manager->GetHandoverRecord(Envelope.TransferId, Record));
    TestEqual(TEXT("Invalid elapsed time was never published"), Record.StateElapsedTime, 0.0f);
    Manager->TickHandover(0.25f); Manager->GetHandoverRecord(Envelope.TransferId, Record);
    TestEqual(TEXT("Subsequent normal time still advances"), Record.StateElapsedTime, 0.25f);
    Manager->Deinitialize(); return true;
}

namespace
{
struct FStoredHandoverCompletion
{
    FGuid TransferId;
    int32 Attempt = 0;
    FProject_JHandoverTransportCallback Callback;
};
// Retains callbacks after Cancel to model responses already in flight. Slots
// are immutable after Send and workers access native callback/value state only.
class FStoredHandoverTransport final : public IProject_JHandoverTransport
{
public:
    TArray<TSharedRef<FStoredHandoverCompletion, ESPMode::ThreadSafe>> Sent;
    virtual bool Send(const FProject_JHandoverTransportRequest& Request, FProject_JHandoverTransportCallback Completion) override
    {
        const auto Slot = MakeShared<FStoredHandoverCompletion, ESPMode::ThreadSafe>();
        Slot->TransferId = Request.Envelope.TransferId; Slot->Attempt = Request.Attempt; Slot->Callback = MoveTemp(Completion);
        Sent.Add(Slot); return true;
    }
    virtual void Cancel(const FGuid&) override {}
    virtual void Tick(float) override {}
};
// Deterministically completes on a native worker before Send returns false.
// The worker only invokes value callbacks and never waits for GT delivery.
class FQueuedWorkerFalseHandoverTransport final : public IProject_JHandoverTransport
{
public:
    virtual bool Send(const FProject_JHandoverTransportRequest& Request, FProject_JHandoverTransportCallback Completion) override
    {
        FProject_JHandoverTransportResponse Response;
        Response.TransferId = Request.Envelope.TransferId; Response.Attempt = Request.Attempt;
        Response.Outcome = EProject_JHandoverTransportOutcome::Accepted;
        Async(EAsyncExecution::Thread, [Response, Callback = MoveTemp(Completion)]
        { Callback(Response); Callback(Response); }).Get();
        return false;
    }
    virtual void Cancel(const FGuid&) override {}
    virtual void Tick(float) override {}
};
FProject_JHandoverEnvelope CorrelationEnvelope(const UProject_JHandoverManager& Manager)
{
    FProject_JHandoverEnvelope Envelope;
    Envelope.TransferId = FGuid::NewGuid(); Envelope.Timestamp = FDateTime::UtcNow();
    Envelope.ActorClassPath = TEXT("/Script/Engine.Actor"); Envelope.SourceNodeId = TEXT("Source"); Envelope.TargetNodeId = TEXT("Target");
    Envelope.PayloadChecksum = Manager.CalculatePayloadChecksum(Envelope.Payload); return Envelope;
}
FProject_JHandoverTransportResponse AcceptedHandover(const FStoredHandoverCompletion& Slot)
{
    FProject_JHandoverTransportResponse Response;
    Response.TransferId = Slot.TransferId; Response.Attempt = Slot.Attempt; Response.Outcome = EProject_JHandoverTransportOutcome::Accepted;
    return Response;
}
struct FHandoverWorkerCompletion { std::atomic<bool> Done{false}; };
class FWaitForHandoverWorker final : public IAutomationLatentCommand
{
public:
    FWaitForHandoverWorker(FAutomationTestBase* InTest, UGameInstance* InGI, UProject_JHandoverManager* InManager,
        TSharedRef<FHandoverWorkerCompletion, ESPMode::ThreadSafe> InWorker, FGuid InA, FGuid InB)
        : Test(InTest), GI(InGI), Manager(InManager), Worker(InWorker), A(InA), B(InB), Started(FPlatformTime::Seconds()) {}
    virtual bool Update() override
    {
        const bool bComplete = Worker->Done.load() && Manager->GetHandoverState(A) == EProject_JHandoverState::Completed
            && Manager->GetHandoverState(B) == EProject_JHandoverState::Completed;
        if (!bComplete && FPlatformTime::Seconds() - Started < 10) return false;
        Test->TestTrue(TEXT("Worker burst of unrelated/matching/duplicate responses completes both exact transfers"), bComplete);
        Manager->Deinitialize(); return true;
    }
private:
    FAutomationTestBase* Test;
    TStrongObjectPtr<UGameInstance> GI;
    TStrongObjectPtr<UProject_JHandoverManager> Manager;
    TSharedRef<FHandoverWorkerCompletion, ESPMode::ThreadSafe> Worker;
    FGuid A, B;
    double Started;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJHandoverResponseCorrelationTest, "ProjectJ.Continuation.HandoverResponseCorrelation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJHandoverResponseCorrelationTest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
    TStrongObjectPtr<UProject_JHandoverManager> Manager(NewObject<UProject_JHandoverManager>(GI.Get()));
    const auto Transport = MakeShared<FStoredHandoverTransport>(); Manager->SetTransport(Transport);
    ON_SCOPE_EXIT { Manager->Deinitialize(); };
    const auto A = CorrelationEnvelope(*Manager), B = CorrelationEnvelope(*Manager);
    if (!TestTrue(TEXT("Both independent transfers start"), Manager->StartEnvelopeTransfer(A) && Manager->StartEnvelopeTransfer(B))) return false;
    const auto First = Transport->Sent[0], Second = Transport->Sent[1];
    auto Unknown = AcceptedHandover(*First); Unknown.TransferId = FGuid::NewGuid();
    First->Callback(Unknown);
    TestEqual(TEXT("An unknown transfer does not claim A's response"), Manager->GetHandoverState(A.TransferId), EProject_JHandoverState::AwaitingAck);
    First->Callback(AcceptedHandover(*Second));
    TestEqual(TEXT("A's callback cannot acknowledge B"), Manager->GetHandoverState(B.TransferId), EProject_JHandoverState::AwaitingAck);
    auto WrongAttempt = AcceptedHandover(*First); ++WrongAttempt.Attempt;
    First->Callback(WrongAttempt);
    TestEqual(TEXT("A wrong attempt cannot acknowledge A"), Manager->GetHandoverState(A.TransferId), EProject_JHandoverState::AwaitingAck);
    First->Callback(AcceptedHandover(*First)); Second->Callback(AcceptedHandover(*Second));
    TestEqual(TEXT("Unrelated responses did not consume A's matching completion"), Manager->GetHandoverState(A.TransferId), EProject_JHandoverState::Completed);
    TestEqual(TEXT("B's own response still completes B"), Manager->GetHandoverState(B.TransferId), EProject_JHandoverState::Completed);
    auto Duplicate = AcceptedHandover(*First); Duplicate.Outcome = EProject_JHandoverTransportOutcome::Rejected;
    First->Callback(Duplicate);
    TestEqual(TEXT("Duplicate matching completion cannot replace a terminal outcome"), Manager->GetHandoverState(A.TransferId), EProject_JHandoverState::Completed);

    const auto Retry = CorrelationEnvelope(*Manager);
    if (!TestTrue(TEXT("Retry scenario starts"), Manager->StartEnvelopeTransfer(Retry))) return false;
    const auto OldAttempt = Transport->Sent.Last();
    Manager->TickHandover(6); Manager->TickHandover(1);
    const auto CurrentAttempt = Transport->Sent.Last();
    if (!TestEqual(TEXT("The normal timeout dispatches attempt two"), CurrentAttempt->Attempt, 2)) return false;
    OldAttempt->Callback(AcceptedHandover(*CurrentAttempt));
    TestEqual(TEXT("A retired attempt callback cannot impersonate its replacement"), Manager->GetHandoverState(Retry.TransferId), EProject_JHandoverState::AwaitingAck);
    OldAttempt->Callback(AcceptedHandover(*OldAttempt));
    TestEqual(TEXT("A late original attempt cannot complete its replacement"), Manager->GetHandoverState(Retry.TransferId), EProject_JHandoverState::AwaitingAck);
    CurrentAttempt->Callback(AcceptedHandover(*CurrentAttempt));
    TestEqual(TEXT("The current exact attempt completes normally"), Manager->GetHandoverState(Retry.TransferId), EProject_JHandoverState::Completed);

    const auto Cancelled = CorrelationEnvelope(*Manager);
    Manager->StartEnvelopeTransfer(Cancelled); const auto CancelledCallback = Transport->Sent.Last();
    Manager->CancelHandoverByTransferId(Cancelled.TransferId); CancelledCallback->Callback(AcceptedHandover(*CancelledCallback));
    TestEqual(TEXT("An in-flight callback cannot resurrect cancelled work"), Manager->GetHandoverState(Cancelled.TransferId), EProject_JHandoverState::Cancelled);
    const auto Closing = CorrelationEnvelope(*Manager);
    Manager->StartEnvelopeTransfer(Closing); const auto ClosingCallback = Transport->Sent.Last();
    Manager->Deinitialize(); ClosingCallback->Callback(AcceptedHandover(*ClosingCallback));
    TestEqual(TEXT("A callback from the closed manager lifetime cannot restore records"), Manager->GetHandoverState(Closing.TransferId), EProject_JHandoverState::None);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJHandoverWorkerCorrelationTest, "ProjectJ.Continuation.HandoverWorkerCorrelation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJHandoverWorkerCorrelationTest::RunTest(const FString&)
{
    auto* GI = NewObject<UGameInstance>(GEngine);
    auto* Manager = NewObject<UProject_JHandoverManager>(GI);
    const auto Transport = MakeShared<FStoredHandoverTransport>(); Manager->SetTransport(Transport);
    const auto A = CorrelationEnvelope(*Manager), B = CorrelationEnvelope(*Manager);
    if (!TestTrue(TEXT("Both worker-response transfers start"), Manager->StartEnvelopeTransfer(A) && Manager->StartEnvelopeTransfer(B)))
    { Manager->Deinitialize(); return false; }
    const auto First = Transport->Sent[0], Second = Transport->Sent[1];
    const auto Worker = MakeShared<FHandoverWorkerCompletion, ESPMode::ThreadSafe>();
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForHandoverWorker(this, GI, Manager, Worker, A.TransferId, B.TransferId));
    // The worker captures native callback/value state only. UObject lifetime and
    // all state assertions belong to the GT latent command.
    Async(EAsyncExecution::Thread, [First, Second, Worker]
    {
        const auto ResponseA = AcceptedHandover(*First), ResponseB = AcceptedHandover(*Second);
        auto WrongAttempt = ResponseA; ++WrongAttempt.Attempt;
        for (int32 Burst = 0; Burst < 128; ++Burst)
        {
            First->Callback(ResponseB); First->Callback(WrongAttempt);
            First->Callback(ResponseA); Second->Callback(ResponseB);
        }
        Worker->Done = true;
    });
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJHandoverQueuedDispatchFailureTest, "ProjectJ.Continuation.HandoverQueuedDispatchFailure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJHandoverQueuedDispatchFailureTest::RunTest(const FString&)
{
    auto* GI = NewObject<UGameInstance>(GEngine);
    auto* Manager = NewObject<UProject_JHandoverManager>(GI);
    Manager->SetTransport(MakeShared<FQueuedWorkerFalseHandoverTransport>());
    const auto Envelope = CorrelationEnvelope(*Manager);
    if (!TestTrue(TEXT("Queued worker-response scenario starts"), Manager->StartEnvelopeTransfer(Envelope)))
    { Manager->Deinitialize(); return false; }
    TestEqual(TEXT("A queued matching worker response wins over Send(false) without entering retry"),
        Manager->GetHandoverState(Envelope.TransferId), EProject_JHandoverState::AwaitingAck);
    const auto Worker = MakeShared<FHandoverWorkerCompletion, ESPMode::ThreadSafe>(); Worker->Done = true;
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForHandoverWorker(this, GI, Manager, Worker, Envelope.TransferId, Envelope.TransferId));
    return true;
}
#endif
