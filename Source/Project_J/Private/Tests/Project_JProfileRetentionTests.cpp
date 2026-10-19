#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Backend/Project_JLocalProfileSnapshot.h"
#include "Backend/Project_JSQLiteRepository.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"

using namespace ProjectJ::MMO;
namespace
{
TArray<uint8> Bytes(FGuid Id)
{
	TArray<uint8> Out;
	FMemoryWriter W(Out);
	W << Id;
	return Out;
}
class FRetentionFinished : public IAutomationLatentCommand
{
	FAutomationTestBase *Test;
	TFuture<TArray<FString>> Work;

  public:
	FRetentionFinished(FAutomationTestBase *T, TFuture<TArray<FString>> &&W) : Test(T), Work(MoveTemp(W))
	{
	}
	bool Update() override
	{
		if (!Work.IsReady())
			return false;
		for (const auto &E : Work.Get())
			Test->AddError(E);
		return true;
	}
};
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProfileRetentionTest, "ProjectJ.MMO.Persistence.BoundedProfileStream",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJProfileRetentionTest::RunTest(const FString &)
{
	FString Phase;
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJRetentionPhase="), Phase);
	if (!Phase.IsEmpty() && Phase != TEXT("Write") && Phase != TEXT("Read"))
	{
		AddError(TEXT("Unknown retention phase"));
		return false;
	}
	if (Phase != TEXT("Read"))
	{
		AddExpectedError(TEXT("Profile storage full \\(SQLITE_FULL\\)"), EAutomationExpectedErrorFlags::Contains, 1);
		AddExpectedError(TEXT("Backup admission full \\("), EAutomationExpectedErrorFlags::Contains, 1);
	}
	const FString Directory =
		FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Validation/Retention") /
										  (Phase.IsEmpty() ? FGuid::NewGuid().ToString() : TEXT("Restart")));
	ADD_LATENT_AUTOMATION_COMMAND(FRetentionFinished(
		this, Async(EAsyncExecution::ThreadPool, [Directory, Phase] {
			TArray<FString> Errors;
			auto Check = [&](bool V, const TCHAR *M) {
				if (!V)
					Errors.Add(M);
			};
			const FString File = Directory / TEXT("Profiles.sqlite"), Backup = Directory / TEXT("Backup.sqlite");
			const FGuid Character(0x7781, 2, 3, 4);
			const FAggregateKey Lease{TEXT("LocalProfileLease"), Character}, Profile{TEXT("LocalProfile"), Character};
			FProject_JSQLiteRepository DB(File, 2);
			FGuid Session = FGuid::NewGuid();
			int64 LeaseRevision = 0, ProfileRevision = 0;
			auto Open = [&](FProject_JSQLiteRepository &R, FGuid Epoch, int64 Revision) {
				return R.CommitProfile(
					{FGuid::NewGuid(), TEXT("LocalProfileSession"), {{Lease, Revision, 1, Bytes(Epoch), false}}},
					{Character, Epoch, -1});
			};
			FGuid OldSession;
			FString OldText;
			FRecord L, P;
			if (Phase == TEXT("Read"))
			{
				Check(FFileHelper::LoadFileToString(OldText, *(Directory / TEXT("Session.txt"))) &&
						  FGuid::Parse(OldText, OldSession),
					  TEXT("Previous process persisted fixture session"));
				Check(DB.Read(Lease, L) && DB.Read(Profile, P) && P.Revision == 16384,
					  TEXT("Second process reads all 16384 committed native commands"));
				auto Opened = Open(DB, Session, L.Revision);
				Check(Opened.Status == ECommitStatus::Committed, TEXT("Restart atomically replaces session"));
				FGuid Last(OldSession.A, OldSession.B, OldSession.C, 16384);
				Check(DB.FindProfileReceipt(Last, TEXT("ProfileEdit.0"), {Character, OldSession, 16384}, {1}).Status ==
						  ECommitStatus::Expired,
					  TEXT("Old process request cannot become new after restart"));
				FGuid New(Session.A, Session.B, Session.C, 1);
				auto C = DB.CommitProfile({New,
										   TEXT("ProfileEdit.0"),
										   {{Lease, Opened.Revisions[0], 1, Bytes(Session), false},
											{Profile, P.Revision, 1, P.Payload, false}}},
										  {Character, Session, 1}, {1});
				Check(C.Status == ECommitStatus::Committed,
					  TEXT("Normal saving continues after restart beyond four old caps"));
				FProject_JRepositoryUsage Usage;
				Check(DB.GetUsage(Usage) && Usage.Records == 2 && Usage.Receipts == 2,
					  TEXT("Restart retires old epoch receipts without deleting state"));
				DB.Close();
				return Errors;
			}
			Check(!IFileManager::Get().FileExists(*File), TEXT("Stress fixture never overwrites a previous run"));
			if (!Errors.IsEmpty())
				return Errors;
			auto Opened = Open(DB, Session, 0);
			Check(Opened.Status == ECommitStatus::Committed, TEXT("New stream opens"));
			if (!Errors.IsEmpty())
				return Errors;
			LeaseRevision = Opened.Revisions[0];
			FProject_JLocalProfileSnapshot Snapshot;
			Snapshot.Account = FGuid(8, 9, 10, 11);
			Snapshot.Character = Character;
			Snapshot.RulesFingerprint = FString::ChrN(40, TEXT('A'));
			for (int32 I = 0; I < 32; ++I)
			{
				auto &Item = Snapshot.Items.AddDefaulted_GetRef();
				Item.Instance = FGuid(12, 13, 14, I + 1);
				Item.Definition = TEXT("FixturePotion");
				Item.Order = I;
			}
			uint64 MemoryPlateau = 0;
			int64 Plateau = 0;
			double Start = FPlatformTime::Seconds();
			for (uint32 Sequence = 1; Sequence <= 16384; ++Sequence)
			{
				Snapshot.Progression.Level = 1;
				Snapshot.Progression.Experience = Sequence % 100;
				TArray<uint8> Payload;
				if (!Snapshot.Encode(Payload))
				{
					Errors.Add(TEXT("Native full snapshot encoding failed"));
					break;
				}
				FGuid Key(Session.A, Session.B, Session.C, Sequence);
				FCommitRequest Request{
					Key,
					TEXT("ProfileEdit.0"),
					{{Lease, LeaseRevision, 1, Bytes(Session), false}, {Profile, ProfileRevision, 1, Payload, false}}};
				auto Result = DB.CommitProfile(Request, {Character, Session, Sequence}, {1});
				if (Result.Status != ECommitStatus::Committed || Result.Revisions.Num() != 2)
				{
					Errors.Add(FString::Printf(TEXT("Native bounded commit failed at %u status %d"), Sequence,
											   int32(Result.Status)));
					break;
				}
				LeaseRevision = Result.Revisions[0];
				ProfileRevision = Result.Revisions[1];
				if (Sequence == 128)
					Check(DB.BackupToNewFile(Backup), TEXT("Backup preserves active stream"));
				if (Sequence % 1024 == 0)
				{
					FProject_JRepositoryUsage Usage;
					Check(DB.GetUsage(Usage) && Usage.Records == 2 && Usage.Receipts == 65,
						  TEXT("Constant 2 records and <=65 receipts after repeated 4096 boundaries"));
					const int64 Size = IFileManager::Get().FileSize(*File);
					if (Sequence == 4096)
					{
						Plateau = Size;
						MemoryPlateau = FPlatformMemory::GetStats().UsedPhysical;
					}
					if (Sequence > 4096)
						Check(FPlatformMemory::GetStats().UsedPhysical <= MemoryPlateau + 32 * 1024 * 1024,
							  TEXT("Native process memory stays within the warmed bounded working set"));
					if (Sequence > 4096)
						Check(Size <= Plateau + 65536,
							  TEXT("Physical database reuses pages instead of growing with request count"));
					UE_LOG(LogTemp, Display,
						   TEXT("Retention synthetic native: sequence=%u records=%lld receipts=%lld bytes=%lld "
								"seconds=%.3f"),
						   Sequence, Usage.Records, Usage.Receipts, Size, FPlatformTime::Seconds() - Start);
				}
			}
			Check(ProfileRevision == 16384, TEXT("Four times 4096 native full profile commands committed"));
			FGuid First(Session.A, Session.B, Session.C, 1), Last(Session.A, Session.B, Session.C, 16384);
			Check(DB.FindProfileReceipt(Last, TEXT("ProfileEdit.0"), {Character, Session, 16384}, {1}).Status ==
					  ECommitStatus::Replayed,
				  TEXT("Current result remains queryable"));
			Check(DB.FindProfileReceipt(Last, TEXT("ProfileEdit.0"), {Character, Session, 16384}, {2}).Status ==
					  ECommitStatus::KeyReused,
				  TEXT("Current ticket rejects changed intent"));
			Check(DB.FindProfileReceipt(First, TEXT("ProfileEdit.0"), {Character, Session, 1}, {1}).Status ==
					  ECommitStatus::Expired,
				  TEXT("Pruned result has explicit expired status"));
			Check(DB.Read(Profile, P), TEXT("Read final state"));
			auto Retired = DB.CommitProfile(
				{First,
				 TEXT("ProfileEdit.0"),
				 {{Lease, LeaseRevision, 1, Bytes(Session), false}, {Profile, ProfileRevision, 1, P.Payload, false}}},
				{Character, Session, 1}, {1});
			Check(Retired.Status == ECommitStatus::Expired,
				  TEXT("Retired command with fresh CAS cannot be paid again"));
			// Failed/rolled-back deletion must preserve both the valid receipt and authoritative state.
			DB.TestHook = [](const TCHAR *Point, FProject_JProfileDatabase &Native) {
				if (FCString::Strcmp(Point, TEXT("AfterBegin")) == 0)
					Native.FailNextWriteForTest();
			};
			FGuid Next(Session.A, Session.B, Session.C, 16385);
			auto Failed = DB.CommitProfile(
				{Next,
				 TEXT("ProfileEdit.0"),
				 {{Lease, LeaseRevision, 1, Bytes(Session), false}, {Profile, ProfileRevision, 1, P.Payload, false}}},
				{Character, Session, 16385}, {1});
			DB.TestHook = {};
			Check(Failed.Status != ECommitStatus::Committed && DB.Read(Profile, P) && P.Revision == ProfileRevision,
				  TEXT("I/O failure does not partially publish state or cleanup"));
			Check(DB.FindProfileReceipt(Last, TEXT("ProfileEdit.0"), {Character, Session, 16384}, {1}).Status ==
					  ECommitStatus::Replayed,
				  TEXT("Failed cleanup preserves valid receipt"));
			if (Phase == TEXT("Write"))
				Check(FFileHelper::SaveStringToFile(Session.ToString(), *(Directory / TEXT("Session.txt"))),
					  TEXT("Save restart fixture metadata"));
			else
			{
				auto NewSession = FGuid::NewGuid();
				auto NewOpen = Open(DB, NewSession, LeaseRevision);
				Check(NewOpen.Status == ECommitStatus::Committed, TEXT("Binding replacement succeeds"));
				Check(DB.FindProfileReceipt(Last, TEXT("ProfileEdit.0"), {Character, Session, 16384}, {1}).Status ==
						  ECommitStatus::Expired,
					  TEXT("Replaced binding cannot replay"));
			}
			auto OtherSession = FGuid::NewGuid();
			auto OtherCharacter = FGuid::NewGuid();
			Check(DB.CommitProfile({FGuid::NewGuid(),
									TEXT("LocalProfileSession"),
									{{{TEXT("LocalProfileLease"), OtherCharacter}, 0, 1, Bytes(OtherSession), false}}},
								   {OtherCharacter, OtherSession, -1})
						  .Status == ECommitStatus::Capacity,
				  TEXT("Full record admission rejects only new characters, not ongoing saves"));
			DB.Close();
			{
				FProject_JSQLiteRepository Full(Directory / TEXT("QuotaFull.sqlite"));
				auto Epoch = FGuid::NewGuid();
				auto QuotaOpened = Full.CommitProfile(
					{FGuid::NewGuid(), TEXT("LocalProfileSession"), {{Lease, 0, 1, Bytes(Epoch), false}}},
					{Character, Epoch, -1});
				Check(QuotaOpened.Status == ECommitStatus::Committed,
					  TEXT("Quota fixture starts with a durable lease"));
				Full.TestHook = [](const TCHAR *Point, FProject_JProfileDatabase &Native) {
					if (FCString::Strcmp(Point, TEXT("AfterBegin")) == 0)
						Native.Execute(TEXT("PRAGMA max_page_count=1"));
				};
				TArray<uint8> Big;
				Big.Init(42, 60000);
				FGuid Key(Epoch.A, Epoch.B, Epoch.C, 1);
				auto Result = Full.CommitProfile(
					{Key, TEXT("ProfileEdit.0"), {{Lease, 1, 1, Bytes(Epoch), false}, {Profile, 0, 1, Big, false}}},
					{Character, Epoch, 1}, {1});
				Full.TestHook = {};
				Check(Result.Status != ECommitStatus::Committed && Full.DescribeFailure().Contains(TEXT("SQLITE_FULL")),
					  TEXT("SQLite native page quota reports storage-full explicitly"));
				Check(Full.Read(Profile, P) == false && Full.Read(Lease, L) && L.Revision == 1,
					  TEXT("Full storage leaves prior lease intact and no partial profile"));
				Full.Close();
			}
			{
				FProject_JSQLiteRepository Legacy(Directory / TEXT("LegacyFull.sqlite"), 4, 1);
				Check(Legacy.Commit({FGuid::NewGuid(),
									 TEXT("Legacy"),
									 {{{TEXT("Other"), FGuid::NewGuid()}, 0, 1, {1}, false}}})
							  .Status == ECommitStatus::Committed,
					  TEXT("Fill legacy receipt capacity"));
				auto Epoch = FGuid::NewGuid();
				auto LegacyOpened = Open(Legacy, Epoch, 0);
				Check(LegacyOpened.Status == ECommitStatus::Committed,
					  TEXT("Legacy full receipt table does not block a profile stream"));
				FGuid Key(Epoch.A, Epoch.B, Epoch.C, 1);
				Check(Legacy.CommitProfile({Key,
											TEXT("ProfileEdit.0"),
											{{Lease, 1, 1, Bytes(Epoch), false}, {Profile, 0, 1, {1}, false}}},
										   {Character, Epoch, 1}, {1})
							  .Status == ECommitStatus::Committed,
					  TEXT("Bounded economy proceeds while legacy dedup receipts remain preserved"));
				Legacy.Close();
			}
			{
				FProject_JSQLiteRepository Boundary(Directory / TEXT("SequenceBoundary.sqlite"), 2);
				auto Epoch = FGuid::NewGuid();
				auto InitialSession = Open(Boundary, Epoch, 0);
				Check(InitialSession.Status == ECommitStatus::Committed, TEXT("Sequence boundary fixture opens"));
				FGuid Key(Epoch.A, Epoch.B, Epoch.C, MAX_uint32);
				Check(Boundary.CommitProfile({Key,
											  TEXT("ProfileEdit.0"),
											  {{Lease, 1, 1, Bytes(Epoch), false}, {Profile, 0, 1, {1}, false}}},
											 {Character, Epoch, MAX_uint32}, {1})
							  .Status == ECommitStatus::Committed,
					  TEXT("Final uint32 sequence has a valid bounded result"));
				Check(Boundary.CommitProfile({Key,
											  TEXT("ProfileEdit.0"),
											  {{Lease, 2, 1, Bytes(Epoch), false}, {Profile, 1, 1, {1}, false}}},
											 {Character, Epoch, int64(MAX_uint32) + 1}, {1})
							  .Status == ECommitStatus::Invalid,
					  TEXT("Sequence overflow cannot wrap into a new command"));
				auto NextEpoch = FGuid::NewGuid();
				auto Reopened = Open(Boundary, NextEpoch, 2);
				Check(Reopened.Status == ECommitStatus::Committed &&
						  Boundary.FindProfileReceipt(Key, TEXT("ProfileEdit.0"), {Character, Epoch, MAX_uint32}, {1})
								  .Status == ECommitStatus::Expired,
					  TEXT("A new epoch safely retires exhausted request numbering"));
				Boundary.Close();
			}
			auto BackupOne = Async(EAsyncExecution::ThreadPool, [File, Directory] {
				FProject_JSQLiteRepository R(File);
				bool OK = R.BackupToNewFile(Directory / TEXT("BoundedBackups/One.sqlite"), 1);
				R.Close();
				return OK;
			});
			auto BackupTwo = Async(EAsyncExecution::ThreadPool, [File, Directory] {
				FProject_JSQLiteRepository R(File);
				bool OK = R.BackupToNewFile(Directory / TEXT("BoundedBackups/Two.sqlite"), 1);
				R.Close();
				return OK;
			});
			const bool One = BackupOne.Get(), Two = BackupTwo.Get();
			Check(One != Two, TEXT("Concurrent native backup calls cannot race beyond the retained-file bound"));
			FProject_JSQLiteRepository Restored(Backup);
			Check(Restored.Read(Lease, L), TEXT("Native backup remains readable"));
			auto NewSession = FGuid::NewGuid();
			auto RestoredOpen = Open(Restored, NewSession, L.Revision);
			Check(
				RestoredOpen.Status == ECommitStatus::Committed &&
					Restored.FindProfileReceipt(Last, TEXT("ProfileEdit.0"), {Character, Session, 16384}, {1}).Status ==
						ECommitStatus::Expired,
				TEXT("Restoring an older backup cannot re-admit a later old-epoch request"));
			Restored.Close();
			return Errors;
		})));
	return true;
}
#endif
