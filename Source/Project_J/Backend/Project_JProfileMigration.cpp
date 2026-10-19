#include "Backend/Project_JProfileMigration.h"
#include "Async/Async.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "Backend/Project_JProfileDatabase.h"
#include "Backend/Project_JSQLiteRepository.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/Project_JPlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
bool KnownSnapshotFields(const TArray<uint8> &Bytes)
{
	FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR *>(Bytes.GetData()), Bytes.Num());
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get())), Root) ||
		!Root.IsValid())
		return false;
	double Format = 0;
	const TSharedPtr<FJsonObject> *P = nullptr, *Q = nullptr;
	if (!Root->TryGetNumberField(TEXT("format"), Format) || Root->Values.Num() != (Format == 1 ? 6 : 7) ||
		!Root->TryGetObjectField(TEXT("progression"), P) || !Root->TryGetObjectField(TEXT("quests"), Q) || !P || !Q ||
		(*P)->Values.Num() != 5 || (*Q)->Values.Num() != 2)
		return false;
	const TArray<TSharedPtr<FJsonValue>> *Values = nullptr;
	if (!(*Q)->TryGetArrayField(TEXT("states"), Values))
		return false;
	for (const auto &Value : *Values)
		if (!Value.IsValid() || Value->Type != EJson::Object || Value->AsObject()->Values.Num() != 4)
			return false;
	if (Format == 2)
	{
		if (!Root->TryGetArrayField(TEXT("items"), Values))
			return false;
		for (const auto &Value : *Values)
			if (!Value.IsValid() || Value->Type != EJson::Object || Value->AsObject()->Values.Num() != 8)
				return false;
	}
	return true;
}
#if WITH_DEV_AUTOMATION_TESTS
thread_local int32 MigrationFailAfterWrites = INDEX_NONE;
#endif
bool HashFile(const FString &Path, FString &Out)
{
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path));
	if (!Reader)
		return false;
	FSHA1 Hash;
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(65536);
	while (Reader->Tell() < Reader->TotalSize())
	{
		int64 N = FMath::Min(int64(Buffer.Num()), Reader->TotalSize() - Reader->Tell());
		Reader->Serialize(Buffer.GetData(), N);
		if (Reader->IsError())
			return false;
		Hash.Update(Buffer.GetData(), uint32(N));
	}
	Hash.Final();
	uint8 Bytes[20];
	Hash.GetHash(Bytes);
	Out = BytesToHex(Bytes, 20);
	return true;
}
bool SameRecord(const FProject_JMigrationRecord &A, const FProject_JMigrationRecord &B)
{
	return A.Kind == B.Kind && A.Id == B.Id && A.Revision == B.Revision && A.Schema == B.Schema &&
		   A.Deleted == B.Deleted && A.Payload == B.Payload;
}
bool SameRecords(const TArray<FProject_JMigrationRecord> &A, const TArray<FProject_JMigrationRecord> &B)
{
	if (A.Num() != B.Num())
		return false;
	for (int32 I = 0; I < A.Num(); ++I)
		if (!SameRecord(A[I], B[I]))
			return false;
	return true;
}
bool HasTable(FProject_JProfileDatabase &DB, const FString &Name)
{
	auto S = DB.PrepareStatement(TEXT("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1"));
	return S.IsValid() && S.SetBindingValueByIndex(1, Name) && S.Step() == ESQLitePreparedStatementStepResult::Row;
}
bool HashOtherState(FProject_JProfileDatabase &DB, FString &Out)
{
	FSHA1 Hash;
	for (const TCHAR *Table : {TEXT("receipts"), TEXT("profile_sessions"), TEXT("profile_receipts")})
	{
		if (!HasTable(DB, Table))
			continue;
		const TCHAR *SQL =
			FCString::Strcmp(Table, TEXT("receipts")) == 0
				? TEXT("SELECT hex(id)||':'||hex(command)||':'||hex(revisions) FROM receipts ORDER BY id")
			: FCString::Strcmp(Table, TEXT("profile_sessions")) == 0
				? TEXT("SELECT hex(character)||':'||hex(epoch)||':'||sequence FROM profile_sessions ORDER BY character")
				: TEXT("SELECT hex(character)||':'||sequence||':'||hex(id)||':'||hex(command)||':'||hex(revisions) "
					   "FROM profile_receipts ORDER BY character,sequence");
		auto S = DB.PrepareStatement(SQL);
		if (!S.IsValid())
			return false;
		int32 Count = 0;
		for (;;)
		{
			auto Step = S.Step();
			if (Step == ESQLitePreparedStatementStepResult::Done)
				break;
			FString Row;
			if (Step != ESQLitePreparedStatementStepResult::Row || !S.GetColumnValueByIndex(0, Row) ||
				Row.Len() > 810000 || ++Count > 40000)
				return false;
			FTCHARToUTF8 Bytes(*Row);
			Hash.Update(reinterpret_cast<const uint8 *>(Bytes.Get()), Bytes.Length());
			uint8 End = 0;
			Hash.Update(&End, 1);
		}
	}
	Hash.Final();
	uint8 Bytes[20];
	Hash.GetHash(Bytes);
	Out = BytesToHex(Bytes, 20);
	return true;
}
} // namespace

