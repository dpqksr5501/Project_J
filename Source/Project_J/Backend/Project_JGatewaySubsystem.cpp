#include "Backend/Project_JGatewaySubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "MMO/Coordination.h"

namespace
{
FProject_JBackendRequestContext NormalizeRequestContext(FProject_JBackendRequestContext RequestContext)
{
	if (!RequestContext.HasRequestId())
	{
		RequestContext.RequestId = FProject_JRequestId::NewId();
	}
	return RequestContext;
}

EProject_JBackendFailureKind ClassifyFailure(int32 StatusCode, bool bConnectedSuccessfully, bool bDispatchFailed)
{
	if (bDispatchFailed)
	{
		return EProject_JBackendFailureKind::DispatchFailed;
	}
	if (!bConnectedSuccessfully || StatusCode <= 0)
	{
		return EProject_JBackendFailureKind::ConnectionFailed;
	}
	if (StatusCode == 429)
	{
		return EProject_JBackendFailureKind::RateLimited;
	}
	if (StatusCode >= 500)
	{
		return EProject_JBackendFailureKind::ServerError;
	}
	if (StatusCode >= 400)
	{
		return EProject_JBackendFailureKind::ClientError;
	}
	return EProject_JBackendFailureKind::Unknown;
}

bool IsRetryableBackendFailure(EProject_JBackendFailureKind FailureKind, int32 StatusCode)
{
	return
		FailureKind == EProject_JBackendFailureKind::ConnectionFailed ||
		FailureKind == EProject_JBackendFailureKind::RateLimited ||
		FailureKind == EProject_JBackendFailureKind::ServerError ||
		StatusCode == 408;
}

FProject_JBackendResponseEnvelope BuildResponseEnvelope(
	const FProject_JBackendRequestContext& RequestContext,
	FHttpResponsePtr ResponsePtr,
	bool bConnectedSuccessfully,
	bool bDispatchFailed)
{
	FProject_JBackendResponseEnvelope Envelope;
	Envelope.RequestContext = RequestContext;
	Envelope.HttpStatusCode = ResponsePtr.IsValid() ? ResponsePtr->GetResponseCode() : 0;
	Envelope.ResponseData = ResponsePtr.IsValid() ? ResponsePtr->GetContentAsString() : TEXT("Connection Failed");
	Envelope.bSucceeded = !bDispatchFailed && bConnectedSuccessfully && ResponsePtr.IsValid() && EHttpResponseCodes::IsOk(Envelope.HttpStatusCode);
	Envelope.FailureKind = Envelope.bSucceeded
		? EProject_JBackendFailureKind::None
		: ClassifyFailure(Envelope.HttpStatusCode, bConnectedSuccessfully, bDispatchFailed);
	Envelope.bRetryable = !Envelope.bSucceeded && IsRetryableBackendFailure(Envelope.FailureKind, Envelope.HttpStatusCode);
	return Envelope;
}

TSharedRef<IHttpRequest, ESPMode::ThreadSafe> CreateGatewayRequest(
	const FString& GatewayUrl,
	const FString& Endpoint,
	const FString& Payload,
	const FProject_JBackendRequestContext& InRequestContext,
	TFunction<void(const FProject_JBackendResponseEnvelope&)> OnEnvelope)
{
	const FProject_JBackendRequestContext RequestContext = NormalizeRequestContext(InRequestContext);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(GatewayUrl + Endpoint);
	Request->SetVerb("POST");
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("X-ProjectJ-Request-Id"), RequestContext.RequestId.ToString());
	if (RequestContext.HasIdempotencyKey())
	{
		Request->SetHeader(TEXT("X-ProjectJ-Idempotency-Key"), RequestContext.IdempotencyKey.ToString());
	}
	if (RequestContext.HasTransactionId())
	{
		Request->SetHeader(TEXT("X-ProjectJ-Transaction-Id"), RequestContext.TransactionId.ToString());
	}
	Request->SetContentAsString(Payload);
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);

	Request->OnProcessRequestComplete().BindLambda([RequestContext, OnEnvelope](FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bConnectedSuccessfully)
	{
		OnEnvelope(BuildResponseEnvelope(RequestContext, ResponsePtr, bConnectedSuccessfully, false));
	});

	return Request;
}
}

