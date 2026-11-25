#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Backend/Project_JOnlineOutboxStore.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JOnlineWorldSubsystem.generated.h"

class APlayerController;
class AProject_JPlayerState;
class FJsonObject;
struct FProject_JRolledLootGrant;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FProjectJOnlineConnectResult, bool, bDispatched);

/** Observability counters for the GameInstance durable backend mutation outbox. */
struct PROJECT_J_API FProject_JOnlineOutboxStats
{
	int32 PendingCount = 0;
	int32 InFlightCount = 0;
	int32 PersistingCount = 0;
	int32 FinalizingCount = 0;
	int32 DeadLetterCount = 0;
	int64 EnqueuedTotal = 0;
	int64 ConfirmedTotal = 0;
	int64 RetriedTotal = 0;
	int64 FailedTotal = 0;
	int64 RejectedCapacityTotal = 0;
	int64 RecoveredTotal = 0;
	int64 DeadLetteredTotal = 0;
	int64 RedrivenTotal = 0;
	int64 DeduplicatedTotal = 0;
	int64 StoreErrorTotal = 0;
};

/** Client-side Nakama sign-in handoff and typed helpers for the server's service port. */
UCLASS(Config=Game)
class PROJECT_J_API UProject_JOnlineWorldSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	static constexpr int32 MaxOutboxEntries = 4096;
	static constexpr int32 MaxOutboxInFlight = 64;
	static constexpr int32 MaxOutboxRetries = 5;
	static constexpr double OutboxInitialBackoffSeconds = 0.25;

	using FOutboxDispatchHook = TFunction<void(
		FName /*Service*/,
		FName /*Operation*/,
		const FGuid& /*IdempotencyKey*/,
		const TArray<uint8>& /*Payload*/,
		TFunction<void(TSharedPtr<FJsonObject>)> /*ReplyCallback*/)>;

	// The access token comes from the existing Nakama account sign-in API. It is never sent to Unreal peers.
	UFUNCTION(BlueprintCallable, Category="MMO|Login")
	bool ConnectCharacter(APlayerController* Controller, const FString& AccessToken, FGuid Character,
		const FString& Zone, int32 Channel);

	UPROPERTY(BlueprintAssignable, Category="MMO|Login")
	FProjectJOnlineConnectResult OnConnectDispatched;

	void SendPendingLogin(APlayerController* Controller);
	bool HasOnlineClientSession() const { return OnlineCharacter.IsValid() && !OnlineAccessToken.IsEmpty(); }
	bool ConnectTravel(APlayerController* Controller, FName Zone, int32 Channel, const FString& TravelUrl);
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static bool UsesOnlineBackend(const UWorld* World);
	static bool Submit(UWorld* World, FName Service, FName Operation, const TSharedRef<FJsonObject>& Intent,
		FGuid Key, TFunction<void(TSharedPtr<FJsonObject>)> Completion);
	/**
	 * GameInstance-scoped durable queue for high-fanout backend writes (e.g. boss rewards).
	 * When backed by FProject_JOnlineOutboxStore (auto-enabled on online dedicated servers or via
	 * ConfigurePersistentOutbox), entries commit to SQLite off-GT before dispatch. The
	 * return value reports admission. Crash recovery starts at the SQLite commit boundary.
	 */
	static bool EnqueueDurableMutation(
		UWorld* World,
		FName Service,
		FName Operation,
		const TSharedRef<FJsonObject>& Intent,
		FGuid StableIdempotencyKey,
		const FString& DebugContext,
		TFunction<void(bool /*bConfirmed*/, TSharedPtr<FJsonObject> /*Reply*/)> OnFinished = {});

	/** Enables SQLite-backed crash-safe persistence at DatabasePath and loads any pending rows for recovery. */
	bool ConfigurePersistentOutbox(const FString& DatabasePath);
	bool HasPersistentOutbox() const { return OutboxStore.IsValid(); }
	bool HasOutboxStoreFailed() const { return bStoreLoadFailed; }
	bool HasOutstandingDrainWork() const
	{
		return bStoreWorkerActive || !QueuedStoreOps.IsEmpty() ||
			(!bStoreLoadFailed && (!ActiveOutboxKeys.IsEmpty() || bRecoverMore || (OutboxStore && !bStoreOpened)));
	}
	bool HasOutstandingQuestProgress(FGuid Character) const;

	/** Moves quarantined DeadLetter rows back to Pending for controlled operator replay. */
	bool RedriveDeadLetters(int32 MaxToRedrive = MaxOutboxEntries);

	FProject_JOnlineOutboxStats GetOutboxStats() const;
	void SetOutboxDispatchHookForTest(FOutboxDispatchHook Hook);
	void PumpOutboxForTest(double NowSeconds);
	void FlushPersistentOutboxForTest();
#if WITH_DEV_AUTOMATION_TESTS
	void FailNextOutboxStoreWriteForTest();
