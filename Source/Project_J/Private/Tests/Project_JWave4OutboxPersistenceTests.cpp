#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Async/Async.h"
#include "Algo/AllOf.h"
#include "Backend/Project_JProfileDatabase.h"
#include "Backend/Project_JGatewaySubsystem.h"
#include "Backend/Project_JNakamaServicePort.h"
#include "Backend/Project_JOnlineWorldSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Gameplay/Project_JLootSubsystem.h"
#include "World/Project_JWorldEncounter.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "UObject/UnrealType.h"
#include "Serialization/JsonSerializer.h"
#include <atomic>

namespace
{
constexpr auto Wave4TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

struct FDispatchedRecord
{
	FName Service;
	FName Operation;
	FGuid Key;
	TArray<uint8> Payload;
	TFunction<void(TSharedPtr<FJsonObject>)> Reply;
};

struct FTestOutboxHost
{
	UWorld* World = nullptr;
	UGameInstance* GI = nullptr;
	UProject_JOnlineWorldSubsystem* Online = nullptr;

	FTestOutboxHost()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		GI = NewObject<UGameInstance>(GEngine);
		GI->AddToRoot();
		GI->Init();
		World->SetGameInstance(GI);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Online = GI->GetSubsystem<UProject_JOnlineWorldSubsystem>();
	}

	~FTestOutboxHost()
	{
		if (World)
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
		if (GI)
		{
			GI->Shutdown();
			GI->RemoveFromRoot();
		}
	}
};

