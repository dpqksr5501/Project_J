#include "Backend/Project_JOnlineOutboxStore.h"

#include "Backend/Project_JProfileDatabase.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

namespace
{
constexpr int32 OutboxStatePending = 0;
constexpr int32 OutboxStateDeadLetter = 2;
constexpr const TCHAR* RecordColumns = TEXT("idempotency_key, created_seq, service, operation, payload, debug_context, total_attempts, last_status, state");

int64 CurrentUtcMillis()
{
	return static_cast<int64>((FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalMilliseconds());
}

bool ReadGuidColumn(const FProject_JProfileStatement& Stmt, int32 ColIdx, FGuid& OutGuid)
{
	TArray<uint8> Bytes;
	if (!Stmt.GetColumnValueByIndex(ColIdx, Bytes) || Bytes.Num() != sizeof(FGuid))
	{
		return false;
	}
	FMemory::Memcpy(&OutGuid, Bytes.GetData(), sizeof(FGuid));
	return OutGuid.IsValid();
}

bool ReadRecord(const FProject_JProfileStatement& Stmt, FProject_JPersistedOutboxRecord& Rec)
{
	FString Service, Operation;
	int32 State = 0;
	if (!ReadGuidColumn(Stmt, 0, Rec.IdempotencyKey) || !Stmt.GetColumnValueByIndex(1, Rec.CreatedSeq)
		|| !Stmt.GetColumnValueByIndex(2, Service) || !Stmt.GetColumnValueByIndex(3, Operation)
		|| !Stmt.GetColumnValueByIndex(4, Rec.SerializedPayload) || !Stmt.GetColumnValueByIndex(5, Rec.DebugContext)
		|| !Stmt.GetColumnValueByIndex(6, Rec.TotalAttempts) || !Stmt.GetColumnValueByIndex(7, Rec.LastStatus)
		|| !Stmt.GetColumnValueByIndex(8, State) || (State != OutboxStatePending && State != OutboxStateDeadLetter)
		|| Rec.CreatedSeq <= 0 || Service.Len() >= NAME_SIZE || Operation.Len() >= NAME_SIZE)
	{
		return false;
	}
	Rec.Service = FName(*Service);
	Rec.Operation = FName(*Operation);
	Rec.bDeadLetter = State == OutboxStateDeadLetter;
	return !Rec.Service.IsNone() && !Rec.Operation.IsNone();
}
}

FProject_JOnlineOutboxStore::FProject_JOnlineOutboxStore(FString InFilename)
	: Database(MakeUnique<FProject_JProfileDatabase>())
	, Filename(MoveTemp(InFilename))
{
	if (!Filename.IsEmpty())
	{
		Filename = FPaths::ConvertRelativePathToFull(Filename);
		FPaths::NormalizeFilename(Filename);
		FPaths::CollapseRelativeDirectories(Filename);
	}
}

FProject_JOnlineOutboxStore::~FProject_JOnlineOutboxStore()
{
	check(!Database || !Database->IsValid() || !IsInGameThread());
}

FString FProject_JOnlineOutboxStore::GetLastFailure() const
{
	FScopeLock Lock(&Mutex);
	return LastFailure;
}

bool FProject_JOnlineOutboxStore::Close()
{
	check(!IsInGameThread());
	FScopeLock Lock(&Mutex);
	return !Database || Database->Close();
}

#if WITH_DEV_AUTOMATION_TESTS
void FProject_JOnlineOutboxStore::FailNextWriteForTest()
{
	FScopeLock Lock(&Mutex);
	bFailNextBatchWrite = true;
}

void FProject_JOnlineOutboxStore::CrashOnNextBatchCommitForTest()
{
	FScopeLock Lock(&Mutex);
	bCrashOnNextBatchCommit = true;
}
#endif

bool FProject_JOnlineOutboxStore::EnsureOpenLocked()
{
	check(!IsInGameThread());
	if (!Database)
	{
		Database = MakeUnique<FProject_JProfileDatabase>();
	}
	if (Database->IsValid())
	{
		return true;
	}
	if (Filename.IsEmpty())
	{
		LastFailure = TEXT("Outbox database filename is empty");
		return false;
	}
	const FString Directory = FPaths::GetPath(Filename);
	if (!Directory.IsEmpty() && !IFileManager::Get().MakeDirectory(*Directory, true))
	{
		LastFailure = FString::Printf(TEXT("Failed to create outbox directory: %s"), *Directory);
		return false;
	}

	if (!Database->Open(*Filename, ESQLiteDatabaseOpenMode::ReadWriteCreate))
	{
		LastFailure = FString::Printf(TEXT("SQLite open failed (rc=%d)"), Database->GetLastResultCode());
		return false;
	}

	if (!Database->Execute(TEXT("PRAGMA busy_timeout=1000;"))
		|| !Database->Execute(TEXT("PRAGMA synchronous=FULL;"))
		|| !Database->Execute(TEXT("PRAGMA locking_mode=EXCLUSIVE;"))
		|| !Database->Execute(TEXT("BEGIN EXCLUSIVE;")))
	{
		LastFailure = FString::Printf(TEXT("SQLite pragma setup failed (rc=%d)"), Database->GetLastResultCode());
		Database->Close();
		return false;
	}

	int32 AppId = 0;
	int32 Version = 0;
	if (!Database->GetApplicationId(AppId) || !Database->GetUserVersion(Version))
	{
		LastFailure = TEXT("Failed reading SQLite header metadata");
		Database->Close();
		return false;
	}

	if (AppId == 0 && Version == 0)
	{
		bool bEmpty = false;
		{
			auto Stmt = Database->PrepareStatement(TEXT("SELECT 1 FROM sqlite_schema LIMIT 1;"));
			bEmpty = Stmt.IsValid() && Stmt.Step() == ESQLitePreparedStatementStepResult::Done;
		}
		const bool bCreated = bEmpty && Database->SetApplicationId(ApplicationId)
			&& Database->SetUserVersion(SchemaVersion)
			&& Database->Execute(TEXT(
				"CREATE TABLE IF NOT EXISTS outbox_entries ("
				"idempotency_key BLOB PRIMARY KEY NOT NULL,"
				"created_seq INTEGER NOT NULL,"
				"service TEXT NOT NULL,"
				"operation TEXT NOT NULL,"
				"payload BLOB NOT NULL,"
				"debug_context TEXT NOT NULL,"
				"state INTEGER NOT NULL,"
				"total_attempts INTEGER NOT NULL,"
				"last_status TEXT NOT NULL,"
				"updated_utc_ms INTEGER NOT NULL);"))
			&& Database->Execute(TEXT("CREATE INDEX IF NOT EXISTS idx_outbox_state_seq ON outbox_entries(state, created_seq);"));
		if (!bCreated)
		{
			Database->Execute(TEXT("ROLLBACK;"));
			LastFailure = FString::Printf(TEXT("Failed initializing outbox schema (rc=%d)"), Database->GetLastResultCode());
			Database->Close();
			return false;
		}
	}
	else if (AppId != ApplicationId || Version != SchemaVersion)
	{
		LastFailure = FString::Printf(TEXT("Foreign or unsupported outbox database (app=%d ver=%d)"), AppId, Version);
		Database->Close();
		return false;
	}

	bool bValidSchema = false;
	{
		auto Mode = Database->PrepareStatement(TEXT("PRAGMA journal_mode;"));
		FString Journal;
		bValidSchema = Mode.IsValid() && Mode.Step() == ESQLitePreparedStatementStepResult::Row
			&& Mode.GetColumnValueByIndex(0, Journal) && Journal == TEXT("delete");
	}
	if (!bValidSchema || !Database->PerformQuickIntegrityCheck())
	{
		LastFailure = TEXT("Outbox SQLite integrity check failed");
		Database->Close();
		return false;
	}

	int64 MaxSeq = 0;
	{
		// The recovery cursor must identify a unique, increasing committed row.
		bValidSchema = Database->Execute(TEXT("CREATE UNIQUE INDEX IF NOT EXISTS idx_outbox_created_seq ON outbox_entries(created_seq);"));
		auto MaxStmt = Database->PrepareStatement(TEXT("SELECT COALESCE(MAX(created_seq), 0) FROM outbox_entries;"));
		bValidSchema = bValidSchema && MaxStmt.IsValid() && MaxStmt.Step() == ESQLitePreparedStatementStepResult::Row
			&& MaxStmt.GetColumnValueByIndex(0, MaxSeq) && MaxSeq >= 0 && MaxSeq < MAX_int64;
	}
	if (!bValidSchema || !Database->Execute(TEXT("COMMIT;")))
	{
		LastFailure = TEXT("Invalid outbox schema or sequence");
		Database->Close();
		return false;
	}
	NextCreatedSeq = MaxSeq + 1;

	LastFailure.Reset();
	return true;
}

bool FProject_JOnlineOutboxStore::ReadDeadLetterCountLocked(int32& OutCount)
{
	OutCount = 0;
	auto Stmt = Database->PrepareStatement(TEXT("SELECT COUNT(*) FROM outbox_entries WHERE state = 2;"));
	int64 Count = 0;
	if (!Stmt.IsValid() || Stmt.Step() != ESQLitePreparedStatementStepResult::Row || !Stmt.GetColumnValueByIndex(0, Count))
	{
		return false;
	}
	OutCount = static_cast<int32>(FMath::Clamp<int64>(Count, 0, MAX_int32));
	return true;
}

bool FProject_JOnlineOutboxStore::OpenAndLoadPending(
	int32 MaxPendingToLoad, TArray<FProject_JPersistedOutboxRecord>& OutPending,
	int32& OutDeadLetterCount, int64 AfterSeq)
{
	check(!IsInGameThread());
	OutPending.Reset();
	OutDeadLetterCount = 0;
	FScopeLock Lock(&Mutex);
	if (!EnsureOpenLocked()) return false;

	auto Stmt = Database->PrepareStatement(*FString::Printf(TEXT(
		"SELECT %s FROM outbox_entries WHERE state = 0 AND created_seq > ?1 ORDER BY created_seq ASC LIMIT ?2;"),
		RecordColumns));
	if (Stmt.IsValid() && Stmt.SetBindingValueByIndex(1, AfterSeq) && Stmt.SetBindingValueByIndex(2, MaxPendingToLoad))
	{
		while (true)
		{
			const auto Step = Stmt.Step();
			if (Step == ESQLitePreparedStatementStepResult::Done && ReadDeadLetterCountLocked(OutDeadLetterCount))
			{
				LastFailure.Reset();
				return true;
			}
			FProject_JPersistedOutboxRecord Record;
			if (Step != ESQLitePreparedStatementStepResult::Row || !ReadRecord(Stmt, Record)) break;
			OutPending.Add(MoveTemp(Record));
		}
	}
	LastFailure = TEXT("Invalid or unreadable pending outbox page");
	OutPending.Reset();
	return false;
}

bool FProject_JOnlineOutboxStore::ApplyBatch(TConstArrayView<FProject_JOutboxStoreOp> Ops, int32& OutDeadLetterCount,
	TMap<FGuid, FProject_JPersistedOutboxRecord>* OutExisting)
{
	check(!IsInGameThread());
	FScopeLock Lock(&Mutex);
	if (OutExisting) OutExisting->Reset();
	if (!EnsureOpenLocked()) return false;
	if (Ops.IsEmpty()) return ReadDeadLetterCountLocked(OutDeadLetterCount);

#if WITH_DEV_AUTOMATION_TESTS
	if (bFailNextBatchWrite)
	{
		bFailNextBatchWrite = false;
		Database->FailNextWriteForTest();
	}
#endif

	if (!Database->Execute(TEXT("BEGIN IMMEDIATE;")))
	{
		LastFailure = FString::Printf(TEXT("BEGIN IMMEDIATE failed (rc=%d)"), Database->GetLastResultCode());
		return false;
	}

	const int64 NowMs = CurrentUtcMillis();
	bool bAllOk = true;

	for (const FProject_JOutboxStoreOp& Op : Ops)
	{
		switch (Op.Kind)
		{
		case EProject_JOutboxStoreOpKind::UpsertPending:
		{
			auto Stmt = Database->PrepareStatement(TEXT(
				"INSERT INTO outbox_entries "
				"(idempotency_key, created_seq, service, operation, payload, debug_context, state, total_attempts, last_status, updated_utc_ms) "
				"VALUES (?1, ?2, ?3, ?4, ?5, ?6, 0, ?7, ?8, ?9) ON CONFLICT(idempotency_key) DO NOTHING RETURNING created_seq;"));
			if (NextCreatedSeq == MAX_int64) { bAllOk = false; break; }
			const int64 Seq = NextCreatedSeq++;
			const void* PayloadPtr = Op.Record.SerializedPayload.IsEmpty() ? static_cast<const void*>("") : Op.Record.SerializedPayload.GetData();
			bAllOk = Stmt.IsValid()
				&& Stmt.SetBindingValueByIndex(1, Op.Record.IdempotencyKey)
				&& Stmt.SetBindingValueByIndex(2, Seq)
				&& Stmt.SetBindingValueByIndex(3, Op.Record.Service.ToString())
				&& Stmt.SetBindingValueByIndex(4, Op.Record.Operation.ToString())
				&& Stmt.SetBindingValueByIndex(5, PayloadPtr, Op.Record.SerializedPayload.Num())
				&& Stmt.SetBindingValueByIndex(6, Op.Record.DebugContext)
				&& Stmt.SetBindingValueByIndex(7, Op.Record.TotalAttempts)
				&& Stmt.SetBindingValueByIndex(8, Op.Record.LastStatus)
				&& Stmt.SetBindingValueByIndex(9, NowMs);
			if (!bAllOk) break;
			const auto Step = Stmt.Step();
			if (Step == ESQLitePreparedStatementStepResult::Row)
			{
				bAllOk = Stmt.Execute();
			}
			else if (Step == ESQLitePreparedStatementStepResult::Done && OutExisting)
			{
				auto Existing = Database->PrepareStatement(*FString::Printf(TEXT(
					"SELECT %s FROM outbox_entries WHERE idempotency_key = ?1;"), RecordColumns));
				FProject_JPersistedOutboxRecord Record;
				bAllOk = Existing.IsValid() && Existing.SetBindingValueByIndex(1, Op.Record.IdempotencyKey)
					&& Existing.Step() == ESQLitePreparedStatementStepResult::Row && ReadRecord(Existing, Record);
				if (bAllOk) OutExisting->Add(Record.IdempotencyKey, MoveTemp(Record));
			}
			else
			{
				bAllOk = Step == ESQLitePreparedStatementStepResult::Done;
			}
			break;
		}
		case EProject_JOutboxStoreOpKind::DeleteConfirmed:
		{
			auto Stmt = Database->PrepareStatement(TEXT("DELETE FROM outbox_entries WHERE idempotency_key = ?1;"));
			bAllOk = Stmt.IsValid()
				&& Stmt.SetBindingValueByIndex(1, Op.Record.IdempotencyKey)
				&& Stmt.Execute();
			break;
		}
		case EProject_JOutboxStoreOpKind::MarkDeadLetter:
		{
			auto Stmt = Database->PrepareStatement(TEXT(
				"UPDATE outbox_entries SET state = 2, total_attempts = ?2, last_status = ?3, updated_utc_ms = ?4 "
				"WHERE idempotency_key = ?1;"));
			bAllOk = Stmt.IsValid()
				&& Stmt.SetBindingValueByIndex(1, Op.Record.IdempotencyKey)
				&& Stmt.SetBindingValueByIndex(2, Op.Record.TotalAttempts)
				&& Stmt.SetBindingValueByIndex(3, Op.Record.LastStatus)
				&& Stmt.SetBindingValueByIndex(4, NowMs)
				&& Stmt.Execute();
			break;
		}
		}

		if (!bAllOk)
		{
			break;
		}
	}

	bAllOk = bAllOk && ReadDeadLetterCountLocked(OutDeadLetterCount);
#if WITH_DEV_AUTOMATION_TESTS
	if (bAllOk && bCrashOnNextBatchCommit)
	{
		bCrashOnNextBatchCommit = false;
		Database->CrashAfterNextSyncForTest();
	}
#endif
	if (!bAllOk || !Database->Execute(TEXT("COMMIT;")))
	{
		LastFailure = FString::Printf(TEXT("Outbox batch commit failed (rc=%d)"), Database->GetLastResultCode());
		Database->Execute(TEXT("ROLLBACK;"));
		if (OutExisting) OutExisting->Reset();
		return false;
	}

	LastFailure.Reset();
	return true;
}

bool FProject_JOnlineOutboxStore::RedriveDeadLetters(
	int32 MaxToRedrive,
	TArray<FProject_JPersistedOutboxRecord>& OutRedriven,
	int32& OutRemainingDeadLetters)
{
	check(!IsInGameThread());
	OutRedriven.Reset();
	OutRemainingDeadLetters = 0;
	FScopeLock Lock(&Mutex);
	if (!EnsureOpenLocked())
	{
		return false;
	}

	if (!Database->Execute(TEXT("BEGIN IMMEDIATE;")))
	{
		LastFailure = FString::Printf(TEXT("BEGIN IMMEDIATE failed for redrive (rc=%d)"), Database->GetLastResultCode());
		return false;
	}

	{
		auto SelectStmt = Database->PrepareStatement(*FString::Printf(TEXT(
			"SELECT %s FROM outbox_entries WHERE state = 2 ORDER BY created_seq ASC LIMIT ?1;"), RecordColumns));
		bool bValid = SelectStmt.IsValid() && SelectStmt.SetBindingValueByIndex(1, MaxToRedrive);
		while (bValid)
		{
			const auto Step = SelectStmt.Step();
			if (Step == ESQLitePreparedStatementStepResult::Done) break;
			FProject_JPersistedOutboxRecord Record;
			bValid = Step == ESQLitePreparedStatementStepResult::Row && ReadRecord(SelectStmt, Record);
			if (bValid) OutRedriven.Add(MoveTemp(Record));
		}
		if (!bValid)
		{
			LastFailure = TEXT("Invalid or unreadable dead-letter row");
			Database->Execute(TEXT("ROLLBACK;"));
			OutRedriven.Reset();
			return false;
		}
	}

	const int64 NowMs = CurrentUtcMillis();
	for (const FProject_JPersistedOutboxRecord& Rec : OutRedriven)
	{
		auto UpdateStmt = Database->PrepareStatement(TEXT(
			"UPDATE outbox_entries SET state = 0, total_attempts = 0, last_status = 'REDRIVEN', updated_utc_ms = ?2 WHERE idempotency_key = ?1;"));
		if (!UpdateStmt.IsValid()
			|| !UpdateStmt.SetBindingValueByIndex(1, Rec.IdempotencyKey)
			|| !UpdateStmt.SetBindingValueByIndex(2, NowMs)
			|| !UpdateStmt.Execute())
		{
			Database->Execute(TEXT("ROLLBACK;"));
			OutRedriven.Reset();
			return false;
		}
	}

	if (!ReadDeadLetterCountLocked(OutRemainingDeadLetters) || !Database->Execute(TEXT("COMMIT;")))
	{
		LastFailure = TEXT("Failed committing outbox redrive");
		Database->Execute(TEXT("ROLLBACK;"));
		OutRedriven.Reset();
		return false;
	}

	LastFailure.Reset();
	return true;
}
