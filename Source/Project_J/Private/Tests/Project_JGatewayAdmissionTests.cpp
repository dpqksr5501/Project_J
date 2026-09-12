#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Backend/Project_JGatewaySubsystem.h"
#include "MMO/Coordination.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGatewayAdmissionTest, "ProjectJ.MMO.Gateway.AdmissionAndShutdown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGatewayAdmissionTest::RunTest(const FString&)
{
    // No network dispatch: saturate/close the adapter before request construction.
    auto* GameInstance = NewObject<UGameInstance>();
    auto* Gateway = NewObject<UProject_JGatewaySubsystem>(GameInstance);
    Gateway->RequestTracker = MakeShared<ProjectJ::MMO::FRequestTracker, ESPMode::ThreadSafe>(1);
    const auto OldSession = Gateway->RequestTracker;
    FGuid Ticket; OldSession->Begin(Ticket);
    FProject_JBackendRequestContext Context; Context.RequestId = FProject_JRequestId::NewId();
    int32 Replies = 0;
    Gateway->DispatchTrackedRequest(TEXT("unused"), TEXT("{}"), Context,
        [&](const FProject_JBackendResponseEnvelope& Response)
        {
            ++Replies;
            TestEqual(TEXT("Capacity has a typed failure"), Response.FailureKind, EProject_JBackendFailureKind::Overloaded);
            TestTrue(TEXT("Request correlation preserved"), Response.RequestContext.RequestId.Value == Context.RequestId.Value);
        });
    TestEqual(TEXT("Exactly one overload reply"), Replies, 1);
    TestTrue(TEXT("No HTTP request created"), Gateway->ActiveRequests.IsEmpty());
    Gateway->Deinitialize();
    TestFalse(TEXT("Shutdown closes old-session completion gate"), OldSession->Complete(Ticket));
    Gateway->DispatchTrackedRequest(TEXT("unused"), TEXT("{}"), Context,
        [&](const FProject_JBackendResponseEnvelope& Response)
        {
            ++Replies;
            TestEqual(TEXT("Closed gateway fails locally"), Response.FailureKind, EProject_JBackendFailureKind::Unavailable);
        });
    TestEqual(TEXT("Closed request reply"), Replies, 2);
    return true;
}
#endif
