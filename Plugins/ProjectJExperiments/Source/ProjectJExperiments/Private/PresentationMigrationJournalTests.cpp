#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "PresentationMigrationJournal.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMigrationReceiptTest, "ProjectJ.Maturity.Tools.MigrationReceipt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMigrationReceiptTest::RunTest(const FString&)
{
	using namespace ProjectJ::Experiments::Migration;
	const FString Folder = Canonical(FPaths::ProjectSavedDir() / TEXT("Validation/MigrationJournalTest") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
	IFileManager::Get().MakeDirectory(*Folder, true);
	TArray<FEntry> Entries; TArray<FString> Targets;
	for (int32 I = 0; I < 2; ++I)
	{
		FEntry Entry; Entry.Target = Folder / FString::Printf(TEXT("%d-target.fixture"), I);
		Entry.Backup = Folder / FString::Printf(TEXT("%d-backup.fixture"), I); Entry.Staged = Folder / FString::Printf(TEXT("%d-staged.fixture"), I);
		FFileHelper::SaveStringToFile(TEXT("original"), *Entry.Target); FFileHelper::SaveStringToFile(TEXT("original"), *Entry.Backup);
		FFileHelper::SaveStringToFile(TEXT("planned"), *Entry.Staged);
		Entry.OriginalDigest = DigestFile(Entry.Target); Entry.PlannedDigest = DigestFile(Entry.Staged);
		Targets.Add(Entry.Target); Entries.Add(Entry);
	}
	const FString Receipt = Folder / TEXT("receipt.json");
	TestTrue(TEXT("Durable planned digests are written before publication"), WriteReceipt(Receipt, Entries, TEXT("ReadyToPublish")));
	ReplaceVerifiedFile(Entries[0].Target, Entries[0].Staged, Entries[0].PlannedDigest); // Interrupted after the first file.
	TestTrue(TEXT("Partial publication can be reconciled without changing files"), RestoreReceipt(Receipt, Targets, false));
	TestEqual(TEXT("Recovery dry run leaves the partial publication intact"), DigestFile(Entries[0].Target), Entries[0].PlannedDigest);
	FFileHelper::SaveStringToFile(TEXT("subsequent-author-edit"), *Entries[1].Target);
	TestFalse(TEXT("An unrelated later edit blocks the entire rollback before any write"), RestoreReceipt(Receipt, Targets, true));
	TestEqual(TEXT("First target remains intact after failed recovery preflight"), DigestFile(Entries[0].Target), Entries[0].PlannedDigest);
	ReplaceVerifiedFile(Entries[1].Target, Entries[1].Backup, Entries[1].OriginalDigest);
	TestTrue(TEXT("Verified partial publication restores every original"), RestoreReceipt(Receipt, Targets, true));
	TestEqual(TEXT("First target restored"), DigestFile(Entries[0].Target), Entries[0].OriginalDigest);
	TestTrue(TEXT("Recovery can be safely rerun"), RestoreReceipt(Receipt, Targets, true));
	FFileHelper::SaveStringToFile(TEXT("corrupt-backup"), *Entries[0].Backup);
	TestFalse(TEXT("Backup corruption cannot overwrite source files"), RestoreReceipt(Receipt, Targets, true));
	return true;
}
#endif
