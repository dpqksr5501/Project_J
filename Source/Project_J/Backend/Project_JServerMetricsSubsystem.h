#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JServerMetricsSubsystem.generated.h"

class FSocket;

/**
 * Zero-dependency Prometheus /metrics & Kubernetes/Agones /healthz HTTP exporter.
 * Aggregates Adaptive Server Load Governor, Iris Crowd Density Filter, SQLite Durable Outbox,
 * Server Integrity Anti-Cheat, and Graceful Drain state into standard Prometheus 0.0.4 text format.
 * Disabled by default (Port=0) unless configured via -ProjectJMetricsPort=<N>, Config MetricsPort,
 * or explicit StartMetricsEndpoint().
 */
UCLASS(Config = Engine)
class PROJECT_J_API UProject_JServerMetricsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Binds a non-blocking loopback TCP listener (127.0.0.1:InPort; pass 0 for ephemeral test port). */
	bool StartMetricsEndpoint(int32 InPort, const FString& BindAddress = TEXT("127.0.0.1"));
	void StopMetricsEndpoint();

	bool IsEndpointRunning() const { return ListenerSocket != nullptr; }
	int32 GetBoundPort() const { return BoundPort; }

	/** Renders Prometheus 0.0.4 exposition text directly (testable without opening sockets). */
	FString RenderPrometheusMetricsText() const;

	/** Renders JSON health payload and returns true when Ready (200 OK) vs NotReady (503). */
	bool RenderHealthJson(FString& OutJson) const;

	/** Polls pending HTTP clients on the listener socket (also driven by 10Hz ticker). */
	void PollMetricsEndpoint();

private:
	UPROPERTY(Config)
	int32 MetricsPort = 0;

	struct FClient
	{
		FSocket* Socket = nullptr;
		double Deadline = 0.;
		TArray<uint8> Request;
		TArray<uint8> Response;
		int32 Sent = 0;
	};
	TArray<FClient> Clients;
	FSocket* ListenerSocket = nullptr;
	int32 BoundPort = 0;
	FTSTicker::FDelegateHandle PollTicker;
};
