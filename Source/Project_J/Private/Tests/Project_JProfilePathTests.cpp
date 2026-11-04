#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS && PLATFORM_WINDOWS
#include "Async/Async.h"
#include "Backend/Project_JProfileDatabase.h"
#include "Backend/Project_JSQLiteRepository.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

namespace
{
class FProfilePathsFinished : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	TFuture<TArray<FString>> Work;
public:
	FProfilePathsFinished(FAutomationTestBase* InTest, TFuture<TArray<FString>>&& InWork)
		: Test(InTest), Work(MoveTemp(InWork)) {}
	bool Update() override
	{
		if (!Work.IsReady()) return false;
		for (const FString& Failure : Work.Get()) Test->AddError(Failure);
		return true;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProfilePathsTest, "ProjectJ.MMO.Persistence.ProfilePaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJProfilePathsTest::RunTest(const FString&)
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() /
		TEXT("Validation/ProfilePaths") / FGuid::NewGuid().ToString());
	ADD_LATENT_AUTOMATION_COMMAND(FProfilePathsFinished(this, Async(EAsyncExecution::ThreadPool, [Root] {
		using namespace ProjectJ::MMO;
		TArray<FString> Failures;
		auto Check = [&](bool Value, const TCHAR* Message) {
			if (!Value) Failures.Add(Message);
			return Value;
		};
		if (!Check(IFileManager::Get().MakeDirectory(*Root, true), TEXT("Create owned path-fixture root"))) return Failures;
		// Jump directly to an extended-length directory: the engine prefixes at
		// MAX_PATH, while legacy CreateDirectory has a smaller unprefixed limit.
		const FString Directory = Root / (TEXT("region_") + FString::ChrN(FMath::Max(1, 290 - Root.Len() - 8), TEXT('r')));
		if (!Check(IFileManager::Get().MakeDirectory(*Directory, true), TEXT("Create owned long-path directory"))) return Failures;
		const FString File = Directory / TEXT("프로필.sqlite");
		const FString Backup = Directory / TEXT("백업.sqlite");
		const FAggregateKey Key{TEXT("ProfilePathContract"), FGuid::NewGuid()};
		const FCommitRequest Request{FGuid::NewGuid(), TEXT("LongPathCommit"), {{Key, 0, 1, {7, 8, 9}, false}}};
		{
			FProject_JSQLiteRepository DB(File);
			if (!Check(DB.Commit(Request).Status == ECommitStatus::Committed, TEXT("Commit with DB and rollback journal beyond MAX_PATH"))) return Failures;
			Check(DB.BackupToNewFile(Backup), TEXT("VACUUM backup uses the same long-path VFS"));
			Check(!DB.BackupToNewFile(Backup), TEXT("Long-path backup preserves no-overwrite contract"));
			DB.Close();
		}
		{
			FProject_JSQLiteRepository Reopened(File), Restored(Backup);
			FRecord Record;
			Check(Reopened.Read(Key, Record) && Record.Revision == 1 && Record.Payload == TArray<uint8>{7, 8, 9}, TEXT("Reopen long Unicode profile retains committed payload"));
			Check(Reopened.Commit(Request).Status == ECommitStatus::Replayed, TEXT("Reopen retains exact-request receipt"));
			Check(Restored.Read(Key, Record) && Record.Payload == TArray<uint8>{7, 8, 9}, TEXT("Long-path backup is readable"));
			Reopened.Close(); Restored.Close();
		}
		sqlite3_vfs* VFS = sqlite3_vfs_find("projectj-win32-rollback");
		if (!Check(VFS != nullptr, TEXT("Per-connection VFS registered"))) return Failures;
		TArray<char> Canonical;
		Canonical.SetNumZeroed(VFS->mxPathname + 1);
		auto FullPath = [&](const FString& Path, FString& Out) {
			if (VFS->xFullPathname(VFS, TCHAR_TO_UTF8(*Path), Canonical.Num(), Canonical.GetData()) != SQLITE_OK) return false;
			Out = UTF8_TO_TCHAR(Canonical.GetData());
			return true;
		};
		FString Result;
		FString Native = File.Replace(TEXT("/"), TEXT("\\"));
		const FString Extended = TEXT("\\\\?\\") + Native;
		Check(FullPath(Extended, Result) && Result == File, TEXT("Explicit drive prefix resolves to the same logical SQLite path"));
		{
			FProject_JProfileDatabase Explicit;
			Check(Explicit.Open(*Extended, ESQLiteDatabaseOpenMode::ReadOnly) && Explicit.PerformQuickIntegrityCheck(),
				TEXT("Explicit extended drive filename opens the existing database"));
			Explicit.Close();
		}
		Check(FullPath(Directory / TEXT("ignored/../프로필.sqlite"), Result) && Result == File, TEXT("Dot segments normalize before native prefix conversion"));
		Check(FullPath(TEXT("\\\\?\\UNC\\projectj-path-contract\\share\\zone\\..\\profile.sqlite"), Result) &&
			Result == TEXT("//projectj-path-contract/share/profile.sqlite"), TEXT("UNC namespace conversion preserves server/share without touching a network share"));
		char Small[2] = {'x', 'y'};
		Check(VFS->xFullPathname(VFS, TCHAR_TO_UTF8(*File), UE_ARRAY_COUNT(Small), Small) == SQLITE_CANTOPEN &&
			Small[0] == 'x' && Small[1] == 'y', TEXT("Too-small full-path output is rejected without truncation"));
		int Exists = 0;
		Check(VFS->xAccess(VFS, TCHAR_TO_UTF8(*File), SQLITE_ACCESS_EXISTS, &Exists) == SQLITE_OK && Exists == 1,
			TEXT("Access uses native extended path for an existing DB"));
		Exists = 1;
		Check(VFS->xAccess(VFS, TCHAR_TO_UTF8(*(File + TEXT("-journal"))), SQLITE_ACCESS_EXISTS, &Exists) == SQLITE_OK && Exists == 0,
			TEXT("Rollback journal is deleted after commit at a long path"));
		{
			const FString Temporary = Directory / TEXT("temporary.sqlite");
			auto* Handle = static_cast<sqlite3_file*>(FMemory::Malloc(VFS->szOsFile));
			FMemory::Memzero(Handle, VFS->szOsFile);
			int OutFlags = 0;
			const int Flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_EXCLUSIVE | SQLITE_OPEN_DELETEONCLOSE;
			if (Check(VFS->xOpen(VFS, TCHAR_TO_UTF8(*Temporary), Handle, Flags, &OutFlags) == SQLITE_OK,
				TEXT("Long-path temporary VFS file opens")))
			{
				Check(Handle->pMethods->xClose(Handle) == SQLITE_OK, TEXT("Long-path temporary handle closes"));
				Exists = 1;
				Check(VFS->xAccess(VFS, TCHAR_TO_UTF8(*Temporary), SQLITE_ACCESS_EXISTS, &Exists) == SQLITE_OK && Exists == 0,
					TEXT("Delete-on-close uses the same resolved native filename"));
			}
			FMemory::Free(Handle);
		}
		// The original trigger can have a short DB path but an over-limit sidecar.
		if (Root.Len() < 245)
		{
			const FString Boundary = Root / (FString::ChrN(255 - Root.Len() - 8, TEXT('p')) + TEXT(".sqlite"));
			Check(Boundary.Len() == 255, TEXT("Short DB / long journal boundary fixture"));
			FProject_JSQLiteRepository BoundaryDB(Boundary);
			Check(BoundaryDB.Commit(Request).Status == ECommitStatus::Committed,
				TEXT("An independently normalized journal may cross MAX_PATH while its DB does not"));
			BoundaryDB.Close();
		}
		return Failures;
	})));
	return true;
}
#endif
