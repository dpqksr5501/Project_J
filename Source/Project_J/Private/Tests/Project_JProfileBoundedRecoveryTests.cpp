#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Backend/Project_JSQLiteRepository.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
using namespace ProjectJ::MMO;
namespace
{
class FBoundedRecoveryFinished : public IAutomationLatentCommand
{
	FAutomationTestBase *Test;
	TFuture<TArray<FString>> Work;

  public:
	FBoundedRecoveryFinished(FAutomationTestBase *T, TFuture<TArray<FString>> &&W) : Test(T), Work(MoveTemp(W))
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJBoundedRecoveryTest, "ProjectJ.MMO.Persistence.BoundedRecovery",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJBoundedRecoveryTest::RunTest(const FString &)
{
	FString Phase, Run;
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJRecoveryPhase="), Phase);
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJRecoveryRun="), Run);
	const bool Crash = Phase == TEXT("BeforeBegin") || Phase == TEXT("BeforeCommit") || Phase == TEXT("DuringCommit") ||
					   Phase == TEXT("AfterCommit");
	FGuid RunId;
	if (!Phase.IsEmpty() &&
		(!FGuid::ParseExact(Run, EGuidFormats::Digits, RunId) ||
		 (!Crash && Phase != TEXT("Initialize") && Phase != TEXT("CheckBefore") && Phase != TEXT("CheckAfter"))))
	{
		AddError(TEXT("Unknown phase or invalid owned recovery run"));
		return false;
	}
	const FString File = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Validation/BoundedRecovery") /
		(Run.IsEmpty() ? FGuid::NewGuid().ToString(EGuidFormats::Digits) : Run) / TEXT("Profiles.sqlite"));
	ADD_LATENT_AUTOMATION_COMMAND(FBoundedRecoveryFinished(
		this, Async(EAsyncExecution::ThreadPool, [File, Phase, Crash] {
			TArray<FString> Errors;
			auto Check = [&](bool OK, const TCHAR *M) {
				if (!OK)
					Errors.Add(M);
			};
			FProject_JSQLiteRepository DB(File);
			const FGuid Character(0xB0ADED, 2, 3, 4), Session(5, 6, 7, 8);
			TArray<uint8> SessionBytes;
			FMemoryWriter W(SessionBytes);
			auto Copy = Session;
			W << Copy;
			const FAggregateKey Lease{TEXT("LocalProfileLease"), Character}, Profile{TEXT("LocalProfile"), Character};
			auto Request = [&](uint32 Sequence, uint8 Value) {
				TArray<uint8> Payload;
				Payload.Init(Value, 60000);
				return FCommitRequest{FGuid(Session.A, Session.B, Session.C, Sequence),
									  TEXT("ProfileEdit.0"),
									  {{Lease, int64(Sequence), 1, SessionBytes, false},
									   {Profile, int64(Sequence - 1), 1, MoveTemp(Payload), false}}};
			};
			auto Next = Request(65, 29);
			FRecord L, P;
			if (Phase.IsEmpty() || Phase == TEXT("Initialize"))
			{
				Check(!IFileManager::Get().FileExists(*File),
					  TEXT("Never initialize over an existing recovery fixture"));
				if (!Errors.IsEmpty())
					return Errors;
				Check(DB.CommitProfile(
							{FGuid::NewGuid(), TEXT("LocalProfileSession"), {{Lease, 0, 1, SessionBytes, false}}},
							{Character, Session, -1})
							  .Status == ECommitStatus::Committed,
					  TEXT("Initialize real bounded profile lease"));
				for (uint32 Seq = 1; Seq <= 64; ++Seq)
					if (DB.CommitProfile(Request(Seq, 17), {Character, Session, Seq}, {1}).Status !=
						ECommitStatus::Committed)
					{
						Errors.Add(TEXT("Initial receipt window write failed"));
						break;
					}
				if (Phase == TEXT("Initialize"))
				{
					DB.Close();
					return Errors;
				}
			}
			if (Crash)
			{
				Check(IFileManager::Get().FileExists(*File), TEXT("Only crash an explicitly initialized fixture"));
				if (!Errors.IsEmpty())
					return Errors;
				if (Phase == TEXT("BeforeBegin"))
					FPlatformMisc::RequestExitWithStatus(true, 99);
				DB.TestHook = [Phase](const TCHAR *Point, FProject_JProfileDatabase &Native) {
					if (FCString::Strcmp(Point, TEXT("AfterBegin")) == 0)
						Native.Execute(TEXT("PRAGMA cache_size=1"));
					if (Phase == Point)
						FPlatformMisc::RequestExitWithStatus(true, 99);
					if (Phase == TEXT("DuringCommit") && FCString::Strcmp(Point, TEXT("BeforeCommit")) == 0)
						Native.CrashAfterNextSyncForTest();
				};
				DB.CommitProfile(Next, {Character, Session, 65}, {1});
				Errors.Add(TEXT("Owned crash point failed to exit"));
				return Errors;
			}
			const bool After = Phase == TEXT("CheckAfter");
			Check(DB.Read(Lease, L) && DB.Read(Profile, P) && L.Revision == (After ? 66 : 65) &&
					  P.Revision == (After ? 65 : 64) && !P.Payload.IsEmpty() && P.Payload[0] == (After ? 29 : 17),
				  TEXT("Native recovery restores state and lease at the same boundary"));
			Check(DB.FindProfileReceipt(FGuid(Session.A, Session.B, Session.C, 1), TEXT("ProfileEdit.0"),
										{Character, Session, 1}, {1})
						  .Status == (After ? ECommitStatus::Expired : ECommitStatus::Replayed),
				  TEXT("Cleanup floor and oldest result recover atomically with state"));
			Check(DB.FindProfileReceipt(Next.IdempotencyKey, Next.Operation, {Character, Session, 65}, {1}).Status ==
					  (After ? ECommitStatus::Replayed : ECommitStatus::Invalid),
				  TEXT("Newest receipt and state have the same recovery boundary"));
			auto Result = DB.CommitProfile(Next, {Character, Session, 65}, {1});
			Check(Result.Status == (After ? ECommitStatus::Replayed : ECommitStatus::Committed),
				  TEXT("Exact retry after interruption pays once"));
			Check(DB.FindProfileReceipt(FGuid(Session.A, Session.B, Session.C, 1), TEXT("ProfileEdit.0"),
										{Character, Session, 1}, {1})
						  .Status == ECommitStatus::Expired,
				  TEXT("Recovered pruning never re-admits retired requests"));
			FProject_JRepositoryUsage Usage;
			Check(DB.GetUsage(Usage) && Usage.Records == 2 && Usage.Receipts == 65,
				  TEXT("Recovery retains the bounded result window"));
			DB.Close();
			return Errors;
		})));
	return true;
}
#endif
