#pragma once

#include "Containers/Ticker.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Project_JNPCCharacter.h"
#include "Project_JLoopbackCohort.generated.h"

class UGameplayEffect;
struct FProject_JConfirmedHealthGELoss;
struct FProject_JRenderedBossSample;

USTRUCT()
struct FProject_JCohortHealthObservation
{
	GENERATED_BODY()
	UPROPERTY() float Health = 0.f;
	UPROPERTY() double ObservedAt = 0.;
};

UCLASS(NotBlueprintable, Transient)
class AProject_JCohortTarget : public AProject_JNPCCharacter
{
	GENERATED_BODY()
public:
	AProject_JCohortTarget();
};

UCLASS(NotBlueprintable)
class AProject_JCohortController : public APlayerController
{
	GENERATED_BODY()
public:
	UFUNCTION(Client, Reliable) void ClientBeginCohortStrike();
	UFUNCTION(Server, Reliable) void ServerCohortStrike();
	UFUNCTION(Client, Reliable) void ClientBeginMeasuredStrike(int32 Sequence);
	UFUNCTION(Server, Reliable) void ServerMeasuredStrike(int32 Sequence);
	UFUNCTION(Client, Reliable) void ClientPrepareBossCombat(AProject_JNPCCharacter* Target);
	UFUNCTION(Server, Reliable) void ServerBossCombatReady();
	UFUNCTION(Client, Reliable) void ClientConfirmCohortFinished();
	UFUNCTION(Client, Reliable) void ClientConfirmMeasuredHealth(AProject_JNPCCharacter* Target, float Health);
	UFUNCTION(Server, Reliable) void ServerAcknowledgeMeasuredHealth(const TArray<FProject_JCohortHealthObservation>& Observations);
	UFUNCTION(Client, Reliable) void ClientReleaseMeasuredConnection();
	bool HasCohortCompleted() const;
	bool PollMeasuredRelease();

private:
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	void MarkRenderedStrike();
	TSharedPtr<FProject_JRenderedBossSample> RenderedBossSample;
	TArray<FProject_JCohortHealthObservation> BossHealthObservations;
#endif
	void PollBossReadiness();
	void PressBossCombatAttack();
	bool HasExpectedBossAudience() const;
	TWeakObjectPtr<AProject_JNPCCharacter> ObservedBoss;
	float ExpectedHealth = 0.f;
	bool bCohortCompleted = false;
	bool bCheckReplicatedHealth = false;
	bool bAcknowledgedHealth = false;
	bool bReleaseMeasuredConnection = false;
	bool bBossCombatPrepared = false;
	bool bBossCombatReady = false;
	bool bBossViewAssigned = false;
	uint32 LastBossBlocker = 0;
};

/** Editor-only loopback test. Every client has an actual game net driver and socket. */
UCLASS(NotBlueprintable)
class AProject_JLoopbackCohort : public AGameModeBase
{
	GENERATED_BODY()
public:
	AProject_JLoopbackCohort();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& Error) override;
	virtual void Logout(AController* Exiting) override;
	void ApplyCohortStrike(AProject_JCohortController* Player);
	void ApplyMeasuredStrike(AProject_JCohortController* Player, int32 Sequence);
	void AcknowledgeMeasuredHealth(AProject_JCohortController* Player, TConstArrayView<FProject_JCohortHealthObservation> Observations);
	void AcknowledgeBossReady(AProject_JCohortController* Player);

