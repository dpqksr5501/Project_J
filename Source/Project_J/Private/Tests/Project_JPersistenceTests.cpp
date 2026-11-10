#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Serialization/MemoryWriter.h"
#include "Backend/Project_JLocalProfileSnapshot.h"
#include "Backend/Project_JQuestHistoryStorage.h"
#include "Backend/Project_JProfileMigration.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Backend/Project_JProfileRules.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "Backend/Project_JSQLiteRepository.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JGameMode.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Misc/AutomationTest.h"
#include "MMO/Coordination.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "UObject/StrongObjectPtr.h"

using namespace ProjectJ::MMO;
namespace
{
constexpr auto PersistenceTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
FString TestDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Validation/LocalProfileTests") /
											 FGuid::NewGuid().ToString());
}
struct FChecks
{
	TArray<FString> Failures;
	void Check(bool Value, const TCHAR *Message)
	{
		if (!Value)
			Failures.Add(Message);
	}
};
class FRepositoryFinished : public IAutomationLatentCommand
{
	FAutomationTestBase *Test;
	TFuture<FChecks> Work;

  public:
	FRepositoryFinished(FAutomationTestBase *InTest, TFuture<FChecks> &&InWork) : Test(InTest), Work(MoveTemp(InWork))
	{
	}
	bool Update() override
	{
		if (!Work.IsReady())
			return false;
		for (const auto &Failure : Work.Get().Failures)
			Test->AddError(Failure);
		return true;
	}
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSQLiteContractTest, "ProjectJ.MMO.Persistence.SQLiteContract", PersistenceTestFlags)
bool FProjectJSQLiteContractTest::RunTest(const FString &)
{
	AddExpectedError(TEXT("Local database has a journal/WAL sidecar."), EAutomationExpectedErrorFlags::Contains, 1);
	const FString Directory = TestDirectory();
	ADD_LATENT_AUTOMATION_COMMAND(FRepositoryFinished(
		this, Async(EAsyncExecution::ThreadPool, [Directory] {
			FChecks C;
			const FString File = Directory / TEXT("Repository.sqlite");
			const FAggregateKey A{TEXT("Character"), FGuid::NewGuid()}, B{TEXT("Quest"), FGuid::NewGuid()};
			FCommitRequest Initial{FGuid::NewGuid(), TEXT("Reward"), {{A, 0, 1, {1, 2}, false}, {B, 0, 1, {3}, false}}};
			{
				FProject_JSQLiteRepository DB(File);
				FRecord R;
				C.Check(DB.ReadChecked(A, R) == EProject_JRecordRead::Missing,
						TEXT("New database distinguishes absent from unavailable"));
				C.Check(DB.Commit(Initial).Status == ECommitStatus::Committed,
						TEXT("State and receipt atomically commit"));
				DB.Close();
			}
			{
				FProject_JSQLiteRepository DB(File);
				FRecord R;
				const auto Replay = DB.Commit(Initial);
				C.Check(Replay.Status == ECommitStatus::Replayed && Replay.Revisions == TArray<int64>{1, 1},
						TEXT("Reopened database returns original durable receipt"));
				auto Changed = Initial;
				Changed.Mutations[0].Payload = {9};
				C.Check(DB.Commit(Changed).Status == ECommitStatus::KeyReused,
						TEXT("A receipt cannot be reused for a changed payload"));
				FCommitRequest Conflict{
					FGuid::NewGuid(), TEXT("Conflict"), {{A, 1, 1, {8}, false}, {B, 0, 1, {8}, false}}};
				C.Check(DB.Commit(Conflict).Status == ECommitStatus::Conflict,
						TEXT("One stale revision rejects the full transaction"));
				C.Check(DB.Read(A, R) && R.Revision == 1 && R.Payload == TArray<uint8>{1, 2},
						TEXT("Conflict leaves earlier mutations untouched"));
				FCommitRequest Delete{FGuid::NewGuid(), TEXT("Delete"), {{A, 1, 1, {}, true}}};
				C.Check(DB.Commit(Delete).Status == ECommitStatus::Committed && DB.Read(A, R) && R.bDeleted &&
							R.Revision == 2 && R.Payload.IsEmpty(),
						TEXT("Tombstone retains its revision with an empty blob"));
				FCommitRequest Recreate{FGuid::NewGuid(), TEXT("Recreate"), {{A, 0, 1, {4}, false}}};
				C.Check(DB.Commit(Recreate).Status == ECommitStatus::Conflict,
						TEXT("Delete does not permit an ABA recreation at revision zero"));
				Recreate.Mutations[0].ExpectedRevision = 2;
				C.Check(DB.Commit(Recreate).Status == ECommitStatus::Committed,
						TEXT("Explicit tombstone revision permits recreation"));
				const FString Backup = Directory / TEXT("Backup.sqlite");
				C.Check(DB.BackupToNewFile(Backup), TEXT("Consistent backup passes native integrity validation"));
				C.Check(!DB.BackupToNewFile(Backup), TEXT("Backup never overwrites an existing file"));
				DB.Close();
				const FString Restored = Directory / TEXT("Restored.sqlite");
				C.Check(IFileManager::Get().Copy(*Restored, *Backup, false) == COPY_OK,
						TEXT("Restore fixture copies a verified closed backup"));
				FProject_JSQLiteRepository Recovery(Restored);
				C.Check(Recovery.Read(A, R) && R.Revision == 3 && R.Payload == TArray<uint8>{4},
						TEXT("Restored backup retains latest data"));
				C.Check(Recovery.Commit(Initial).Status == ECommitStatus::Replayed,
						TEXT("Restored backup retains deduplication receipts"));
				Recovery.Close();
			}
			{
				// Independent adapters race one expected revision. The OS file gate is
				// required by unreal-fs; the DB transaction still rejects the stale writer.
				auto Writer = [File, A](uint8 Value) {
					FProject_JSQLiteRepository DB(File);
					const auto Status =
						DB.Commit({FGuid::NewGuid(), TEXT("Concurrent"), {{A, 3, 1, {Value}, false}}}).Status;
					DB.Close();
					return Status;
				};
				auto First = Async(EAsyncExecution::Thread, [Writer] { return Writer(5); });
				auto Second = Async(EAsyncExecution::Thread, [Writer] { return Writer(6); });
				const auto One = First.Get(), Two = Second.Get();
				C.Check((One == ECommitStatus::Committed && Two == ECommitStatus::Conflict) ||
							(Two == ECommitStatus::Committed && One == ECommitStatus::Conflict),
						TEXT("Independent SQLite writers admit exactly one expected revision"));
			}
			{
				TArray<uint8> Before, After;
				FProject_JSQLiteRepository DB(File);
				const FGuid Id = FGuid::NewGuid();
				const FAggregateKey Lease{TEXT("LocalProfileLease"), Id}, Profile{TEXT("LocalProfile"), Id};
				TArray<uint8> One, Two;
				One.Init(1, 16);
				Two.Init(2, 16);
				C.Check(DB.Commit({FGuid::NewGuid(),
								   TEXT("OldSession"),
								   {{Lease, 0, 1, One, false}, {Profile, 0, 1, {17}, false}}})
								.Status == ECommitStatus::Committed,
						TEXT("Owned profile and initial writer epoch commit together"));
				C.Check(DB.Commit({FGuid::NewGuid(), TEXT("NewSession"), {{Lease, 1, 1, Two, false}}}).Status ==
							ECommitStatus::Committed,
						TEXT("A new session advances the durable writer epoch"));
				C.Check(DB.Commit({FGuid::NewGuid(),
								   TEXT("LateOldWriter"),
								   {{Lease, 1, 1, One, false}, {Profile, 1, 1, {99}, false}}})
								.Status == ECommitStatus::Conflict,
						TEXT("Stale writer epoch rejects the whole state/receipt commit"));
				FRecord Current;
				C.Check(DB.Read(Profile, Current) && Current.Revision == 1 && Current.Payload == TArray<uint8>{17},
						TEXT("Fenced late writer cannot partly overwrite the admitted profile"));
				DB.Close();
				FFileHelper::LoadFileToArray(Before, *File);
				C.Check(FFileHelper::SaveArrayToFile(TArray<uint8>{1, 2, 3}, *(File + TEXT("-journal"))),
						TEXT("Interrupted-file fixture exists"));
				FRecord R;
				C.Check(DB.ReadChecked(A, R) == EProject_JRecordRead::Unavailable,
						TEXT("A leftover journal blocks admission before inspecting uncommitted pages"));
				DB.Close();
				FFileHelper::LoadFileToArray(After, *File);
				C.Check(Before == After && IFileManager::Get().FileSize(*(File + TEXT("-journal"))) == 3,
						TEXT("Rejected journal and database remain untouched for offline recovery"));
			}
			{
				FProject_JSQLiteRepository Limited(Directory / TEXT("Capacity.sqlite"), 2, 1);
				C.Check(Limited.Commit(Initial).Status == ECommitStatus::Committed,
						TEXT("Bounded repository admits its final receipt"));
				C.Check(Limited.Commit(Initial).Status == ECommitStatus::Replayed,
						TEXT("Capacity does not evict an original receipt"));
				Initial.IdempotencyKey = FGuid::NewGuid();
				C.Check(Limited.Commit(Initial).Status == ECommitStatus::Capacity, TEXT("Capacity fails closed"));
				Limited.Close();
			}
			{
				FSQLiteDatabase Future;
				const FString Unknown = Directory / TEXT("Unknown.sqlite");
				C.Check(Future.Open(*Unknown, ESQLiteDatabaseOpenMode::ReadWriteCreate) &&
							Future.SetApplicationId(0x504A5250) && Future.SetUserVersion(99),
						TEXT("Unknown schema fixture exists"));
				Future.Close();
				FProject_JSQLiteRepository DB(Unknown);
				FRecord R;
				C.Check(DB.ReadChecked(A, R) == EProject_JRecordRead::Unavailable &&
							DB.Commit(Initial).Status == ECommitStatus::Unavailable,
						TEXT("Unknown schema cannot be silently reset or written"));
				DB.Close();
				C.Check(Future.Open(*Unknown, ESQLiteDatabaseOpenMode::ReadOnly),
						TEXT("Unknown file remains readable"));
				int32 Version = 0;
				C.Check(Future.GetUserVersion(Version) && Version == 99, TEXT("Rejected database keeps its version"));
				Future.Close();
				const FString Corrupt = Directory / TEXT("Corrupt.sqlite");
				FFileHelper::SaveStringToFile(TEXT("not a database"), *Corrupt);
				FProject_JSQLiteRepository Broken(Corrupt);
				C.Check(Broken.ReadChecked(A, R) == EProject_JRecordRead::Unavailable,
						TEXT("Corruption is unavailable, never an empty new profile"));
				Broken.Close();
				FString Text;
				C.Check(FFileHelper::LoadFileToString(Text, *Corrupt) && Text == TEXT("not a database"),
						TEXT("Corrupt file is preserved"));
			}
			{
				const FString IntentFile = Directory / TEXT("Intent.sqlite");
				FCommitRequest Command{FGuid::NewGuid(), TEXT("ProfileEdit.1"), {{A, 0, 1, {4}, false}}};
				FProject_JSQLiteRepository DB(IntentFile);
				const TArray<uint8> Intent{1, 2, 3};
				C.Check(DB.CommitWithIntent(Command, Intent).Status == ECommitStatus::Committed,
						TEXT("Opaque intent and state commit atomically"));
				DB.Close();
				C.Check(DB.FindReceipt(Command.IdempotencyKey, Command.Operation, Intent).Status ==
							ECommitStatus::Replayed,
						TEXT("Exact intent reconciles after reopening independent of aggregate revisions"));
				C.Check(DB.FindReceipt(Command.IdempotencyKey, Command.Operation, {1, 2, 4}).Status ==
							ECommitStatus::KeyReused,
						TEXT("Changed intent cannot reuse a logical request"));
				C.Check(DB.Commit(Command).Status == ECommitStatus::KeyReused,
						TEXT("Generic retry cannot silently drop an existing intent"));
				DB.Close();
				FProject_JSQLiteRepository Missing(Directory / TEXT("Absent.sqlite"));
				C.Check(!Missing.BackupToNewFile(Directory / TEXT("AbsentBackup.sqlite")) &&
							!IFileManager::Get().FileExists(*(Directory / TEXT("Absent.sqlite"))),
						TEXT("Backup does not create an absent source database"));
				FSQLiteDatabase Foreign;
				const FString ForeignFile = Directory / TEXT("Foreign.sqlite");
				C.Check(Foreign.Open(*ForeignFile, ESQLiteDatabaseOpenMode::ReadWriteCreate) &&
							Foreign.Execute(TEXT("CREATE VIEW foreign_view AS SELECT 1")),
						TEXT("Unmarked view-only foreign schema fixture exists"));
				Foreign.Close();
				FProject_JSQLiteRepository Unknown(ForeignFile);
				FRecord R;
				C.Check(Unknown.ReadChecked(A, R) == EProject_JRecordRead::Unavailable,
						TEXT("A view-only foreign database is not adopted as an empty profile"));
				Unknown.Close();
			}
			return C;
		})));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProfileCodecTest, "ProjectJ.MMO.Persistence.ProfileCodec", PersistenceTestFlags)
