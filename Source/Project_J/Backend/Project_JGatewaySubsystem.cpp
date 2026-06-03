#include "Backend/Project_JGatewaySubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"

void UProject_JGatewaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
	// Set up rate-limited flush timer (e.g. every 5 seconds)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(LogFlushTimerHandle, this, &UProject_JGatewaySubsystem::FlushRemoteLogs, 5.0f, true);
	}
}

void UProject_JGatewaySubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LogFlushTimerHandle);
	}

	Super::Deinitialize();
}

void UProject_JGatewaySubsystem::SendAsyncRequest(const FString& Endpoint, const FString& Payload, FOnBackendResponse OnResponse)
{
	// Example HTTP implementation skeleton
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(GatewayUrl + Endpoint);
	Request->SetVerb("POST");
	Request->SetHeader("Content-Type", "application/json");
	Request->SetContentAsString(Payload);

	Request->OnProcessRequestComplete().BindLambda([OnResponse](FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bConnectedSuccessfully)
	{
		const bool bSucceeded = bConnectedSuccessfully && ResponsePtr.IsValid() && EHttpResponseCodes::IsOk(ResponsePtr->GetResponseCode());
		if (bSucceeded)
		{
			OnResponse.ExecuteIfBound(true, ResponsePtr->GetContentAsString());
		}
		else
		{
			const FString ResponseData = ResponsePtr.IsValid() ? ResponsePtr->GetContentAsString() : TEXT("Connection Failed");
			OnResponse.ExecuteIfBound(false, ResponseData);
		}
	});

	Request->ProcessRequest();
}

void UProject_JGatewaySubsystem::EnqueueRemoteLog(const FString& Message, const FString& Severity)
{
	FLogPayload Payload;
	Payload.Message = Message;
	Payload.Severity = Severity;
	LogQueue.Add(Payload);
	
	// Hard cap to prevent memory leak on disconnected environments
	if (LogQueue.Num() > 100)
	{
		LogQueue.RemoveAt(0, LogQueue.Num() - 100);
	}
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

	FOnBackendResponse ResponseDelegate;
	ResponseDelegate.BindDynamic(this, &UProject_JGatewaySubsystem::HandleRemoteLogFlushResponse);
	SendAsyncRequest(TEXT("telemetry"), Payload, ResponseDelegate);
}

void UProject_JGatewaySubsystem::HandleRemoteLogFlushResponse(bool bSucceeded, const FString& Response)
{
	bLogFlushInFlight = false;

	if (bSucceeded)
	{
		PendingLogFlushBatch.Reset();
		return;
	}

	LogQueue.Insert(PendingLogFlushBatch, 0);
	PendingLogFlushBatch.Reset();
	if (LogQueue.Num() > 100)
	{
		LogQueue.RemoveAt(0, LogQueue.Num() - 100);
	}
}