FProject_JMigrationInspection FProject_JProfileMigration::Inspect(const FString &Source)
{
	check(!IsInGameThread());
	FProject_JMigrationInspection Result;
	Result.Error = TEXT("Source missing, active sidecar, corrupt database or unsupported schema; original preserved");
	// Never attempt recovery against an original. Preserve non-empty sidecars for a separate inspected recovery.
	for (const TCHAR *Suffix : {TEXT("-journal"), TEXT("-wal"), TEXT("-shm")})
		if (IFileManager::Get().FileSize(*(Source + Suffix)) > 0)
			return Result;
	if (!HashFile(Source, Result.SourceHash))
		return Result;
	FProject_JProfileDatabase DB;
	int32 App = 0, Version = 0;
	if (!DB.Open(*Source, ESQLiteDatabaseOpenMode::ReadOnly) || !DB.GetApplicationId(App) || App != 0x504A5250 ||
		!DB.GetUserVersion(Version) || (Version != 1 && Version != 2) || !DB.PerformQuickIntegrityCheck() ||
		!HasTable(DB, TEXT("records")) || !HasTable(DB, TEXT("receipts")))
		return Result;
	Result.DatabaseVersion = Version;
	auto S = DB.PrepareStatement(
		TEXT("SELECT kind,id,revision,schema_version,payload,deleted,length(payload) FROM records ORDER BY kind,id"));
	if (!S.IsValid())
		return Result;
	for (;;)
	{
		auto Step = S.Step();
		if (Step == ESQLitePreparedStatementStepResult::Done)
			break;
		FProject_JMigrationRecord R;
		int32 Length = 0;
		if (Step != ESQLitePreparedStatementStepResult::Row || Result.Records.Num() >= 1024 ||
			!S.GetColumnValueByIndex(0, R.Kind) || !S.GetColumnValueByIndex(1, R.Id) || R.Id.Num() != 16 ||
			!S.GetColumnValueByIndex(2, R.Revision) || R.Revision <= 0 || !S.GetColumnValueByIndex(3, R.Schema) ||
			R.Schema < 1 || !S.GetColumnValueByIndex(5, R.Deleted) || R.Deleted < 0 || R.Deleted > 1 ||
			!S.GetColumnValueByIndex(6, Length) || Length < 0 || Length > 65536 ||
			!S.GetColumnValueByIndex(4, R.Payload) || (R.Deleted && Length))
			return Result;
		if (R.Kind == TEXT("localprofile") && !R.Deleted)
		{
			FProject_JLocalProfileSnapshot Snapshot;
			FGuid Id;
			FMemory::Memcpy(&Id, R.Id.GetData(), 16);
			if (R.Schema != 1 || !FProject_JLocalProfileSnapshot::Decode(R.Payload, Snapshot) ||
				!KnownSnapshotFields(R.Payload) || Snapshot.Character != Id)
				return Result;
			if (Snapshot.Format == 1)
				Result.MissingOwnership.Add(Id);
		}
		Result.Records.Add(MoveTemp(R));
	}
	if (!HashOtherState(DB, Result.OtherStateHash))
		return Result;
	FString After;
	if (!HashFile(Source, After) || After != Result.SourceHash)
		return Result;
	Result.Error.Reset();
	return Result;
}
bool FProject_JProfileMigration::Plan(const FProject_JMigrationInspection &Source,
									  const TArray<FProject_JLocalProfileSnapshot> &Ownership,
									  TArray<FProject_JMigrationRecord> &Result, FString &Error)
{
	Result.Reset();
	Error = TEXT("Missing/duplicate ownership or changed P/Q/rules/identity; explicit complete v2 snapshot required");
	if (!Source.IsValid() || Ownership.Num() != Source.MissingOwnership.Num())
		return false;
	TSet<FGuid> Seen, Instances;
	for (const auto &Snapshot : Ownership)
	{
		if (Snapshot.Format != 2 || !Source.MissingOwnership.Contains(Snapshot.Character) ||
			Seen.Contains(Snapshot.Character))
			return false;
		else
			Seen.Add(Snapshot.Character);
		for (const auto &Item : Snapshot.Items)
		{
			if (Instances.Contains(Item.Instance))
				return false;
			Instances.Add(Item.Instance);
		}
	}
	for (const auto &R : Source.Records)
	{
		if (R.Kind != TEXT("localprofile") || R.Deleted)
			continue;
		FProject_JLocalProfileSnapshot Existing;
		if (!FProject_JLocalProfileSnapshot::Decode(R.Payload, Existing))
			return false;
		if (Existing.Format != 2)
			continue;
		for (const auto &Item : Existing.Items)
		{
			if (Instances.Contains(Item.Instance))
				return false;
			Instances.Add(Item.Instance);
		}
	}
	auto Planned = Source.Records;
	for (auto &R : Planned)
	{
		if (R.Kind != TEXT("localprofile") || R.Deleted)
			continue;
		FProject_JLocalProfileSnapshot Old;
		if (!FProject_JLocalProfileSnapshot::Decode(R.Payload, Old))
			return false;
		if (Old.Format == 2)
			continue;
		auto *Replacement = Ownership.FindByPredicate([&](const auto &V) { return V.Character == Old.Character; });
		if (!Replacement || R.Revision == MAX_int64)
			return false;
		TArray<uint8> OldCanonical, NewCanonical, Encoded;
		auto WithoutItems = *Replacement;
		WithoutItems.Format = 1;
		WithoutItems.Items.Reset();
		if (!Old.Encode(OldCanonical) || !WithoutItems.Encode(NewCanonical) || OldCanonical != NewCanonical ||
			!Replacement->Encode(Encoded))
			return false;
		R.Payload = MoveTemp(Encoded);
		++R.Revision;
	}
	Result = MoveTemp(Planned);
	Error.Reset();
	return true;
}
bool FProject_JProfileMigration::Create(const FString &Source, const FString &Destination,
										const FProject_JMigrationInspection &Inspection,
										const TArray<FProject_JMigrationRecord> &Planned, FString &Error)
{
	check(!IsInGameThread());
	Error = TEXT("Migration rejected; source preserved. Inspect destination conflict or I/O failure");
#if WITH_DEV_AUTOMATION_TESTS
	const int32 FailAfter = MigrationFailAfterWrites;
	MigrationFailAfterWrites = INDEX_NONE;
	int32 Writes = 0;
#endif
	if (FPaths::IsSamePath(Source, Destination) || !Inspection.IsValid() || Planned.Num() != Inspection.Records.Num())
		return false;
	const auto Current = Inspect(Source);
	if (!Current.IsValid() || Current.SourceHash != Inspection.SourceHash ||
		Current.OtherStateHash != Inspection.OtherStateHash || !SameRecords(Current.Records, Inspection.Records))
		return false;
	// Independently constrain the write port: only v1 -> v2 items may differ, never arbitrary records.
	TArray<FProject_JLocalProfileSnapshot> Ownership;
	for (int32 I = 0; I < Planned.Num(); ++I)
	{
		if (SameRecord(Planned[I], Inspection.Records[I]))
			continue;
		FProject_JLocalProfileSnapshot V;
		if (!FProject_JLocalProfileSnapshot::Decode(Planned[I].Payload, V))
			return false;
		Ownership.Add(MoveTemp(V));
	}
	TArray<FProject_JMigrationRecord> Checked;
	FString PlanError;
	if (!Plan(Inspection, Ownership, Checked, PlanError) || !SameRecords(Checked, Planned))
		return false;
	if (IFileManager::Get().FileExists(*Destination))
	{
		auto Existing = Inspect(Destination);
		FProject_JProfileDatabase DB;
		FString Provenance;
		if (Existing.IsValid() && Existing.DatabaseVersion == 2 &&
			Existing.OtherStateHash == Inspection.OtherStateHash && SameRecords(Existing.Records, Planned) &&
			DB.Open(*Destination, ESQLiteDatabaseOpenMode::ReadOnly))
		{
			auto S = DB.PrepareStatement(TEXT("SELECT source_hash FROM profile_migration WHERE version=1"));
			if (S.IsValid() && S.Step() == ESQLitePreparedStatementStepResult::Row &&
				S.GetColumnValueByIndex(0, Provenance) && Provenance == Inspection.SourceHash)
			{
				Error = TEXT("Already migrated; identical result preserved");
				return true;
			}
		}
		return false;
	}
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true))
		return false;
	const FString Stage = Destination + TEXT(".migration-") + FGuid::NewGuid().ToString();
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*Stage, false, true);
		IFileManager::Get().Delete(*(Stage + TEXT("-journal")), false, true);
	};
	{
		FProject_JProfileDatabase DB;
		if (!DB.Open(*Source, ESQLiteDatabaseOpenMode::ReadOnly))
			return false;
		auto S = DB.PrepareStatement(TEXT("VACUUM INTO ?1"));
		if (!S.IsValid() || !S.SetBindingValueByIndex(1, Stage) || !S.Execute())
			return false;
	}
	auto Copy = Inspect(Stage);
	if (!Copy.IsValid() || !SameRecords(Copy.Records, Inspection.Records) ||
		Copy.OtherStateHash != Inspection.OtherStateHash)
		return false;
	{
		FProject_JProfileDatabase DB;
		if (!DB.Open(*Stage, ESQLiteDatabaseOpenMode::ReadWrite) ||
			!DB.Execute(TEXT("PRAGMA synchronous=EXTRA; BEGIN IMMEDIATE")))
			return false;
		bool Commit = false;
		ON_SCOPE_EXIT
		{
			if (!Commit)
				DB.Execute(TEXT("ROLLBACK"));
		};
		for (int32 I = 0; I < Planned.Num(); ++I)
		{
			const auto &R = Planned[I];
			if (SameRecord(R, Inspection.Records[I]))
				continue;
			auto S = DB.PrepareStatement(
				TEXT("UPDATE records SET payload=?1,revision=?2 WHERE kind=?3 AND id=?4 AND revision=?5"));
			if (!S.IsValid() || !S.SetBindingValueByIndex(1, R.Payload.GetData(), R.Payload.Num()) ||
				!S.SetBindingValueByIndex(2, R.Revision) || !S.SetBindingValueByIndex(3, R.Kind) ||
				!S.SetBindingValueByIndex(4, R.Id.GetData(), R.Id.Num()) ||
				!S.SetBindingValueByIndex(5, Inspection.Records[I].Revision) || !S.Execute())
				return false;
#if WITH_DEV_AUTOMATION_TESTS
			if (++Writes == FailAfter)
				return false;
#endif
		}
		if (!DB.Execute(TEXT(
				"CREATE TABLE IF NOT EXISTS profile_migration(version INTEGER PRIMARY KEY,source_hash TEXT NOT NULL)")))
			return false;
		auto S = DB.PrepareStatement(TEXT("INSERT INTO profile_migration VALUES(1,?1) ON CONFLICT(version) DO UPDATE "
										  "SET source_hash=excluded.source_hash"));
		if (!S.IsValid() || !S.SetBindingValueByIndex(1, Inspection.SourceHash) || !S.Execute() ||
			!DB.SetUserVersion(2) || !DB.Execute(TEXT("COMMIT")))
			return false;
		Commit = true;
	}
	{
		FProject_JSQLiteRepository Staging(Stage);
		FProject_JRepositoryUsage Usage;
		const bool Ready = Staging.GetUsage(Usage);
		Staging.Close();
		if (!Ready)
			return false;
	}
	auto Verified = Inspect(Stage);
	auto Original = Inspect(Source);
	if (!Verified.IsValid() || Verified.DatabaseVersion != 2 || !SameRecords(Verified.Records, Planned) ||
		Verified.OtherStateHash != Inspection.OtherStateHash || !Original.IsValid() ||
		Original.SourceHash != Inspection.SourceHash)
		return false;
	// No overwrite. A failed/partial staging write never becomes the destination.
	if (!IFileManager::Get().Move(*Destination, *Stage, false, false, false, true))
		return false;
	Error.Reset();
	return true;
}
#if WITH_DEV_AUTOMATION_TESTS
void FProject_JProfileMigration::FailAfterWritesForTest(int32 Count)
{
	check(!IsInGameThread());
	MigrationFailAfterWrites = Count;
}
#endif
bool FProject_JProfileMigration::RollbackCopy(const FString &Original, const FString &Migrated,
											  const FString &Destination, FString &Error)
{
	check(!IsInGameThread());
	Error = TEXT("Rollback refused: original provenance, destination conflict or I/O failure");
	if (FPaths::IsSamePath(Original, Destination) || FPaths::IsSamePath(Migrated, Destination) ||
		IFileManager::Get().FileExists(*Destination))
		return false;
	auto Source = Inspect(Original);
	if (!Source.IsValid())
		return false;
	{
		FProject_JProfileDatabase DB;
		if (!DB.Open(*Migrated, ESQLiteDatabaseOpenMode::ReadOnly))
			return false;
		auto S = DB.PrepareStatement(TEXT("SELECT source_hash FROM profile_migration WHERE version=1"));
		FString Hash;
		if (!S.IsValid() || S.Step() != ESQLitePreparedStatementStepResult::Row || !S.GetColumnValueByIndex(0, Hash) ||
			Hash != Source.SourceHash)
			return false;
	}
	const FString Stage = Destination + TEXT(".rollback-") + FGuid::NewGuid().ToString();
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*Stage, false, true);
	};
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true))
		return false;
	{
		FProject_JProfileDatabase DB;
		if (!DB.Open(*Original, ESQLiteDatabaseOpenMode::ReadOnly))
			return false;
		auto S = DB.PrepareStatement(TEXT("VACUUM INTO ?1"));
		if (!S.IsValid() || !S.SetBindingValueByIndex(1, Stage) || !S.Execute())
			return false;
	}
	auto Result = Inspect(Stage), After = Inspect(Original);
	if (!Result.IsValid() || !After.IsValid() || After.SourceHash != Source.SourceHash ||
		!SameRecords(Result.Records, Source.Records) || Result.OtherStateHash != Source.OtherStateHash ||
		!IFileManager::Get().Move(*Destination, *Stage, false, false, false, true))
		return false;
	Error.Reset();
	return true;
}

