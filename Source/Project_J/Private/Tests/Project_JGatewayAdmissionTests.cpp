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
    Gateway->GatewayUrl.Reset();
    FProject_JBackendRequestContext InvalidMutation;
    InvalidMutation.Intent = EProject_JBackendRequestIntent::Mutation;
    int32 PreflightReplies = 0;
    Gateway->DispatchTrackedRequest(TEXT("unused"), TEXT("{}"), InvalidMutation,
        [&](const FProject_JBackendResponseEnvelope& Response)
        {
            ++PreflightReplies;
            TestEqual(TEXT("Unconfigured URL retains priority over invalid mutation and closed tracker"), Response.FailureKind, EProject_JBackendFailureKind::DispatchFailed);
            TestEqual(TEXT("Both response adapters retain the existing diagnostic"), Response.ResponseData, FString(TEXT("Gateway URL is not configured")));
            TestTrue(TEXT("Preflight normalizes correlation once"), Response.RequestContext.HasRequestId());
            TestFalse(TEXT("Configuration failure is not retryable"), Response.bRetryable);
        });
    TestEqual(TEXT("Unconfigured request completes exactly once"), PreflightReplies, 1);
    TestTrue(TEXT("Preflight creates no HTTP request"), Gateway->ActiveRequests.IsEmpty());
    Gateway->GatewayUrl = TEXT("https://example.invalid/"); // Every following case rejects before HTTP creation.
    Gateway->DispatchTrackedRequest(TEXT("unused"), TEXT("{}"), InvalidMutation,
        [&](const FProject_JBackendResponseEnvelope& Response)
        {
            ++PreflightReplies;
            TestEqual(TEXT("Configured URL retains mutation validation before closed admission"), Response.FailureKind, EProject_JBackendFailureKind::InvalidRequest);
        });
    TestEqual(TEXT("Invalid mutation completes exactly once"), PreflightReplies, 2);
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
