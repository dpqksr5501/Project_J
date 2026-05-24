#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Project_JBackendConnection.generated.h"

// Delegate for async backend responses
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnBackendResponse, bool, bSuccess, const FString&, ResponseData);

UINTERFACE(MinimalAPI, BlueprintType)
class UProject_JBackendConnection : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface to abstract backend communications (Gateway, Social, Matchmaking).
 * Pillar 1: Gateway Routing and Multi-Process Backend Communication.
 */
class PROJECT_J_API IProject_JBackendConnection
{
	GENERATED_BODY()

public:
	/**
	 * Send an asynchronous request to the backend.
	 * @param Endpoint The API endpoint route
	 * @param Payload JSON payload string
	 * @param OnResponse Callback when the response is received
	 */
	virtual void SendAsyncRequest(const FString& Endpoint, const FString& Payload, FOnBackendResponse OnResponse) = 0;
};
