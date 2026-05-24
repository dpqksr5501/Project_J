#include "Backend/Project_JGatewaySubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "JsonObjectConverter.h"

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
	// Cleanup connections
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
		if (bConnectedSuccessfully && ResponsePtr.IsValid())
		{
			OnResponse.ExecuteIfBound(true, ResponsePtr->GetContentAsString());
		}
		else
		{
			OnResponse.ExecuteIfBound(false, TEXT("Connection Failed"));
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

	// In a real implementation, serialize LogQueue to a JSON Array and send via Discord Webhook or ELK endpoint.
	// Example Discord Webhook format:
	// FString Payload = FString::Printf(TEXT("{\"content\": \"[%s] %s\"}"), *LogQueue[0].Severity, *LogQueue[0].Message);
	
	FString Payload = TEXT("{ \"logs\": [");
	for (int32 i = 0; i < LogQueue.Num(); ++i)
	{
		Payload += FString::Printf(TEXT("{\"severity\":\"%s\", \"message\":\"%s\"}%s"), 
			*LogQueue[i].Severity, *LogQueue[i].Message, (i == LogQueue.Num() - 1) ? TEXT("") : TEXT(","));
	}
	Payload += TEXT("] }");

	SendAsyncRequest(TEXT("telemetry"), Payload, FOnBackendResponse());
	LogQueue.Empty();
}