bool FProjectJProfileCodecTest::RunTest(const FString &)
{
	FGuid A, C, A2, C2;
	TestTrue(TEXT("Local profile names resolve"),
			 UProject_JLocalProfileSubsystem::ResolveLocalIdentity(TEXT("Developer_1"), A, C));
	TestTrue(TEXT("Names normalize case"),
			 UProject_JLocalProfileSubsystem::ResolveLocalIdentity(TEXT("developer_1"), A2, C2) && A == A2 && C == C2 &&
				 A != C);
	TestFalse(TEXT("Path traversal cannot be a profile"),
			  UProject_JLocalProfileSubsystem::ResolveLocalIdentity(TEXT("../profile"), A2, C2));
	FProject_JLocalProfileSnapshot Snapshot;
	Snapshot.Account = A;
	Snapshot.Character = C;
	Snapshot.RulesFingerprint = FString::ChrN(40, 'A');
	Snapshot.Progression.Experience = MAX_int64;
	Snapshot.Quests.States.Add({TEXT("Quest"), 2, true, false});
	TArray<uint8> Bytes;
	FProject_JLocalProfileSnapshot Restored;
	TestTrue(TEXT("Values encode"), Snapshot.Encode(Bytes));
	TestTrue(TEXT("64-bit XP survives without JSON rounding"),
			 FProject_JLocalProfileSnapshot::Decode(Bytes, Restored) && Restored.Progression.Experience == MAX_int64);
	TestTrue(TEXT("Quest state stays paired with progression"), Restored.Quests.States.Num() == 1 &&
																	Restored.Quests.States[0].bClaimed &&
																	!Restored.Quests.States[0].bTracked);
	const auto ReadDocument = [](const TArray<uint8>& Source)
	{
		FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Source.GetData()), Source.Num());
		TSharedPtr<FJsonObject> Document;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get())), Document);
		return Document;
	};
	const auto WriteDocument = [](const TSharedPtr<FJsonObject>& Document)
	{
		FString Text; FJsonSerializer::Serialize(Document.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
		FTCHARToUTF8 Encoded(*Text); TArray<uint8> Result;
		Result.Append(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length()); return Result;
	};
	const auto LastGoodBytes = Bytes;
	const auto Duplicate = Snapshot.Quests.States[0];
	Snapshot.Quests.States.Add(Duplicate);
	TestFalse(TEXT("Writer cannot commit an unreadable duplicate-ID snapshot"), Snapshot.Encode(Bytes));
	TestTrue(TEXT("Failed encoding retains the previous caller-owned bytes"), Bytes == LastGoodBytes);
	auto DuplicateDocument = ReadDocument(LastGoodBytes);
	if (!TestTrue(TEXT("Real duplicate-document fixture parses"), DuplicateDocument.IsValid())) return false;
	auto DuplicateStates = DuplicateDocument->GetObjectField(TEXT("quests"))->GetArrayField(TEXT("states"));
	const auto DuplicateState = DuplicateStates[0]; DuplicateStates.Add(DuplicateState); DuplicateDocument->GetObjectField(TEXT("quests"))->SetArrayField(TEXT("states"), DuplicateStates);
	TestFalse(TEXT("Duplicate quest IDs cannot restore"), FProject_JLocalProfileSnapshot::Decode(WriteDocument(DuplicateDocument), Restored));
	TArray<uint8> PreservedBytes;
	TestTrue(TEXT("Failed decode retains the entire previously decoded value"), Restored.Encode(PreservedBytes) && PreservedBytes == LastGoodBytes);
	Snapshot.Quests.States.Pop();
	FProject_JPersistentItem Item;
	Item.Instance = FGuid::NewGuid();
	Item.Definition = TEXT("Potion.Test");
	Item.Order = 0;
	Snapshot.Items.Add(Item);
	TestTrue(TEXT("Stable item values encode"), Snapshot.Encode(Bytes));
	TestTrue(TEXT("Stable item values round trip"), FProject_JLocalProfileSnapshot::Decode(Bytes, Restored) &&
														Restored.Items.Num() == 1 &&
														Restored.Items[0].Instance == Item.Instance);
	Snapshot.Items.Add(Item);
	TestFalse(TEXT("Duplicate instance rejects before writing"), Snapshot.Encode(Bytes));
	Snapshot.Items[1].Instance = FGuid::NewGuid();
	TestFalse(TEXT("Duplicate bag order rejects before writing"), Snapshot.Encode(Bytes));
	Snapshot.Items.Pop();
	Snapshot.Items[0].DefinitionVersion = 0;
	TestFalse(TEXT("Unknown definition version cannot encode"), Snapshot.Encode(Bytes));
	Snapshot.Items[0].DefinitionVersion = 1;
	Snapshot.Format = 1;
	TestFalse(TEXT("Legacy format cannot silently discard owned items"), Snapshot.Encode(Bytes));
	Bytes.Init('x', 65537);
	TestFalse(TEXT("Oversized documents reject before parsing"),
			  FProject_JLocalProfileSnapshot::Decode(Bytes, Restored));

	// Genuine Format2 is still readable and never acquires invisible job ownership.
	auto Legacy = Snapshot; Legacy.Format = 2;
	TestTrue(TEXT("Legacy Format2 remains explicitly writable and decodes with no jobs"), Legacy.Encode(Bytes) &&
		FProject_JLocalProfileSnapshot::Decode(Bytes, Restored) && Restored.Format == 2 && Restored.CraftingJobs.IsEmpty());
	auto Timed = Legacy; Timed.Format = 3;
	ProjectJ::Crafting::FJobValues Job;
	Job.Id = FGuid::NewGuid(); Job.RecipeId = TEXT("Codec.TimedRecipe"); Job.Batches = 2;
	Job.RulesDigest = FString::ChrN(40, 'a');
	// Deliberately beyond JSON double's exact range, with a non-round low tick.
	Job.StartedUtcTicks = 638000000000000001ll;
	Job.DueUtcTicks = Job.StartedUtcTicks + 10 * ETimespan::TicksPerSecond;
	auto Escrow = Timed.Items[0]; Escrow.Instance = FGuid::NewGuid(); Escrow.Count = 2;
	Job.Escrow.Add(Escrow);
	ProjectJ::Crafting::FResolvedLine Output; Output.Definition = TEXT("Codec.Result"); Output.CountPerBatch = 3; Output.MaxStackCount = 100;
	Job.Outputs.Add(Output); Timed.CraftingJobs.Add(Job);
	TestTrue(TEXT("Format3 writes and reads bounded immutable production escrow"), Timed.Encode(Bytes) && FProject_JLocalProfileSnapshot::Decode(Bytes, Restored));
	if (!TestEqual(TEXT("One pending job round trips"), Restored.CraftingJobs.Num(), 1)) return false;
	const auto& DecodedJob = Restored.CraftingJobs[0];
	TestTrue(TEXT("Job identity terms and non-round int64 deadline survive exactly"), Restored.Format == 3 && DecodedJob.SchemaVersion == 1 &&
		DecodedJob.Id == Job.Id && DecodedJob.RecipeId == Job.RecipeId && DecodedJob.Batches == Job.Batches && DecodedJob.RulesDigest == Job.RulesDigest &&
		DecodedJob.StartedUtcTicks == Job.StartedUtcTicks && DecodedJob.DueUtcTicks == Job.DueUtcTicks &&
		DecodedJob.Escrow.Num() == 1 && DecodedJob.Escrow[0].Instance == Escrow.Instance && DecodedJob.Escrow[0].Count == 2 &&
		DecodedJob.Outputs.Num() == 1 && DecodedJob.Outputs[0].CountPerBatch == 3 && DecodedJob.Outputs[0].MaxStackCount == 100);
	const auto TimedBytes = Bytes;
	const auto RejectValue = [&](const FProject_JLocalProfileSnapshot& Invalid)
	{
		TestFalse(TEXT("Invalid owned job values fail encoding"), Invalid.Encode(Bytes));
		TestTrue(TEXT("Invalid job encoding preserves previously committed bytes"), Bytes == TimedBytes);
	};
	auto Invalid = Timed; Invalid.Format = 2; RejectValue(Invalid);
	Invalid = Timed; Invalid.Format = 1; Invalid.Items.Reset(); RejectValue(Invalid);
	Invalid = Timed; Invalid.CraftingJobs.Add(Job); RejectValue(Invalid);
	Invalid = Timed; Invalid.CraftingJobs[0].Escrow[0].Instance = Invalid.Items[0].Instance; RejectValue(Invalid);
	Invalid = Timed; Invalid.CraftingJobs[0].Outputs.Add(Output); RejectValue(Invalid);
	Invalid = Timed; Invalid.CraftingJobs[0].DueUtcTicks = Job.StartedUtcTicks; RejectValue(Invalid);
	Invalid = Timed; Invalid.CraftingJobs[0].DueUtcTicks = Job.StartedUtcTicks + int64(ProjectJ::Crafting::MaxJobSeconds + 1) * ETimespan::TicksPerSecond; RejectValue(Invalid);
	Invalid = Timed; Invalid.CraftingJobs[0].Escrow.SetNum(ProjectJ::Crafting::MaxEscrowEntries + 1); RejectValue(Invalid);
	Invalid = Timed; Invalid.Items.SetNum(ProjectJ::Crafting::MaxOwnedEntries); RejectValue(Invalid);
	const auto RejectDocument = [&](TFunctionRef<void(FJsonObject&)> Mutate, const TArray<uint8>* Source = nullptr)
	{
		Restored = Timed; // Independent full-value sentinel, including when the old reader wrongly accepts a prior case.
		auto Document = ReadDocument(Source ? *Source : TimedBytes);
		if (!TestTrue(TEXT("Native malformed-job fixture parses"), Document.IsValid())) return;
		Mutate(*Document);
		TestFalse(TEXT("Malformed job JSON is rejected before publication"), FProject_JLocalProfileSnapshot::Decode(WriteDocument(Document), Restored));
		TArray<uint8> After;
		TestTrue(TEXT("Malformed job decode leaves the entire prior value unchanged"), Restored.Encode(After) && After == TimedBytes);
	};
	RejectDocument([](FJsonObject& D) { D.GetArrayField(TEXT("jobs"))[0]->AsObject()->SetNumberField(TEXT("due_utc_ticks"), 638000000000000000.0); });
	RejectDocument([](FJsonObject& D) { D.GetArrayField(TEXT("jobs"))[0]->AsObject()->SetStringField(TEXT("due_utc_ticks"), TEXT("0638000000100000001")); });
	RejectDocument([](FJsonObject& D) { D.GetArrayField(TEXT("jobs"))[0]->AsObject()->SetStringField(TEXT("due_utc_ticks"), TEXT("9223372036854775808")); });
	RejectDocument([](FJsonObject& D) { D.GetArrayField(TEXT("jobs"))[0]->AsObject()->SetStringField(TEXT("due_utc_ticks"), TEXT("-1")); });
	RejectDocument([](FJsonObject& D) { D.GetArrayField(TEXT("jobs"))[0]->AsObject()->SetNumberField(TEXT("schema"), 2); });
	RejectDocument([](FJsonObject& D) { D.GetArrayField(TEXT("jobs"))[0]->AsObject()->SetBoolField(TEXT("unknown_terms"), true); });
	RejectDocument([](FJsonObject& D) { auto Jobs = D.GetArrayField(TEXT("jobs")); const auto First = Jobs[0]; Jobs.Init(First, ProjectJ::Crafting::MaxJobs + 1); D.SetArrayField(TEXT("jobs"), Jobs); });
	RejectDocument([](FJsonObject& D) { auto J = D.GetArrayField(TEXT("jobs"))[0]->AsObject(); auto Escrow = J->GetArrayField(TEXT("escrow")); const auto First = Escrow[0]; Escrow.Init(First, ProjectJ::Crafting::MaxEscrowEntries + 1); J->SetArrayField(TEXT("escrow"), Escrow); });
	RejectDocument([](FJsonObject& D) { D.RemoveField(TEXT("jobs")); });
	RejectDocument([](FJsonObject& D) { D.SetNumberField(TEXT("format"), 2); });

	// Representative closed-format documents, including legacy quest states and
	// compound AND state v2, must remain readable and byte-stable after re-encode.
	TArray<TArray<uint8>> Shape128Documents;
	for (int32 Shape128Format = 1; Shape128Format <= 3; ++Shape128Format)
	{
		auto Shape128Snapshot = Timed; Shape128Snapshot.Format = Shape128Format;
		if (Shape128Format < 3) Shape128Snapshot.CraftingJobs.Reset();
		if (Shape128Format == 1) Shape128Snapshot.Items.Reset();
		else
		{
			Shape128Snapshot.Progression.SchemaVersion = 2; Shape128Snapshot.Quests.Version = 2;
			auto& Shape128State = Shape128Snapshot.Quests.States[0]; Shape128State.Count = 0;
			Shape128State.Objectives = {{TEXT("Gather"), 2}, {TEXT("Craft"), 1}};
		}
		TArray<uint8> Shape128Encoded, Shape128Reencoded;
		FProject_JLocalProfileSnapshot Shape128Restored;
		if (!TestTrue(TEXT("Canonical legacy/AND/job format round-trips without field loss"),
			Shape128Snapshot.Encode(Shape128Encoded) && FProject_JLocalProfileSnapshot::Decode(Shape128Encoded, Shape128Restored) &&
			Shape128Restored.Format == Shape128Format && Shape128Restored.Quests.Version == Shape128Snapshot.Quests.Version &&
			Shape128Restored.Encode(Shape128Reencoded) && Shape128Encoded == Shape128Reencoded)) return false;
		Shape128Documents.Add(MoveTemp(Shape128Encoded));
	}
	for (const auto& Shape128Bytes : Shape128Documents)
		RejectDocument([](FJsonObject& D) { D.SetNumberField(TEXT("future_account_currency"), 17); }, &Shape128Bytes);
	const auto& Shape128Compound = Shape128Documents.Last();
	RejectDocument([](FJsonObject& D) { D.GetObjectField(TEXT("progression"))->SetNumberField(TEXT("future_stat"), 17); }, &Shape128Compound);
	RejectDocument([](FJsonObject& D) { D.GetObjectField(TEXT("quests"))->SetBoolField(TEXT("future_season"), true); }, &Shape128Compound);
	RejectDocument([](FJsonObject& D) { D.GetObjectField(TEXT("quests"))->GetArrayField(TEXT("states"))[0]->AsObject()->SetBoolField(TEXT("future_branch"), true); }, &Shape128Compound);
	RejectDocument([](FJsonObject& D) { D.GetObjectField(TEXT("quests"))->GetArrayField(TEXT("states"))[0]->AsObject()->GetArrayField(TEXT("objectives"))[0]->AsObject()->SetNumberField(TEXT("future_reward"), 17); }, &Shape128Compound);
	// Keep the already strict ownership shapes under the same all-or-nothing boundary.
	RejectDocument([](FJsonObject& D) { D.GetArrayField(TEXT("items"))[0]->AsObject()->SetBoolField(TEXT("future_binding"), true); });
	RejectDocument([](FJsonObject& D) { D.GetArrayField(TEXT("jobs"))[0]->AsObject()->GetArrayField(TEXT("escrow"))[0]->AsObject()->SetBoolField(TEXT("future_binding"), true); });
	RejectDocument([](FJsonObject& D) { D.GetArrayField(TEXT("jobs"))[0]->AsObject()->GetArrayField(TEXT("outputs"))[0]->AsObject()->SetBoolField(TEXT("future_quality"), true); });
	TestTrue(TEXT("Empty current-format jobs are valid without changing the legacy writer"), Snapshot.CraftingJobs.IsEmpty() &&
		[&] { auto Empty = Snapshot; Empty.Format = 3; return Empty.Encode(Bytes) && FProject_JLocalProfileSnapshot::Decode(Bytes, Restored) && Restored.Format == 3 && Restored.CraftingJobs.IsEmpty(); }());

	// One value-only codec boundary: tracked=true -> false grows JSON by exactly one byte.
	// This is test data, not a larger production slot/byte budget or a GT save preflight.
	FProject_JLocalProfileSnapshot Boundary;
	Boundary.Account = A; Boundary.Character = C; Boundary.RulesFingerprint = FString::ChrN(40, 'A');
	Boundary.Quests.States.Add({TEXT("CapacityQuest"), 0, false, true});
	for (int32 Index = 0; Index < 512; ++Index)
	{
		FProject_JPersistentItem Value;
		Value.Instance = FGuid(1, 2, 3, Index + 1); Value.Definition = TEXT("P"); Value.Order = Index;
		Boundary.Items.Add(Value);
	}
	int32 Low = 0, High = Boundary.Items.Num();
	while (Low < High)
	{
		const int32 Middle = Low + (High - Low + 1) / 2;
		auto Probe = Boundary; Probe.Items.SetNum(Middle);
		if (Probe.Encode(Bytes)) Low = Middle;
		else High = Middle - 1;
	}
	Boundary.Items.SetNum(Low);
	if (!TestTrue(TEXT("Codec finds one bounded valid baseline"), Low > 0 && Boundary.Encode(Bytes))) return false;
	int32 Padding = 65536 - Bytes.Num();
	for (int32 Index = 0; Padding > 0 && Index < Boundary.Items.Num(); ++Index)
	{
		const int32 AddedBytes = FMath::Min(Padding, 127);
		Boundary.Items[Index].Definition = FName(*FString::ChrN(1 + AddedBytes, 'P'));
		Padding -= AddedBytes;
	}
	if (!TestTrue(TEXT("Valid value baseline reaches the existing exact byte ceiling"),
		Padding == 0 && Boundary.Encode(Bytes) && Bytes.Num() == 65536)) return false;
	const auto OriginalQuestValues = Boundary.Quests.States;
	const auto OriginalBytes = Bytes;
	auto ProposedTracking = Boundary;
	TestTrue(TEXT("Desired tracking plans on a captured value"),
		UProject_JQuestComponent::PlanTracking(TEXT("CapacityQuest"), false, ProposedTracking.Quests));
	TestFalse(TEXT("One-byte tracking growth is rejected before a value can commit"), ProposedTracking.Encode(Bytes));
	TestTrue(TEXT("Rejected proposed tracking does not change the original quest value"), Boundary.Quests.States == OriginalQuestValues);
	TestTrue(TEXT("The original bounded snapshot remains readable and byte-identical"),
		Boundary.Encode(Bytes) && Bytes == OriginalBytes && FProject_JLocalProfileSnapshot::Decode(Bytes, Restored));
	return true;
}

