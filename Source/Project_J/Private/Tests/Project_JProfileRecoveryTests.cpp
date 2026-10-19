#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Backend/Project_JSQLiteRepository.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

using namespace ProjectJ::MMO;
namespace
{
FCommitRequest RecoveryRequest(int64 Revision, uint8 Value)
{
	FCommitRequest R{FGuid(1, 2, 3, uint32(Revision + 1)), TEXT("OwnedRecoveryFixture"), {}};
	for (int32 I = 0; I < 3; ++I)
	{
		TArray<uint8> P;
		P.Init(Value, 60000);
		R.Mutations.Add({{TEXT("Recovery"), FGuid(4, 5, 6, I + 1)}, Revision, 1, MoveTemp(P), false});
	}
	return R;
}
class FRecoveryFinished : public IAutomationLatentCommand
{
	FAutomationTestBase *Test;
	TFuture<TArray<FString>> Work;

  public:
	FRecoveryFinished(FAutomationTestBase *T, TFuture<TArray<FString>> &&W) : Test(T), Work(MoveTemp(W))
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProfileRecoveryTest, "ProjectJ.MMO.Persistence.ProcessRecovery",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJProfileRecoveryTest::RunTest(const FString &)
{
	FString Phase, Run;
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJRecoveryPhase="), Phase);
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJRecoveryRun="), Run);
	// Destructive self-exit is only reachable from this explicit automation test,
	// a new GUID-owned fixture directory and a separate admitted test process.
	FGuid RunId;
	const bool CrashPhase = Phase == TEXT("BeforeBegin") || Phase == TEXT("BeforeCommit") ||
							Phase == TEXT("DuringCommit") || Phase == TEXT("AfterCommit");
	if (!Phase.IsEmpty() && !FGuid::ParseExact(Run, EGuidFormats::Digits, RunId))
	{
		AddError(TEXT("Recovery phase requires an isolated GUID run"));
		return false;
	}
	const FString File = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Validation/NativeRecovery") /
		(Run.IsEmpty() ? FGuid::NewGuid().ToString(EGuidFormats::Digits) : Run) / TEXT("Profiles.sqlite"));
	ADD_LATENT_AUTOMATION_COMMAND(FRecoveryFinished(
		this, Async(EAsyncExecution::ThreadPool, [File, Phase, CrashPhase] {
			TArray<FString> Errors;
			auto Check = [&](bool OK, const TCHAR *Message) {
				if (!OK)
					Errors.Add(Message);
			};
			FProject_JSQLiteRepository DB(File);
			const auto Initial = RecoveryRequest(0, 17), Next = RecoveryRequest(1, 29);
			FRecord Record;
			if (CrashPhase)
			{
				if (!IFileManager::Get().FileExists(*File))
				{
					Errors.Add(TEXT("Crash fixture must be initialized in a previous normal process"));
					return Errors;
				}
				if (Phase == TEXT("BeforeBegin"))
					FPlatformMisc::RequestExitWithStatus(true, 99, TEXT("Owned recovery test before transaction"));
				DB.TestHook = [Phase](const TCHAR *Point, FProject_JProfileDatabase &Connection) {
					if (FCString::Strcmp(Point, TEXT("AfterBegin")) == 0)
						Connection.Execute(TEXT("PRAGMA cache_size=1"));
					if (Phase == TEXT("DuringCommit") && FCString::Strcmp(Point, TEXT("BeforeCommit")) == 0)
						Connection.CrashAfterNextSyncForTest();
					if (Phase == Point)
						FPlatformMisc::RequestExitWithStatus(true, 99, TEXT("Owned repository interruption fixture"));
				};
				DB.Commit(Next);
				Errors.Add(TEXT("Requested native interruption did not execute"));
				return Errors;
			}
			if (Phase.StartsWith(TEXT("Check")))
			{
				const bool Committed = Phase == TEXT("CheckAfter");
				for (const auto &M : Initial.Mutations)
					Check(DB.Read(M.Key, Record) && Record.Revision == (Committed ? 2 : 1) &&
							  Record.Payload == (Committed ? Next.Mutations[0].Payload : M.Payload),
						  TEXT("Recovery restores all aggregate pages to the same commit boundary"));
				FProject_JRepositoryUsage Usage;
				Check(DB.GetUsage(Usage) && Usage.Records == 3 && Usage.Receipts == (Committed ? 2 : 1),
					  TEXT("Receipt and aggregate recover together"));
				Check(DB.Commit(Next).Status == (Committed ? ECommitStatus::Replayed : ECommitStatus::Committed),
					  TEXT("Original command retry is either first commit or exact replay"));
				Check(DB.Commit(Next).Status == ECommitStatus::Replayed,
					  TEXT("No duplicate award after recovered retry"));
			}
			else
			{
				Check(DB.Commit(Initial).Status == ECommitStatus::Committed, TEXT("Initial native fixture commit"));
				if (Phase.IsEmpty())
				{
					DB.bTestLoseCommitResponse = true;
					Check(DB.Commit(Next).Status == ECommitStatus::UnknownOutcome,
						  TEXT("A real commit can lose its acknowledgement"));
					Check(DB.Commit(Next).Status == ECommitStatus::Replayed,
						  TEXT("Same-key reconciliation returns the durable receipt"));
					auto Wrong = Next;
					Wrong.Mutations[0].Payload[0] = 5;
					Check(DB.Commit(Wrong).Status == ECommitStatus::KeyReused,
						  TEXT("Lost response does not permit payload reuse"));
					const auto Failed = RecoveryRequest(2, 31);
					DB.TestHook = [](const TCHAR *Point, FProject_JProfileDatabase &Connection) {
						if (FCString::Strcmp(Point, TEXT("AfterBegin")) == 0)
						{
							Connection.Execute(TEXT("PRAGMA cache_size=1"));
							Connection.FailNextWriteForTest();
						}
					};
					const auto Failure = DB.Commit(Failed).Status;
					Check(Failure == ECommitStatus::Unavailable || Failure == ECommitStatus::UnknownOutcome,
						  TEXT("Actual VFS write failure is not success"));
					DB.TestHook = {};
					Check(DB.Read(Initial.Mutations[0].Key, Record) && Record.Revision == 2 &&
							  Record.Payload == Next.Mutations[0].Payload,
						  TEXT("Failed native I/O leaves last committed state"));
					FProject_JProfileDatabase Holder;
					Check(Holder.Open(*File, ESQLiteDatabaseOpenMode::ReadWrite) &&
							  Holder.Execute(TEXT("BEGIN IMMEDIATE")),
						  TEXT("Independent native connection holds a real OS reserved lock"));
					Check(DB.Commit(Failed).Status == ECommitStatus::Unavailable,
						  TEXT("Native lock conflict fails bounded admission"));
					Holder.Execute(TEXT("ROLLBACK"));
					Holder.Close();
					Check(DB.Commit(Failed).Status == ECommitStatus::Committed,
						  TEXT("Writer resumes after lock release"));
				}
			}
			DB.Close();
			return Errors;
		})));
	return true;
}
#endif