FString MakeTempOutboxDbPath(const TCHAR* Tag)
{
	return FPaths::ProjectSavedDir() / TEXT("AutomationTemp") / FString::Printf(TEXT("Wave4Outbox_%s_%s.sqlite"), Tag, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
}

void CleanupOutboxDbFiles(const FString& DbPath)
{
	IFileManager::Get().Delete(*DbPath, false, true, true);
	IFileManager::Get().Delete(*(DbPath + TEXT("-journal")), false, true, true);
}

FProject_JOutboxStoreOp SeedPending(FGuid Key, const TCHAR* Text = TEXT("{\"source\":\"seed\"}"))
{
	FProject_JOutboxStoreOp Op;
	Op.Record.IdempotencyKey = Key;
	Op.Record.Service = TEXT("Reward");
	Op.Record.Operation = TEXT("Boss");
	FTCHARToUTF8 Bytes(Text);
	Op.Record.SerializedPayload.Append(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
	return Op;
}

TSharedRef<FJsonObject> MakeReply(const FString& Status)
{
	auto Reply = MakeShared<FJsonObject>();
	Reply->SetStringField(TEXT("status"), Status);
	return Reply;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJBossRewardSettlementTest, "ProjectJ.Online.BossRewardSettlement", Wave4TestFlags)
bool FProjectJBossRewardSettlementTest::RunTest(const FString&)
{
	const FString DbPath = MakeTempOutboxDbPath(TEXT("BossSettlement"));
	ON_SCOPE_EXIT
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJKeepBossLayoutFixture")))
		{
			UE_LOG(LogTemp, Display, TEXT("BOSS_LAYOUT_FIXTURE %s"), *FPaths::ConvertRelativePathToFull(DbPath));
		}
		else
		{
			CleanupOutboxDbFiles(DbPath);
		}
	};
	FString Scope, MovedScope, CopyScope;
	const FGuid Defeat = FGuid::NewGuid();
	constexpr int32 RecipientCount = 200;
	TArray<FProject_JRolledLootGrant> Grants;
	TMap<FGuid, TSharedPtr<FJsonObject>> QuestCredits;
	FGuid QuestRecipient;
	TSet<FGuid> Recipients;
	TSet<FGuid> DispatchedKeys;
	TArray<FDispatchedRecord> Dispatched;
	const auto RecordDispatch = [&](FName Service, FName Operation, const FGuid& Key, const TArray<uint8>& Payload, auto Reply)
	{
		Dispatched.Add({Service, Operation, Key, Payload, MoveTemp(Reply)});
	};
	{
		FTestOutboxHost Boot;
		// Model a layout patch before closing the old database. Query those placements on the next boot.
		auto* Old = Boot.World->SpawnActor<AProject_JWorldEncounter>();
		Old->EncounterId = TEXT("Test.Boss.A");
		Scope = Old->GetBossSettlementKey();
		Old->Destroy();
		auto* Moved = Boot.World->SpawnActor<AProject_JWorldEncounter>(FVector(8000, 0, 0), FRotator::ZeroRotator);
		Moved->EncounterId = TEXT("Test.Boss.A");
		MovedScope = Moved->GetBossSettlementKey();
		auto* Copy = Boot.World->SpawnActor<AProject_JWorldEncounter>();
		Copy->EncounterId = TEXT("Test.Boss.B");
		CopyScope = Copy->GetBossSettlementKey();
		TestEqual(TEXT("Replacing and moving a placement within its map preserves its settlement identity"), MovedScope, Scope);
		TestNotEqual(TEXT("A copy at the original location with a new ID has its own settlement scope"), CopyScope, Scope);
		Boot.Online->SetOutboxDispatchHookForTest(RecordDispatch);
		TestTrue(TEXT("Persistent store opens"), Boot.Online->ConfigurePersistentOutbox(DbPath));
		Boot.Online->FlushPersistentOutboxForTest();
		auto* Table = NewObject<UProject_JLootTableDefinition>();
		auto& Entry = Table->Entries.AddDefaulted_GetRef();
		Entry.ItemDefinitionId = TEXT("BossSword");
		Entry.ItemDefinitionVersion = 2;
		Entry.bGuaranteed = true;
		auto* Loot = Boot.GI->GetSubsystem<UProject_JLootSubsystem>();
		for (int32 Index = 0; Index < RecipientCount; ++Index)
		{
			const FGuid Character = FGuid::NewGuid();
			Recipients.Add(Character);
			Grants.Add(Loot->RollPersonalLoot(Table, Character, Index));
		}
		TestEqual(TEXT("Version is taken from the server loot table"), Grants[0].Items[0].ItemDefinitionVersion, 2);
		QuestRecipient = Grants[0].RecipientCharacterId;
		auto Quest = MakeShared<FJsonObject>();
		Quest->SetStringField(TEXT("character_id"), UProject_JOnlineWorldSubsystem::GuidText(QuestRecipient));
		Quest->SetStringField(TEXT("session_epoch"), TEXT("7"));
		Quest->SetStringField(TEXT("earned_at"), TEXT("123456"));
		QuestCredits.Add(QuestRecipient, Quest);
		int32 Commits = 0;
		int32 Failures = 0;
		const auto OnCommitted = [&](bool bCommitted, double Remaining)
		{
			TestEqual(TEXT("No network dispatch precedes the local commit callback"), Dispatched.Num(), 0);
			if (bCommitted)
			{
				++Commits;
				TestTrue(TEXT("Cooldown starts after the durable write"), Remaining > 0.0 && Remaining <= 300.0);
			}
			else ++Failures;
		};
		const TArray<FProject_JRolledLootGrant> Duplicate{Grants[0], Grants[0]};
		TestFalse(TEXT("Duplicate recipient rejects whole cohort"), UProject_JOnlineWorldSubsystem::EnqueueBossRewards(
			Boot.World, Scope, Defeat, 300.0, Duplicate, OnCommitted));
		Boot.Online->FailNextOutboxStoreWriteForTest();
		AddExpectedError(TEXT("Outbox batch commit failed"), EAutomationExpectedErrorFlags::Contains, 1);
		TestTrue(TEXT("Admit the 200-recipient cohort as one unit"), UProject_JOnlineWorldSubsystem::EnqueueBossRewards(
			Boot.World, Scope, Defeat, 300.0, Grants, OnCommitted, QuestCredits));
		TestFalse(TEXT("Do not submit the same defeat concurrently"), UProject_JOnlineWorldSubsystem::EnqueueBossRewards(
			Boot.World, Scope, Defeat, 300.0, Grants, OnCommitted, QuestCredits));
		Boot.Online->FlushPersistentOutboxForTest();
		TestEqual(TEXT("Single failure for the whole cohort"), Failures, 1);
		TestEqual(TEXT("Rollback dispatches zero requests"), Dispatched.Num(), 0);
		double Remaining = -1.0;
		TestTrue(TEXT("Cooldown state remains readable"), Boot.Online->TryGetBossRewardCooldown(Scope, Remaining));
		TestEqual(TEXT("Rollback never publishes cooldown"), Remaining, 0.0);
		TestTrue(TEXT("Same manifest can be retried"), UProject_JOnlineWorldSubsystem::EnqueueBossRewards(
			Boot.World, Scope, Defeat, 300.0, Grants, OnCommitted, QuestCredits));
		Boot.Online->FlushPersistentOutboxForTest();
		TestEqual(TEXT("One durable settlement callback"), Commits, 1);
		TestEqual(TEXT("Wire ceiling is shared by the whole cohort"), Dispatched.Num(), UProject_JOnlineWorldSubsystem::MaxOutboxInFlight);
		Dispatched.Reset();
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJKeepBossLayoutFixture")))
	{
		const FString Snapshot = DbPath + TEXT(".prepatch.sqlite");
		TestEqual(TEXT("Export closed settlement with pending deliveries for offline migration verification"),
			IFileManager::Get().Copy(*Snapshot, *DbPath, false), uint32(COPY_OK));
	}
	{
		FTestOutboxHost Boot;
		Boot.Online->SetOutboxDispatchHookForTest(RecordDispatch);
		TestTrue(TEXT("Reopen the committed store"), Boot.Online->ConfigurePersistentOutbox(DbPath));
		Boot.Online->FlushPersistentOutboxForTest();
		const int32 RowCount = RecipientCount + QuestCredits.Num();
		TestEqual(TEXT("All committed loot and quest rows recover together after restart"), Boot.Online->GetOutboxStats().RecoveredTotal, int64(RowCount));
		double Remaining = 0.0;
		TestTrue(TEXT("Moved placement recovers its cooldown after database restart"), Boot.Online->TryGetBossRewardCooldown(MovedScope, Remaining) && Remaining > 0.0);
		TestTrue(TEXT("A replacement with a fresh ID cannot inherit the removed placement's cooldown"),
			Boot.Online->TryGetBossRewardCooldown(CopyScope, Remaining) && Remaining == 0.0);
		int32 RecoveredQuestCredits = 0;
		for (int32 Index = 0; Index < RowCount && !Dispatched.IsEmpty(); ++Index)
		{
			FDispatchedRecord Record = Dispatched.Pop(EAllowShrinking::No);
			const FUTF8ToTCHAR Payload(reinterpret_cast<const ANSICHAR*>(Record.Payload.GetData()), Record.Payload.Num());
			TSharedPtr<FJsonObject> Body;
			if (!TestTrue(TEXT("Read recovered reward JSON"), FJsonSerializer::Deserialize(
				TJsonReaderFactory<>::Create(FString(Payload.Length(), Payload.Get())), Body) && Body.IsValid())) return false;
			FGuid Character;
			FGuid::Parse(Body->GetStringField(TEXT("character_id")), Character);
			TestFalse(TEXT("Stable transport key is unique"), DispatchedKeys.Contains(Record.Key));
			DispatchedKeys.Add(Record.Key);
			if (Record.Operation == TEXT("Quest_Progress"))
			{
				TestEqual(TEXT("Quest credit retains the same eligible character"), Character, QuestRecipient);
				TestEqual(TEXT("Original earned clock survives settlement and restart"), Body->GetStringField(TEXT("earned_at")), FString(TEXT("123456")));
				++RecoveredQuestCredits;
			}
			else
			{
				TestEqual(TEXT("All recipients retain the same defeat id"), Body->GetStringField(TEXT("defeat_id")), UProject_JOnlineWorldSubsystem::GuidText(Defeat));
				TestEqual(TEXT("Each recipient occurs once"), Recipients.Remove(Character), 1);
				const auto& Items = Body->GetArrayField(TEXT("items"));
				if (!TestEqual(TEXT("Rolled item preserved"), Items.Num(), 1)) return false;
				TestEqual(TEXT("Reward wire format retains v2 through restart"), Items[0]->AsObject()->GetNumberField(TEXT("definition_version")), 2.0);
			}
			Record.Reply(MakeReply(TEXT("REPLAYED")));
			if (Dispatched.IsEmpty()) Boot.Online->FlushPersistentOutboxForTest();
		}
		Boot.Online->FlushPersistentOutboxForTest();
		TestEqual(TEXT("Each expected recipient dispatched"), Recipients.Num(), 0);
		TestEqual(TEXT("Exactly one quest credit dispatched"), RecoveredQuestCredits, 1);
		TestEqual(TEXT("All stable receipts confirm"), Boot.Online->GetOutboxStats().ConfirmedTotal, int64(RowCount));
		int32 Replay = 0;
		TestTrue(TEXT("Reconciles a repeat of an already committed defeat"), UProject_JOnlineWorldSubsystem::EnqueueBossRewards(
			Boot.World, Scope, Defeat, 300.0, Grants, [&Replay](bool bCommitted, double) { Replay += bCommitted; }, QuestCredits));
		Boot.Online->FlushPersistentOutboxForTest();
		TestEqual(TEXT("Stored defeat receipt confirms the replay"), Replay, 1);
		TestEqual(TEXT("Repeat does not reissue already delivered mail"), Dispatched.Num(), 0);
		Grants[0].Gold = 1;
		AddExpectedError(TEXT("Defeat identity was reused with a different reward manifest"), EAutomationExpectedErrorFlags::Contains, 1);
		int32 Mismatched = 0;
		TestTrue(TEXT("Sent conflicting content to durable duplicate check"), UProject_JOnlineWorldSubsystem::EnqueueBossRewards(
			Boot.World, Scope, Defeat, 300.0, Grants, [&Mismatched](bool bCommitted, double) { Mismatched += !bCommitted; }, QuestCredits));
		Boot.Online->FlushPersistentOutboxForTest();
		TestEqual(TEXT("Changed manifest is rejected"), Mismatched, 1);
		TestEqual(TEXT("Conflict has no network side effects"), Dispatched.Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOutboxAgeAndAdmissionTest, "ProjectJ.Online.OutboxAgeAndAdmission", Wave4TestFlags)
bool FProjectJOutboxAgeAndAdmissionTest::RunTest(const FString&)
{
	const FString DbPath = MakeTempOutboxDbPath(TEXT("Age"));
	ON_SCOPE_EXIT { CleanupOutboxDbFiles(DbPath); };
	const FGuid Key = FGuid::NewGuid();
	const FGuid FreshKey = FGuid::NewGuid();
	auto Seed = SeedPending(Key);
	Seed.Record.PendingSinceUtcMs = static_cast<int64>((FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalMilliseconds()) - 120000;
	const bool bSeeded = Async(EAsyncExecution::ThreadPool, [DbPath, Seed, FreshKey]
	{
		FProject_JOnlineOutboxStore Store(DbPath);
		int32 DeadLetters = 0;
		const TArray<FProject_JOutboxStoreOp> Ops{Seed, SeedPending(FreshKey)};
		const bool bSaved = Store.ApplyBatch(Ops, DeadLetters);
		if (!Store.Close() || !bSaved) return false;
		// Version 1 already stored a pending row's UTC time in updated_utc_ms.
		FProject_JProfileDatabase Legacy;
		const bool bLegacy = Legacy.Open(*DbPath, ESQLiteDatabaseOpenMode::ReadWrite)
			&& Legacy.Execute(TEXT("DROP TABLE boss_reward_settlements;")) && Legacy.SetUserVersion(1);
		return Legacy.Close() && bLegacy;
	}).Get();
	if (!TestTrue(TEXT("Seed two unresolved mutations in the version 1 schema"), bSeeded)) return false;

	FTestOutboxHost Host;
	TMap<FGuid, TFunction<void(TSharedPtr<FJsonObject>)>> Confirmations;
	Host.Online->SetOutboxDispatchHookForTest([&](FName, FName, const FGuid& DispatchedKey, const TArray<uint8>&, auto Reply)
	{
		Confirmations.Add(DispatchedKey, MoveTemp(Reply));
	});
	if (!TestTrue(TEXT("Open the existing outbox"), Host.Online->ConfigurePersistentOutbox(DbPath))) return false;
	auto Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("character_id"), UProject_JOnlineWorldSubsystem::GuidText(FGuid::NewGuid()));
	TestFalse(TEXT("FIFO progress awaits recovery"), UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(
		Host.World, TEXT("World"), TEXT("Quest_Progress"), Body, FGuid::NewGuid(), TEXT("startup")));
	TestEqual(TEXT("Startup backpressure increments the capacity rejection counter"), Host.Online->GetOutboxStats().RejectedCapacityTotal, int64(1));
	Host.Online->FlushPersistentOutboxForTest();
	if (!TestTrue(TEXT("Migration preserves both pending keys"), Confirmations.Contains(Key) && Confirmations.Contains(FreshKey))) return false;
	if (!TestTrue(TEXT("Active age includes the recovered legacy inflight mutation"), Host.Online->GetOutboxStats().OldestActiveAgeSeconds >= 119.0))
		return false;
	Confirmations.FindChecked(Key)(MakeReply(TEXT("COMMITTED")));
	Host.Online->FlushPersistentOutboxForTest();
	TestTrue(TEXT("The oldest age advances when that key is finalized"), Host.Online->GetOutboxStats().OldestActiveAgeSeconds < 119.0);
	Confirmations.FindChecked(FreshKey)(MakeReply(TEXT("COMMITTED")));
	Host.Online->FlushPersistentOutboxForTest();
	TestEqual(TEXT("Completed durable work leaves no active age"), Host.Online->GetOutboxStats().OldestActiveAgeSeconds, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOutboxCrashRestartRecoveryTest, "ProjectJ.Online.OutboxCrashRestartRecovery", Wave4TestFlags)
bool FProjectJOutboxCrashRestartRecoveryTest::RunTest(const FString&)
{
	const FString DbPath = MakeTempOutboxDbPath(TEXT("Restart"));
	ON_SCOPE_EXIT { CleanupOutboxDbFiles(DbPath); };

	constexpr int32 TotalRewards = 64;
	constexpr int32 ConfirmedBeforeRestart = 20;
	TArray<FGuid> AllKeys;
	TMap<FGuid, TArray<uint8>> ExpectedPayloads;
	AllKeys.Reserve(TotalRewards);

	// Boot 1: accept 64 rewards in one burst, confirm 20, leave 44 unconfirmed across restart.
	{
		FTestOutboxHost Boot1;
		if (!TestNotNull(TEXT("Boot1 OnlineWorldSubsystem"), Boot1.Online)) { return false; }

		TArray<FDispatchedRecord> Dispatched;
		Boot1.Online->SetOutboxDispatchHookForTest(
			[&](FName Service, FName Operation, const FGuid& Key, const TArray<uint8>& Payload, TFunction<void(TSharedPtr<FJsonObject>)> Reply)
			{
				Dispatched.Add({Service, Operation, Key, Payload, MoveTemp(Reply)});
			});
		TestTrue(TEXT("Boot1 configures persistent SQLite outbox"), Boot1.Online->ConfigurePersistentOutbox(DbPath));
		TestFalse(TEXT("Admission waits for the store open acknowledgement"), Boot1.Online->IsPersistentOutboxReady());
		Boot1.Online->FlushPersistentOutboxForTest();
		TestTrue(TEXT("Admission sees the recovered store"), Boot1.Online->IsPersistentOutboxReady());

		for (int32 i = 0; i < TotalRewards; ++i)
		{
			const FGuid Key = FGuid::NewGuid();
			AllKeys.Add(Key);
			auto Body = MakeShared<FJsonObject>();
			Body->SetNumberField(TEXT("reward_index"), i);
			Body->SetStringField(TEXT("tag"), FString::Printf(TEXT("보스-보상-%d"), i));
			FString Encoded;
			FJsonSerializer::Serialize(Body, TJsonWriterFactory<>::Create(&Encoded));
			FTCHARToUTF8 Utf8(*Encoded);
			TArray<uint8> Bytes;
			Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			ExpectedPayloads.Add(Key, MoveTemp(Bytes));

			TestTrue(TEXT("Boot1 enqueues durable reward"), UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(
				Boot1.World, TEXT("Reward"), TEXT("Boss"), Body, Key, FString::Printf(TEXT("boot1:%d"), i)));
		}

		// Commit batched UpsertPending off-GT and dispatch up to MaxOutboxInFlight (64).
		Boot1.Online->FlushPersistentOutboxForTest();
		TestEqual(TEXT("All 64 rewards dispatched after SQLite commit"), Dispatched.Num(), TotalRewards);

		// Confirm first 20; abandon remaining 44 in-flight to simulate abrupt server restart.
		for (int32 i = 0; i < ConfirmedBeforeRestart; ++i)
		{
			auto Ok = MakeShared<FJsonObject>();
			Ok->SetStringField(TEXT("status"), TEXT("COMMITTED"));
			Dispatched[i].Reply(Ok);
		}
		Boot1.Online->FlushPersistentOutboxForTest();

		const FProject_JOnlineOutboxStats Boot1Stats = Boot1.Online->GetOutboxStats();
		TestEqual(TEXT("Boot1 confirmed 20 rewards"), Boot1Stats.ConfirmedTotal, static_cast<int64>(ConfirmedBeforeRestart));
		TestEqual(TEXT("Boot1 has 44 unconfirmed in-flight"), Boot1Stats.InFlightCount, TotalRewards - ConfirmedBeforeRestart);
		TestEqual(TEXT("Boot1 zero store errors"), Boot1Stats.StoreErrorTotal, static_cast<int64>(0));
	}

	// Boot 2: reopen the same SQLite DB; exact 44 unconfirmed rows must recover in FIFO order and replay cleanly.
	{
		FTestOutboxHost Boot2;
		if (!TestNotNull(TEXT("Boot2 OnlineWorldSubsystem"), Boot2.Online)) { return false; }

		TArray<FDispatchedRecord> RecoveredDispatches;
		Boot2.Online->SetOutboxDispatchHookForTest(
			[&](FName Service, FName Operation, const FGuid& Key, const TArray<uint8>& Payload, TFunction<void(TSharedPtr<FJsonObject>)> Reply)
			{
				RecoveredDispatches.Add({Service, Operation, Key, Payload, MoveTemp(Reply)});
			});
		TestTrue(TEXT("Boot2 opens existing SQLite outbox"), Boot2.Online->ConfigurePersistentOutbox(DbPath));
		Boot2.Online->FlushPersistentOutboxForTest();

		const int32 ExpectedRecovered = TotalRewards - ConfirmedBeforeRestart;
		TestEqual(TEXT("Boot2 recovered exact 44 unconfirmed rows"), Boot2.Online->GetOutboxStats().RecoveredTotal, static_cast<int64>(ExpectedRecovered));
		TestEqual(TEXT("Boot2 dispatched exact 44 recovered rows"), RecoveredDispatches.Num(), ExpectedRecovered);

		for (int32 i = 0; i < RecoveredDispatches.Num(); ++i)
		{
			const FGuid& ExpectedKey = AllKeys[ConfirmedBeforeRestart + i];
			TestEqual(TEXT("Recovered key preserves exact FIFO order and idempotency key"), RecoveredDispatches[i].Key, ExpectedKey);
			TestEqual(TEXT("Recovered service preserved"), RecoveredDispatches[i].Service, FName(TEXT("Reward")));
			TestEqual(TEXT("Recovered operation preserved"), RecoveredDispatches[i].Operation, FName(TEXT("Boss")));
			TestTrue(TEXT("Recovered UTF-8 payload matches bit-for-bit"), RecoveredDispatches[i].Payload == ExpectedPayloads[ExpectedKey]);

			auto Replayed = MakeShared<FJsonObject>();
			Replayed->SetStringField(TEXT("status"), TEXT("REPLAYED"));
			RecoveredDispatches[i].Reply(Replayed);
		}
		Boot2.Online->FlushPersistentOutboxForTest();
		TestEqual(TEXT("Boot2 confirmed all 44 recovered rows"), Boot2.Online->GetOutboxStats().ConfirmedTotal, static_cast<int64>(ExpectedRecovered));
		TestEqual(TEXT("Boot2 left zero pending or in-flight"),
			Boot2.Online->GetOutboxStats().PendingCount + Boot2.Online->GetOutboxStats().InFlightCount, 0);
	}

	// Boot 3: verify SQLite is now completely drained (0 pending, 0 dead-letters).
	{
		FTestOutboxHost Boot3;
		if (!TestNotNull(TEXT("Boot3 OnlineWorldSubsystem"), Boot3.Online)) { return false; }

		int32 UnexpectedDispatches = 0;
		Boot3.Online->SetOutboxDispatchHookForTest(
			[&](FName, FName, const FGuid&, const TArray<uint8>&, TFunction<void(TSharedPtr<FJsonObject>)>)
			{
				++UnexpectedDispatches;
			});
		TestTrue(TEXT("Boot3 opens drained SQLite outbox"), Boot3.Online->ConfigurePersistentOutbox(DbPath));
		Boot3.Online->FlushPersistentOutboxForTest();

		TestEqual(TEXT("Boot3 recovers zero rows after full drain"), Boot3.Online->GetOutboxStats().RecoveredTotal, static_cast<int64>(0));
		TestEqual(TEXT("Boot3 dispatches zero rows"), UnexpectedDispatches, 0);
		TestEqual(TEXT("Boot3 zero dead-letters"), Boot3.Online->GetOutboxStats().DeadLetterCount, 0);
	}

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOutboxMixedFanoutRestartTest, "ProjectJ.Online.OutboxMixedFanoutRestart", Wave4TestFlags)
bool FProjectJOutboxMixedFanoutRestartTest::RunTest(const FString&)
{
	const FString DbPath = MakeTempOutboxDbPath(TEXT("MixedFanout"));
	ON_SCOPE_EXIT { CleanupOutboxDbFiles(DbPath); };
	constexpr int32 Characters = 400;
	constexpr int32 Total = Characters * 2 + 2;
	TArray<FGuid> ProgressKeys;
	TSet<FGuid> AppliedAtService;
	TMap<FGuid, TArray<uint8>> ObservedPayloads;
	TArray<FDispatchedRecord> InFlight;
	int32 Peak = 0;
	int32 WireDuplicates = 0;
	const auto Dispatch = [&](FName Service, FName Operation, const FGuid& Key, const TArray<uint8>& Payload,
		TFunction<void(TSharedPtr<FJsonObject>)> Reply)
	{
		if (const auto* OldPayload = ObservedPayloads.Find(Key))
			TestTrue(TEXT("A restart/retry preserves the original bytes"), *OldPayload == Payload);
		else ObservedPayloads.Add(Key, Payload);
		InFlight.Add({Service, Operation, Key, Payload, MoveTemp(Reply)});
		Peak = FMath::Max(Peak, InFlight.Num());
	};
	{
		FTestOutboxHost Boot;
		Boot.Online->SetOutboxDispatchHookForTest(Dispatch);
		TestTrue(TEXT("Open SQLite"), Boot.Online->ConfigurePersistentOutbox(DbPath));
		Boot.Online->FlushPersistentOutboxForTest();
		TArray<FGuid> ActorIds;
		for (int32 Index = 0; Index < Characters; ++Index) ActorIds.Add(FGuid::NewGuid());
		const auto Add = [&](FName Service, FName Operation, FGuid Key, FGuid Actor)
		{
			auto Body = MakeShared<FJsonObject>();
			Body->SetStringField(TEXT("character_id"), UProject_JOnlineWorldSubsystem::GuidText(Actor));
			return UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(Boot.World, Service, Operation, Body, Key, TEXT("mixed fanout"));
		};
		for (int32 Index = 0; Index < Characters; ++Index)
		{
			ProgressKeys.Add(FGuid::NewGuid());
			TestTrue(TEXT("Admit independently keyed boss reward"), Add(TEXT("Reward"), TEXT("Boss"), FGuid::NewGuid(), ActorIds[Index]));
			TestTrue(TEXT("Admit character progress"), Add(TEXT("World"), TEXT("Quest_Progress"), ProgressKeys.Last(), ActorIds[Index]));
		}
		ProgressKeys.Add(FGuid::NewGuid());
		TestTrue(TEXT("Admit subsequent progress on the first character lane"),
			Add(TEXT("World"), TEXT("Quest_Progress"), ProgressKeys.Last(), ActorIds[0]));
		TestTrue(TEXT("Admit dungeon clear"), Add(TEXT("Dungeon"), TEXT("Clear"), FGuid::NewGuid(), ActorIds[0]));
		Boot.Online->FlushPersistentOutboxForTest();
		TestEqual(TEXT("Only one shared pool of wire slots"), InFlight.Num(), UProject_JOnlineWorldSubsystem::MaxOutboxInFlight);
		if (InFlight.Num() < 2) return false;
		// One lost response after business commit, one transient 503 before commit.
		AppliedAtService.Add(InFlight[0].Key);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			auto Reply = MoveTemp(InFlight[0].Reply);
			InFlight.RemoveAt(0, EAllowShrinking::No);
			Reply(Index == 2 ? TSharedPtr<FJsonObject>(MakeReply(TEXT("PRE_DISPATCH_REJECTED"))) : nullptr);
		}
		Boot.Online->FlushPersistentOutboxForTest();
		TestEqual(TEXT("Lost response, transient HTTP and capacity consume the shared retry budget"),
			Boot.Online->GetOutboxStats().RetriedTotal, int64(3));
	}
	InFlight.Reset();
	{
		FTestOutboxHost Boot;
		Boot.Online->SetOutboxDispatchHookForTest(Dispatch);
		TestTrue(TEXT("Recover every unresolved class of mutation"), Boot.Online->ConfigurePersistentOutbox(DbPath));
		Boot.Online->FlushPersistentOutboxForTest();
		TestEqual(TEXT("All accepted work survives restart"), Boot.Online->GetOutboxStats().RecoveredTotal, int64(Total));
		for (int32 Completed = 0; Completed < Total && !InFlight.IsEmpty(); ++Completed)
		{
			auto Request = MoveTemp(InFlight[0]);
			InFlight.RemoveAt(0, EAllowShrinking::No);
			if (Request.Key == ProgressKeys.Last())
				TestTrue(TEXT("The prior event on this character lane was confirmed first"), AppliedAtService.Contains(ProgressKeys[0]));
			const bool bReplay = AppliedAtService.Contains(Request.Key);
			WireDuplicates += bReplay;
			AppliedAtService.Add(Request.Key);
			Request.Reply(MakeReply(bReplay ? TEXT("REPLAYED") : TEXT("COMMITTED")));
			if (InFlight.IsEmpty() || Completed % 32 == 31) Boot.Online->FlushPersistentOutboxForTest();
		}
		Boot.Online->FlushPersistentOutboxForTest();
		const auto Stats = Boot.Online->GetOutboxStats();
		TestEqual(TEXT("Every key has one business effect and local confirmation"), Stats.ConfirmedTotal, int64(Total));
		TestEqual(TEXT("No quarantined or outstanding work"), Stats.DeadLetterCount + Stats.InFlightCount + Stats.PendingCount, 0);
	}
	TestEqual(TEXT("Global transport ceiling holds across retries and restart"), Peak, UProject_JOnlineWorldSubsystem::MaxOutboxInFlight);
	TestEqual(TEXT("Lost reply is replayed once without duplicate business effect"), WireDuplicates, 1);
	TestEqual(TEXT("All unique business effects recorded"), AppliedAtService.Num(), Total);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOutboxDeadLetterRedriveAndFaultCloseTest, "ProjectJ.Online.OutboxDeadLetterRedriveAndFaultClose", Wave4TestFlags)
bool FProjectJOutboxDeadLetterRedriveAndFaultCloseTest::RunTest(const FString&)
{
	const FString DbPath = MakeTempOutboxDbPath(TEXT("DeadLetter"));
	ON_SCOPE_EXIT { CleanupOutboxDbFiles(DbPath); };

	const FGuid InvalidKey = FGuid::NewGuid();
	const FGuid ExhaustedKey = FGuid::NewGuid();
	const FGuid FaultKey = FGuid::NewGuid();

	// Boot 1: quarantine 1 INVALID + 1 retry-exhausted dead-letter, verify duplicate key dedup, and verify fail-closed VFS write error.
	{
		FTestOutboxHost Boot1;
		if (!TestNotNull(TEXT("Boot1 OnlineWorldSubsystem"), Boot1.Online)) { return false; }

		AddExpectedMessage(TEXT("Durable outbox failed permanently"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
		AddExpectedMessage(TEXT("Persistent online outbox batch write failed"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 1);

		TArray<FDispatchedRecord> Dispatched;
		Boot1.Online->SetOutboxDispatchHookForTest(
			[&](FName Service, FName Operation, const FGuid& Key, const TArray<uint8>& Payload, TFunction<void(TSharedPtr<FJsonObject>)> Reply)
			{
				Dispatched.Add({Service, Operation, Key, Payload, MoveTemp(Reply)});
			});
		TestTrue(TEXT("Boot1 configures persistent SQLite outbox"), Boot1.Online->ConfigurePersistentOutbox(DbPath));
		Boot1.Online->FlushPersistentOutboxForTest();

		auto Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("case"), TEXT("deadletter-검증"));
		TestTrue(TEXT("Enqueue INVALID case"), UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(
			Boot1.World, TEXT("Reward"), TEXT("Boss"), Body, InvalidKey, TEXT("invalid")));
		TestTrue(TEXT("Enqueue EXHAUSTED case"), UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(
			Boot1.World, TEXT("Reward"), TEXT("Boss"), Body, ExhaustedKey, TEXT("exhausted")));
		TestFalse(TEXT("Duplicate active idempotency key is deduplicated"), UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(
			Boot1.World, TEXT("Reward"), TEXT("Boss"), Body, ExhaustedKey, TEXT("duplicate")));
		TestEqual(TEXT("DeduplicatedTotal incremented"), Boot1.Online->GetOutboxStats().DeduplicatedTotal, static_cast<int64>(1));

		Boot1.Online->FlushPersistentOutboxForTest();
		TestEqual(TEXT("Both distinct entries dispatched"), Dispatched.Num(), 2);

		// 1) Immediate non-retryable rejection -> DeadLetter.
		{
			auto InvalidReply = MakeShared<FJsonObject>();
			InvalidReply->SetStringField(TEXT("status"), TEXT("INVALID"));
			auto Cb = MoveTemp(Dispatched[0].Reply);
			Cb(InvalidReply);
		}
		// 2) Timeout across all 6 attempts -> DeadLetter.
		{
			auto FirstCb = MoveTemp(Dispatched[1].Reply);
			Dispatched.Reset();
			FirstCb(nullptr);
			for (int32 Retry = 0; Retry < UProject_JOnlineWorldSubsystem::MaxOutboxRetries; ++Retry)
			{
				Boot1.Online->PumpOutboxForTest(FPlatformTime::Seconds() + 300.0);
				if (!TestEqual(TEXT("Retry dispatches exhausted key"), Dispatched.Num(), 1)) { break; }
				auto Cb = MoveTemp(Dispatched[0].Reply);
				Dispatched.Reset();
				Cb(nullptr);
			}
		}
		Boot1.Online->FlushPersistentOutboxForTest();

		TestEqual(TEXT("Both terminal failures quarantined as dead-letters"), Boot1.Online->GetOutboxStats().DeadLetterCount, 2);
		TestEqual(TEXT("DeadLetteredTotal == 2"), Boot1.Online->GetOutboxStats().DeadLetteredTotal, static_cast<int64>(2));

		// 3) Real VFS write fault must fail closed without dispatching over the wire.
		bool bFaultCallbackInvoked = false;
		bool bFaultConfirmed = true;
		Boot1.Online->FailNextOutboxStoreWriteForTest();
		TestTrue(TEXT("Fault case accepted into persist stage"), UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(
			Boot1.World, TEXT("Reward"), TEXT("Boss"), Body, FaultKey, TEXT("vfs_fault"),
			[&](bool bConfirmed, const TSharedPtr<FJsonObject>&)
			{
				bFaultCallbackInvoked = true;
				bFaultConfirmed = bConfirmed;
			}));
		Boot1.Online->FlushPersistentOutboxForTest();

		TestTrue(TEXT("Write fault reports failure callback"), bFaultCallbackInvoked && !bFaultConfirmed);
		TestEqual(TEXT("Write fault never dispatches unpersisted mutation over wire"), Dispatched.Num(), 0);
		TestEqual(TEXT("StoreErrorTotal == 1"), Boot1.Online->GetOutboxStats().StoreErrorTotal, static_cast<int64>(1));
	}

	// Boot 2: verify dead-letters stay quarantined on startup (NOT auto-replayed) until explicit RedriveDeadLetters().
	{
		FTestOutboxHost Boot2;
		if (!TestNotNull(TEXT("Boot2 OnlineWorldSubsystem"), Boot2.Online)) { return false; }

		TArray<FDispatchedRecord> Dispatched;
		Boot2.Online->SetOutboxDispatchHookForTest(
			[&](FName Service, FName Operation, const FGuid& Key, const TArray<uint8>& Payload, TFunction<void(TSharedPtr<FJsonObject>)> Reply)
			{
				Dispatched.Add({Service, Operation, Key, Payload, MoveTemp(Reply)});
			});
		TestTrue(TEXT("Boot2 opens DB with quarantined dead-letters"), Boot2.Online->ConfigurePersistentOutbox(DbPath));
		Boot2.Online->FlushPersistentOutboxForTest();

		TestEqual(TEXT("Dead-letters do not auto-replay on boot"), Dispatched.Num(), 0);
		TestEqual(TEXT("Boot2 reports 2 persisted dead-letters"), Boot2.Online->GetOutboxStats().DeadLetterCount, 2);

		TestTrue(TEXT("Explicit RedriveDeadLetters succeeds"), Boot2.Online->RedriveDeadLetters());
		Boot2.Online->FlushPersistentOutboxForTest();

		TestEqual(TEXT("Redrive dispatched both dead-letter entries"), Dispatched.Num(), 2);
		TestEqual(TEXT("RedrivenTotal == 2"), Boot2.Online->GetOutboxStats().RedrivenTotal, static_cast<int64>(2));
		TestEqual(TEXT("DeadLetterCount cleared to 0 after redrive"), Boot2.Online->GetOutboxStats().DeadLetterCount, 0);

		if (Dispatched.Num() == 2)
		{
			TestEqual(TEXT("First redriven key matches InvalidKey"), Dispatched[0].Key, InvalidKey);
			TestEqual(TEXT("Second redriven key matches ExhaustedKey"), Dispatched[1].Key, ExhaustedKey);
			for (auto& Item : Dispatched)
			{
				auto Replayed = MakeShared<FJsonObject>();
				Replayed->SetStringField(TEXT("status"), TEXT("REPLAYED"));
				Item.Reply(Replayed);
			}
		}
		Boot2.Online->FlushPersistentOutboxForTest();
		TestEqual(TEXT("Both redriven entries confirmed"), Boot2.Online->GetOutboxStats().ConfirmedTotal, static_cast<int64>(2));
	}

	// Boot 3: verify zero pending and zero dead-letters remain.
	{
		FTestOutboxHost Boot3;
		if (!TestNotNull(TEXT("Boot3 OnlineWorldSubsystem"), Boot3.Online)) { return false; }
		TestTrue(TEXT("Boot3 opens clean DB"), Boot3.Online->ConfigurePersistentOutbox(DbPath));
		Boot3.Online->FlushPersistentOutboxForTest();
		TestEqual(TEXT("Boot3 zero pending recovered"), Boot3.Online->GetOutboxStats().RecoveredTotal, static_cast<int64>(0));
		TestEqual(TEXT("Boot3 zero dead-letters remaining"), Boot3.Online->GetOutboxStats().DeadLetterCount, 0);
	}

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOutboxMixedCommitTest, "ProjectJ.Online.OutboxMixedCommitAndShutdown", Wave4TestFlags)
bool FProjectJOutboxMixedCommitTest::RunTest(const FString&)
{
	const FString Path = MakeTempOutboxDbPath(TEXT("Mixed"));
	ON_SCOPE_EXIT { CleanupOutboxDbFiles(Path); };
	const FGuid Confirmed = FGuid::NewGuid(), Dead = FGuid::NewGuid(), New = FGuid::NewGuid(), PendingShutdown = FGuid::NewGuid();
	const FGuid QueuedShutdown = FGuid::NewGuid(), ConfirmedShutdown = FGuid::NewGuid(), DeadShutdown = FGuid::NewGuid();
	TMap<FGuid, TFunction<void(TSharedPtr<FJsonObject>)>> Replies;
	AddExpectedMessage(TEXT("Persistent online outbox batch write failed"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessage(TEXT("Durable outbox failed permanently"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	{
		FTestOutboxHost Host;
		auto* Online = Host.Online;
		Online->SetOutboxDispatchHookForTest([&](FName, FName, const FGuid& Key, const auto&, auto Reply)
			{ Replies.Add(Key, MoveTemp(Reply)); });
		TestTrue(TEXT("Configure"), Online->ConfigurePersistentOutbox(Path));
		TestTrue(TEXT("Repeated same-path configuration does not replace an active store"), Online->ConfigurePersistentOutbox(Path));
		TestFalse(TEXT("A different live database cannot replace the store"), Online->ConfigurePersistentOutbox(Path + TEXT(".other")));
		auto Enqueue = [&](FGuid Key, TFunction<void(bool, TSharedPtr<FJsonObject>)> Done = {})
		{
			return UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(Host.World, TEXT("Reward"), TEXT("Boss"),
				MakeShared<FJsonObject>(), Key, TEXT("mixed"), MoveTemp(Done));
		};
		TestTrue(TEXT("Accept before the open callback"), Enqueue(Confirmed));
		TestTrue(TEXT("Accept another before the open callback"), Enqueue(Dead));
		Online->FlushPersistentOutboxForTest();
		if (!TestTrue(TEXT("Committed keys were dispatched"), Replies.Contains(Confirmed) && Replies.Contains(Dead))) return false;
		TestTrue(TEXT("Hold the worker until its queued GT callback"), Online->RedriveDeadLetters(1));
		bool bNewRejected = false;
		TestTrue(TEXT("Queue new write alongside terminal cleanup"), Enqueue(New,
			[&](bool bConfirmed, auto) { bNewRejected = !bConfirmed; }));
		Replies.FindChecked(Confirmed)(MakeReply(TEXT("COMMITTED")));
		Replies.FindChecked(Dead)(MakeReply(TEXT("INVALID")));
		TestEqual(TEXT("Both slots stay pinned until terminal commit"), Online->GetOutboxStats().FinalizingCount, 2);
		TestFalse(TEXT("An uncommitted terminal key cannot be reused"), Enqueue(Confirmed));
		Online->FailNextOutboxStoreWriteForTest();
		Online->FlushPersistentOutboxForTest();
		TestTrue(TEXT("Rolled-back admission fails without dispatch"), bNewRejected && !Replies.Contains(New));
		TestEqual(TEXT("Terminal updates retry without another HTTP send"), Online->GetOutboxStats().FinalizingCount, 0);
		TestEqual(TEXT("Only committed dead letters count"), Online->GetOutboxStats().DeadLetterCount, 1);
		TestEqual(TEXT("Remote confirmation counted once"), Online->GetOutboxStats().ConfirmedTotal, int64(1));

		TestTrue(TEXT("Seed a dispatched operation for shutdown cleanup"), Enqueue(ConfirmedShutdown));
		TestTrue(TEXT("Seed a dispatched operation for shutdown quarantine"), Enqueue(DeadShutdown));
		Online->FlushPersistentOutboxForTest();
		if (!TestTrue(TEXT("Both shutdown operations reached dispatch"), Replies.Contains(ConfirmedShutdown) && Replies.Contains(DeadShutdown))) return false;
		Online->FailNextOutboxStoreWriteForTest();
		TestTrue(TEXT("Start one SQLite write before shutdown"), Enqueue(PendingShutdown));
		TestTrue(TEXT("Leave another persistence write queued"), Enqueue(QueuedShutdown));
		Replies.FindChecked(ConfirmedShutdown)(MakeReply(TEXT("COMMITTED")));
		Replies.FindChecked(DeadShutdown)(MakeReply(TEXT("INVALID")));
		// There is intentionally no GT task-graph flush before the host goes away.
	}
	Async(EAsyncExecution::ThreadPool, [&]
	{
		FProject_JOnlineOutboxStore Store(Path);
		TArray<FProject_JPersistedOutboxRecord> Pending;
		int32 DeadLetters = 0;
		TestTrue(TEXT("Reopen after shutdown raced the worker callback"), Store.OpenAndLoadPending(16, Pending, DeadLetters));
		TestTrue(TEXT("Shutdown retries the failed active append and commits its queued append and delete"),
			Pending.Num() == 2 && Pending[0].IdempotencyKey == PendingShutdown && Pending[1].IdempotencyKey == QueuedShutdown);
		TestEqual(TEXT("Both dead letters reached disk, including the queued transition at shutdown"), DeadLetters, 2);
	}).Wait();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOutboxStartupCapacityTest, "ProjectJ.Online.OutboxStartupCapacityAndDeduplication", Wave4TestFlags)
bool FProjectJOutboxStartupCapacityTest::RunTest(const FString&)
{
	const FString Path = MakeTempOutboxDbPath(TEXT("Capacity"));
	ON_SCOPE_EXIT { CleanupOutboxDbFiles(Path); };
	const int32 Total = UProject_JOnlineWorldSubsystem::MaxOutboxEntries + 3;
	const FGuid Collision(1, 2, 3, 1);
	const FGuid Quarantined(1, 2, 4, 1);
	if (!TestTrue(TEXT("Seed an over-capacity disk backlog and one quarantined key"), Async(EAsyncExecution::ThreadPool, [&]
	{
		FProject_JOnlineOutboxStore Store(Path);
		TArray<FProject_JOutboxStoreOp> Ops;
		for (int32 Index = 1; Index <= Total; ++Index) Ops.Add(SeedPending(FGuid(1, 2, 3, Index)));
		Ops.Add(SeedPending(Quarantined));
		auto& Dead = Ops.AddDefaulted_GetRef();
		Dead.Kind = EProject_JOutboxStoreOpKind::MarkDeadLetter;
		Dead.Record.IdempotencyKey = Quarantined;
		Dead.Record.LastStatus = TEXT("INVALID");
		int32 Count = 0;
		return Store.ApplyBatch(Ops, Count) && Count == 1;
	}).Get())) return false;

	TMap<FGuid, int32> Sent;
	bool bPersistedPayload = false;
	bool bConflictingInputRejected = false;
	FTestOutboxHost Host;
	auto* Online = Host.Online;
	Online->SetOutboxDispatchHookForTest([&](FName, FName, const FGuid& Key, const TArray<uint8>& Payload, auto Reply)
	{
		++Sent.FindOrAdd(Key);
		if (Key == Collision) bPersistedPayload = Payload == SeedPending(Collision).Record.SerializedPayload;
		Reply(MakeReply(TEXT("REPLAYED")));
	});
	TestTrue(TEXT("Start disk recovery"), Online->ConfigurePersistentOutbox(Path));
	TestTrue(TEXT("Admit collision while the recovery callback is pending"), UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(
		Host.World, TEXT("Reward"), TEXT("Boss"), MakeShared<FJsonObject>(), Collision, TEXT("different body"),
		[&](bool bOk, auto) { bConflictingInputRejected = !bOk; }));
	TestTrue(TEXT("Enqueue duplicate of quarantined operation"), UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(
		Host.World, TEXT("Reward"), TEXT("Boss"), MakeShared<FJsonObject>(), Quarantined, TEXT("quarantined")));
	for (int32 Pass = 0; Pass < 100 && Sent.Num() < Total; ++Pass)
	{
		Online->FlushPersistentOutboxForTest();
		Online->PumpOutboxForTest(FPlatformTime::Seconds());
	}
	Online->FlushPersistentOutboxForTest();
	TestTrue(TEXT("Disk body wins and conflicting caller fails"), bPersistedPayload && bConflictingInputRejected);
	TestEqual(TEXT("All recovery pages drain without duplicate or dropped work"), Sent.Num(), Total);
	TestTrue(TEXT("Each key was sent once"), Algo::AllOf(Sent, [](const auto& Pair) { return Pair.Value == 1; }));
	TestFalse(TEXT("Admission alone never redrives a quarantined key"), Sent.Contains(Quarantined));
	TestEqual(TEXT("No capacity is stranded"), Online->GetOutboxStats().PendingCount + Online->GetOutboxStats().PersistingCount
		+ Online->GetOutboxStats().InFlightCount + Online->GetOutboxStats().FinalizingCount, 0);
	return !HasAnyErrors();
}


#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOutboxNativeCrashTest, "ProjectJ.Online.OutboxNativeProcessRecovery", Wave4TestFlags)
bool FProjectJOutboxNativeCrashTest::RunTest(const FString&)
{
	FString Phase, Run;
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJOutboxCrashPhase="), Phase);
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJOutboxCrashRun="), Run);
	FGuid RunId;
	if (!Phase.IsEmpty() && !FGuid::ParseExact(Run, EGuidFormats::Digits, RunId))
	{
		AddError(TEXT("Native outbox phases require a GUID-owned fixture directory"));
		return false;
	}
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("Validation/NativeOutboxRecovery") /
		(Phase.IsEmpty() ? FGuid::NewGuid().ToString(EGuidFormats::Digits) : Run);
	const FString File = Dir / TEXT("Outbox.sqlite");
	auto Work = MakeShared<TFuture<TArray<FString>>>(Async(EAsyncExecution::ThreadPool, [File, Phase]
	{
		TArray<FString> Errors;
		const auto Check = [&](bool bOk, const TCHAR* Message) { if (!bOk) Errors.Add(Message); };
		const FGuid Removed(1, 1, 1, 1), Quarantined(1, 1, 1, 2), New(1, 1, 1, 3);
		FProject_JOnlineOutboxStore Store(File);
		TArray<FProject_JPersistedOutboxRecord> Rows;
		int32 DeadCount = 0;
		if (Phase.IsEmpty() || Phase == TEXT("Seed"))
		{
			if (IFileManager::Get().FileExists(*File)) return TArray<FString>{TEXT("Seed must use a fresh fixture")};
			Check(Store.ApplyBatch({SeedPending(Removed), SeedPending(Quarantined)}, DeadCount), TEXT("Seed must commit"));
			if (Phase.IsEmpty())
			{
				FProject_JOnlineOutboxStore Second(File);
				int32 Count = 0;
				Check(!Second.OpenAndLoadPending(16, Rows, Count), TEXT("Another owner cannot claim the active SQLite path"));
				Check(Store.Close() && Second.OpenAndLoadPending(16, Rows, Count) && Rows.Num() == 2,
					TEXT("Path can be acquired after first owner closes"));
				Second.Close();
				CleanupOutboxDbFiles(File);
			}
			return Errors;
		}
		if (!IFileManager::Get().FileExists(*File)) return TArray<FString>{TEXT("Seed the fixture in a previous process")};
		auto Delete = SeedPending(Removed);
		Delete.Kind = EProject_JOutboxStoreOpKind::DeleteConfirmed;
		auto Dead = SeedPending(Quarantined);
		Dead.Kind = EProject_JOutboxStoreOpKind::MarkDeadLetter;
		Dead.Record.TotalAttempts = 6;
		Dead.Record.LastStatus = TEXT("EXHAUSTED");
		const TArray<FProject_JOutboxStoreOp> Mixed{Delete, Dead, SeedPending(New)};
		if (Phase == TEXT("Crash") || Phase == TEXT("CrashAfterCommit"))
		{
			if (Phase == TEXT("Crash")) Store.CrashOnNextBatchCommitForTest();
			const bool bOk = Store.ApplyBatch(Mixed, DeadCount);
			if (Phase == TEXT("CrashAfterCommit") && bOk)
				FPlatformMisc::RequestExitWithStatus(true, 99, TEXT("Owned outbox crash fixture after SQLite COMMIT"));
			Errors.Add(FString::Printf(TEXT("Expected controlled exit inside/outside COMMIT, rc was %d: %s"), bOk, *Store.GetLastFailure()));
			return Errors;
		}
		const bool bCommitted = Phase == TEXT("CheckCommitted");
		Check(bCommitted || Phase == TEXT("CheckRollback"), TEXT("Unknown crash fixture phase"));
		if (!bCommitted)
		{
			TArray<uint8> Journal;
			Check(FFileHelper::LoadFileToArray(Journal, *(File + TEXT("-journal"))) && Journal.Num() > 512 && Journal[0] == 0xd9,
				TEXT("Native crash left a hot rollback journal"));
		}
		Check(Store.OpenAndLoadPending(16, Rows, DeadCount), TEXT("New process reopens with quick_check"));
		Check(Rows.Num() == (bCommitted ? 1 : 2) && Rows[0].IdempotencyKey == (bCommitted ? New : Removed)
			&& (bCommitted || Rows[1].IdempotencyKey == Quarantined) && DeadCount == int32(bCommitted),
			TEXT("Recovery restores the mixed transaction entirely at the correct boundary"));
		return Errors;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Work]
	{
		if (!Work->IsReady()) return false;
		for (const auto& Error : Work->Get()) AddError(Error);
		return true;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOutboxForeignFileTest, "ProjectJ.Online.OutboxRejectsForeignAndCorruptFiles", Wave4TestFlags)
bool FProjectJOutboxForeignFileTest::RunTest(const FString&)
{
	const FString File = MakeTempOutboxDbPath(TEXT("Foreign"));
	auto Work = MakeShared<TFuture<bool>>(Async(EAsyncExecution::ThreadPool, [File]
	{
		ON_SCOPE_EXIT { CleanupOutboxDbFiles(File); };
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
		FProject_JProfileDatabase Native;
		if (!Native.Open(*File, ESQLiteDatabaseOpenMode::ReadWriteCreate) ||
			!Native.Execute(TEXT("CREATE TABLE do_not_modify (value TEXT); INSERT INTO do_not_modify VALUES ('preserve');")) ||
			!Native.Close()) return false;
		{
			FProject_JOnlineOutboxStore Store(File);
			TArray<FProject_JPersistedOutboxRecord> Rows;
			int32 Count = 0;
			if (Store.OpenAndLoadPending(16, Rows, Count) || !Store.Close()) return false;
		}
		int32 AppId = -1, Version = -1;
		if (!Native.Open(*File, ESQLiteDatabaseOpenMode::ReadWrite) || !Native.GetApplicationId(AppId) || AppId != 0 ||
			!Native.GetUserVersion(Version) || Version != 0 || !Native.PerformQuickIntegrityCheck() ||
			!Native.SetApplicationId(1234) || !Native.SetUserVersion(3) || !Native.Close()) return false;
		{
			FProject_JOnlineOutboxStore Store(File);
			TArray<FProject_JPersistedOutboxRecord> Rows;
			int32 Count = 0;
			if (Store.OpenAndLoadPending(16, Rows, Count) || !Store.Close()) return false;
		}
		CleanupOutboxDbFiles(File);
		if (!FFileHelper::SaveStringToFile(FString::ChrN(4096, TEXT('X')), *File)) return false;
		FProject_JOnlineOutboxStore Store(File);
		TArray<FProject_JPersistedOutboxRecord> Rows;
		int32 Count = 0;
		return !Store.OpenAndLoadPending(16, Rows, Count) && Store.Close() && IFileManager::Get().FileSize(*File) == 4096;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Work]
	{
		if (!Work->IsReady()) return false;
		TestTrue(TEXT("Unmarked, foreign and corrupt files fail closed and release their handles"), Work->Get());
		return true;
	}));
	return true;
}
#endif


#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOutboxPersistentHTTPTest, "ProjectJ.Online.OutboxPersistentGatewayHTTP", Wave4TestFlags)
bool FProjectJOutboxPersistentHTTPTest::RunTest(const FString&)
{
	auto* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	auto* Listener = Sockets->CreateSocket(NAME_Stream, TEXT("ProjectJOutboxPersistentHTTP"), false);
	if (!TestNotNull(TEXT("Local listener"), Listener)) return false;
	auto Address = Sockets->CreateInternetAddr();
	bool bValid = false;
	Address->SetIp(TEXT("127.0.0.1"), bValid);
	Address->SetPort(0);
	if (!bValid || !Listener->Bind(*Address) || !Listener->Listen(2))
	{
		Sockets->DestroySocket(Listener);
		AddError(TEXT("Cannot open ephemeral loopback port"));
		return false;
	}
	Listener->GetAddress(*Address);
	Listener->SetNonBlocking(true);
	struct FRun
	{
		TStrongObjectPtr<UGameInstance> GI{NewObject<UGameInstance>(GEngine)};
		FString Path = MakeTempOutboxDbPath(TEXT("RealHTTP"));
		FString OldKey;
		FGuid Key = FGuid::NewGuid();
		FGuid Defeat = FGuid::NewGuid();
		FGuid Character = FGuid::NewGuid();
		TArray<FString> Requests;
		std::atomic<bool> bDone{false};
		int32 Confirmed = 0;
		int32 Callbacks = 0;
		TMap<FString, TPair<FString, TSharedRef<FJsonObject>>> Expected;
		double Deadline = FPlatformTime::Seconds() + 15;
	};
	auto Run = MakeShared<FRun, ESPMode::ThreadSafe>();
	Run->GI->InitializeStandalone();
	auto* World = Run->GI->GetWorld();
	World->WorldType = EWorldType::PIE;
	World->SetPlayInEditorInitialNetMode(NM_DedicatedServer);
	auto* Gateway = Run->GI->GetSubsystem<UProject_JGatewaySubsystem>();
	auto* Online = Run->GI->GetSubsystem<UProject_JOnlineWorldSubsystem>();
	auto* Port = Run->GI->GetSubsystem<UProject_JNakamaServicePortSubsystem>();
	auto* Url = FindFProperty<FStrProperty>(UProject_JGatewaySubsystem::StaticClass(), TEXT("GatewayUrl"));
	if (!Gateway || !Online || !Port || !Url)
	{
		Run->GI->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		Sockets->DestroySocket(Listener);
		AddError(TEXT("Missing HTTP fixture dependencies"));
		return false;
	}
	Url->SetPropertyValue_InContainer(Gateway, FString::Printf(TEXT("http://127.0.0.1:%d"), Address->GetPort()));
	Port->SetRuntimeMode(EProject_JBackendRuntimeMode::LocalDockerNakama);
	Run->OldKey = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_J_NAKAMA_HTTP_KEY"));
	FPlatformMisc::SetEnvironmentVar(TEXT("PROJECT_J_NAKAMA_HTTP_KEY"), TEXT("local-wave4-audit-key"));

	Async(EAsyncExecution::Thread, [Run, Listener, Sockets]
	{
		while (Run->Requests.Num() < 4 && FPlatformTime::Seconds() < Run->Deadline)
		{
			bool bPending = false;
			Listener->WaitForPendingConnection(bPending, FTimespan::FromMilliseconds(50));
			if (!bPending) continue;
			auto* Client = Listener->Accept(TEXT("ProjectJWave4HTTPClient"));
			if (!Client) continue;
			Client->SetNonBlocking(true);
			TArray<uint8> Bytes;
			FString Request;
			const double Until = FPlatformTime::Seconds() + 2.;
			while (FPlatformTime::Seconds() < Until && Bytes.Num() < 16384)
			{
				Client->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(50));
				uint8 Chunk[1024];
				int32 Read = 0;
				if (!Client->Recv(Chunk, sizeof(Chunk), Read) || Read <= 0) continue;
				Bytes.Append(Chunk, Read);
				const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
				Request = FString(Text.Length(), Text.Get());
				const int32 Separator = Request.Find(TEXT("\r\n\r\n"));
				const int32 Length = Request.Find(TEXT("Content-Length:"));
				if (Separator != INDEX_NONE && Length != INDEX_NONE
					&& Bytes.Num() >= Separator + 4 + FCString::Atoi(*Request + Length + 15)) break;
			}
			const bool bFirst = Run->Requests.IsEmpty();
			Run->Requests.Add(MoveTemp(Request));
			const FString Reply = bFirst ? TEXT("{\"temporary\":true}") : TEXT("{\"status\":\"REPLAYED\"}");
			const FString Response = FString::Printf(TEXT("HTTP/1.1 %s\r\nContent-Length: %d\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n%s"),
				bFirst ? TEXT("503 Unavailable") : TEXT("200 OK"), Reply.Len(), *Reply);
			const FTCHARToUTF8 Wire(*Response);
			for (int32 Sent = 0; Sent < Wire.Length() && FPlatformTime::Seconds() < Until;)
			{
				int32 Count = 0;
				if (!Client->Send(reinterpret_cast<const uint8*>(Wire.Get()) + Sent, Wire.Length() - Sent, Count)) break;
				Sent += Count;
			}
			Client->Close();
			Sockets->DestroySocket(Client);
		}
		Listener->Close();
		Sockets->DestroySocket(Listener);
		Run->bDone = true;
	});

	TestTrue(TEXT("Persistent store configured on the real HTTP path"), Online->ConfigurePersistentOutbox(Run->Path));
	const auto Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("defeat_id"), UProject_JOnlineWorldSubsystem::GuidText(Run->Defeat));
	Body->SetStringField(TEXT("character_id"), UProject_JOnlineWorldSubsystem::GuidText(Run->Character));
	Body->SetStringField(TEXT("gold"), TEXT("25"));
	Body->SetArrayField(TEXT("items"), {});
	Run->Expected.Add(UProject_JOnlineWorldSubsystem::GuidText(Run->Key), {TEXT("j_reward_boss"), Body});
	TestTrue(TEXT("Admission queues a valid boss mutation"), UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(World,
		TEXT("Reward"), TEXT("Boss"), Body, Run->Key, TEXT("native HTTP and SQLite"),
		[Run](bool bConfirmed, auto) { ++Run->Callbacks; Run->Confirmed += bConfirmed; }));
	TestTrue(TEXT("A new mutation waits for SQLite before wire dispatch"), Online->GetOutboxStats().PersistingCount == 1
		&& Online->GetOutboxStats().InFlightCount == 0);
	Online->FlushPersistentOutboxForTest();
	const auto Add = [&](FName Service, FName Operation, const FString& Rpc, const TSharedRef<FJsonObject>& Payload, FGuid Key)
	{
		Run->Expected.Add(UProject_JOnlineWorldSubsystem::GuidText(Key), {Rpc, Payload});
		return UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(World, Service, Operation, Payload, Key, TEXT("mixed native HTTP and SQLite"),
			[Run](bool bConfirmed, auto) { ++Run->Callbacks; Run->Confirmed += bConfirmed; });
	};
	auto Quest = MakeShared<FJsonObject>();
	Quest->SetStringField(TEXT("kind"), TEXT("progress"));
	Quest->SetStringField(TEXT("character_id"), UProject_JOnlineWorldSubsystem::GuidText(Run->Character));
	Quest->SetStringField(TEXT("session_epoch"), TEXT("1"));
	Quest->SetStringField(TEXT("earned_at"), LexToString(FDateTime::UtcNow().ToUnixTimestamp()));
	Quest->SetArrayField(TEXT("changes"), {});
	TestTrue(TEXT("Serialize the server-earned progress RPC"), Add(TEXT("World"), TEXT("Quest_Progress"), TEXT("j_world_quest_progress"), Quest, FGuid::NewGuid()));
	const FGuid DungeonKey = FGuid::NewGuid();
	auto Clear = MakeShared<FJsonObject>();
	Clear->SetStringField(TEXT("worker_id"), UProject_JOnlineWorldSubsystem::GuidText(FGuid::NewGuid()));
	Clear->SetStringField(TEXT("instance_id"), UProject_JOnlineWorldSubsystem::GuidText(DungeonKey));
	Clear->SetObjectField(TEXT("rewards"), MakeShared<FJsonObject>());
	TestTrue(TEXT("Serialize the dungeon clear RPC"), Add(TEXT("Dungeon"), TEXT("Clear"), TEXT("j_dungeon_clear"), Clear, DungeonKey));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Run, World, Online]
	{
		if (!Run->bDone || ((Run->Callbacks < 3 || Online->GetOutboxStats().FinalizingCount > 0)
			&& FPlatformTime::Seconds() < Run->Deadline)) return false;
		TestTrue(TEXT("All three mutations finish once after HTTP 503 and replay"), Run->Callbacks == 3
			&& Run->Confirmed == 3 && Online->GetOutboxStats().RetriedTotal == 1);
		TestEqual(TEXT("Three mutations plus one real HTTP retry"), Run->Requests.Num(), 4);
		TSet<FString> RequestIds;
		TMap<FString, int32> Attempts;
		for (const auto& Request : Run->Requests)
		{
			const int32 Separator = Request.Find(TEXT("\r\n\r\n"));
			TSharedPtr<FJsonObject> Envelope, Intent;
			if (!TestTrue(TEXT("HTTP body parses"), Separator != INDEX_NONE &&
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Request.Mid(Separator + 4)), Envelope) && Envelope)) continue;
			const FString Key = Envelope->GetStringField(TEXT("idempotency_key"));
			const auto* Expected = Run->Expected.Find(Key);
			if (!TestNotNull(TEXT("Stable application key maps to one of the three original mutations"), Expected)) continue;
			TestTrue(TEXT("Uses the real Nakama RPC route"),
				Request.StartsWith(FString::Printf(TEXT("POST /v2/rpc/%s?unwrap "), *Expected->Key)));
			const FString RequestId = Envelope->GetStringField(TEXT("request_id"));
			TestTrue(TEXT("Every attempt has a distinct transport request ID"), !RequestId.IsEmpty() && !RequestIds.Contains(RequestId));
			RequestIds.Add(RequestId);
			++Attempts.FindOrAdd(Key);
			FString Payload;
			FString ExpectedPayload;
			FJsonSerializer::Serialize(Expected->Value, TJsonWriterFactory<>::Create(&ExpectedPayload));
			TestTrue(TEXT("The actual Gateway receives the exact persisted payload"),
				FBase64::Decode(Envelope->GetStringField(TEXT("payload_b64")), Payload) && Payload == ExpectedPayload);
		}
		TestEqual(TEXT("All three stable keys were observed on the wire"), Attempts.Num(), 3);
		FPlatformMisc::SetEnvironmentVar(TEXT("PROJECT_J_NAKAMA_HTTP_KEY"), *Run->OldKey);
		Run->GI->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		TestTrue(TEXT("Reopen sees zero pending or quarantined rows after HTTP confirmation"), Async(EAsyncExecution::ThreadPool, [Run]
		{
			FProject_JOnlineOutboxStore Store(Run->Path);
			TArray<FProject_JPersistedOutboxRecord> Rows;
			int32 Count = -1;
			return Store.OpenAndLoadPending(4, Rows, Count) && Rows.IsEmpty() && Count == 0;
		}).Get());
		CleanupOutboxDbFiles(Run->Path);
		return true;
	}));
	return true;
}
#endif


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOutboxRedriveReservationTest, "ProjectJ.Online.OutboxPartialRedriveReservation", Wave4TestFlags)
bool FProjectJOutboxRedriveReservationTest::RunTest(const FString&)
{
	const FString Path = MakeTempOutboxDbPath(TEXT("RedriveCapacity"));
	ON_SCOPE_EXIT { CleanupOutboxDbFiles(Path); };
	const int32 Cap = UProject_JOnlineWorldSubsystem::MaxOutboxEntries;
	const FGuid First(4, 5, 6, 1), Second(4, 5, 6, 2);
	if (!TestTrue(TEXT("Seed a full-minus-one pending queue and two ordered dead letters"), Async(EAsyncExecution::ThreadPool, [&]
	{
		FProject_JOnlineOutboxStore Store(Path);
		TArray<FProject_JOutboxStoreOp> Ops;
		for (int32 Index = 1; Index < Cap; ++Index) Ops.Add(SeedPending(FGuid(1, 2, 4, Index)));
		for (const FGuid Key : {First, Second})
		{
			Ops.Add(SeedPending(Key));
			auto& Dead = Ops.AddDefaulted_GetRef();
			Dead.Kind = EProject_JOutboxStoreOpKind::MarkDeadLetter;
			Dead.Record.IdempotencyKey = Key;
			Dead.Record.LastStatus = TEXT("EXHAUSTED");
		}
		int32 Count = 0;
		return Store.ApplyBatch(Ops, Count) && Count == 2;
	}).Get())) return false;
	{
		FTestOutboxHost Host;
		Host.Online->SetOutboxDispatchHookForTest([](FName, FName, const FGuid&, const auto&, auto) {});
		TestTrue(TEXT("Load existing page"), Host.Online->ConfigurePersistentOutbox(Path));
		Host.Online->FlushPersistentOutboxForTest();
		TestTrue(TEXT("Request both dead letters with one slot available"), Host.Online->RedriveDeadLetters(2));
		AddExpectedMessage(TEXT("Durable outbox capacity"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 1);
		TestFalse(TEXT("Concurrent admission cannot take the reserved redrive slot"),
			UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(Host.World, TEXT("Reward"), TEXT("Boss"),
				MakeShared<FJsonObject>(), FGuid::NewGuid(), TEXT("redrive capacity")));
		Host.Online->FlushPersistentOutboxForTest();
		const auto Stats = Host.Online->GetOutboxStats();
		TestEqual(TEXT("One dead letter moved and one remained"), Stats.RedrivenTotal, int64(1));
		TestEqual(TEXT("Partial redrive remains discoverable"), Stats.DeadLetterCount, 1);
		TestEqual(TEXT("Resident work stays at cap"), Stats.PendingCount + Stats.InFlightCount, Cap);
	}
	TestTrue(TEXT("Fresh SQLite owner sees only the first dead letter redriven"), Async(EAsyncExecution::ThreadPool, [&]
	{
		FProject_JOnlineOutboxStore Store(Path);
		TArray<FProject_JPersistedOutboxRecord> Pending;
		int32 Count = 0;
		return Store.OpenAndLoadPending(Cap, Pending, Count) && Count == 1 && Pending.Num() == Cap
			&& Pending.Last().IdempotencyKey == First;
	}).Get());
	return !HasAnyErrors();
}

#endif