namespace
{
struct FQuestWriterBarrier
{
	FEvent* Entered = FPlatformProcess::GetSynchEventFromPool(true);
	FEvent* Release = FPlatformProcess::GetSynchEventFromPool(true);
	TAtomic<bool> TimedOut{false};
	bool bOriginalFailed = false, bOriginalFirst = true;
	FProject_JProfileDurableResult OriginalDurable;
	~FQuestWriterBarrier()
	{
		Release->Trigger();
		FPlatformProcess::ReturnSynchEventToPool(Entered);
		FPlatformProcess::ReturnSynchEventToPool(Release);
	}
};
struct FProfileLifecycle : IAutomationLatentCommand
{
	FAutomationTestBase *Test;
	TStrongObjectPtr<UGameInstance> GI;
	UWorld *World = nullptr;
	UProject_JLocalProfileSubsystem *Profiles = nullptr;
	TWeakObjectPtr<AProject_JPlayerState> Player;
	FString Phase, Database;
	int32 Step = 0;
	int32 SmallGrants = 0;
	TSharedPtr<FQuestWriterBarrier, ESPMode::ThreadSafe> QuestBarrier;
	bool bReverseProgressStarted = false;
	uint64 RegistryRefreshesAtOpen = 0;

	FGuid Potion, Gear, UseRequest, Split;
	float HealedHealth = 0;
	bool bItemsVerified = false;
	bool bOpenDone = false, bOpenSuccess = false, bBackupDone = false, bBackupSuccess = false;
	bool bShutdown = false;
	TFuture<bool> ShutdownRead;
	FString Backup;
	double Started = FPlatformTime::Seconds();
	FProfileLifecycle(FAutomationTestBase *InTest, UGameInstance *InGI, FString InPhase, FString InDatabase)
		: Test(InTest), GI(InGI), World(InGI->GetWorld()),
		  Profiles(InGI->GetSubsystem<UProject_JLocalProfileSubsystem>()), Phase(MoveTemp(InPhase)),
		  Database(MoveTemp(InDatabase))
	{
	}
	~FProfileLifecycle()
	{
		if (QuestBarrier) QuestBarrier->Release->Trigger();
		if (!bShutdown) Profiles->HoldCommandCompletionsForTest(false);
		if (World->HasBegunPlay())
			World->EndPlay(EEndPlayReason::Quit);
		if (!bShutdown)
			GI->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
	void Open()
	{
		auto *State = World->SpawnActor<AProject_JPlayerState>();
		Player = State;
		auto *Q = State->FindComponentByClass<UProject_JQuestComponent>();
		auto *Definition = NewObject<UProject_JQuestDefinition>(State);
		Definition->QuestId = TEXT("PersistenceQuest");
		Definition->ObjectiveEvent = TEXT("PersistenceEvent");
		Definition->RequiredCount = 2;
		Definition->RewardExperience = 17;
		Q->Definitions.Add(Definition);
		auto* ShutdownDefinition = NewObject<UProject_JQuestDefinition>(State);
		ShutdownDefinition->QuestId = TEXT("ShutdownProgressQuest");
		ShutdownDefinition->ObjectiveEvent = TEXT("ShutdownProgressEvent");
		ShutdownDefinition->RequiredCount = 4;
		Q->Definitions.Add(ShutdownDefinition);
		bOpenDone = bOpenSuccess = false;
		RegistryRefreshesAtOpen = GI->GetSubsystem<UProject_JCharacterDataSubsystem>()->GetRegistryStats().Refreshes;
		Profiles->OpenProfile(State, TEXT("AutomationProfile"), [this](bool Success) {
			bOpenDone = true;
			bOpenSuccess = Success;
		});
	}
	bool Check(bool Condition, const TCHAR *Message)
	{
		return Test->TestTrue(Message, Condition);
	}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 30)
		{
			Test->AddError(TEXT("Local profile lifecycle timed out"));
			return true;
		}
		if (Step == 4)
		{
			if (!ShutdownRead.IsReady())
				return false;
			Check(ShutdownRead.Get(), TEXT("Normal GameInstance shutdown drains the newest accepted checkpoint"));
			return true;
		}
		if (Step == 0)
		{
			Open();
			Step = 1;
			return false;
		}
		if (Step == 1)
		{
			if (!bOpenDone)
				return false;
			Check(GI->GetSubsystem<UProject_JCharacterDataSubsystem>()->GetRegistryStats().Refreshes == RegistryRefreshesAtOpen,
				TEXT("Actual profile open/reopen consumes the GI registry without a catalog rescan"));
			if (!Check(bOpenSuccess && Profiles->IsReady(Player.Get()),
					   TEXT("Actual subsystem finishes restoring before gameplay admission")))
				return true;
			auto *Duplicate = World->SpawnActor<AProject_JPlayerState>();
			bool DuplicateAccepted = true;
			Profiles->OpenProfile(Duplicate, TEXT("AutomationProfile"),
								  [&DuplicateAccepted](bool Success) { DuplicateAccepted = Success; });
			Check(!DuplicateAccepted && Profiles->IsReady(Player.Get()),
				  TEXT("Duplicate local binding is rejected without retiring the admitted player"));
			Duplicate->Destroy();
			auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>();
			auto *Q = Player->FindComponentByClass<UProject_JQuestComponent>();
			if (Phase == TEXT("Read") || Phase == TEXT("Reopen"))
			{
				const auto Items = Player->GetInventoryComponent()->GetItemInstances();
				FProject_JItemInstanceData Equipped;
				Check(Items.Num() == 2 && Items.ContainsByPredicate([](const auto &V) {
					return Cast<UProject_JConsumableDefinition>(V.ItemDef) && V.StackCount == 2;
				}) &&
						  Player->GetEquipmentManagerComponent()->GetEquippedItemInstance(
							  EProject_JEquipmentSlot::Weapon, Equipped) &&
						  Equipped.bIsEquipped && Equipped.bIsLocked &&
						  Items.ContainsByPredicate([&](const auto &V) { return V.InstanceId == Equipped.InstanceId; }),
					  TEXT("Consumed inventory, stable instance and equipped ownership restore together"));
				Check(P->GetExperience() == 74 && P->GetState().Level == 1,
					  TEXT("New session restores the last committed XP"));
				Check(Q->Claim(TEXT("PersistenceQuest")) && !Profiles->IsCheckpointPending(Player.Get()) &&
						  P->GetExperience() == 74,
					  TEXT("Restored claim reports durable claimed state without a new receipt or reward"));
				Check(Q->HasCompleted(TEXT("PersistenceQuest")) && !Q->States.ContainsByPredicate([](const auto& S) { return S.QuestId == TEXT("PersistenceQuest"); }),
					  TEXT("XP and claimed state restore together"));
				Step = 20;
				return false;
			}
			Player->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(Player.Get(), nullptr);
			P->InitializeDefaults(nullptr, nullptr, 1);
			Step = 10;
			return false;
		}
		if (Step == 20)
		{
			if (Profiles->IsCheckpointPending(Player.Get()))
				return false;
			auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>();
			Check(Profiles->IsReady(Player.Get()) && P->GetExperience() == 74,
				  TEXT("Durable claim replay restores state without duplicate XP"));
			if (Phase == TEXT("Reopen"))
			{
				Check(Player->FindComponentByClass<UProject_JQuestComponent>()->States.ContainsByPredicate([](const auto& State) {
					return State.QuestId == TEXT("ShutdownProgressQuest") && State.Count == 2;
				}), TEXT("Reopened progress objective retains its previously confirmed counters"));
				Step = 21;
				return false;
			}
			// Identity retirement must prevent later callbacks from writing to the old profile.
			const auto Character = Player->GetCharacterId();
			Player->SetIdentity(Player->GetAccountId(), FProject_JCharacterId::NewId());
			Check(!Profiles->IsReady(Player.Get()), TEXT("Identity replacement immediately revokes the local binding"));
			P->GrantExperience(1);
			Profiles->RequestCheckpoint(Player.Get());
			Check(!Profiles->IsCheckpointPending(Player.Get()), TEXT("Retired identity cannot enqueue another write"));
			return true;
		}
		if (Step == 21)
		{
			if (Profiles->IsCheckpointPending(Player.Get())) return false;
			QuestBarrier = MakeShared<FQuestWriterBarrier, ESPMode::ThreadSafe>();
			Profiles->SetCommandStageHookForTest(Player.Get(), [Barrier = QuestBarrier](const TCHAR* Point) {
				if (FCString::Strcmp(Point, TEXT("BeforeCommandEncode")) == 0)
				{
					Barrier->Entered->Trigger();
					if (!Barrier->Release->Wait(5000)) Barrier->TimedOut.Store(true);
				}
			});
			if (!Check(Player->FindComponentByClass<UProject_JProgressionComponent>()->GrantExperience(1),
				TEXT("Final native primary is admitted before normal shutdown"))) return true;
			Step = 22;
			return false;
		}
		if (Step == 22)
		{
			if (!QuestBarrier->Entered->Wait(0)) return false;
			auto* Quest = Player->FindComponentByClass<UProject_JQuestComponent>();
			const bool FirstAccepted = Quest->TryRecordObjective(TEXT("ShutdownProgressEvent"), 1);
			const bool SecondAccepted = Quest->TryRecordObjective(TEXT("ShutdownProgressEvent"), 1);
			QuestBarrier->Release->Trigger();
			if (!Check(!QuestBarrier->TimedOut.Load() && FirstAccepted && SecondAccepted,
				TEXT("Normal shutdown starts with native-owned accepted pending progress"))) return true;
			const FGuid Id = Player->GetCharacterId().Value;
			World->EndPlay(EEndPlayReason::Quit);
			GI->Shutdown();
			bShutdown = true;
			ShutdownRead = Async(EAsyncExecution::ThreadPool, [File = Database, Id]() {
				FProject_JSQLiteRepository Repository(File);
				FRecord Record;
				FProject_JLocalProfileSnapshot Snapshot;
                const bool ReadCore = Repository.Read({TEXT("LocalProfile"), Id}, Record);
                const auto Values = ReadCore ? ProjectJ::LocalProfileHistory::ReadProfileValues(Repository, Id, Record.Payload) : ProjectJ::LocalProfileHistory::FProfileValues();
                if (Values) Snapshot = *Values;
                const bool Success = Values && Snapshot.Progression.Experience == 75 && Snapshot.Format == 4 &&
					Snapshot.Quests.States.ContainsByPredicate([](const auto& State) {
						return State.QuestId == TEXT("ShutdownProgressQuest") && State.Count == 4;
					});
				Repository.Close();
				return Success;
			});
			Step = 4;
			return false;
		}
		if (Step >= 10 && Step <= 19)
		{
			if (Step == 17)
			{
				if (!QuestBarrier->Entered->Wait(0)) return false;
				if (!Check(!QuestBarrier->TimedOut.Load(), TEXT("Native encode barrier entered before its bounded timeout")))
				{ QuestBarrier->Release->Trigger(); return true; }
				auto* Quest = Player->FindComponentByClass<UProject_JQuestComponent>();
				const bool FirstAccepted = Quest->TryRecordObjective(TEXT("PersistenceEvent"), 1);
				const bool SecondAccepted = Quest->TryRecordObjective(TEXT("PersistenceEvent"), 1);
				QuestBarrier->Release->Trigger();
				if (!Check(FirstAccepted && SecondAccepted && Quest->States.ContainsByPredicate([](const auto& State) {
					return State.QuestId == TEXT("PersistenceQuest") && State.Count == 0;
				}), TEXT("Two actual native-current events are accepted without live mutation"))) return true;
				Step = 18;
				return false;
			}
			if (Step != 18 && Profiles->IsCheckpointPending(Player.Get())) return false;
			auto *P = Player->FindComponentByClass<UProject_JProgressionComponent>();
			auto *Q = Player->FindComponentByClass<UProject_JQuestComponent>();
			if (Step == 10)
			{
				Check(P->GrantExperience(50) && P->GetExperience() == 0,
					  TEXT("Existing XP entry admits a native commit before publication"));
				Check(!P->GrantExperience(1), TEXT("A competing command is rejected while one commit is pending"));
				Step = 11;
				return false;
			}
			if (Step == 11)
			{
				Check(P->GetExperience() == 50, TEXT("Confirmed native XP command publishes once"));
				Check(Q->Accept(TEXT("PersistenceQuest")) && Profiles->IsCheckpointPending(Player.Get()) &&
					  !Q->States.ContainsByPredicate([](const auto& S) { return S.QuestId == TEXT("PersistenceQuest"); }),
					  TEXT("Legacy acceptance admits a command before publishing live quest state"));
				Step = 15;
				return false;
			}
			if (Step == 15)
			{
				Check(Q->States.ContainsByPredicate([](const auto& S) {
					return S.QuestId == TEXT("PersistenceQuest") && S.Count == 0 && S.bTracked && !S.bClaimed;
				}), TEXT("Legacy acceptance is installed after commit before progress producers run"));
				Check(!Q->Accept(TEXT("PersistenceQuest")) && !Profiles->IsCheckpointPending(Player.Get()),
					  TEXT("Repeated committed acceptance does not issue another command"));
				Q->RequestTracking(TEXT("PersistenceQuest"), false);
				Check(Profiles->IsCheckpointPending(Player.Get()) && Q->States.ContainsByPredicate([](const auto& S) {
					return S.QuestId == TEXT("PersistenceQuest") && S.bTracked;
				}), TEXT("Tracking admission preserves the currently committed UI value"));
				Step = 16;
				return false;
			}
			if (Step == 16)
			{
				Check(Q->States.ContainsByPredicate([](const auto& S) {
					return S.QuestId == TEXT("PersistenceQuest") && !S.bTracked && !S.bClaimed;
				}), TEXT("Tracking installs only the confirmed desired value"));
				Q->RequestTracking(TEXT("PersistenceQuest"), false);
				Check(!Profiles->IsCheckpointPending(Player.Get()), TEXT("Unchanged tracking does not issue another command"));
				if (!Check(Q->Accept(TEXT("ShutdownProgressQuest")), TEXT("Reverse projection objective acceptance is admitted"))) return true;
				Step = 19;
				return false;
			}
			if (Step == 19)
			{
				QuestBarrier = MakeShared<FQuestWriterBarrier, ESPMode::ThreadSafe>();
				Profiles->SetCommandStageHookForTest(Player.Get(), [Barrier = QuestBarrier](const TCHAR* Point) {
					if (FCString::Strcmp(Point, TEXT("BeforeCommandEncode")) == 0)
					{
						Barrier->Entered->Trigger();
						if (!Barrier->Release->Wait(5000)) Barrier->TimedOut.Store(true);
					}
				});
				Profiles->LoseNextCommitAcknowledgementForTest(Player.Get());
				Profiles->HoldCommandCompletionsForTest(true);
				FProject_JPersistentEdit InvalidTracking;
				InvalidTracking.Kind = EProject_JPersistentEdit::SetQuestTracking;
				InvalidTracking.Quest = TEXT("PersistenceQuest");
				InvalidTracking.Amount = 0; // Already false: a new request has an invalid/no-change plan.
				InvalidTracking.Request = Profiles->NewRequestId(Player.Get());
				if (!Check(Profiles->SubmitEdit(Player.Get(), InvalidTracking,
					[Barrier = QuestBarrier](bool Confirmed, bool First) { Barrier->bOriginalFailed = !Confirmed; Barrier->bOriginalFirst = First; },
					[Barrier = QuestBarrier](const FProject_JProfileDurableResult& Result) { Barrier->OriginalDurable = Result; }),
					TEXT("Invalid primary plan is still admitted for receipt reconciliation")))
				{ QuestBarrier->Release->Trigger(); return true; }
				Step = 17;
				return false;
			}
			if (Step == 18)
			{
				if (!Profiles->GetHeldCompletionCountForTest()) return false;
				if (!bReverseProgressStarted)
				{
					if (!Check(Q->TryRecordObjective(TEXT("ShutdownProgressEvent"), 2), TEXT("Observer-delivered primary admits a newer owned progress job"))) return true;
					bReverseProgressStarted = true;
					return false;
				}
				if (!Q->States.ContainsByPredicate([](const auto& State) { return State.QuestId == TEXT("ShutdownProgressQuest") && State.Count == 2; })) return false;
				Profiles->HoldCommandCompletionsForTest(false);
				Profiles->SetCommandStageHookForTest(Player.Get(), {});
				Check(QuestBarrier->bOriginalFailed && !QuestBarrier->bOriginalFirst &&
					QuestBarrier->OriginalDurable.StoreStatus == ProjectJ::MMO::ECommitStatus::Invalid &&
					QuestBarrier->OriginalDurable.Outcome == EProject_JProfileDurableOutcome::ProvenAbsent,
					TEXT("Primary Invalid callback/durable/First stay failed independently of a successful tail"));
				Check(Q->States.ContainsByPredicate([](const auto& State) {
					return State.QuestId == TEXT("PersistenceQuest") && State.Count == 2 && !State.bTracked;
				}), TEXT("Accepted coalesced tail survives lost acknowledgement and applies exactly two events"));
				Check(Q->States.ContainsByPredicate([](const auto& State) { return State.QuestId == TEXT("ShutdownProgressQuest") && State.Count == 2; }),
					TEXT("Older primary projection is skipped while its pending/callback cleanup still completes"));
				const int64 NoopRevision = Profiles->GetCommittedRevision(Player.Get());
				Check(Q->TryRecordObjective(TEXT("PersistenceEvent"), 1) && Q->TryRecordObjective(TEXT("NoAcceptedObjective"), 1) &&
					!Profiles->IsCheckpointPending(Player.Get()) && Profiles->GetCommittedRevision(Player.Get()) == NoopRevision,
					TEXT("Completed/absent goals are successful no-ops without a job or receipt"));
				Step = 12;
				return false;
			}
			if (Step == 12)
			{
				Check(Q->Claim(TEXT("PersistenceQuest")) && P->GetExperience() == 50,
					  TEXT("Claim and XP remain unchanged before durable confirmation"));
				Step = 13;
				return false;
			}
			if (Step == 13)
			{
				Check(P->GetExperience() == 67 && Q->HasCompleted(TEXT("PersistenceQuest")) &&
                    !Q->States.ContainsByPredicate([](const auto& S) { return S.QuestId == TEXT("PersistenceQuest"); }),
					  TEXT("Claim and XP publish together after atomic commit"));
				Step = 14;
			}
			if (SmallGrants < 7)
			{
				Check(P->GrantExperience(1) && P->GetExperience() == 67 + SmallGrants,
					  TEXT("Each accepted trusted grant waits for disk before publication"));
				++SmallGrants;
				return false;
			}
			Check(P->GetExperience() == 74, TEXT("All confirmed grants retained"));
			Step = 2;
			return false;
		}
		if (Step == 2)
		{
			if (Profiles->IsCheckpointPending(Player.Get()))
				return false;
			if (!Check(Profiles->IsReady(Player.Get()) && Profiles->GetCommittedRevision(Player.Get()) > 0,
					   TEXT("Save is acknowledged only after native commit")))
				return true;
			if (!bItemsVerified)
			{
				auto *Def = LoadObject<UProject_JConsumableDefinition>(
					nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"));
				Check(Def && Def->RestoreHealth > 0, TEXT("Actual authored consumable resolves"));
				Check(!Player->GetInventoryComponent()->AddItemDefinition(Def, 3).IsValid() &&
						  Player->GetInventoryComponent()->GetItemInstances().IsEmpty() &&
						  Profiles->IsCheckpointPending(Player.Get()),
					  TEXT("Existing AddItem entry publishes ownership only after disk commit"));
				Step = 30;
				return false;
			}
			Profiles->RequestBackup([this](bool Success, FString Path) {
				bBackupDone = true;
				bBackupSuccess = Success;
				Backup = MoveTemp(Path);
			});
			Step = 3;
			return false;
		}
		if (Step >= 30 && Step <= 40)
		{
			if (Step != 38 && Profiles->IsCheckpointPending(Player.Get()))
				return false;
			if (!Check(Profiles->IsReady(Player.Get()), TEXT("Persistent item command remains ready")))
				return true;
			auto *I = Player->GetInventoryComponent();
			auto *E = Player->GetEquipmentManagerComponent();
			auto *ASC = Player->GetProjectJAbilitySystemComponent();
			const auto Items = I->GetItemInstances();
			FProject_JItemInstanceData Item;
			if (Step == 30)
			{
				if (!Check(Items.Num() == 1 && Items[0].StackCount == 3,
						   TEXT("Confirmed AddItem publishes exact authored stack")))
					return true;
				Potion = Items[0].InstanceId;
				auto *Def = LoadObject<UProject_JEquipmentItemDefinition>(
					nullptr,
					TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.DA_Greatsword_Equip"));
				Check(Def && Def->EquipmentSlot == EProject_JEquipmentSlot::Weapon,
					  TEXT("Actual authored equipment resolves"));
				I->AddItemDefinition(Def);
				Step = 31;
				return false;
			}
			if (Step == 31)
			{
				const auto *FoundGear = Items.FindByPredicate(
					[](const auto &V) { return Cast<UProject_JEquipmentItemDefinition>(V.ItemDef); });
				if (!Check(FoundGear != nullptr, TEXT("Equipment ownership is committed")))
					return true;
				Gear = FoundGear->InstanceId;
				E->RequestEquipmentChange(E->NewRequestId(), Gear, EProject_JEquipmentSlot::Weapon, false);
				Check(!E->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon),
					  TEXT("Equip request does not publish before commit"));
				Step = 32;
				return false;
			}
			if (Step == 32)
			{
				Check(E->GetEquippedItemInstance(EProject_JEquipmentSlot::Weapon, Item) && Item.InstanceId == Gear &&
						  I->FindItemInstance(Gear, Item) && Item.bIsEquipped && Item.bIsLocked,
					  TEXT("Slot and inventory lock publish coherently"));
				ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 100);
				ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxManaAttribute(), 100);
				ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 40);
				ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetManaAttribute(), 100);
				UseRequest = I->NewRequestId();
				Profiles->LoseNextCommitAcknowledgementForTest(Player.Get());
				I->RequestUseItem(UseRequest, Potion);
				Check(I->FindItemInstance(Potion, Item) && Item.StackCount == 3 &&
						  ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) == 40,
					  TEXT("Consumption and healing wait for durable acknowledgement"));
				Step = 33;
				return false;
			}
			if (Step == 33)
			{
				HealedHealth = ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
				Check(I->FindItemInstance(Potion, Item) && Item.StackCount == 2 && HealedHealth > 40,
					  TEXT("Lost acknowledgement reconciles same key and applies consumption/heal once"));
				I->RequestUseItem(UseRequest, Potion);
				Step = 34;
				return false;
			}
			if (Step == 34)
			{
				Check(I->FindItemInstance(Potion, Item) && Item.StackCount == 2 &&
						  ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) == HealedHealth,
					  TEXT("Same use request replays without consuming/healing twice"));
				I->RequestUseItem(UseRequest, Gear);
				Step = 35;
				return false;
			}
			if (Step == 35)
			{
				Check(I->FindItemInstance(Potion, Item) && Item.StackCount == 2 &&
						  E->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon),
					  TEXT("Same request with a different item is rejected without mutation"));
				FProject_JBagRequest R;
				R.Source = Potion;
				R.Count = 1;
				R.ExpectedSourceCount = 2;
				R.ExpectedSourceOrder = Item.BagOrder;
				I->RequestBagChange(I->NewRequestId(), R);
				Step = 36;
				return false;
			}
			if (Step == 36)
			{
				Check(Items.Num() == 3 && I->FindItemInstance(Potion, Item) && Item.StackCount == 1,
					  TEXT("Persistent split preserves both stacks"));
				const auto *New = Items.FindByPredicate(
					[&](const auto &V) { return V.InstanceId != Potion && V.InstanceId != Gear; });
				if (!Check(New != nullptr, TEXT("Split creates one stable instance")))
					return true;
				Split = New->InstanceId;
				FProject_JBagRequest R;
				R.Operation = EProject_JBagOperation::Merge;
				R.Source = Split;
				R.Target = Potion;
				R.Count = 1;
				R.ExpectedSourceCount = 1;
				R.ExpectedTargetCount = 1;
				R.ExpectedSourceOrder = New->BagOrder;
				R.ExpectedTargetOrder = Item.BagOrder;
				I->RequestBagChange(I->NewRequestId(), R);
				Step = 37;
				return false;
			}
			if (Step == 37)
			{
				Check(Items.Num() == 2 && I->FindItemInstance(Potion, Item) && Item.StackCount == 2 &&
						  !I->HasItemInstance(Split),
					  TEXT("Persistent merge consumes the source exactly once"));
				// UWorld clamps a large delta. Advance the real test world in bounded
				// ticks until the authored cooldown actually expires; never reset it.
				World->Tick(LEVELTICK_All, .1f);
				if (I->GetUseCooldownRemaining() > 0)
					return false;
				ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 40);
				UseRequest = I->NewRequestId();
				Profiles->HoldCommandCompletionsForTest(true);
				I->RequestUseItem(UseRequest, Potion);
				Step = 38;
				return false;
			}
			if (Step == 38)
			{
				if (!Profiles->GetHeldCompletionCountForTest())
					return false;
				const uint64 Previous = ASC->GetActorInfoRevision();
				ASC->ClearActorInfo();
				ASC->InitAbilityActorInfo(Player.Get(), nullptr);
				Profiles->HoldCommandCompletionsForTest(false);
				Test->TestEqual(TEXT("Retired recipient health stays unchanged"),
								ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), 40.f);
				Test->TestEqual(TEXT("Retired recipient cost remains committed"),
								I->FindItemInstance(Potion, Item) ? Item.StackCount : INDEX_NONE, 1);
				Check(ASC->GetActorInfoRevision() != Previous && I->FindItemInstance(Potion, Item) &&
						  Item.StackCount == 1 &&
						  ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) == 40,
					  TEXT("Late consumption completion cannot heal a replaced actor-info binding even when "
						   "owner/avatar pointers match"));
				I->RequestUseItem(UseRequest, Potion);
				Step = 39;
				return false;
			}
			if (Step == 39)
			{
				Check(I->FindItemInstance(Potion, Item) && Item.StackCount == 1 &&
						  ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) == 40,
					  TEXT("Reconciliation of a retired recipient never applies its recovery twice"));
				Check(I->SetItemStackCount(Potion, 2) && I->FindItemInstance(Potion, Item) && Item.StackCount == 1,
					  TEXT("Trusted replenishment uses the same durable boundary"));
				Step = 40;
				return false;
			}
			Check(I->FindItemInstance(Potion, Item) && Item.StackCount == 2,
				  TEXT("Replenishment is confirmed before restart"));
			bItemsVerified = true;
			Step = 2;
			return false;
		}
		if (!bBackupDone)
			return false;
		Check(bBackupSuccess && IFileManager::Get().FileExists(*Backup),
			  TEXT("Actual backup consumer produces a verified new file"));
		Profiles->Detach(Player.Get());
		Player->Destroy();
		if (Phase == TEXT("Write"))
			return true;
		Phase = TEXT("Reopen");
		Open();
		Step = 1;
		return false;
	}
};
struct FSchema128Seed
{
	bool bCommitted = false, bMigrationRejected = false;
	int64 Revision = 0;
	TArray<uint8> Payload;
};
/** Reuse the existing real profile open/definitions and normal teardown. */
struct FProfileSchema128Preservation : FProfileLifecycle
{
	TFuture<FSchema128Seed> Schema128Seed;
	TFuture<bool> Schema128Read;
	FSchema128Seed Schema128Expected;
	FProfileSchema128Preservation(FAutomationTestBase* InTest, UGameInstance* InGI, FString InDatabase)
		: FProfileLifecycle(InTest, InGI, TEXT("Schema128"), MoveTemp(InDatabase)) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 30) { Test->AddError(TEXT("Unknown-field profile fixture timed out")); return true; }
		if (Step == 0) { Open(); Step = 1; return false; }
		if (Step == 1)
		{
			if (!bOpenDone || Profiles->IsCheckpointPending(Player.Get())) return false;
			if (!Check(bOpenSuccess && Profiles->IsReady(Player.Get()), TEXT("Canonical control profile opens through the real subsystem"))) return true;
			Player->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(Player.Get(), nullptr);
			Player->FindComponentByClass<UProject_JProgressionComponent>()->InitializeDefaults(nullptr, nullptr, 1);
			Step = 2; return false;
		}
		if (Step == 2)
		{
			if (Profiles->IsCheckpointPending(Player.Get())) return false;
			if (!Check(Player->FindComponentByClass<UProject_JProgressionComponent>()->GrantExperience(19),
				TEXT("Unknown-field control obtains XP through the actual public durable mutation"))) return true;
			Step = 3; return false;
		}
		if (Step == 3)
		{
			if (Profiles->IsCheckpointPending(Player.Get())) return false;
			if (!Check(Profiles->IsReady(Player.Get()) && Player->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 19,
				TEXT("The real durable control confirms XP19 before adding only an unknown field"))) return true;
			const FGuid Schema128Character = Player->GetCharacterId().Value;
			Profiles->Detach(Player.Get()); Player->Destroy();
			Schema128Seed = Async(EAsyncExecution::ThreadPool, [Schema128File = Database, Schema128Character]
			{
				FSchema128Seed Schema128Result;
				// The original public-commit payload must also pass offline inspection.
				if (!FProject_JProfileMigration::Inspect(Schema128File).IsValid()) return Schema128Result;
				FProject_JSQLiteRepository Schema128DB(Schema128File);
				FRecord Schema128Record; FProject_JLocalProfileSnapshot Schema128Snapshot;
				const FAggregateKey Schema128Key{TEXT("LocalProfile"), Schema128Character};
				TArray<uint8> Schema128Canonical;
				if (Schema128DB.Read(Schema128Key, Schema128Record) && FProject_JLocalProfileSnapshot::Decode(Schema128Record.Payload, Schema128Snapshot) &&
					Schema128Snapshot.Progression.Experience == 19 && Schema128Snapshot.Encode(Schema128Canonical) && Schema128Canonical == Schema128Record.Payload)
				{
					FUTF8ToTCHAR Schema128Text(reinterpret_cast<const ANSICHAR*>(Schema128Canonical.GetData()), Schema128Canonical.Num());
					TSharedPtr<FJsonObject> Schema128Document;
					if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Schema128Text.Length(), Schema128Text.Get())), Schema128Document) && Schema128Document.IsValid())
					{
						Schema128Document->SetNumberField(TEXT("future_account_currency"), 17);
						FString Schema128JSON;
						if (FJsonSerializer::Serialize(Schema128Document.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Schema128JSON)))
						{
							FTCHARToUTF8 Schema128UTF8(*Schema128JSON);
							Schema128Result.Payload.Append(reinterpret_cast<const uint8*>(Schema128UTF8.Get()), Schema128UTF8.Length());
							Schema128Result.bCommitted = Schema128DB.Commit({FGuid::NewGuid(), TEXT("OwnedUnknownFieldFixture"),
								{{Schema128Key, Schema128Record.Revision, 1, Schema128Result.Payload, false}}}).Status == ECommitStatus::Committed;
							Schema128Result.Revision = Schema128Record.Revision + 1;
						}
					}
				}
				Schema128DB.Close();
				Schema128Result.bMigrationRejected = !FProject_JProfileMigration::Inspect(Schema128File).IsValid();
				return Schema128Result;
			});
			Step = 4; return false;
		}
		if (Step == 4)
		{
			if (!Schema128Seed.IsReady()) return false;
			Schema128Expected = Schema128Seed.Get();
			if (!Check(Schema128Expected.bCommitted && Schema128Expected.bMigrationRejected,
				TEXT("Only the unknown field changes a valid control into an offline-rejected scratch record"))) return true;
			Open(); Step = 5; return false;
		}
		if (Step == 5)
		{
			if (!bOpenDone || Profiles->IsCheckpointPending(Player.Get())) return false;
			Check(!bOpenSuccess && !Profiles->IsReady(Player.Get()), TEXT("The actual profile loader rejects the same unknown shape as offline migration"));
			auto* Schema128Progress = Player->FindComponentByClass<UProject_JProgressionComponent>();
			Check(Schema128Progress->GetExperience() == 0 && Player->GetInventoryComponent()->GetItemInstances().IsEmpty() &&
				Player->GetEquipmentManagerComponent()->GetPrivateEquipmentCount() == 0,
				TEXT("Rejected schema installs no saved XP19 or partial ownership"));
			Player->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(Player.Get(), nullptr);
			const int32 Schema128JobsBefore = Profiles->GetNativeJobCountForTest();
			FProject_JPersistentEdit Schema128Edit; Schema128Edit.Kind = EProject_JPersistentEdit::Experience;
			Schema128Edit.Request = FGuid::NewGuid(); Schema128Edit.Amount = 1;
			Check(!Profiles->SubmitEdit(Player.Get(), Schema128Edit) && !Schema128Progress->GrantExperience(1),
				TEXT("Rejected profile declines both a direct persistent edit and the public progression consumer"));
			Profiles->RequestCheckpoint(Player.Get());
			Check(Profiles->GetNativeJobCountForTest() == Schema128JobsBefore && !Profiles->IsCheckpointPending(Player.Get()),
				TEXT("Rejected profile admits no command or checkpoint work"));
			Step = 6; return false;
		}
		if (Step == 6)
		{
			if (Profiles->IsCheckpointPending(Player.Get())) return false;
			const FGuid Schema128Character = Player->GetCharacterId().Value;
			Profiles->Detach(Player.Get());
			Schema128Read = Async(EAsyncExecution::ThreadPool, [Schema128File = Database, Schema128Character, Schema128Saved = Schema128Expected]
			{
				FProject_JSQLiteRepository Schema128DB(Schema128File); FRecord Schema128Record;
				const bool Schema128Preserved = Schema128DB.Read({TEXT("LocalProfile"), Schema128Character}, Schema128Record) &&
					!Schema128Record.bDeleted && Schema128Record.Revision == Schema128Saved.Revision && Schema128Record.Payload == Schema128Saved.Payload;
				Schema128DB.Close();
				return Schema128Preserved && !FProject_JProfileMigration::Inspect(Schema128File).IsValid();
			});
			Step = 7; return false;
		}
		if (!Schema128Read.IsReady()) return false;
		Check(Schema128Read.Get(), TEXT("Failed open and refused mutations preserve exact SQLite payload/revision and offline rejection"));
		return true;
	}
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLocalProfileLifecycleTest, "ProjectJ.MMO.Persistence.LocalProfileLifecycle",
								 PersistenceTestFlags)
