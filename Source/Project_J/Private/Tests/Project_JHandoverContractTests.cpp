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
#endif