void UProject_JGatewaySubsystem::DispatchTrackedRequest(const FString& Endpoint, const FString& Payload,
	const FProject_JBackendRequestContext& InContext,
	TFunction<void(const FProject_JBackendResponseEnvelope&)> Completion)
{
	check(IsInGameThread());
	const auto Context = NormalizeRequestContext(InContext);
	const auto Tracker = RequestTracker;
	FGuid Ticket;
	const auto Admission = Tracker ? Tracker->Begin(Ticket) : ProjectJ::MMO::EAdmission::Closed;
	if (Admission != ProjectJ::MMO::EAdmission::Accepted)
	{
		FProject_JBackendResponseEnvelope Response;
		Response.RequestContext = Context;
		Response.FailureKind = Admission == ProjectJ::MMO::EAdmission::Capacity
			? EProject_JBackendFailureKind::Overloaded : EProject_JBackendFailureKind::Unavailable;
		Response.bRetryable = Admission == ProjectJ::MMO::EAdmission::Capacity;
		Completion(Response); return;
	}
	TWeakObjectPtr<UProject_JGatewaySubsystem> WeakThis(this);
	auto Finish = [WeakThis, Tracker, Ticket, Completion](const FProject_JBackendResponseEnvelope& Response)
	{
		check(IsInGameThread());
		// ProcessRequest failure and HTTP completion may race/reenter. Deliver once;
		// old-session completions after Deinitialize are deliberately suppressed.
		if (!Tracker->Complete(Ticket)) { return; }
		if (auto* Self = WeakThis.Get())
		{
			Self->ActiveRequests.Remove(Ticket);
			Completion(Response);
		}
	};
	auto Request = CreateGatewayRequest(GatewayUrl, Endpoint, Payload, Context, Finish);
	Request->SetTimeout(FMath::Max(1.0f, RequestTimeoutSeconds));
	ActiveRequests.Add(Ticket, Request);
	if (!Request->ProcessRequest())
	{
		auto Response = BuildResponseEnvelope(Context, nullptr, false, true);
		Response.ResponseData = TEXT("Request dispatch failed");
		Finish(Response);
	}
}

void UProject_JGatewaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RequestTracker = MakeShared<ProjectJ::MMO::FRequestTracker, ESPMode::ThreadSafe>(FMath::Clamp(MaxInFlightRequests, 1, 4096));
	
	// Set up rate-limited flush timer (e.g. every 5 seconds)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(LogFlushTimerHandle, this, &UProject_JGatewaySubsystem::FlushRemoteLogs, RemoteLogFlushInterval, true);
	}
}

void UProject_JGatewaySubsystem::Deinitialize()
{
	check(IsInGameThread());
	if (RequestTracker) { RequestTracker->Close(); }
	// Close first so cancellation callbacks cannot mutate ActiveRequests during iteration.
	for (auto& Pair : ActiveRequests) { Pair.Value->CancelRequest(); }
	ActiveRequests.Reset();
	RequestTracker.Reset();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LogFlushTimerHandle);
	}

	LogQueue.Reset();
	PendingLogFlushBatch.Reset();
	bLogFlushInFlight = false;

	Super::Deinitialize();
}

void UProject_JGatewaySubsystem::SendAsyncRequest(const FString& Endpoint, const FString& Payload, FOnBackendResponse OnResponse)
{
	SendAsyncRequestWithContext(Endpoint, Payload, FProject_JBackendRequestContext(), OnResponse);
}

void UProject_JGatewaySubsystem::SendAsyncRequestWithContext(
	const FString& Endpoint,
	const FString& Payload,
	const FProject_JBackendRequestContext& RequestContext,
	FOnBackendResponse OnResponse)
{
	if (GatewayUrl.IsEmpty())
	{
		FProject_JBackendResponseEnvelope Envelope;
		Envelope.RequestContext = NormalizeRequestContext(RequestContext);
		Envelope.FailureKind = EProject_JBackendFailureKind::DispatchFailed;
		Envelope.ResponseData = TEXT("Gateway URL is not configured");
		OnResponse.ExecuteIfBound(false, Envelope.ResponseData);
		return;
	}
	DispatchTrackedRequest(Endpoint, Payload, RequestContext, [OnResponse](const FProject_JBackendResponseEnvelope& Envelope)
	{
		OnResponse.ExecuteIfBound(Envelope.bSucceeded, Envelope.ResponseData);
	});
}