bool FProjectJLocalProfileLifecycleTest::RunTest(const FString &)
{
	FString Phase;
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJProfilePhase="), Phase);
	if (!Phase.IsEmpty() && Phase != TEXT("Write") && Phase != TEXT("Read"))
	{
		AddError(TEXT("Unknown profile fixture phase"));
		return false;
	}
	FString Directory = TestDirectory();
	if (!Phase.IsEmpty())
		Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() /
													  TEXT("Validation/LocalProfileProcessRestart"));
	const FString Database = Directory / TEXT("Profiles.sqlite");
	if (Phase == TEXT("Write") && IFileManager::Get().FileExists(*Database))
	{
		AddError(TEXT("Preserve existing process-restart fixture; use a fresh validation run directory"));
		return false;
	}
	auto *GI = NewObject<UGameInstance>(GEngine);
	GI->InitializeStandalone();
	auto *World = GI->GetWorld();
	auto *Profiles = GI->GetSubsystem<UProject_JLocalProfileSubsystem>();
	if (!TestNotNull(TEXT("Real GameInstance owns the persistence adapter"), Profiles))
		return false;
	Profiles->DatabasePath = Database;
	auto *Definitions = GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
	TestTrue(TEXT("Existing registry explicitly loads authored PrimaryAssets"),
			 Definitions && Definitions->LoadPrimaryAssetDefinitions());
	auto *AuthoredClass = LoadObject<UProject_JCharacterClassDefinition>(
		nullptr, TEXT("/Game/DataAssetSets/Class/DA_Class_Greatsword.DA_Class_Greatsword"));
	TestTrue(TEXT("Actual default character class resolves through the same saved-ID registry"),
			 AuthoredClass && Definitions->GetClassDefinition(AuthoredClass->ClassId) == AuthoredClass);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	ADD_LATENT_AUTOMATION_COMMAND(FProfileLifecycle(this, GI, Phase, Database));
	return true;
}