UProject_JProfileMigrationCommandlet::UProject_JProfileMigrationCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}
int32 UProject_JProfileMigrationCommandlet::Main(const FString &Params)
{
	FString Source, Destination, OwnershipFile;
	if (!FParse::Value(*Params, TEXT("Source="), Source))
		return 2;
	auto Inspection =
		Async(EAsyncExecution::ThreadPool, [Source] { return FProject_JProfileMigration::Inspect(Source); }).Get();
	UE_LOG(LogTemp, Display,
		   TEXT("Migration inspection: storageVersion=%d records=%d requiredOwnership=%d source=%s error=%s"),
		   Inspection.DatabaseVersion, Inspection.Records.Num(), Inspection.MissingOwnership.Num(),
		   *Inspection.SourceHash, *Inspection.Error);
	for (auto Id : Inspection.MissingOwnership)
		UE_LOG(LogTemp, Display,
			   TEXT("Required explicit v2 ownership snapshot: %s (items, versions, instance GUIDs, bag order and "
					"equipment slots)"),
			   *Id.ToString());
	if (!Inspection.IsValid())
		return 3;
	if (FParse::Param(*Params, TEXT("RollbackCopy")))
	{
		FString Migrated, Error;
		if (!FParse::Value(*Params, TEXT("Migrated="), Migrated) ||
			!FParse::Value(*Params, TEXT("Destination="), Destination))
			return 2;
		bool OK = Async(EAsyncExecution::ThreadPool, [&] {
					  return FProject_JProfileMigration::RollbackCopy(Source, Migrated, Destination, Error);
				  }).Get();
		UE_LOG(LogTemp, Display, TEXT("Rollback separate copy=%d %s; no live path changed"), OK, *Error);
		return OK ? 0 : 7;
	}
	if (FParse::Param(*Params, TEXT("Inspect")))
		return 0;
	if (Inspection.MissingOwnership.IsEmpty() && Inspection.DatabaseVersion == 2)
	{
		UE_LOG(LogTemp, Display, TEXT("No v1 migration target in this database"));
		return 0;
	}
	TArray<FProject_JLocalProfileSnapshot> Ownership;
	if (!Inspection.MissingOwnership.IsEmpty())
	{
		if (!FParse::Value(*Params, TEXT("Ownership="), OwnershipFile))
			return 4;
		FString Text;
		if (IFileManager::Get().FileSize(*OwnershipFile) > 16 * 1024 * 1024 ||
			!FFileHelper::LoadFileToString(Text, *OwnershipFile))
			return 4;
		TArray<TSharedPtr<FJsonValue>> Values;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Values) || Values.Num() > 512)
			return 4;
		for (auto &Value : Values)
		{
			FString Json;
			auto Writer = TJsonWriterFactory<>::Create(&Json);
			if (!Value.IsValid() || Value->Type != EJson::Object ||
				!FJsonSerializer::Serialize(Value->AsObject().ToSharedRef(), Writer))
				return 4;
			FTCHARToUTF8 Bytes(*Json);
			TArray<uint8> Raw;
			Raw.Append(reinterpret_cast<const uint8 *>(Bytes.Get()), Bytes.Length());
			FProject_JLocalProfileSnapshot Snapshot;
			if (!FProject_JLocalProfileSnapshot::Decode(Raw, Snapshot) || !KnownSnapshotFields(Raw))
				return 4;
			Ownership.Add(MoveTemp(Snapshot));
		}
	}
	TArray<FProject_JMigrationRecord> Planned;
	FString Error;
	if (!FProject_JProfileMigration::Plan(Inspection, Ownership, Planned, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("%s"), *Error);
		return 5;
	}
	const TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
	GI->InitializeStandalone();
	auto *World = GI->GetWorld();
	ON_SCOPE_EXIT
	{
		if (World->HasBegunPlay())
			World->EndPlay(EEndPlayReason::Quit);
		GI->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	if (!GI->GetSubsystem<UProject_JCharacterDataSubsystem>()->LoadPrimaryAssetDefinitions())
		return 6;
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	for (const auto &R : Planned)
	{
		if (R.Kind != TEXT("localprofile") || R.Deleted)
			continue;
		FProject_JLocalProfileSnapshot Snapshot;
		if (!FProject_JLocalProfileSnapshot::Decode(R.Payload, Snapshot))
			return 6;
		auto *Player = World->SpawnActor<AProject_JPlayerState>();
		const bool Valid = UProject_JLocalProfileSubsystem::ValidateMigrationSnapshot(Player, Snapshot, Error);
		Player->Destroy();
		if (!Valid)
		{
			UE_LOG(LogTemp, Error, TEXT("Definition/rule validation rejected: %s"), *Error);
			return 6;
		}
	}
	if (FParse::Param(*Params, TEXT("DryRun")))
	{
		UE_LOG(LogTemp, Display, TEXT("Dry-run verified; source unchanged, no destination created"));
		return 0;
	}
	if (!FParse::Value(*Params, TEXT("Destination="), Destination))
		return 2;
	bool Created = Async(EAsyncExecution::ThreadPool, [&] {
					   return FProject_JProfileMigration::Create(Source, Destination, Inspection, Planned, Error);
				   }).Get();
	UE_LOG(LogTemp, Display,
		   TEXT("Migration separate file result=%d %s; rollback retains original source and sidecars. No runtime "
				"switch performed."),
		   Created, *Error);
	return Created ? 0 : 7;
}