#endif

	static bool ApplyInventory(AProject_JPlayerState* Player, const FJsonObject& Response, TFunctionRef<bool()> Current);
	static FString GuidText(FGuid Id);
	static TSharedRef<FJsonObject> WriteLoot(const FProject_JRolledLootGrant& Grant);

private:
	struct FOutboxEntry
	{
		FName Service;
		FName Operation;
		FGuid IdempotencyKey;
		FGuid QuestCharacter;
		TArray<uint8> SerializedPayload;
		FString DebugContext;
		TFunction<void(bool, TSharedPtr<FJsonObject>)> OnFinished;
		int32 Attempts = 0;
		double NextAttemptAt = 0.0;
		uint64 QueueOrder = 0;
	};
	static bool IsOutboxEntryEarlier(const FOutboxEntry& A, const FOutboxEntry& B);
	static FProject_JPersistedOutboxRecord MakeStoreRecord(const FOutboxEntry& Entry, const FString& LastStatus = FString());
	void QueueOutboxEntry(FOutboxEntry&& Entry);
	void ReserveOutboxKey(const FOutboxEntry& Entry);
	void ReleaseOutboxKey(FGuid Key);
	bool CanDispatchOutboxEntry(const FOutboxEntry& Entry) const;
	void QueueRecoveredEntry(FProject_JPersistedOutboxRecord&& Record, double Now,
		TFunction<void(bool, TSharedPtr<FJsonObject>)> Done = {});

	int32 GetTotalActiveOutboxCount() const;
	bool EnqueueDurableMutationInternal(FOutboxEntry&& Entry);
	void PumpOutbox(double NowSeconds);
	void EnsureOutboxTicker();
	void StopOutboxTicker();
	void HandleOutboxReply(uint64 ExpectedGeneration, FOutboxEntry Entry, TSharedPtr<FJsonObject> Reply);

	void KickStoreWorker();
	struct FStoreBatch
	{
		TArray<FProject_JOutboxStoreOp> Ops;
		TMap<FGuid, FProject_JPersistedOutboxRecord> Existing;
		FString Failure;
		int32 DeadLetters = 0;
		bool bCommitted = false;
	};
	bool ClosePersistentStoreSync();
	void HandleStoreOpenLoaded(uint64 ExpectedGeneration, int32 Limit, bool bOk, FString Error,
		TArray<FProject_JPersistedOutboxRecord> Recovered, int32 DeadLetterCount);
	void HandleStoreBatchApplied(uint64 ExpectedGeneration, const FStoreBatch& Batch, TArray<FOutboxEntry> Entries);
	void HandleStoreRedriven(uint64 ExpectedGeneration, bool bOk, FString Error,
		TArray<FProject_JPersistedOutboxRecord> Redriven, int32 RemainingDeadLetters);
	void ReportDeadLetterCount(int32 Count);

	UPROPERTY(Config)
	FString NakamaClientUrl;
	UPROPERTY(Config)
	FString OnlineOutboxDatabasePath;
	FGuid PendingLogin;
	FString PendingEndpoint;
	FString OnlineAccessToken;
	FGuid OnlineCharacter;
	bool RequestLogin(APlayerController* Controller, const FString& AccessToken, FGuid Character,
		const FString& Zone, int32 Channel, const FString& TravelUrl);
	uint64 ConnectGeneration = 0;

	TArray<FOutboxEntry> PendingOutbox;
	TArray<FOutboxEntry> QueuedPersistEntries;
	TArray<FProject_JOutboxStoreOp> QueuedStoreOps;
	TSet<FGuid> ActiveOutboxKeys;
	struct FQuestProgressLane
	{
		int32 ActiveCount = 0;
		TArray<FGuid> Ready;
	};
	TMap<FGuid, FQuestProgressLane> QuestProgressLanes;
	TMap<FGuid, FGuid> QuestCharactersByKey;
	TSharedPtr<FProject_JOnlineOutboxStore, ESPMode::ThreadSafe> OutboxStore;
	TSharedPtr<FStoreBatch, ESPMode::ThreadSafe> ActiveStoreBatch;
	TFuture<void> ActiveStoreTask;
	int32 InFlightOutbox = 0;
	int32 InFlightPersistCount = 0;
	int32 PendingRedriveLimit = 0;
	int32 ReservedRedrive = 0;
	int32 FinalizingCount = 0;
	int64 RecoveryAfterSeq = 0;
	double StoreRetryAt = 0.0;
	uint64 OutboxGeneration = 0;
	uint64 NextOutboxQueueOrder = 0;
	bool bPumpingOutbox = false;
	bool bOutboxClosed = false;
	bool bStoreWorkerActive = false;
	bool bStoreOpened = false;
	bool bStoreLoadFailed = false;
	bool bRecoverMore = false;
	FTSTicker::FDelegateHandle OutboxTicker;
	FProject_JOnlineOutboxStats OutboxStats;
	FOutboxDispatchHook OutboxTestDispatchHook;
};
