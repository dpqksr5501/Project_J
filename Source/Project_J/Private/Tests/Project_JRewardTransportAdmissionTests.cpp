#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
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
