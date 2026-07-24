#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JBackendConnection.h"
#include "Project_JGatewaySubsystem.generated.h"

/**
 * Subsystem to manage all connections to the Gateway backend.
 * Uses the IProject_JBackendConnection interface to decouple the underlying implementation (HTTP/Socket).
 */
UCLASS(Config=Game, DefaultConfig)
class PROJECT_J_API UProject_JGatewaySubsystem : public UGameInstanceSubsystem, public IProject_JBackendConnection
{
	GENERATED_BODY()

public:
	// USubsystem overrides
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// IProject_JBackendConnection interface
	virtual void SendAsyncRequest(const FString& Endpoint, const FString& Payload, FOnBackendResponse OnResponse) override;
	virtual void SendAsyncRequestWithContext(
		const FString& Endpoint,
		const FString& Payload,
		const FProject_JBackendRequestContext& RequestContext,
		FOnBackendResponse OnResponse) override;
	virtual void SendAsyncRequestEnvelope(
		const FString& Endpoint,
		const FString& Payload,
		const FProject_JBackendRequestContext& RequestContext,
		FOnBackendEnvelopeResponse OnResponse) override;

	// Enqueue a log message to be sent to the backend asynchronously (Rate Limited)
	void EnqueueRemoteLog(const FString& Message, const FString& Severity);
	bool IsRemoteTelemetryEnabled() const;

private:
	/** Remote telemetry is opt-in. Do not enable it without a production HTTPS endpoint and a server-side redaction policy. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Backend|Telemetry")
	bool bEnableRemoteTelemetry = false;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Backend")
	FString GatewayUrl;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Backend|Telemetry", meta = (ClampMin = "128", UIMin = "128"))
	int32 MaxRemoteLogMessageLength = 2048;

	UPROPERTY(EditDefaultsOnly, Category = "Backend|Telemetry", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxQueuedRemoteLogs = 100;

	UPROPERTY(EditDefaultsOnly, Category = "Backend|Telemetry", meta = (ClampMin = "0.1", UIMin = "0.1", Units = "s"))
	float RemoteLogFlushInterval = 5.0f;

	// Log Batching
	struct FLogPayload
	{
		FString Message;
		FString Severity;
	};
	TArray<FLogPayload> LogQueue;
	TArray<FLogPayload> PendingLogFlushBatch;
	FTimerHandle LogFlushTimerHandle;
	bool bLogFlushInFlight = false;

	void FlushRemoteLogs();
	void TrimRemoteLogQueue();
	UFUNCTION()
	void HandleRemoteLogFlushEnvelopeResponse(const FProject_JBackendResponseEnvelope& Response);
};
