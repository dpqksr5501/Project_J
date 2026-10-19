#include "Backend/Project_JSQLiteRepository.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

using namespace ProjectJ::MMO;
DEFINE_LOG_CATEGORY_STATIC(LogProjectJLocalRepository, Log, All);
namespace
{
constexpr uint32 IntentMagic = 0x4A505243;
constexpr int32 IntentVersion = 1, MaxIntentBytes = 1024;
constexpr int32 ApplicationId = 0x504A5250; // PJRP
FString Kind(const FAggregateKey &Key)
{
	return Key.Kind.ToString().ToLower();
}
bool BindKey(FProject_JProfileStatement &S, const FAggregateKey &Key)
{
	return S.SetBindingValueByIndex(1, Kind(Key)) && S.SetBindingValueByIndex(2, Key.Id);
}
bool Scalar(FProject_JProfileDatabase &DB, const TCHAR *SQL, int64 &Out)
{
	auto S = DB.PrepareStatement(SQL);
	return S.IsValid() && S.Step() == ESQLitePreparedStatementStepResult::Row && S.GetColumnValueByIndex(0, Out);
}
TArray<uint8> CommandBytes(const FCommitRequest &Request, const TArray<uint8> &Intent)
{
	TArray<uint8> Bytes;
	FMemoryWriter W(Bytes);
	if (!Intent.IsEmpty())
	{
		uint32 Magic = IntentMagic;
		int32 Version = IntentVersion;
		auto Copy = Intent;
		W << Magic << Version << Copy;
	}
	FString Operation = Request.Operation.ToString().ToLower();
	W << Operation;
	for (const auto &M : Request.Mutations)
	{
		FString K = Kind(M.Key);
		FGuid Id = M.Key.Id;
		int64 Revision = M.ExpectedRevision;
		int32 Schema = M.SchemaVersion;
		bool Deleted = M.bDelete;
		auto Payload = M.Payload;
		W << K << Id << Revision << Schema << Deleted << Payload;
	}
	return Bytes;
}
} // namespace

FProject_JSQLiteRepository::FProject_JSQLiteRepository(FString InFilename, int32 InMaxRecords, int32 InMaxReceipts)
	: Filename(MoveTemp(InFilename)), MaxRecords(FMath::Max(1, InMaxRecords)), MaxReceipts(FMath::Max(1, InMaxReceipts))
{
	if (!Filename.IsEmpty())
	{
		Filename = FPaths::ConvertRelativePathToFull(Filename);
		FPaths::NormalizeFilename(Filename);
		FPaths::CollapseRelativeDirectories(Filename);
	}
	LockName = TEXT("ProjectJ.LocalRepository.") + FGuid::NewDeterministicGuid(Filename.ToLower()).ToString();
}
void FProject_JSQLiteRepository::Close()
{
	check(!IsInGameThread());
	FScopeLock Lock(&Mutex);
	Database.Close();
}
bool FProject_JSQLiteRepository::BackupToNewFile(const FString &Destination, int32 MaximumFiles)
{
	check(!IsInGameThread());
	FScopeLock Lock(&Mutex);
	FSystemWideCriticalSection FileLock(LockName, FTimespan::FromSeconds(1));
	if (!FileLock.IsValid())
		return false;
	ON_SCOPE_EXIT
	{
		Database.Close();
	};
	if (Destination.IsEmpty() || !IFileManager::Get().FileExists(*Filename) ||
		IFileManager::Get().FileExists(*Destination) ||
		!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true) || !OpenLocked())
		return false;
	if (MaximumFiles != MAX_int32)
	{
		TArray<FString> Existing;
		IFileManager::Get().FindFiles(Existing, *(FPaths::GetPath(Destination) / TEXT("*.sqlite")), true, false);
		if (MaximumFiles < 1 || Existing.Num() >= MaximumFiles)
		{
			UE_LOG(LogProjectJLocalRepository, Warning,
				   TEXT("Backup admission full (%d retained files). Preserve/archive existing backups; no file was "
						"deleted."),
				   MaximumFiles);
			return false;
		}
	}
	{
		auto Statement = Database.PrepareStatement(TEXT("VACUUM INTO ?1"));
		if (!Statement.IsValid() || !Statement.SetBindingValueByIndex(1, Destination) || !Statement.Execute())
			return false;
	}
	FProject_JProfileDatabase Backup;
	ON_SCOPE_EXIT
	{
		Backup.Close();
	};
	int32 App = 0, Version = 0;
	return Backup.Open(*Destination, ESQLiteDatabaseOpenMode::ReadOnly) && Backup.GetApplicationId(App) &&
		   Backup.GetUserVersion(Version) && App == ApplicationId && Version == 2 &&
		   Backup.PerformQuickIntegrityCheck();
}

