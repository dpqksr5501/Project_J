#pragma once

#include "CoreMinimal.h"

class FProject_JProfileDatabase;

/** Persisted server outbox mutation record stored in SQLite for crash/restart recovery. */
struct FProject_JPersistedOutboxRecord
{
	FGuid IdempotencyKey;
	int64 CreatedSeq = 0;
	FName Service;
	FName Operation;
	TArray<uint8> SerializedPayload;
	FString DebugContext;
	int32 TotalAttempts = 0;
	FString LastStatus;
	bool bDeadLetter = false;
};

enum class EProject_JOutboxStoreOpKind : uint8
{
	UpsertPending,
	DeleteConfirmed,
	MarkDeadLetter
};

struct FProject_JOutboxStoreOp
{
	EProject_JOutboxStoreOpKind Kind = EProject_JOutboxStoreOpKind::UpsertPending;
	FProject_JPersistedOutboxRecord Record;
};

/**
 * Worker-thread-only crash-safe SQLite store backing UProject_JOnlineWorldSubsystem's durable outbox.
 * Uses Project_J's Win32 rollback-journal VFS (FProject_JProfileDatabase) with batched transactions
 * so 200-player boss reward bursts commit in a single off-GT atomic transaction without GT hitches.
 */
class FProject_JOnlineOutboxStore final
{
public:
	static constexpr int32 ApplicationId = 0x504A4F42; // 'PJOB'
	static constexpr int32 SchemaVersion = 1;
	static constexpr int32 DeadLetterAdmissionLimit = 16384;

	explicit FProject_JOnlineOutboxStore(FString InFilename);
	~FProject_JOnlineOutboxStore();

	FProject_JOnlineOutboxStore(const FProject_JOnlineOutboxStore&) = delete;
	FProject_JOnlineOutboxStore& operator=(const FProject_JOnlineOutboxStore&) = delete;

	/** Open and read a bounded page of pending rows in durable FIFO order. */
	bool OpenAndLoadPending(int32 MaxPendingToLoad, TArray<FProject_JPersistedOutboxRecord>& OutPending,
		int32& OutDeadLetterCount, int64 AfterSeq = 0);

	/** Applies a batch of UpsertPending / DeleteConfirmed / MarkDeadLetter operations in one atomic transaction. */
	bool ApplyBatch(TConstArrayView<FProject_JOutboxStoreOp> Ops, int32& OutDeadLetterCount,
		TMap<FGuid, FProject_JPersistedOutboxRecord>* OutExisting = nullptr);

	/** Moves DeadLetter (state=2) rows back to Pending (state=0) atomically and returns them for immediate replay. */
	bool RedriveDeadLetters(int32 MaxToRedrive, TArray<FProject_JPersistedOutboxRecord>& OutRedriven, int32& OutRemainingDeadLetters);

	/** Worker-thread-only close. */
	bool Close();

	FString GetLastFailure() const;
	const FString& GetFilename() const { return Filename; }

#if WITH_DEV_AUTOMATION_TESTS
	void FailNextWriteForTest();
	void CrashOnNextBatchCommitForTest();
#endif

private:
	bool EnsureOpenLocked();
	bool ReadDeadLetterCountLocked(int32& OutCount);

	mutable FCriticalSection Mutex;
	TUniquePtr<FProject_JProfileDatabase> Database;
	FString Filename;
	FString LastFailure;
	int64 NextCreatedSeq = 1;
#if WITH_DEV_AUTOMATION_TESTS
	bool bCrashOnNextBatchCommit = false;
	bool bFailNextBatchWrite = false;
#endif
};