void UProject_JGatewaySubsystem::SendAsyncRequestEnvelope(
	const FString& Endpoint,
	const FString& Payload,
	const FProject_JBackendRequestContext& RequestContext,
	FOnBackendEnvelopeResponse OnResponse)
{
	if (GatewayUrl.IsEmpty())
	{
		FProject_JBackendResponseEnvelope Envelope;
		Envelope.RequestContext = NormalizeRequestContext(RequestContext);
		Envelope.FailureKind = EProject_JBackendFailureKind::DispatchFailed;
		Envelope.ResponseData = TEXT("Gateway URL is not configured");
		OnResponse.ExecuteIfBound(Envelope);
		return;
	}
	DispatchTrackedRequest(Endpoint, Payload, RequestContext, [OnResponse](const FProject_JBackendResponseEnvelope& Envelope)
	{
		OnResponse.ExecuteIfBound(Envelope);
	});
}

void UProject_JGatewaySubsystem::EnqueueRemoteLog(const FString& Message, const FString& Severity)
{
	if (!IsRemoteTelemetryEnabled())
	{
		return;
	}

	FLogPayload Payload;
	Payload.Message = Message.Left(FMath::Max(128, MaxRemoteLogMessageLength));
	Payload.Severity = Severity;
	LogQueue.Add(Payload);
	TrimRemoteLogQueue();
}

bool UProject_JGatewaySubsystem::IsRemoteTelemetryEnabled() const
{
	const UWorld* World = GetWorld();
	return bEnableRemoteTelemetry &&
		!GatewayUrl.IsEmpty() &&
		GatewayUrl.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase) &&
		World &&
		World->GetNetMode() != NM_Client;
}

void UProject_JGatewaySubsystem::FlushRemoteLogs()
{
	if (LogQueue.Num() == 0) return;
	if (bLogFlushInFlight) return;

	Swap(PendingLogFlushBatch, LogQueue);

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> LogValues;
	LogValues.Reserve(PendingLogFlushBatch.Num());
	for (const FLogPayload& Log : PendingLogFlushBatch)
	{
		TSharedRef<FJsonObject> LogObject = MakeShared<FJsonObject>();
		LogObject->SetStringField(TEXT("severity"), Log.Severity);
		LogObject->SetStringField(TEXT("message"), Log.Message);
		LogValues.Add(MakeShared<FJsonValueObject>(LogObject));
	}
	RootObject->SetArrayField(TEXT("logs"), LogValues);

	FString Payload;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
	FJsonSerializer::Serialize(RootObject, Writer);

	bLogFlushInFlight = true;

	FProject_JBackendRequestContext RequestContext;
	RequestContext.RequestId = FProject_JRequestId::NewId();

	FOnBackendEnvelopeResponse ResponseDelegate;
	ResponseDelegate.BindDynamic(this, &UProject_JGatewaySubsystem::HandleRemoteLogFlushEnvelopeResponse);
	SendAsyncRequestEnvelope(TEXT("telemetry"), Payload, RequestContext, ResponseDelegate);
}

void UProject_JGatewaySubsystem::HandleRemoteLogFlushEnvelopeResponse(const FProject_JBackendResponseEnvelope& Response)
{
	bLogFlushInFlight = false;

	if (Response.bSucceeded)
	{
		PendingLogFlushBatch.Reset();
		return;
	}

	if (Response.bRetryable)
	{
		LogQueue.Insert(PendingLogFlushBatch, 0);
	}
	PendingLogFlushBatch.Reset();
	TrimRemoteLogQueue();
}

void UProject_JGatewaySubsystem::TrimRemoteLogQueue()
{
	if (LogQueue.Num() > MaxQueuedRemoteLogs)
	{
		LogQueue.RemoveAt(0, LogQueue.Num() - MaxQueuedRemoteLogs);
	}
}