namespace
{
struct FJobProfileRestore : IAutomationLatentCommand
{
	struct FJob
	{
		const TCHAR* Name;
		const TCHAR* Weapon;
		const TCHAR* Pawn;
	};
	const FJob Jobs[2] = {
		{TEXT("GreatSword"), TEXT("Greatsword.Starter"), TEXT("BP_Greatsword_C")},
		{TEXT("Spellblade"), TEXT("Spellblade.Starter"), TEXT("BP_Spellblade_C")}
	};
	FAutomationTestBase* Test;
	TStrongObjectPtr<UGameInstance> GI;
	UProject_JLocalProfileSubsystem* Profiles;
	AProject_JPlayerState* Player = nullptr;
	AProject_JGameMode* Mode;
	TSharedPtr<TOptional<bool>> OpenResult;
	int32 Index = 0;
	int32 Step = 0;
	double Started = FPlatformTime::Seconds();

	FJobProfileRestore(FAutomationTestBase* InTest, UGameInstance* InGI, AProject_JGameMode* InMode)
		: Test(InTest), GI(InGI), Profiles(InGI->GetSubsystem<UProject_JLocalProfileSubsystem>()), Mode(InMode) {}
	~FJobProfileRestore()
	{
		auto* World = GI->GetWorld();
		GI->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
	void Open(const TCHAR* Name)
	{
		Player = GI->GetWorld()->SpawnActor<AProject_JPlayerState>();
		OpenResult = MakeShared<TOptional<bool>>();
		Profiles->OpenProfile(Player, Name, [Result = OpenResult](bool bOK) { *Result = bOK; });
	}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 25)
		{
			Test->AddError(TEXT("Job profile restore fixture timed out"));
			return true;
		}
		const auto& Job = Jobs[Index];
		if (Step == 0) { Open(Job.Name); Step = 1; return false; }
		if (!OpenResult->IsSet()) return false;
		if (!Test->TestTrue(TEXT("Profile admitted"), OpenResult->GetValue() && Profiles->IsReady(Player))) return true;
		if (Profiles->IsCheckpointPending(Player)) return false;
		auto* Data = GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
		auto* Class = Data->GetClassDefinition(Job.Name);
		auto* Item = Data->GetItemDefinition(Job.Weapon, 1);
		if (!Test->TestNotNull(TEXT("Authored class"), Class) || !Test->TestNotNull(TEXT("Authored starter weapon"), Item)) return true;
		auto* Progression = Player->FindComponentByClass<UProject_JProgressionComponent>();
		auto* Inventory = Player->GetInventoryComponent();
		auto* Equipment = Player->GetEquipmentManagerComponent();
		if (Step == 1)
		{
			Player->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(Player, nullptr);
			Progression->InitializeDefaults(Class, nullptr, 1);
			Step = 2;
		}
		else if (Step == 2) { Inventory->AddItemDefinition(Item); Step = 3; }
		else if (Step == 3)
		{
			const auto Items = Inventory->GetItemInstances();
			if (!Test->TestTrue(TEXT("Starter weapon persisted in inventory"), Items.Num() == 1 && Items[0].ItemDef == Item)) return true;
			Equipment->RequestEquipmentChange(Equipment->NewRequestId(), Items[0].InstanceId, EProject_JEquipmentSlot::Weapon, false);
			Step = 4;
		}
		else if (Step == 4)
		{
			if (!Test->TestTrue(TEXT("Starter weapon committed"), Equipment->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon) == Item)) return true;
			Profiles->Detach(Player);
			Player->Destroy();
			Open(Job.Name);
			Step = 5;
		}
		else
		{
			Test->TestTrue(TEXT("Reopened SQLite restores the exact job"), Progression->GetState().ClassDefinition == Class);
			Test->TestTrue(TEXT("Reopened SQLite restores the exact equipped weapon"), Equipment->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon) == Item);
			auto* Controller = GI->GetWorld()->SpawnActor<APlayerController>();
			Player->SetOwner(Controller);
			Controller->SetPlayerState(Player);
			auto* Pawn = Mode->SpawnDefaultPawnAtTransform(Controller, FTransform(FVector(Index * 1000, 0, 400)));
			Test->TestTrue(TEXT("Restored job spawns its own authored pawn"), Pawn && Pawn->GetClass()->GetFName() == FName(Job.Pawn));
			Step = 0;
			return ++Index == UE_ARRAY_COUNT(Jobs);
		}
		return false;
	}
};

