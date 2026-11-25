#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Interfaces/IHttpRequest.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JServerDrainSubsystem.generated.h"

UENUM(BlueprintType)
enum class EProject_JServerDrainState : uint8
{
	InService = 0,
	Draining = 1,
	Flushing = 2,
	ReadyToExit = 3
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FProjectJServerDrainStateChanged, EProject_JServerDrainState /*NewState*/, const FString& /*Reason*/);

/**
 * Coordinates zero-downtime Graceful Server Drain (AAA live-ops shutdown & evacuation):
 *   InService -> Draining (blocks new logins, advertises draining=true on heartbeat, migrates connected clients)
 *             -> Flushing (waits for players == 0 and SQLite Outbox to settle)
 *             -> ReadyToExit (signals clean process exit on dedicated servers).
 * Also monitors UProject_JOnlineWorldSubsystem::HasOutboxStoreFailed() to auto-drain on storage faults.
 */
UCLASS(Config = Game)
class PROJECT_J_API UProject_JServerDrainSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Initiates graceful drain. Idempotent if already draining. */
	bool BeginGracefulDrain(
		const FString& Reason,
		float GraceSeconds = 15.0f,
		const FString& AlternateChannelEndpoint = FString(),
		bool bRequestProcessExitOnComplete = false);

	/** Advances the drain state machine deterministically (also driven by internal 0.25s ticker). */
	void TickDrain(double NowSeconds);

	EProject_JServerDrainState GetDrainState() const { return State; }
	bool IsDraining() const { return State != EProject_JServerDrainState::InService; }
	bool IsReadyForAdmission() const;
	const FString& GetDrainReason() const { return DrainReason; }
	const FString& GetAlternateEndpoint() const { return AlternateEndpoint; }
	int32 GetEvacuatedPlayerCount() const { return EvacuatedPlayersTotal; }

	FProjectJServerDrainStateChanged OnDrainStateChanged;

private:
	void SetDrainState(EProject_JServerDrainState NewState);
	void TickAgones(double NowSeconds);
	FHttpRequestPtr AgonesRequest;
	double NextAgonesReportAt = 0.;
	int32 AgonesSdkPort = 0;
	bool bAgonesReady = false;
	void EvacuateConnectedPlayers(bool bForceDisconnectRemaining);
	int32 CountConnectedPlayers() const;

	EProject_JServerDrainState State = EProject_JServerDrainState::InService;
	FString DrainReason;
	FString AlternateEndpoint;
	double FlushDeadlineAt = 0.0;
	double DrainDeadlineAt = 0.0;
	bool bRequestExitWhenReady = false;
	bool bEvacuationDispatched = false;
	bool bForcedDisconnect = false;
	int32 EvacuatedPlayersTotal = 0;
	FTSTicker::FDelegateHandle DrainTicker;
#if PLATFORM_LINUX
	bool bOwnTerminationSignal = false;
#endif
};
