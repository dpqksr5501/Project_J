#pragma once

#include "CoreMinimal.h"
#include "Project_JMMOTypes.h"
#include "UObject/Interface.h"
#include "Project_JBackendConnection.generated.h"

// Delegate for async backend responses
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnBackendResponse, bool, bSuccess, const FString&, ResponseData);

UENUM(BlueprintType)
enum class EProject_JBackendFailureKind : uint8
{
	None,
	DispatchFailed,
	ConnectionFailed,
	ClientError,
	RateLimited,
	ServerError,
	Unknown
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JBackendRequestContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Backend")
	FProject_JRequestId RequestId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Backend")
	FProject_JIdempotencyKey IdempotencyKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Backend")
	FProject_JTransactionId TransactionId;

	bool HasRequestId() const { return RequestId.IsValid(); }
	bool HasIdempotencyKey() const { return IdempotencyKey.IsValid(); }
	bool HasTransactionId() const { return TransactionId.IsValid(); }
};

USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JBackendResponseEnvelope
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Backend")
	bool bSucceeded = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Backend")
	bool bRetryable = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Backend")
	int32 HttpStatusCode = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Backend")
	FString ResponseData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Backend")
	EProject_JBackendFailureKind FailureKind = EProject_JBackendFailureKind::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Backend")
	FProject_JBackendRequestContext RequestContext;
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnBackendEnvelopeResponse, const FProject_JBackendResponseEnvelope&, Response);

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

	virtual void SendAsyncRequestWithContext(
		const FString& Endpoint,
		const FString& Payload,
		const FProject_JBackendRequestContext& RequestContext,
		FOnBackendResponse OnResponse)
	{
		SendAsyncRequest(Endpoint, Payload, OnResponse);
	}

	virtual void SendAsyncRequestEnvelope(
		const FString& Endpoint,
		const FString& Payload,
		const FProject_JBackendRequestContext& RequestContext,
		FOnBackendEnvelopeResponse OnResponse) = 0;
};
