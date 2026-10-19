#pragma once

#include "Backend/Project_JProfileDatabase.h"
#include "MMO/Repository.h"

/** Profile stream: no wall-clock expiry. Session replacement retires every old
 * ticket; each committed sequence retires results older than the last 64 commands.
 * Sequence zero is the replaceable checkpoint; -1 opens a fresh session. */
struct FProject_JProfileStream
{
	FGuid Character, Session;
	int64 Sequence = 0;
};
enum class EProject_JRecordRead : uint8
{
	Found,
	Missing,
	Unavailable
};
struct FProject_JRepositoryUsage
{
	int64 Records = 0, Receipts = 0;
	int32 RecordLimit = 1024, ReceiptLimit = 4096;
};

/** Local single-host durable adapter for the existing value repository.
 * Worker-only, per-connection project Win32 rollback VFS. An OS named mutex bounds
 * project connection admission; native SQLite byte locks also protect against
 * independent native tools. Revision/receipt validation belongs to the transaction.
 * Not distributed storage, authentication, or a claim of power-loss durability.
 * Records, tombstones and exact command receipts commit in one transaction.
 */
class FProject_JSQLiteRepository final : public ProjectJ::MMO::IRepository
{
  public:
	explicit FProject_JSQLiteRepository(FString InFilename, int32 InMaxRecords = 1024, int32 InMaxReceipts = 4096);
	bool Read(const ProjectJ::MMO::FAggregateKey &Key, ProjectJ::MMO::FRecord &Out) const override;
	EProject_JRecordRead ReadChecked(const ProjectJ::MMO::FAggregateKey &Key, ProjectJ::MMO::FRecord &Out) const;
	ProjectJ::MMO::FCommitResult Commit(const ProjectJ::MMO::FCommitRequest &Request) override;
	ProjectJ::MMO::FCommitResult CommitProfile(const ProjectJ::MMO::FCommitRequest &Request,
											   const FProject_JProfileStream &Stream, const TArray<uint8> &Intent = {});
	ProjectJ::MMO::FCommitResult FindProfileReceipt(FGuid Key, FName Operation, const FProject_JProfileStream &Stream,
													const TArray<uint8> &Intent) const;
	void Close(); // Worker-only; no open connection is released on the game thread.
	bool BackupToNewFile(const FString &Destination, int32 MaximumFiles = MAX_int32);
	bool GetUsage(FProject_JRepositoryUsage &Out) const;
	FString DescribeFailure() const;
	/** Result lookup for an unchanged logical command after reconnect/response loss.
	 * Invalid means absent. Intent is opaque bounded command data, not an interned name. */
	ProjectJ::MMO::FCommitResult FindReceipt(FGuid Key, FName Operation, const TArray<uint8> &Intent = {}) const;
	ProjectJ::MMO::FCommitResult CommitWithIntent(const ProjectJ::MMO::FCommitRequest &Request,
												  const TArray<uint8> &Intent);
#if WITH_DEV_AUTOMATION_TESTS
	/** Per-owned-fixture fault points; no production command-line crash switch. */
	TFunction<void(const TCHAR *, FProject_JProfileDatabase &)> TestHook;
	bool bTestLoseCommitResponse = false;
#endif
  private:
	ProjectJ::MMO::FCommitResult CommitInternal(const ProjectJ::MMO::FCommitRequest &Request,
												const TArray<uint8> &Intent,
												const FProject_JProfileStream *Stream = nullptr);
	ProjectJ::MMO::FCommitResult FindReceiptInternal(FGuid Key, FName Operation, const TArray<uint8> &Intent,
													 const FProject_JProfileStream *Stream) const;
	bool OpenLocked() const;
	EProject_JRecordRead ReadLocked(const ProjectJ::MMO::FAggregateKey &Key, ProjectJ::MMO::FRecord &Out) const;
	mutable FCriticalSection Mutex;
	mutable FProject_JProfileDatabase Database;
	mutable FString LastFailure;
	FString Filename;
	FString LockName;
	int32 MaxRecords, MaxReceipts;
};
