#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JBackendConnection.h"
#include "Project_JGatewaySubsystem.generated.h"

/**
 * Subsystem to manage all connections to the Gateway backend.
 * Uses the IProject_JBackendConnection interface to decouple the underlying implementation (HTTP/Socket).
 */
UCLASS()
class PROJECT_J_API UProject_JGatewaySubsystem : public UGameInstanceSubsystem, public IProject_JBackendConnection
{
	GENERATED_BODY()

public:
	// USubsystem overrides
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// IProject_JBackendConnection interface
	virtual void SendAsyncRequest(const FString& Endpoint, const FString& Payload, FOnBackendResponse OnResponse) override;

	// Enqueue a log message to be sent to the backend asynchronously (Rate Limited)
	void EnqueueRemoteLog(const FString& Message, const FString& Severity);

private:
	// Implementation details for HTTP pooling or Socket persistence would go here.
	FString GatewayUrl = TEXT("http://127.0.0.1:8080/api/v1/");

	// Log Batching
	struct FLogPayload
	{
		FString Message;
		FString Severity;
	};
	TArray<FLogPayload> LogQueue;
	FTimerHandle LogFlushTimerHandle;

	void FlushRemoteLogs();
};
