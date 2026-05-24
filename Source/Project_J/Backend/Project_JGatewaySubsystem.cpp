#include "Backend/Project_JGatewaySubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

void UProject_JGatewaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Initialization logic for HTTP module or Socket pooling
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