private:
	bool AdvanceCohort(float Delta);
	void FinishCohort(bool bSuccess);
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	enum class ECombatPhase : uint8 { Waiting, Idle, Burst, Sustained, Drain, Verify, Close };
	struct FParticipant
	{
		int32 Lane = 0;
		int32 Received = 0;
		TArray<double> SentAt;
		int32 BossStrikeIndex = INDEX_NONE;
		bool bAcknowledgedHealth = false;
		bool bReleaseSent = false;
		bool bLoggedOut = false;
		bool bBossReady = false;
	};
	struct FTickSample
	{
		ECombatPhase Phase;
		double Started;
		double IntervalMs;
		double WorkMs;
		double EngineIdleMs;
		double EngineIdleOvershootMs;
		uint32 InBytes;
		uint32 OutBytes;
		int32 Strikes;
		int32 Closed;
	};
	bool AdvanceMeasuredCombat(const UNetDriver& Driver, int32 Ready, double Now);
	bool PrepareBossCombat();
	int32 CountBossCombatAimedParticipants() const;
	bool CheckBossMulticastAudience(const UNetDriver& Driver) const;
	bool SpawnBossParticipant(AProject_JCohortController& Player, int32 Slot);
	void ObserveBossDamage(const FProject_JConfirmedHealthGELoss& Loss);
	bool WriteCombatMetrics() const;
	void BeginMeasuredTick(UWorld* World, ELevelTick TickType, float Delta);
	void EndMeasuredTick(UWorld* World, ELevelTick TickType, float Delta);
	void RetireClientContexts(TConstArrayView<TObjectPtr<UGameInstance>> Retiring);
	TMap<TWeakObjectPtr<AProject_JCohortController>, FParticipant> Participants;
	TArray<FTickSample> TickSamples;
	TArray<double> StrikeLatenciesMs;
	struct FBossStrikeTiming
	{
		double HitWindowMs = -1.;
		double AuthorityMs = -1.;
		double ReplicationMs = -1.;
		double ConfirmedAt = 0.;
		float ConfirmedHealth = 0.f;
	};
	TArray<FBossStrikeTiming> BossStrikeTimings;
	FDelegateHandle TickStartHandle;
	FDelegateHandle TickEndHandle;
	FDelegateHandle BossDamageHandle;
	FDelegateHandle NetworkFailureHandle;
	FString MetricsPath;
	ECombatPhase CombatPhase = ECombatPhase::Waiting;
	double PhaseStarted = 0.;
	double NextBossPrepare = 0.;
	double NextBossClientLaunch = 0.;
	double PreviousTickStart = 0.;
	FTickSample CurrentTick{};
	int32 NextBatch = 0;
	int32 MeasuredStrikes = 0;
	int32 StrikeViolations = 0;
	int32 ServerTickRate = 0;
	int32 CloseBatchSize = 0;
	int32 ConfirmedHealth = 0;
	int32 ReleasesSent = 0;
	int32 ClosedClients = 0;
	int32 LaunchedClients = 0;
	int32 BossPawns = 0;
	int32 BossReadyPlayers = 0;
	int32 MemoryReserveMiB = 0;
	double NextMemoryCheck = 0.;
	uint64 MinFreePhysical = MAX_uint64;
	uint64 MinFreeCommit = MAX_uint64;
	float BossStartingHealth = 0.f;
	float ConfirmedBossDamage = 0.f;
	uint64 BossObservationFailures = 0;
	bool bBossCombat = false;
	bool bStableBossLayout = false;
	bool bBossHitDiagnostics = false;
	bool bOwnerOnlyPeers = false;
	bool bMeasuredClient = false;
	bool bChildNetworkFailed = false;
#endif
	UPROPERTY() TObjectPtr<UClass> BossPlayerClass;
	UPROPERTY() TArray<TObjectPtr<UGameInstance>> Clients;
	UPROPERTY() TObjectPtr<AProject_JNPCCharacter> Boss;
	UPROPERTY() TObjectPtr<UGameplayEffect> Damage;
	TSet<TWeakObjectPtr<AProject_JCohortController>> Strikes;
	FTSTicker::FDelegateHandle Ticker;
	FString Endpoint;
	double Started = 0.;
	int32 Expected = 0;
	int32 Peak = 0;
	bool bSentStrike = false;
	bool bFixtureEnabled = false;
};