struct FProfileProgressAdmission : IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	TStrongObjectPtr<UGameInstance> GI;
	UWorld* World;
	UProject_JLocalProfileSubsystem* Profiles;
	TSharedPtr<FAggregateGate, ESPMode::ThreadSafe> Gate;
	TWeakObjectPtr<AProject_JPlayerState> Player;
	TArray<FGuid> Reservations;
	TSharedPtr<FQuestWriterBarrier, ESPMode::ThreadSafe> Barrier;
	bool bOpened = false, bOpenSuccess = false;
	int32 Step = 0;
	int64 BaseRevision = 0;
	double Started = FPlatformTime::Seconds();
	FProfileProgressAdmission(FAutomationTestBase* InTest, UGameInstance* InGI,
		TSharedPtr<FAggregateGate, ESPMode::ThreadSafe> InGate)
		: Test(InTest), GI(InGI), World(InGI->GetWorld()),
		  Profiles(InGI->GetSubsystem<UProject_JLocalProfileSubsystem>()), Gate(MoveTemp(InGate)) {}
	~FProfileProgressAdmission()
	{
		if (Barrier) Barrier->Release->Trigger();
		ReleaseReservations();
		if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
		GI->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
	void ReleaseReservations()
	{
		for (const auto& Ticket : Reservations) Gate->Release(Ticket);
		Reservations.Reset();
	}
	bool Check(bool Value, const TCHAR* Message) { return Test->TestTrue(Message, Value); }
	int32 Count() const
	{
		const auto* Q = Player->FindComponentByClass<UProject_JQuestComponent>();
		const auto* State = Q->States.FindByPredicate([](const auto& S) { return S.QuestId == TEXT("AdmissionQuest"); });
		return State ? State->Count : -1;
	}
	bool ReserveAll()
	{
		for (int32 I = 0; I < 32; ++I)
		{
			FGuid Ticket;
			if (!Check(Gate->TryAcquire({{TEXT("AdmissionFixture"), FGuid::NewGuid()}}, Ticket) == EAdmission::Accepted,
				TEXT("All 32 actual adapter gate slots are individually reusable"))) return false;
			Reservations.Add(Ticket);
		}
		FGuid Rejected;
		return Check(Gate->TryAcquire({{TEXT("AdmissionFixture"), FGuid::NewGuid()}}, Rejected) == EAdmission::Capacity && !Rejected.IsValid(),
			TEXT("The 33rd reservation cannot exceed the real adapter capacity"));
	}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 30)
		{
			Test->AddError(TEXT("Progress admission fixture timed out"));
			return true;
		}
		if (Step == 0)
		{
			auto* State = World->SpawnActor<AProject_JPlayerState>();
			Player = State;
			auto* Q = State->FindComponentByClass<UProject_JQuestComponent>();
			auto* Definition = NewObject<UProject_JQuestDefinition>(State);
			Definition->QuestId = TEXT("AdmissionQuest");
			Definition->ObjectiveEvent = TEXT("AdmissionEvent");
			Definition->RequiredCount = 4;
			Q->Definitions.Add(Definition);
			Profiles->OpenProfile(State, TEXT("ProgressAdmissionProfile"), [this](bool Success) {
				bOpened = true;
				bOpenSuccess = Success;
			});
			Step = 1;
			return false;
		}
		if (Step == 1)
		{
			if (!bOpened) return false;
			if (!Check(bOpenSuccess && Profiles->IsReady(Player.Get()), TEXT("Actual profile restore completes before admission checks"))) return true;
			Player->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(Player.Get(), nullptr);
			Player->FindComponentByClass<UProject_JProgressionComponent>()->InitializeDefaults(nullptr, nullptr, 1);
			Step = 2;
			return false;
		}
		if (Step == 2)
		{
			if (Profiles->IsCheckpointPending(Player.Get())) return false;
			if (!Check(Player->FindComponentByClass<UProject_JQuestComponent>()->Accept(TEXT("AdmissionQuest")),
				TEXT("Actual acceptance commits the fixture objective"))) return true;
			Step = 3;
			return false;
		}
		if (Step == 3)
		{
			if (Profiles->IsCheckpointPending(Player.Get())) return false;
			if (!Check(Count() == 0, TEXT("Confirmed objective starts at zero")) || !ReserveAll()) return true;
			BaseRevision = Profiles->GetCommittedRevision(Player.Get());
			const int32 JobsBefore = Profiles->GetNativeJobCountForTest();
			auto* Q = Player->FindComponentByClass<UProject_JQuestComponent>();
			Check(!Q->TryRecordObjective(TEXT("AdmissionEvent"), 1), TEXT("Idle automatic producer reports full-gate rejection"));
			Q->RecordObjective(TEXT("AdmissionEvent"), 1);
			Check(Q->TryRecordObjective(TEXT("NoAcceptedObjective"), 1) && !Q->TryRecordObjective(NAME_None, 1),
				TEXT("Absent goals remain successful no-ops and invalid events are rejected before admission"));
			Check(Count() == 0 && Profiles->GetCommittedRevision(Player.Get()) == BaseRevision &&
				!Profiles->IsCheckpointPending(Player.Get()) && Profiles->GetNativeJobCountForTest() == JobsBefore,
				TEXT("Rejected Try/legacy void do not mutate counters, revisions or enqueue work"));
			Check(Profiles->GetStatusText(Player.Get()).ToString().Contains(TEXT("미수락 요청 2회")),
				TEXT("Existing HUD status observes rejected Try and legacy void without counting no-op/invalid events"));
			Check(Gate->Release(Reservations.Pop()), TEXT("A completed independent owner returns one shared slot"));
			Barrier = MakeShared<FQuestWriterBarrier, ESPMode::ThreadSafe>();
			Profiles->SetCommandStageHookForTest(Player.Get(), [Held = Barrier](const TCHAR* Point) {
				if (FCString::Strcmp(Point, TEXT("AfterBegin")) == 0)
				{
					Held->Entered->Trigger();
					if (!Held->Release->Wait(5000)) Held->TimedOut.Store(true);
				}
			});
			if (!Check(Q->TryRecordObjective(TEXT("AdmissionEvent"), 1),
				TEXT("Explicit retry acquires the returned slot and starts the actual idle writer"))) return true;
			Step = 4;
			return false;
		}
		if (Step == 4)
		{
			if (!Barrier->Entered->Wait(0)) return false;
			if (!Check(!Barrier->TimedOut.Load(), TEXT("Actual tail transaction is held within its finite timeout"))) return true;
			auto* Q = Player->FindComponentByClass<UProject_JQuestComponent>();
			Check(!Q->TryRecordObjective(TEXT("AdmissionEvent"), 1), TEXT("Frozen tail rejects later ingress explicitly"));
			Q->RecordObjective(TEXT("AdmissionEvent"), 1);
			FGuid Extra;
			Check(Gate->TryAcquire({{TEXT("AdmissionFixture"), FGuid::NewGuid()}}, Extra) == EAdmission::Capacity && !Extra.IsValid(),
				TEXT("The writer retains its physical gate slot until its transaction actually finishes"));
			Check(Profiles->IsCheckpointPending(Player.Get()) && Count() == 0 &&
				Profiles->GetCommittedRevision(Player.Get()) == BaseRevision &&
				Profiles->GetStatusText(Player.Get()).ToString().Contains(TEXT("미수락 요청 4회")),
				TEXT("Closed-tail rejections are visible while accepted values remain unpublished"));
			Barrier->Release->Trigger();
			Step = 5;
			return false;
		}
		if (Step == 5)
		{
			if (Profiles->IsCheckpointPending(Player.Get())) return false;
			Check(!Barrier->TimedOut.Load() && Profiles->IsReady(Player.Get()) && Count() == 1 &&
				Profiles->GetCommittedRevision(Player.Get()) == BaseRevision + 1,
				TEXT("Only the accepted event publishes once; rejected events are never replayed automatically"));
			Profiles->SetCommandStageHookForTest(Player.Get(), {});
			FGuid OtherOwner;
			const bool bOtherAccepted = Gate->TryAcquire({{TEXT("AdmissionFixture"), FGuid::NewGuid()}}, OtherOwner) == EAdmission::Accepted;
			Check(bOtherAccepted, TEXT("Another owner can acquire the slot released by the actual completed writer"));
			if (bOtherAccepted) Gate->Release(OtherOwner);
			if (!Check(Player->FindComponentByClass<UProject_JQuestComponent>()->TryRecordObjective(TEXT("AdmissionEvent"), 1),
				TEXT("A subsequent explicit producer call can start a fresh finite writer"))) return true;
			Step = 6;
			return false;
		}
		if (Profiles->IsCheckpointPending(Player.Get())) return false;
		Check(Count() == 2 && Profiles->GetCommittedRevision(Player.Get()) == BaseRevision + 2 &&
			Profiles->GetStatusText(Player.Get()).ToString().Contains(TEXT("미수락 요청 4회")),
			TEXT("Fresh accepted input is independent of the session's rejected-admission observation"));
		ReleaseReservations();
		ReserveAll();
		ReleaseReservations();
		return true;
	}
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJJobProfileRestoreTest, "ProjectJ.MMO.Persistence.JobPawnRestore", PersistenceTestFlags)
bool FProjectJJobProfileRestoreTest::RunTest(const FString&)
{
	auto* ModeClass = LoadClass<AProject_JGameMode>(nullptr, TEXT("/Game/ThirdPerson/Blueprints/BP_Project_JGameMode.BP_Project_JGameMode_C"));
	if (!TestNotNull(TEXT("Authored game mode"), ModeClass)) return false;
	auto* GI = NewObject<UGameInstance>(GEngine);
	GI->InitializeStandalone();
	auto* World = GI->GetWorld();
	GI->GetSubsystem<UProject_JLocalProfileSubsystem>()->DatabasePath = TestDirectory() / TEXT("JobProfiles.sqlite");
	auto* Mode = World->SpawnActor<AProject_JGameMode>(ModeClass);
	if (!TestNotNull(TEXT("Game mode instance"), Mode))
	{
		GI->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	ADD_LATENT_AUTOMATION_COMMAND(FJobProfileRestore(this, GI, Mode));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProfileProgressAdmissionTest, "ProjectJ.MMO.Persistence.ProgressAdmission", PersistenceTestFlags)
bool FProjectJProfileProgressAdmissionTest::RunTest(const FString&)
{
	auto* GI = NewObject<UGameInstance>(GEngine);
	GI->InitializeStandalone();
	auto* World = GI->GetWorld();
	auto* Profiles = GI->GetSubsystem<UProject_JLocalProfileSubsystem>();
	if (!TestNotNull(TEXT("Real GameInstance owns the progress adapter"), Profiles)) return false;
	Profiles->DatabasePath = TestDirectory() / TEXT("Admission.sqlite");
	auto* Definitions = GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
	TestTrue(TEXT("Authored PrimaryAssets load through the existing registry"), Definitions && Definitions->LoadPrimaryAssetDefinitions());
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	ADD_LATENT_AUTOMATION_COMMAND(FProfileProgressAdmission(this, GI, Profiles->IOGate));
	return true;
}


namespace
{
struct FCapacityDiskRead
{
	bool bRead = false;
	int32 Bytes = 0, Count = 0, Items = 0;
	int64 Experience = 0, Revision = 0;
};
struct FCapacityOpenResult { bool bDone = false, bSuccess = false; };
struct FCapacityPrimaryResult
{
	bool bDone = false, bConfirmed = false, bFirst = false;
	FProject_JProfileDurableResult Durable;
};
/** Exact canonical bytes, an actual restore and real command/progress consumers.
 * Long but bounded definition IDs tune the fixture; no unknown JSON padding is used. */
struct FProfileProgressCapacity : IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	TStrongObjectPtr<UGameInstance> GI;
	UWorld* World;
	UProject_JLocalProfileSubsystem* Profiles;
	TWeakObjectPtr<AProject_JPlayerState> Player;
	FString Database;
	FName QuestId = TEXT("CapacityProgress"), FillerId = TEXT("CapacityFiller");
	FGuid Account, Character, MergeSource, MergeTarget;
	int32 Step = 0, SeedItems = 0;
	int64 InitialRevision = 0, MergeRevision = 0, FinalRevision = 0;
	TFuture<bool> Seed;
	TFuture<FCapacityDiskRead> Read;
	TSharedPtr<FCapacityOpenResult> OpenResult;
	TSharedPtr<FQuestWriterBarrier, ESPMode::ThreadSafe> Barrier;
	TSharedPtr<FCapacityPrimaryResult> Primary = MakeShared<FCapacityPrimaryResult>();
	double Started = FPlatformTime::Seconds();
	FProfileProgressCapacity(FAutomationTestBase* InTest, UGameInstance* InGI, FString InDatabase)
		: Test(InTest), GI(InGI), World(InGI->GetWorld()),
		  Profiles(InGI->GetSubsystem<UProject_JLocalProfileSubsystem>()), Database(MoveTemp(InDatabase)) {}
	~FProfileProgressCapacity()
	{
		if (Barrier) Barrier->Release->Trigger();
		if (Player.IsValid()) Profiles->Detach(Player.Get());
		if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
		GI->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
	bool Check(bool Value, const TCHAR* Message) { return Test->TestTrue(Message, Value); }
	UProject_JQuestComponent* Quests() const { return Player->FindComponentByClass<UProject_JQuestComponent>(); }
	UProject_JProgressionComponent* Progression() const { return Player->FindComponentByClass<UProject_JProgressionComponent>(); }
	int32 Count() const
	{
		const auto* State = Quests()->States.FindByPredicate([&](const auto& Value) { return Value.QuestId == QuestId; });
		return State ? State->Count : INDEX_NONE;
	}
	void MakePlayer()
	{
		Player = World->SpawnActor<AProject_JPlayerState>();
		auto* Q = Quests();
		Q->Definitions.Reset();
		for (const auto Id : {QuestId, FillerId})
		{
			auto* Def = NewObject<UProject_JQuestDefinition>(Player.Get());
			Def->QuestId = Id;
			Def->ObjectiveEvent = Id == QuestId ? TEXT("CapacityProgressEvent") : TEXT("CapacityFillerEvent");
			Def->RequiredCount = Id == QuestId ? 20 : 1;
			Q->Definitions.Add(Def);
		}
	}
	void Open()
	{
		OpenResult = MakeShared<FCapacityOpenResult>();
		Profiles->OpenProfile(Player.Get(), TEXT("CapacityProfile"), [Result = OpenResult](bool Success) {
			Result->bSuccess = Success; Result->bDone = true;
		});
	}
	void ReadDisk()
	{
		Read = Async(EAsyncExecution::ThreadPool, [File = Database, Id = Character, Quest = QuestId] {
			FCapacityDiskRead Result;
			FProject_JSQLiteRepository Repository(File);
			FRecord Record;
			FProject_JLocalProfileSnapshot Snapshot;
            const bool ReadCore = Repository.Read({TEXT("LocalProfile"), Id}, Record);
            const auto Values = ReadCore ? ProjectJ::LocalProfileHistory::ReadProfileValues(Repository, Id, Record.Payload) : ProjectJ::LocalProfileHistory::FProfileValues();
            Result.bRead = Values.IsValid(); if (Values) Snapshot = *Values;
			if (Result.bRead)
			{
				Result.Bytes = Record.Payload.Num(); Result.Revision = Record.Revision;
				Result.Experience = Snapshot.Progression.Experience; Result.Items = Snapshot.Items.Num();
				const auto* State = Snapshot.Quests.States.FindByPredicate([&](const auto& Value) { return Value.QuestId == Quest; });
				Result.bRead &= State != nullptr;
				Result.Count = State ? State->Count : INDEX_NONE;
			}
			Repository.Close();
			return Result;
		});
	}
	bool PrepareSeed()
	{
		MakePlayer();
		auto* Potion = LoadObject<UProject_JConsumableDefinition>(nullptr,
			TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"));
		auto* Data = GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
		if (!Check(Potion && Potion->MaxStackCount >= 2 &&
			Data->GetItemDefinition(Potion->ItemId, Potion->SchemaVersion) == Potion,
			TEXT("Canonical capacity fixture uses the actual registered stackable potion"))) return false;
		if (!Check(Profiles->ResolveLocalIdentity(TEXT("CapacityProfile"), Account, Character),
			TEXT("Capacity fixture resolves the same profile identity as normal open"))) return false;
		FProject_JLocalProfileSnapshot Snapshot;
		Snapshot.Account = Account; Snapshot.Character = Character;
		Snapshot.RulesFingerprint = FString::ChrN(40, TEXT('0')); // Fixed-size field, replaced by the real rules digest below.
		Snapshot.Quests.States.Add({QuestId, 9, false, true});
		Snapshot.Quests.States.Add({FillerId, 0, false, true});
		TArray<uint8> Bytes;
		for (int32 N = 0; N < 4096; ++N)
		{
			auto& Item = Snapshot.Items.AddDefaulted_GetRef();
			Item.Instance = FGuid::NewGuid(); Item.Definition = Potion->ItemId;
			Item.DefinitionVersion = Potion->SchemaVersion; Item.Order = N;
			if (!Snapshot.Encode(Bytes)) { Snapshot.Items.Pop(); break; }
		}
		if (!Check(Snapshot.Encode(Bytes) && Snapshot.Items.Num() >= 2,
			TEXT("Unpadded canonical inventory fits the bounded writer before exact sizing"))) return false;
		int32 Remaining = 65536 - Bytes.Num();
		FName* Names[] = {&QuestId, &FillerId};
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FString Original = Names[Index]->ToString();
			const int32 Extra = FMath::Min(Remaining, 128 - Original.Len());
			*Names[Index] = FName(*(Original + FString::ChrN(Extra, TEXT('x'))));
			Snapshot.Quests.States[Index].QuestId = *Names[Index];
			Quests()->Definitions[Index]->QuestId = *Names[Index];
			Remaining -= Extra;
		}
		FProject_JQuestDefinitionIndex Index;
		Quests()->BuildDefinitionIndex(Index);
		Snapshot.RulesFingerprint = ProjectJ::LocalProfileRules::Fingerprint(*Progression(), Index, nullptr, {}, &Snapshot.Quests);
		FString Error;
		if (!Check(Remaining == 0 && Snapshot.Encode(Bytes) && Bytes.Num() == 65536 &&
			Profiles->ValidateMigrationSnapshot(Player.Get(), Snapshot, Error),
			TEXT("Exactly 65536 canonical bytes pass the actual normal restore preflight")))
		{
			Test->AddInfo(FString::Printf(TEXT("Capacity sizing/preflight: remaining=%d bytes=%d reason=%s"), Remaining, Bytes.Num(), *Error));
			return false;
		}
		SeedItems = Snapshot.Items.Num();
		MergeSource = Snapshot.Items[0].Instance; MergeTarget = Snapshot.Items[1].Instance;
		Test->AddInfo(FString::Printf(TEXT("Canonical capacity seed: bytes=%d items=%d quests=%d; actual registered definition and scoped rules digest"),
			Bytes.Num(), SeedItems, Snapshot.Quests.States.Num()));
		Seed = Async(EAsyncExecution::ThreadPool, [File = Database, Id = Character, Payload = MoveTemp(Bytes)]() mutable {
			FProject_JSQLiteRepository Repository(File);
			FCommitRequest Request{FGuid::NewGuid(), TEXT("CapacityFixtureSeed"), {{ {TEXT("LocalProfile"), Id}, 0, 1, MoveTemp(Payload), false}}};
			const bool Confirmed = Repository.Commit(Request).Status == ECommitStatus::Committed;
			Repository.Close();
			return Confirmed;
		});
		return true;
	}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 30) { Test->AddError(TEXT("Canonical progress capacity lifecycle timed out")); return true; }
		if (Step == 0) { if (!PrepareSeed()) return true; Step = 1; return false; }
		if (Step == 1)
		{
			if (!Seed.IsReady()) return false;
			if (!Check(Seed.Get(), TEXT("Actual SQLite record durably contains the exact canonical boundary profile"))) return true;
			Open(); Step = 2; return false;
		}
		if (Step == 2)
		{
			// Open readiness precedes its bounded native no-op checkpoint; wait before replacing the hook.
			if (!OpenResult->bDone || Profiles->IsCheckpointPending(Player.Get())) return false;
			if (!Check(OpenResult->bSuccess && Profiles->IsReady(Player.Get()) && Count() == 9 &&
				Progression()->GetExperience() == 0 && Player->GetInventoryComponent()->GetItemInstances().Num() == SeedItems,
				TEXT("Normal profile open restores every canonical boundary value before command admission"))) return true;
			InitialRevision = Profiles->GetCommittedRevision(Player.Get());
			if (!Check(InitialRevision == 1, TEXT("Normal open checkpoint is byte-identical to the canonical seed and writes no extra profile revision"))) return true;
			Barrier = MakeShared<FQuestWriterBarrier, ESPMode::ThreadSafe>();
			Profiles->SetCommandStageHookForTest(Player.Get(), [Value = Barrier](const TCHAR* Point) {
				if (FCString::Strcmp(Point, TEXT("BeforeCommandEncode")) == 0)
				{
					Value->Entered->Trigger();
					if (!Value->Release->Wait(5000)) Value->TimedOut.Store(true);
				}
			});
			FProject_JPersistentEdit Edit;
			Edit.Kind = EProject_JPersistentEdit::Experience; Edit.Amount = 1;
			Edit.Request = Profiles->NewRequestId(Player.Get());
			if (!Check(Profiles->SubmitEdit(Player.Get(), Edit,
				[Value = Primary](bool Confirmed, bool First) { Value->bDone = true; Value->bConfirmed = Confirmed; Value->bFirst = First; },
				[Value = Primary](const FProject_JProfileDurableResult& Result) { Value->Durable = Result; }),
				TEXT("An actual XP primary is admitted while its same-width canonical result still fits"))) return true;
			Step = 3; return false;
		}
		if (Step == 3)
		{
			if (!Barrier->Entered->Wait(0)) return false;
			const bool Accepted = Quests()->TryRecordObjective(TEXT("CapacityProgressEvent"), 1);
			Barrier->Release->Trigger();
			if (!Check(Accepted && !Barrier->TimedOut.Load() && Count() == 9 && Progression()->GetExperience() == 0,
				TEXT("Count9-to10 is native-owned during the primary and never optimistically published"))) return true;
			Step = 4; return false;
		}
		if (Step == 4)
		{
			if (!Primary->bDone || Profiles->IsCheckpointPending(Player.Get())) return false;
			Profiles->SetCommandStageHookForTest(Player.Get(), {});
			if (!Check(Primary->bConfirmed && Primary->bFirst &&
				Primary->Durable.Outcome == EProject_JProfileDurableOutcome::Committed && Primary->Durable.StoreStatus == ECommitStatus::Committed &&
				Progression()->GetExperience() == 1 && Count() == 9 &&
				Profiles->GetCommittedRevision(Player.Get()) == InitialRevision + 1 && Profiles->IsReady(Player.Get()),
				TEXT("Oversize progress leaves the primary result/receipt successful, the durable counter unchanged and the profile recoverable"))) return true;
			Check(Profiles->GetStatusText(Player.Get()).ToString().Contains(TEXT("진척 저장 용량")),
				TEXT("Actual HUD status exposes separately blocked progress despite primary success"));
			const int32 JobsBefore = Profiles->GetNativeJobCountForTest();
			for (int32 Attempt = 0; Attempt < 8; ++Attempt)
				Check(!Quests()->TryRecordObjective(TEXT("CapacityProgressEvent"), 1), TEXT("The blocked revision explicitly rejects more progress"));
			Check(Profiles->GetNativeJobCountForTest() == JobsBefore && Profiles->GetCommittedRevision(Player.Get()) == InitialRevision + 1,
				TEXT("Repeated rejected ingress starts no native job or revision spin"));
			ReadDisk(); Step = 5; return false;
		}
		if (Step == 5)
		{
			if (!Read.IsReady()) return false;
			const auto Result = Read.Get();
			if (!Check(Result.bRead && Result.Bytes == 65536 && Result.Experience == 1 && Result.Count == 9 &&
				Result.Items == SeedItems && Result.Revision == InitialRevision + 1,
				TEXT("Reopened SQLite proves primary bytes committed and the oversized counter was never persisted"))) return true;
			FProject_JItemInstanceData Source, Target;
			auto* Inventory = Player->GetInventoryComponent();
			if (!Check(Inventory->FindItemInstance(MergeSource, Source) && Inventory->FindItemInstance(MergeTarget, Target),
				TEXT("Actual restored stacks provide the merge preconditions"))) return true;
			FProject_JBagRequest Request;
			Request.Operation = EProject_JBagOperation::Merge; Request.Source = MergeSource; Request.Target = MergeTarget; Request.Count = 1;
			Request.ExpectedSourceCount = Source.StackCount; Request.ExpectedTargetCount = Target.StackCount;
			Request.ExpectedSourceOrder = Source.BagOrder; Request.ExpectedTargetOrder = Target.BagOrder;
			MergeRevision = Profiles->GetCommittedRevision(Player.Get());
			Inventory->RequestBagChange(Inventory->NewRequestId(), Request);
			if (!Check(Profiles->IsCheckpointPending(Player.Get()) && Count() == 9 && Inventory->HasItemInstance(MergeSource),
				TEXT("Real bag merge admission preserves live ownership and blocked counter until confirmation"))) return true;
			Step = 6; return false;
		}
		if (Step == 6)
		{
			if (Profiles->IsCheckpointPending(Player.Get())) return false;
			FProject_JItemInstanceData Target;
			auto* Inventory = Player->GetInventoryComponent();
			if (!Check(Profiles->IsReady(Player.Get()) && Count() == 10 && Progression()->GetExperience() == 1 &&
				Inventory->GetItemInstances().Num() == SeedItems - 1 && !Inventory->HasItemInstance(MergeSource) &&
				Inventory->FindItemInstance(MergeTarget, Target) && Target.StackCount == 2 &&
				Profiles->GetCommittedRevision(Player.Get()) == MergeRevision + 2 &&
				!Profiles->GetStatusText(Player.Get()).ToString().Contains(TEXT("진척 저장 용량")),
				TEXT("One real shrink commit plus one frozen tail recover the retained event exactly once without a new producer call"))) return true;
			FinalRevision = Profiles->GetCommittedRevision(Player.Get());
			if (!Check(Quests()->TryRecordObjective(TEXT("CapacityProgressEvent"), 1) && Count() == 10,
				TEXT("Recovered idle writer admits new progress without optimistic mutation"))) return true;
			Step = 7; return false;
		}
		if (Step == 7)
		{
			if (Profiles->IsCheckpointPending(Player.Get())) return false;
			if (!Check(Count() == 11 && Profiles->GetCommittedRevision(Player.Get()) == FinalRevision + 1,
				TEXT("A later distinct event produces exactly one new committed revision"))) return true;
			FinalRevision = Profiles->GetCommittedRevision(Player.Get());
			Profiles->Detach(Player.Get()); Player->Destroy();
			MakePlayer(); Open(); Step = 8; return false;
		}
		if (Step == 8)
		{
			// Open readiness precedes its bounded native no-op checkpoint; wait before replacing the hook.
			if (!OpenResult->bDone || Profiles->IsCheckpointPending(Player.Get())) return false;
			if (!Check(OpenResult->bSuccess && Profiles->IsReady(Player.Get()) && Count() == 11 && Progression()->GetExperience() == 1 &&
				Player->GetInventoryComponent()->GetItemInstances().Num() == SeedItems - 1 &&
				Profiles->GetCommittedRevision(Player.Get()) == FinalRevision,
				TEXT("Session replacement restores recovered progress, merged ownership and the primary once"))) return true;
			ReadDisk(); Step = 9; return false;
		}
		if (!Read.IsReady()) return false;
		const auto Result = Read.Get();
		Check(Result.bRead && Result.Bytes < 65536 && Result.Experience == 1 && Result.Count == 11 &&
			Result.Items == SeedItems - 1 && Result.Revision == FinalRevision,
			TEXT("Actual reopened database retains the reduced canonical profile and all recovered counters"));
		Test->AddInfo(FString::Printf(TEXT("Canonical capacity recovery: bytes=%d items=%d count=%d xp=%lld revision=%lld seedRevision=%lld"),
			Result.Bytes, Result.Items, Result.Count, Result.Experience, Result.Revision, InitialRevision));
		return true;
	}
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProfileProgressCapacityTest, "ProjectJ.MMO.Persistence.ProgressCapacityRecovery", PersistenceTestFlags)
bool FProjectJProfileProgressCapacityTest::RunTest(const FString&)
{
	auto* GI = NewObject<UGameInstance>(GEngine);
	GI->InitializeStandalone();
	auto* Profiles = GI->GetSubsystem<UProject_JLocalProfileSubsystem>();
	if (!TestNotNull(TEXT("Capacity test uses the actual profile subsystem"), Profiles)) return false;
	const FString Database = TestDirectory() / TEXT("Capacity.sqlite");
	Profiles->DatabasePath = Database;
	auto* Definitions = GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
	if (!TestTrue(TEXT("Capacity test loads the normal authored registry"), Definitions && Definitions->LoadPrimaryAssetDefinitions())) return false;
	auto* World = GI->GetWorld();
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	ADD_LATENT_AUTOMATION_COMMAND(FProfileProgressCapacity(this, GI, Database));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProfileUnknownShapeTest, "ProjectJ.MMO.Persistence.UnknownSnapshotFieldsPreserved", PersistenceTestFlags)
bool FProjectJProfileUnknownShapeTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("Local profile load rejected;"), EAutomationExpectedErrorFlags::Contains, 1);
	auto* Schema128GI = NewObject<UGameInstance>(GEngine); Schema128GI->InitializeStandalone();
	auto* Schema128Profiles = Schema128GI->GetSubsystem<UProject_JLocalProfileSubsystem>();
	if (!TestNotNull(TEXT("Unknown-shape fixture uses the actual profile subsystem"), Schema128Profiles)) return false;
	const FString Schema128Database = TestDirectory() / TEXT("UnknownShape.sqlite");
	Schema128Profiles->DatabasePath = Schema128Database;
	auto* Schema128Definitions = Schema128GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
	TestTrue(TEXT("Unknown-shape fixture uses the normal authored registry"), Schema128Definitions && Schema128Definitions->LoadPrimaryAssetDefinitions());
	auto* Schema128World = Schema128GI->GetWorld();
	Schema128World->InitializeActorsForPlay(FURL());
	Schema128World->GetWorldSettings()->NotifyBeginPlay(); Schema128World->GetWorldSettings()->NotifyMatchStarted();
	ADD_LATENT_AUTOMATION_COMMAND(FProfileSchema128Preservation(this, Schema128GI, Schema128Database));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProfilePagedHistoryTest, "ProjectJ.MMO.Persistence.PagedQuestHistory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJProfilePagedHistoryTest::RunTest(const FString&)
{
    const FString Directory = TestDirectory();
    const auto Errors = Async(EAsyncExecution::ThreadPool, [Directory]
    {
        namespace H = ProjectJ::LocalProfileHistory;
        namespace QH = ProjectJ::QuestHistory;
        TArray<FString> Failures;
        const auto Check = [&](bool Value, const TCHAR* Message) { if (!Value) Failures.Add(Message); return Value; };
        FProject_JSQLiteRepository DB(Directory / TEXT("PagedHistory.sqlite"), H::LocalProfileRecordLimit);
        FProject_JLocalProfileSnapshot Empty;
        Empty.Account = FGuid::NewGuid(); Empty.Character = FGuid::NewGuid(); Empty.Progression.Level = 1;
        Empty.RulesFingerprint = FString::ChrN(40, 'A');
        auto Full = Empty;
        TArray<TArray<FName>> IdPages; auto& FirstIds = IdPages.AddDefaulted_GetRef();
        for (int32 I = 0; I < QH::IdsPerPage; ++I) FirstIds.Add(FName(*FString::Printf(TEXT("History.%04d"), I)));
        FString Failure;
        if (!Check(QH::BuildValidated(IdPages, QH::IdsPerPage, {}, Full.Quests.History, Failure), TEXT("Trusted bounded history values build"))) return Failures;
        Full.Quests.Version = 3; Full.Quests.HistoryCount = QH::Count(Full.Quests.History); Full.Quests.HistoryRootDigest = QH::RootDigest(Full.Quests.History);
        Full.Progression.Experience = 17;
        TArray<uint8> Core;
        FProject_JStoredProfileCore Header;
        auto Sentinel = Empty; Sentinel.Progression.Experience = 99;
        if (!Check(Full.Encode(Core) && FProject_JLocalProfileSnapshot::DecodeStoredCore(Core, Header) && Header.HotValues.Format == 4 &&
            Header.HotValues.Quests.HistoryCount == 64 && !Header.HotValues.Quests.HasHydratedHistory(), TEXT("Format4 core is explicitly metadata-only"))) return Failures;
        Check(!FProject_JLocalProfileSnapshot::Decode(Core, Sentinel) && Sentinel.Progression.Experience == 99, TEXT("Full decode cannot return unhydrated success or alter failure output"));
        const FGuid Session = FGuid::NewGuid(); TArray<uint8> LeaseBytes;
        { FMemoryWriter Writer(LeaseBytes); auto Value = Session; Writer << Value; }
        const FAggregateKey Lease{TEXT("LocalProfileLease"), Empty.Character}, Profile{TEXT("LocalProfile"), Empty.Character};
        if (!Check(DB.CommitProfile({FGuid::NewGuid(), TEXT("LocalProfileSession"), {{Lease, 0, 1, LeaseBytes, false}}},
            {Empty.Character, Session, -1}).Status == ECommitStatus::Committed, TEXT("Actual session opens"))) return Failures;
        auto Request = [&](uint32 Sequence, int64 LeaseRevision, int64 CoreRevision, const TArray<uint8>& Bytes)
        {
            return FCommitRequest{FGuid(Session.A, Session.B, Session.C, Sequence), TEXT("ProfileEdit.History"),
                {{Lease, LeaseRevision, 1, LeaseBytes, false}, {Profile, CoreRevision, 1, Bytes, false}}};
        };
        auto Initial = Request(1, 1, 0, Core);
        if (!Check(H::PrepareHistoryMutations(DB, Empty.Character, Empty, Full, Initial, Failure) && Initial.Mutations.Num() == 3,
            TEXT("Initial core+page is one bounded transaction"))) return Failures;
        DB.bTestLoseCommitResponse = true;
        Check(DB.CommitProfile(Initial, {Empty.Character, Session, 1}, {1}).Status == ECommitStatus::UnknownOutcome, TEXT("Actual commit acknowledgement can be lost"));
        const auto Replayed = DB.CommitProfile(Initial, {Empty.Character, Session, 1}, {1});
        if (!Check(Replayed.Status == ECommitStatus::Replayed && Replayed.Revisions.Num() == 3,
            TEXT("Exact page/core/receipt retry reconciles without another write"))) return Failures;
        int32 PageReads = 0;
        DB.TestHook = [&](const TCHAR* Stage, FProject_JProfileDatabase&) { if (FCString::Strcmp(Stage, TEXT("ReadHistoryPage")) == 0) ++PageReads; };
        auto Loaded = H::ReadProfileValues(DB, Empty.Character, Core);
        if (!Check(Loaded && Loaded->Format == 4 && QH::Count(Loaded->Quests.History) == 64 && PageReads == 1,
            TEXT("Cold read fully hydrates and validates one real page"))) return Failures;
        auto Hot = *Loaded; Hot.Progression.Experience = 18;
        TArray<uint8> HotBytes;
        if (!Check(Hot.Encode(HotBytes), TEXT("Hot-only mutation encodes"))) return Failures;
        auto HotRequest = Request(2, 2, 1, HotBytes); PageReads = 0;
        Check(H::PrepareHistoryMutations(DB, Empty.Character, *Loaded, Hot, HotRequest, Failure) && HotRequest.Mutations.Num() == 2 && PageReads == 0,
            TEXT("Hot-only command neither reads nor mutates history pages"));
        if (!Check(DB.CommitProfile(HotRequest, {Empty.Character, Session, 2}, {2}).Status == ECommitStatus::Committed, TEXT("Actual hot-only commit succeeds"))) return Failures;
        auto HotLoaded = H::ReadProfileValues(DB, Empty.Character, HotBytes, Loaded);
        Check(HotLoaded && HotLoaded->Quests.History == Loaded->Quests.History && PageReads == 0, TEXT("Hot readback reuses exact immutable history with zero page reads"));
        FRecord FirstPageBefore; DB.Read(H::PageKey(Empty.Character, 0), FirstPageBefore);
        auto Appended = Hot;
        if (!Check(QH::Append(Hot.Quests.History, TEXT("History.0064"), Appended.Quests.History, Failure), TEXT("One actual append crosses the full-page boundary"))) return Failures;
        Appended.Quests.HistoryCount = QH::Count(Appended.Quests.History); Appended.Quests.HistoryRootDigest = QH::RootDigest(Appended.Quests.History);
        Appended.Progression.Experience = 19;
        TArray<uint8> AppendBytes; Appended.Encode(AppendBytes);
        auto AppendRequest = Request(3, 3, 2, AppendBytes);
        if (!Check(H::PrepareHistoryMutations(DB, Empty.Character, Hot, Appended, AppendRequest, Failure) && AppendRequest.Mutations.Num() == 3 &&
            AppendRequest.Mutations[2].Key == H::PageKey(Empty.Character, 1), TEXT("Rollover touches only the new page"))) return Failures;
        if (!Check(DB.CommitProfile(AppendRequest, {Empty.Character, Session, 3}, {3}).Status == ECommitStatus::Committed, TEXT("Append/root/XP commit atomically"))) return Failures;
        PageReads = 0;
        auto AppendLoaded = H::ReadProfileValues(DB, Empty.Character, AppendBytes, HotLoaded);
        FRecord FirstPageAfter; DB.Read(H::PageKey(Empty.Character, 0), FirstPageAfter);
        Check(AppendLoaded && PageReads == 1 && QH::GetPage(AppendLoaded->Quests.History, 0) == QH::GetPage(Hot.Quests.History, 0) &&
            FirstPageAfter.Revision == FirstPageBefore.Revision && FirstPageAfter.Payload == FirstPageBefore.Payload,
            TEXT("Incremental readback reads only new page and preserves sealed page value/revision/identity"));
        auto Tail = Appended;
        if (!Check(QH::Append(Appended.Quests.History, TEXT("History.0065"), Tail.Quests.History, Failure), TEXT("Partial tail appends"))) return Failures;
        Tail.Quests.HistoryCount = QH::Count(Tail.Quests.History); Tail.Quests.HistoryRootDigest = QH::RootDigest(Tail.Quests.History);
        TArray<uint8> TailBytes; Tail.Encode(TailBytes);
        auto TailRequest = Request(4, 4, 3, TailBytes);
        Check(H::PrepareHistoryMutations(DB, Empty.Character, Appended, Tail, TailRequest, Failure) && TailRequest.Mutations.Num() == 3 && TailRequest.Mutations[2].ExpectedRevision == 1,
            TEXT("COW tail takes the actual stored tail CAS revision"));
        if (!Check(DB.CommitProfile(TailRequest, {Empty.Character, Session, 4}, {4}).Status == ECommitStatus::Committed, TEXT("Tail COW commits"))) return Failures;
        PageReads = 0;
        auto TailLoaded = H::ReadProfileValues(DB, Empty.Character, TailBytes, AppendLoaded);
        Check(TailLoaded && PageReads == 1 && QH::Count(TailLoaded->Quests.History) == 66, TEXT("COW tail read does not rehydrate sealed history"));
        TArray<FRecord> Captured;
        if (!Check(DB.ReadProfilePages(Empty.Character, TailBytes, 0, 2, Captured, 4), TEXT("Actual full page range can be captured consistently"))) return Failures;
        auto Missing = Captured; Missing.Pop();
        Check(!H::HydrateProfileValues(Empty.Character, TailBytes, Missing), TEXT("Missing sealed history cannot return a partial profile"));
        auto Changed = Captured;
        const ANSICHAR Needle[] = "history.0000";
        constexpr int32 NeedleLength = static_cast<int32>(UE_ARRAY_COUNT(Needle)) - 1;
        bool ChangedId = false;
        for (int32 I = 0; I + NeedleLength <= Changed[0].Payload.Num(); ++I)
            if (!FMemory::Memcmp(Changed[0].Payload.GetData() + I, Needle, NeedleLength))
            { Changed[0].Payload[I] = 'z'; ChangedId = true; break; }
        Check(ChangedId && !H::HydrateProfileValues(Empty.Character, TailBytes, Changed), TEXT("Changed valid JSON ID cannot bypass the stored page checksum/root"));
        TArray<FRecord> UnchangedOut{FirstPageBefore};
        Check(!DB.ReadProfilePages(Empty.Character, AppendBytes, 0, 1, UnchangedOut) && UnchangedOut.Num() == 1 && UnchangedOut[0].Payload == FirstPageBefore.Payload,
            TEXT("Stale core bytes reject a coherent range read and preserve output"));
        auto SameBytes = Request(5, 5, 4, TailBytes);
        Check(DB.CommitProfile(SameBytes, {Empty.Character, Session, 5}, {5}).Status == ECommitStatus::Committed,
            TEXT("Actual byte-identical command still advances aggregate revision"));
        Check(!H::ReadProfileValues(DB, Empty.Character, TailBytes, TailLoaded, 4) &&
            H::ReadProfileValues(DB, Empty.Character, TailBytes, TailLoaded, 5),
            TEXT("Revision publication rejects stale revision even when every core byte matches"));
        auto Invalid = Request(6, 6, 5, TailBytes); Invalid.Mutations.Add(TailRequest.Mutations[2]);
        Invalid.Mutations.Last().ExpectedRevision = 2;
        Check(DB.CommitProfile(Invalid, {Empty.Character, Session, 6}, {6}).Status == ECommitStatus::Invalid, TEXT("Unchanged head cannot rewrite a page"));
        auto PageOnly = Invalid; PageOnly.Mutations.RemoveAt(0, 2);
        Check(DB.CommitProfile(PageOnly, {Empty.Character, Session, 6}, {6}).Status == ECommitStatus::Invalid, TEXT("Page-only profile write is rejected"));
        auto BadPages = IdPages; BadPages[0][0] = TEXT("DifferentHistory");
        auto Bad = Tail; QH::BuildValidated(BadPages, 64, {}, Bad.Quests.History, Failure);
        Bad.Quests.HistoryCount = QH::Count(Bad.Quests.History); Bad.Quests.HistoryRootDigest = QH::RootDigest(Bad.Quests.History);
        const auto OriginalMutations = Invalid.Mutations;
        Check(!H::PrepareHistoryMutations(DB, Empty.Character, Tail, Bad, Invalid, Failure) && Invalid.Mutations == OriginalMutations,
            TEXT("Retraction/prefix failure preserves the full request"));
        DB.TestHook = {}; DB.Close();
        const auto Inspection = FProject_JProfileMigration::Inspect(Directory / TEXT("PagedHistory.sqlite"));
        TArray<FProject_JMigrationRecord> Planned;
        Check(Inspection.IsValid() && FProject_JProfileMigration::Plan(Inspection, {}, Planned, Failure) && Planned.Num() == Inspection.Records.Num(),
            TEXT("Offline inspection and no-op legacy migration accept an existing paged character"));
        Check(FProject_JProfileMigration::Create(Directory / TEXT("PagedHistory.sqlite"), Directory / TEXT("PagedCopy.sqlite"), Inspection, Planned, Failure),
            TEXT("Offline migration preserves an existing paged core/history and receipts"));
        // Coalesced progress may legitimately retain verified inline legacy claimed rows.
        FProject_JSQLiteRepository LegacyDB(Directory / TEXT("LegacyHotHistory.sqlite"));
        auto Legacy = Empty; Legacy.Character = FGuid::NewGuid();
        auto& Claimed = Legacy.Quests.States.AddDefaulted_GetRef(); Claimed.QuestId = TEXT("Legacy.Claimed"); Claimed.Count = 1; Claimed.bClaimed = true; Claimed.bTracked = false;
        auto& Active = Legacy.Quests.States.AddDefaulted_GetRef(); Active.QuestId = TEXT("Legacy.Active");
        const FAggregateKey LegacyLease{TEXT("LocalProfileLease"), Legacy.Character}, LegacyCore{TEXT("LocalProfile"), Legacy.Character};
        Check(LegacyDB.CommitProfile({FGuid::NewGuid(), TEXT("LocalProfileSession"), {{LegacyLease, 0, 1, LeaseBytes, false}}},
            {Legacy.Character, Session, -1}).Status == ECommitStatus::Committed, TEXT("Legacy progress session opens"));
        TArray<uint8> LegacyBytes; Legacy.Encode(LegacyBytes);
        Check(LegacyDB.CommitProfile({FGuid::NewGuid(), TEXT("LocalProfileCheckpoint"), {{LegacyLease, 1, 1, LeaseBytes, false}, {LegacyCore, 0, 1, LegacyBytes, false}}},
            {Legacy.Character, Session, 0}).Status == ECommitStatus::Committed, TEXT("Actual inline claimed and active rows persist"));
        auto Advanced = Legacy; Advanced.Quests.States[1].Count = 1;
        TArray<uint8> AdvancedBytes; Advanced.Encode(AdvancedBytes);
        FCommitRequest LegacyProgress{FGuid::NewGuid(), TEXT("LocalProfileCheckpoint"), {{LegacyLease, 2, 1, LeaseBytes, false}, {LegacyCore, 1, 1, AdvancedBytes, false}}};
        Check(H::PrepareHistoryMutations(LegacyDB, Legacy.Character, Legacy, Advanced, LegacyProgress, Failure) && LegacyProgress.Mutations.Num() == 2 &&
            LegacyDB.CommitProfile(LegacyProgress, {Legacy.Character, Session, 0}).Status == ECommitStatus::Committed,
            TEXT("Legacy claimed row preservation cannot silently block an active-only progress checkpoint"));
        auto LegacyRead = H::ReadProfileValues(LegacyDB, Legacy.Character, AdvancedBytes);
        Check(LegacyRead && LegacyRead->Format == 3 && LegacyRead->Quests.States[0] == Legacy.Quests.States[0] && LegacyRead->Quests.States[1].Count == 1,
            TEXT("Coalesced legacy progress keeps the exact completion fact without premature migration"));
        LegacyDB.Close();
        return Failures;
    }).Get();
    for (const auto& Error : Errors) AddError(Error);
    return Errors.IsEmpty();
}

#endif