bool FProject_JSQLiteRepository::OpenLocked() const
{
	check(!IsInGameThread());
	if (Database.IsValid())
		return true;
	// Native rollback recovery is allowed. Unsupported WAL and malformed sidecars
	// remain fail-closed; never delete a journal to make an admission succeed.
	const int64 JournalSize = IFileManager::Get().FileSize(*(Filename + TEXT("-journal")));
	bool JournalInvalid = JournalSize > 0 && JournalSize < 512;
	if (JournalSize >= 512)
	{
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*(Filename + TEXT("-journal"))));
		uint8 Header[8] = {};
		const uint8 Magic[8] = {0xd9, 0xd5, 0x05, 0xf9, 0x20, 0xa1, 0x63, 0xd7};
		if (Reader)
			Reader->Serialize(Header, 8);
		const uint8 Zero[8] = {};
		JournalInvalid =
			!Reader || Reader->IsError() || (FMemory::Memcmp(Header, Magic, 8) && FMemory::Memcmp(Header, Zero, 8));
	}
	if (JournalInvalid || IFileManager::Get().FileSize(*(Filename + TEXT("-wal"))) > 0)
	{
		LastFailure = TEXT("복구 필요: 잘리거나 손상된 journal 또는 미지원 WAL. DB와 sidecar 원형을 보존하세요");
		UE_LOG(LogProjectJLocalRepository, Warning,
			   TEXT("Local database has a journal/WAL sidecar. The journal is malformed/truncated or WAL is "
					"unsupported; preserve the DB and sidecars for inspected recovery."));
		return false;
	}
	LastFailure = TEXT("DB 파일 접근/OS 잠금 실패: 경로·쓰기 권한과 다른 writer를 확인하세요");
	if (Filename.IsEmpty() || !IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true) ||
		!Database.Open(*Filename, ESQLiteDatabaseOpenMode::ReadWriteCreate))
		return false;
	bool bReady = false;
	ON_SCOPE_EXIT
	{
		if (!bReady)
			Database.Close();
	};
	int32 App = 0, Version = 0;
	LastFailure = TEXT("DB 헤더/형식 읽기 실패: 손상·접근 상태를 확인하고 원형을 보존하세요");
	if (!Database.Execute(TEXT("PRAGMA busy_timeout=1000")) || !Database.GetApplicationId(App) ||
		!Database.GetUserVersion(Version))
		return false;
	if (App == 0 && Version == 0)
	{
		LastFailure = TEXT("알 수 없는 DB 개체 또는 초기화 실패: 기존 파일을 초기화하지 않았습니다");
		int64 Tables = 0;
		if (!Scalar(Database, TEXT("SELECT count(*) FROM sqlite_master WHERE name NOT LIKE 'sqlite_%'"), Tables) ||
			Tables != 0)
			return false;
		if (!Database.Execute(TEXT("BEGIN IMMEDIATE")))
			return false;
		const bool Created =
			Database.Execute(TEXT("CREATE TABLE records(kind TEXT NOT NULL,id BLOB NOT NULL,revision INTEGER NOT NULL "
								  "CHECK(revision>0),schema_version INTEGER NOT NULL,payload BLOB NOT NULL,deleted "
								  "INTEGER NOT NULL,PRIMARY KEY(kind,id))")) &&
			Database.Execute(TEXT(
				"CREATE TABLE receipts(id BLOB PRIMARY KEY NOT NULL,command BLOB NOT NULL,revisions BLOB NOT NULL)")) &&
			Database.SetApplicationId(ApplicationId) && Database.SetUserVersion(2) && Database.Execute(TEXT("COMMIT"));
		if (!Created)
		{
			Database.Execute(TEXT("ROLLBACK"));
			return false;
		}
	}
	else if (App != ApplicationId || Version != 2)
	{
		LastFailure =
			FString::Printf(TEXT("지원하지 않는 DB 형식(application=%d, version=%d): 명시적 이관 필요"), App, Version);
		return false;
	}
	// Bundled SQLite 3.47.x: conservative rollback journal, not a new WAL deployment.
	// The Win32 VFS flushes files, but does not claim durable directory metadata.
	// Do not promise power-loss durability from a synchronous pragma alone.
	LastFailure = TEXT("DB journal 설정/무결성 검사 실패: 원형 보존 후 복구 필요");
	FString Journal;
	{
		auto S = Database.PrepareStatement(TEXT("PRAGMA journal_mode=DELETE"));
		if (!S.IsValid() || S.Step() != ESQLitePreparedStatementStepResult::Row ||
			!S.GetColumnValueByIndex(0, Journal) || Journal != TEXT("delete"))
			return false;
	}
	int64 Sync = 0;
	if (!Database.Execute(TEXT("PRAGMA synchronous=EXTRA")) || !Scalar(Database, TEXT("PRAGMA synchronous"), Sync) ||
		Sync != 3 || !Database.PerformQuickIntegrityCheck())
		return false;
	if (!Database.Execute(TEXT("CREATE TABLE IF NOT EXISTS profile_sessions(character BLOB PRIMARY KEY NOT NULL,epoch "
							   "BLOB NOT NULL,sequence INTEGER NOT NULL CHECK(sequence>=0))")) ||
		!Database.Execute(TEXT(
			"CREATE TABLE IF NOT EXISTS profile_receipts(character BLOB NOT NULL,id BLOB NOT NULL,sequence INTEGER NOT "
			"NULL,command BLOB NOT NULL,revisions BLOB NOT NULL,PRIMARY KEY(character,sequence),UNIQUE(id))")))
		return false;
	LastFailure.Reset();
	bReady = true;
	return true;
}

