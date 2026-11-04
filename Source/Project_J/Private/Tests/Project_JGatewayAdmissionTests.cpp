#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Backend/Project_JGatewaySubsystem.h"
#include "MMO/Coordination.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Subsystems/SubsystemCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "Project_JLogger.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGatewayAdmissionTest, "ProjectJ.MMO.Gateway.AdmissionAndShutdown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGatewayAdmissionTest::RunTest(const FString&)
{
    UE_LOG_REMOTE(LogTemp, Display, TEXT("Gateway logging API native compile coverage"));
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

    // Exercise the real GI timer without ticking a fabricated clock/frame or issuing HTTP.
    TStrongObjectPtr<UGameInstance> TelemetryInstance(NewObject<UGameInstance>(GEngine));
    TStrongObjectPtr<UProject_JGatewaySubsystem> Telemetry(NewObject<UProject_JGatewaySubsystem>(TelemetryInstance.Get()));
    FSubsystemCollection<UGameInstanceSubsystem> EmptyCollection;
    Telemetry->bEnableRemoteTelemetry = false;
    Telemetry->MaxInFlightRequests = 1;
    Telemetry->Initialize(EmptyCollection);
    TestNull(TEXT("A constructed GI has no world"), TelemetryInstance->GetWorld());
    TestFalse(TEXT("No idle timer is installed at initialization"), TelemetryInstance->GetTimerManager().TimerExists(Telemetry->LogFlushTimerHandle));
    Telemetry->GatewayUrl = TEXT("https://example.invalid/");
    Telemetry->EnqueueRemoteLog(TEXT("disabled"), TEXT("Error"));
    TestTrue(TEXT("Disabled telemetry does not queue"), Telemetry->LogQueue.IsEmpty());
    Telemetry->bEnableRemoteTelemetry = true;
    Telemetry->EnqueueRemoteLog(TEXT("no-world"), TEXT("Error"));
    TestTrue(TEXT("No-world admission does not queue"), Telemetry->LogQueue.IsEmpty());

    TelemetryInstance->InitializeStandalone();
    FWorldContext* const TelemetryContext = TelemetryInstance->GetWorldContext();
    UWorld* TelemetryWorld = TelemetryInstance->GetWorld();
    ON_SCOPE_EXIT
    {
        if (TelemetryContext) TelemetryContext->SetCurrentWorld(TelemetryWorld);
        Telemetry->Deinitialize();
        TelemetryInstance->Shutdown();
        if (TelemetryWorld)
        {
            TelemetryWorld->DestroyWorld(false);
            GEngine->DestroyWorldContext(TelemetryWorld);
        }
    };
    if (!TestNotNull(TEXT("Standalone supplies the later authoritative world"), TelemetryWorld)
        || !TestNotNull(TEXT("Engine owns the standalone context across detach"), TelemetryContext)) return false;
    FTimerManager& Timers = TelemetryInstance->GetTimerManager();
    auto* AutomaticGateway = TelemetryInstance->GetSubsystem<UProject_JGatewaySubsystem>();
    if (!TestNotNull(TEXT("GI initialization retains its actual Gateway subsystem"), AutomaticGateway)) return false;
    TestFalse(TEXT("Automatic initialization with an empty queue has no idle timer"), Timers.TimerExists(AutomaticGateway->LogFlushTimerHandle));
    Telemetry->EnqueueRemoteLog(TEXT("first"), TEXT("Error"));
    const FTimerHandle FirstFlush = Telemetry->LogFlushTimerHandle;
    TestTrue(TEXT("First accepted late-world log arms the GI timer"), Timers.IsTimerActive(FirstFlush));
    Telemetry->EnqueueRemoteLog(TEXT("second"), TEXT("Error"));
    TestTrue(TEXT("Further logs preserve the first batch deadline"), FirstFlush == Telemetry->LogFlushTimerHandle);

    // Oversized JSON rejects synchronously before CreateRequest/ProcessRequest.
    const int32 OriginalByteLimit = Telemetry->MaxRequestBytes;
    Telemetry->MaxRequestBytes = 1;
    Telemetry->FlushRemoteLogs();
    TestTrue(TEXT("Nonretryable local preflight drains the batch"), Telemetry->LogQueue.IsEmpty() && Telemetry->PendingLogFlushBatch.IsEmpty());
    TestFalse(TEXT("An empty queue leaves no recurring timer"), Timers.TimerExists(FirstFlush));
    TestFalse(TEXT("Synchronous preflight releases in-flight state"), Telemetry->bLogFlushInFlight);
    Telemetry->MaxRequestBytes = OriginalByteLimit;

    const auto TelemetrySession = Telemetry->RequestTracker;
    FGuid CapacityTicket;
    if (!TestTrue(TEXT("Reserve the real tracker before retryable flush"), TelemetrySession->Begin(CapacityTicket) == ProjectJ::MMO::EAdmission::Accepted)) return false;
    Telemetry->EnqueueRemoteLog(TEXT("retry-old"), TEXT("Error"));
    Telemetry->FlushRemoteLogs();
    if (!TestEqual(TEXT("Capacity rejection requeues the existing batch"), Telemetry->LogQueue.Num(), 1)) return false;
    TestTrue(TEXT("Retryable local preflight schedules one next batch"), Timers.IsTimerActive(Telemetry->LogFlushTimerHandle));
    Telemetry->EnqueueRemoteLog(TEXT("retry-new"), TEXT("Error"));
    TestEqual(TEXT("Retry and fresh logs retain batch order"), Telemetry->LogQueue[0].Message, FString(TEXT("retry-old")));
    TestTrue(TEXT("All telemetry cases reject before HTTP construction"), Telemetry->ActiveRequests.IsEmpty());

    const FTimerHandle ShutdownFlush = Telemetry->LogFlushTimerHandle;
    // A supported context detach proves no-world cleanup; it is not a travel reproduction.
    TelemetryContext->SetCurrentWorld(nullptr);
    TestNull(TEXT("Teardown case has no current world"), Telemetry->GetWorld());
    Telemetry->Deinitialize();
    TestFalse(TEXT("No-world shutdown clears the GI-owned timer"), Timers.TimerExists(ShutdownFlush));
    TestFalse(TEXT("Shutdown closes the retained telemetry tracker"), TelemetrySession->Complete(CapacityTicket));
    TelemetryContext->SetCurrentWorld(TelemetryWorld);
    Telemetry->EnqueueRemoteLog(TEXT("retired-with-valid-world"), TEXT("Error"));
    TestFalse(TEXT("A retired API with a valid world cannot readmit telemetry"), Telemetry->IsRemoteTelemetryEnabled());
    TestTrue(TEXT("Post-shutdown enqueue cannot resurrect queue or timer"), Telemetry->LogQueue.IsEmpty() && !Timers.TimerExists(Telemetry->LogFlushTimerHandle));
    return true;
}
#endif