EProject_JRecordRead FProject_JSQLiteRepository::ReadLocked(const FAggregateKey &Key, FRecord &Out) const
{
	Out = {};
	auto S = Database.PrepareStatement(
		TEXT("SELECT revision,schema_version,payload,deleted,length(payload) FROM records WHERE kind=?1 AND id=?2"));
	if (!S.IsValid() || !BindKey(S, Key))
		return EProject_JRecordRead::Unavailable;
	const auto Step = S.Step();
	if (Step == ESQLitePreparedStatementStepResult::Done)
		return EProject_JRecordRead::Missing;
	int32 Deleted = 0, Length = 0;
	if (Step != ESQLitePreparedStatementStepResult::Row || !S.GetColumnValueByIndex(0, Out.Revision) ||
		!S.GetColumnValueByIndex(1, Out.SchemaVersion) || !S.GetColumnValueByIndex(3, Deleted) ||
		!S.GetColumnValueByIndex(4, Length) || Out.Revision <= 0 || Out.SchemaVersion < 1 || Length < 0 ||
		Length > 65536 || (Deleted != 0 && Deleted != 1) || (Deleted && Length) ||
		!S.GetColumnValueByIndex(2, Out.Payload))
	{
		Out = {};
		return EProject_JRecordRead::Unavailable;
	}
	Out.bDeleted = Deleted != 0;
	return EProject_JRecordRead::Found;
}
EProject_JRecordRead FProject_JSQLiteRepository::ReadChecked(const FAggregateKey &Key, FRecord &Out) const
{
	check(!IsInGameThread());
	FScopeLock Lock(&Mutex);
	Out = {};
	FSystemWideCriticalSection FileLock(LockName, FTimespan::FromSeconds(1));
	if (!FileLock.IsValid())
		return EProject_JRecordRead::Unavailable;
	ON_SCOPE_EXIT
	{
		Database.Close();
	};
	return Key.IsValid() && OpenLocked() ? ReadLocked(Key, Out) : EProject_JRecordRead::Unavailable;
}
bool FProject_JSQLiteRepository::Read(const FAggregateKey &Key, FRecord &Out) const
{
	return ReadChecked(Key, Out) == EProject_JRecordRead::Found;
}
FString FProject_JSQLiteRepository::DescribeFailure() const
{
	FScopeLock Lock(&Mutex);
	return LastFailure.IsEmpty() ? TEXT("DB transaction/읽기 실패 또는 잠금 대기 초과: 원형 보존 후 재입장 필요")
								 : LastFailure;
}
bool FProject_JSQLiteRepository::GetUsage(FProject_JRepositoryUsage &Out) const
{
	check(!IsInGameThread());
	FScopeLock Lock(&Mutex);
	Out = {};
	Out.RecordLimit = MaxRecords;
	Out.ReceiptLimit = MaxReceipts + (MaxRecords / 2) * 66;
	FSystemWideCriticalSection FileLock(LockName, FTimespan::FromSeconds(1));
	if (!FileLock.IsValid())
		return false;
	ON_SCOPE_EXIT
	{
		Database.Close();
	};
	return OpenLocked() && Scalar(Database, TEXT("SELECT count(*) FROM records"), Out.Records) &&
		   Scalar(Database, TEXT("SELECT (SELECT count(*) FROM receipts)+(SELECT count(*) FROM profile_receipts)"),
				  Out.Receipts);
}
FCommitResult FProject_JSQLiteRepository::Commit(const FCommitRequest &Request)
{
	return CommitInternal(Request, {});
}
FCommitResult FProject_JSQLiteRepository::CommitWithIntent(const FCommitRequest &Request, const TArray<uint8> &Intent)
{
	if (Intent.IsEmpty() || Intent.Num() > MaxIntentBytes)
		return {};
	return CommitInternal(Request, Intent);
}
FCommitResult FProject_JSQLiteRepository::CommitProfile(const FCommitRequest &Request,
														const FProject_JProfileStream &Stream,
														const TArray<uint8> &Intent)
{
	if (!Stream.Character.IsValid() || !Stream.Session.IsValid() || Stream.Sequence < -1 ||
		Stream.Sequence > MAX_uint32 || Intent.Num() > MaxIntentBytes)
		return {};
	// This port cannot be used as a receipt eviction API for arbitrary aggregates.
	bool Lease = false;
	for (const auto &M : Request.Mutations)
	{
		if (M.Key.Id != Stream.Character || M.bDelete || M.SchemaVersion != 1)
			return {};
		if (M.Key.Kind == TEXT("LocalProfileLease"))
		{
			TArray<uint8> Bytes;
			FMemoryWriter W(Bytes);
			auto Session = Stream.Session;
			W << Session;
			if (M.Payload != Bytes)
				return {};
			Lease = true;
		}
		else if (M.Key.Kind != TEXT("LocalProfile") || Stream.Sequence < 0)
			return {};
	}
	if (!Lease || (Stream.Sequence > 0 &&
				   (Intent.IsEmpty() || Request.IdempotencyKey.A != Stream.Session.A ||
					Request.IdempotencyKey.B != Stream.Session.B || Request.IdempotencyKey.C != Stream.Session.C ||
					Request.IdempotencyKey.D != uint32(Stream.Sequence))))
		return {};
	return CommitInternal(Request, Intent, &Stream);
}
FCommitResult FProject_JSQLiteRepository::CommitInternal(const FCommitRequest &Request, const TArray<uint8> &Intent,
														 const FProject_JProfileStream *Stream)
{
	check(!IsInGameThread());
	if (!IsValidCommitRequest(Request))
		return {};
	const auto Command = CommandBytes(Request, Intent);
	FScopeLock Lock(&Mutex);
	FSystemWideCriticalSection FileLock(LockName, FTimespan::FromSeconds(1));
	if (!FileLock.IsValid())
		return {ECommitStatus::Unavailable, {}};
	ON_SCOPE_EXIT
	{
		Database.Close();
	};
	if (!OpenLocked() || !Database.Execute(TEXT("BEGIN IMMEDIATE")))
		return {ECommitStatus::Unavailable, {}};
#if WITH_DEV_AUTOMATION_TESTS
	if (TestHook)
		TestHook(TEXT("AfterBegin"), Database);
#endif
	bool bCommitted = false;
	ON_SCOPE_EXIT
	{
		if (!bCommitted)
		{
			if ((Database.GetLastResultCode() & 0xff) == SQLITE_FULL)
			{
				LastFailure =
					TEXT("저장 공간 부족(SQLITE_FULL): 원형과 유효한 요청을 보존했습니다. 공간 확보 후 재입장하세요");
				UE_LOG(LogProjectJLocalRepository, Warning,
					   TEXT("Profile storage full (SQLITE_FULL); transaction rolled back, original state and receipts "
							"preserved."));
			}
			Database.Execute(TEXT("ROLLBACK"));
		}
	};
	if (Stream)
	{
		auto S = Database.PrepareStatement(TEXT("SELECT epoch,sequence FROM profile_sessions WHERE character=?1"));
		TArray<uint8> Epoch;
		int64 Sequence = 0;
		if (!S.IsValid() || !S.SetBindingValueByIndex(1, Stream->Character))
			return {ECommitStatus::Unavailable, {}};
		auto Step = S.Step();
		if (Step == ESQLitePreparedStatementStepResult::Error)
			return {ECommitStatus::Unavailable, {}};
		if (Stream->Sequence >= 0)
		{
			if (Step != ESQLitePreparedStatementStepResult::Row || !S.GetColumnValueByIndex(0, Epoch) ||
				!S.GetColumnValueByIndex(1, Sequence))
				return {ECommitStatus::Expired, {}};
			if (Epoch.Num() != sizeof(FGuid) || FMemory::Memcmp(Epoch.GetData(), &Stream->Session, sizeof(FGuid)))
				return {ECommitStatus::Expired, {}};
			// A missing result at/below the high-water mark is retired, never a new command.
			if (Stream->Sequence > 0 && Stream->Sequence <= Sequence)
			{
				auto R = Database.PrepareStatement(TEXT("SELECT 1 FROM profile_receipts WHERE character=?1 AND id=?2"));
				if (!R.IsValid() || !R.SetBindingValueByIndex(1, Stream->Character) ||
					!R.SetBindingValueByIndex(2, Request.IdempotencyKey))
					return {ECommitStatus::Unavailable, {}};
				auto Found = R.Step();
				if (Found == ESQLitePreparedStatementStepResult::Error)
					return {ECommitStatus::Unavailable, {}};
				if (Found != ESQLitePreparedStatementStepResult::Row)
					return {ECommitStatus::Expired, {}};
			}
		}
	}
	{
		auto S = Database.PrepareStatement(
			Stream ? TEXT("SELECT command,revisions,length(command),length(revisions) FROM profile_receipts WHERE "
						  "id=?1 AND character=?2")
				   : TEXT("SELECT command,revisions,length(command),length(revisions) FROM receipts WHERE id=?1"));
		if (!S.IsValid() || !S.SetBindingValueByIndex(1, Request.IdempotencyKey) ||
			(Stream && !S.SetBindingValueByIndex(2, Stream->Character)))
			return {ECommitStatus::Unavailable, {}};
		const auto Step = S.Step();
		if (Step == ESQLitePreparedStatementStepResult::Row)
		{
			TArray<uint8> Original, Versions;
			int32 Length = 0, VersionsLength = 0;
			if (!S.GetColumnValueByIndex(2, Length) || !S.GetColumnValueByIndex(3, VersionsLength) || Length < 0 ||
				Length > 400000 || VersionsLength <= 0 || VersionsLength > 64 * int32(sizeof(int64)) ||
				VersionsLength % sizeof(int64) || !S.GetColumnValueByIndex(0, Original) ||
				!S.GetColumnValueByIndex(1, Versions))
				return {ECommitStatus::Unavailable, {}};
			if (Original != Command)
				return {ECommitStatus::KeyReused, {}};
			if (Versions.Num() != Request.Mutations.Num() * sizeof(int64))
				return {ECommitStatus::Unavailable, {}};
			FCommitResult Result{ECommitStatus::Replayed, {}};
			FMemoryReader R(Versions);
			for (int32 Index = 0; Index < Request.Mutations.Num(); ++Index)
			{
				int64 Revision = 0;
				R << Revision;
				if (Revision != Request.Mutations[Index].ExpectedRevision + 1)
					return {ECommitStatus::Unavailable, {}};
				Result.Revisions.Add(Revision);
			}
			return Result;
		}
		if (Step != ESQLitePreparedStatementStepResult::Done)
			return {ECommitStatus::Unavailable, {}};
	}
	int64 Records = 0, Receipts = 0;
	int32 NewRecords = 0;
	if (!Scalar(Database, TEXT("SELECT count(*) FROM records"), Records) ||
		!Scalar(Database, TEXT("SELECT count(*) FROM receipts"), Receipts))
		return {ECommitStatus::Unavailable, {}};
	if (!Stream && Receipts >= MaxReceipts)
		return {ECommitStatus::Capacity, {}};
	for (const auto &M : Request.Mutations)
	{
		FRecord Existing;
		const auto ReadStatus = ReadLocked(M.Key, Existing);
		if (ReadStatus == EProject_JRecordRead::Unavailable)
			return {ECommitStatus::Unavailable, {}};
		if ((ReadStatus == EProject_JRecordRead::Found ? Existing.Revision : 0) != M.ExpectedRevision)
			return {ECommitStatus::Conflict, {}};
		NewRecords += ReadStatus == EProject_JRecordRead::Missing;
	}
	if (Records + NewRecords > MaxRecords)
		return {ECommitStatus::Capacity, {}};
	FCommitResult Result{ECommitStatus::Committed, {}};
	TArray<uint8> Versions;
	FMemoryWriter W(Versions);
	for (const auto &M : Request.Mutations)
	{
		auto S = Database.PrepareStatement(TEXT("INSERT INTO records(kind,id,revision,schema_version,payload,deleted) "
												"VALUES(?1,?2,?3,?4,?5,?6) ON CONFLICT(kind,id) DO UPDATE SET "
												"revision=excluded.revision,schema_version=excluded.schema_version,"
												"payload=excluded.payload,deleted=excluded.deleted"));
		int64 Revision = M.ExpectedRevision + 1;
		const uint8 EmptyBlob = 0;
		if (!S.IsValid() || !BindKey(S, M.Key) || !S.SetBindingValueByIndex(3, Revision) ||
			!S.SetBindingValueByIndex(4, M.SchemaVersion) ||
			!S.SetBindingValueByIndex(5, M.Payload.IsEmpty() ? &EmptyBlob : M.Payload.GetData(), M.Payload.Num()) ||
			!S.SetBindingValueByIndex(6, int32(M.bDelete)) || !S.Execute())
			return {ECommitStatus::Unavailable, {}};
		Result.Revisions.Add(Revision);
		W << Revision;
	}
	if (Stream)
	{
		if (Stream->Sequence < 0)
		{
			auto D = Database.PrepareStatement(TEXT("DELETE FROM profile_receipts WHERE character=?1"));
			if (!D.IsValid() || !D.SetBindingValueByIndex(1, Stream->Character) || !D.Execute())
				return {ECommitStatus::Unavailable, {}};
		}
		auto U = Database.PrepareStatement(
			Stream->Sequence < 0
				? TEXT("INSERT INTO profile_sessions(character,epoch,sequence) VALUES(?1,?2,0) ON CONFLICT(character) "
					   "DO UPDATE SET epoch=excluded.epoch,sequence=0")
				: TEXT("UPDATE profile_sessions SET sequence=max(sequence,?3) WHERE character=?1 AND epoch=?2"));
		if (!U.IsValid() || !U.SetBindingValueByIndex(1, Stream->Character) ||
			!U.SetBindingValueByIndex(2, Stream->Session) ||
			(Stream->Sequence >= 0 && !U.SetBindingValueByIndex(3, Stream->Sequence)) || !U.Execute())
			return {ECommitStatus::Unavailable, {}};
		if (Stream->Sequence > 0)
		{
			auto D = Database.PrepareStatement(
				TEXT("DELETE FROM profile_receipts WHERE character=?1 AND sequence>0 AND sequence<=?2"));
			if (!D.IsValid() || !D.SetBindingValueByIndex(1, Stream->Character) ||
				!D.SetBindingValueByIndex(2, FMath::Max(int64(0), Stream->Sequence - 64)) || !D.Execute())
				return {ECommitStatus::Unavailable, {}};
		}
	}
	{
		auto S = Database.PrepareStatement(
			Stream ? TEXT("INSERT INTO profile_receipts(id,command,revisions,character,sequence) "
						  "VALUES(?1,?2,?3,?4,?5) ON CONFLICT(character,sequence) DO UPDATE SET "
						  "id=excluded.id,command=excluded.command,revisions=excluded.revisions")
				   : TEXT("INSERT INTO receipts(id,command,revisions) VALUES(?1,?2,?3)"));
		if (!S.IsValid() || !S.SetBindingValueByIndex(1, Request.IdempotencyKey) ||
			!S.SetBindingValueByIndex(2, Command.GetData(), Command.Num()) ||
			!S.SetBindingValueByIndex(3, Versions.GetData(), Versions.Num()) ||
			(Stream &&
			 (!S.SetBindingValueByIndex(4, Stream->Character) || !S.SetBindingValueByIndex(5, Stream->Sequence))) ||
			!S.Execute())
			return {ECommitStatus::Unavailable, {}};
	}
#if WITH_DEV_AUTOMATION_TESTS
	if (TestHook)
		TestHook(TEXT("BeforeCommit"), Database);
#endif
	if (!Database.Execute(TEXT("COMMIT")))
		return {ECommitStatus::UnknownOutcome, {}};
	bCommitted = true;
#if WITH_DEV_AUTOMATION_TESTS
	if (TestHook)
		TestHook(TEXT("AfterCommit"), Database);
	if (bTestLoseCommitResponse)
	{
		bTestLoseCommitResponse = false;
		return {ECommitStatus::UnknownOutcome, {}};
	}
#endif
	return Result;
}
FCommitResult FProject_JSQLiteRepository::FindReceipt(FGuid Key, FName Operation, const TArray<uint8> &Intent) const
{
	return FindReceiptInternal(Key, Operation, Intent, nullptr);
}
FCommitResult FProject_JSQLiteRepository::FindProfileReceipt(FGuid Key, FName Operation,
															 const FProject_JProfileStream &Stream,
															 const TArray<uint8> &Intent) const
{
	return FindReceiptInternal(Key, Operation, Intent, &Stream);
}
FCommitResult FProject_JSQLiteRepository::FindReceiptInternal(FGuid Key, FName Operation, const TArray<uint8> &Intent,
															  const FProject_JProfileStream *Stream) const
{
	check(!IsInGameThread());
	FScopeLock Lock(&Mutex);
	FSystemWideCriticalSection FileLock(LockName, FTimespan::FromSeconds(1));
	if (!FileLock.IsValid() || !Key.IsValid() || Operation.IsNone())
		return {ECommitStatus::Unavailable, {}};
	ON_SCOPE_EXIT
	{
		Database.Close();
	};
	if (!OpenLocked())
		return {ECommitStatus::Unavailable, {}};
	int64 HighWater = 0;
	if (Stream)
	{
		if (Stream->Sequence < 1 || Stream->Sequence > MAX_uint32 || Key.A != Stream->Session.A ||
			Key.B != Stream->Session.B || Key.C != Stream->Session.C || Key.D != uint32(Stream->Sequence))
			return {ECommitStatus::Expired, {}};
		auto E = Database.PrepareStatement(TEXT("SELECT epoch,sequence FROM profile_sessions WHERE character=?1"));
		TArray<uint8> Epoch;
		if (!E.IsValid() || !E.SetBindingValueByIndex(1, Stream->Character))
			return {ECommitStatus::Unavailable, {}};
		auto Step = E.Step();
		if (Step == ESQLitePreparedStatementStepResult::Error)
			return {ECommitStatus::Unavailable, {}};
		if (Step != ESQLitePreparedStatementStepResult::Row)
			return {ECommitStatus::Expired, {}};
		if (!E.GetColumnValueByIndex(0, Epoch) || !E.GetColumnValueByIndex(1, HighWater))
			return {ECommitStatus::Unavailable, {}};
		if (Epoch.Num() != sizeof(FGuid) || FMemory::Memcmp(Epoch.GetData(), &Stream->Session, sizeof(FGuid)))
			return {ECommitStatus::Expired, {}};
	}
	auto S = Database.PrepareStatement(
		Stream ? TEXT("SELECT command,revisions,length(command),length(revisions) FROM profile_receipts WHERE id=?1 "
					  "AND character=?2")
			   : TEXT("SELECT command,revisions,length(command),length(revisions) FROM receipts WHERE id=?1"));
	if (!S.IsValid() || !S.SetBindingValueByIndex(1, Key) ||
		(Stream && !S.SetBindingValueByIndex(2, Stream->Character)))
		return {ECommitStatus::Unavailable, {}};
	const auto Step = S.Step();
	if (Step == ESQLitePreparedStatementStepResult::Done)
		return Stream && Stream->Sequence <= HighWater ? FCommitResult{ECommitStatus::Expired, {}} : FCommitResult{};
	int32 Length = 0, VersionLength = 0;
	TArray<uint8> Command, Versions;
	if (Step != ESQLitePreparedStatementStepResult::Row || !S.GetColumnValueByIndex(2, Length) ||
		!S.GetColumnValueByIndex(3, VersionLength) || Length < 5 || Length > 400000 || VersionLength < 8 ||
		VersionLength > 512 || VersionLength % 8 || !S.GetColumnValueByIndex(0, Command) ||
		!S.GetColumnValueByIndex(1, Versions) || Command.Num() != Length)
		return {ECommitStatus::Unavailable, {}};
	// Versioned opaque prefix preserves legacy generic receipt bytes and bounds disk allocation.
	FMemoryReader Reader(Command);
	uint32 Magic = 0;
	Reader << Magic;
	int32 Offset = 0;
	if (Magic == IntentMagic)
	{
		if (Command.Num() < 17)
			return {ECommitStatus::Unavailable, {}};
		int32 Version = 0, IntentLength = 0;
		Reader << Version << IntentLength;
		if (Version != IntentVersion || IntentLength < 1 || IntentLength > MaxIntentBytes ||
			int64(12) + IntentLength + 5 > Command.Num())
			return {ECommitStatus::Unavailable, {}};
		if (Intent.Num() != IntentLength || FMemory::Memcmp(Command.GetData() + 12, Intent.GetData(), IntentLength))
			return {ECommitStatus::KeyReused, {}};
		Offset = 12 + IntentLength;
	}
	else if (!Intent.IsEmpty())
		return {ECommitStatus::KeyReused, {}};
	int32 Count = 0;
	FMemory::Memcpy(&Count, Command.GetData() + Offset, 4);
	if (!Count || Count < -1024 || Count > 1024 ||
		int64(Offset) + 4 + (Count < 0 ? -int64(Count) * 2 : Count) > Command.Num())
		return {ECommitStatus::Unavailable, {}};
	Reader.Seek(Offset);
	FString Original;
	Reader << Original;
	if (Reader.IsError())
		return {ECommitStatus::Unavailable, {}};
	if (Original != Operation.ToString().ToLower())
		return {ECommitStatus::KeyReused, {}};
	FCommitResult Result{ECommitStatus::Replayed, {}};
	FMemoryReader Revisions(Versions);
	while (!Revisions.AtEnd())
	{
		int64 Revision = 0;
		Revisions << Revision;
		if (Revision < 1)
			return {ECommitStatus::Unavailable, {}};
		Result.Revisions.Add(Revision);
	}
	return Result;
}
