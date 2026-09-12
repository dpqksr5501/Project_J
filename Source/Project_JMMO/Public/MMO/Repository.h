#pragma once
#include "MMO/Coordination.h"

namespace ProjectJ::MMO
{
struct FRecord
{
    int64 Revision = 0; // 0 means absent; committed versions start at 1.
    int32 SchemaVersion = 1;
    TArray<uint8> Payload;
    bool bDeleted = false; // Tombstones retain the revision; no ABA on recreation.
};
struct PROJECT_JMMO_API FMutation
{
    FAggregateKey Key;
    int64 ExpectedRevision = 0;
    int32 SchemaVersion = 1;
    TArray<uint8> Payload;
    bool bDelete = false;
    bool operator==(const FMutation& Other) const;
};
struct FCommitRequest
{
    FGuid IdempotencyKey; // Existing FProject_JIdempotencyKey.Value at UE adapter boundary.
    FName Operation;
    TArray<FMutation> Mutations;
};
enum class ECommitStatus : uint8 { Committed, Replayed, Conflict, Invalid, KeyReused, Capacity };
struct FCommitResult
{
    ECommitStatus Status = ECommitStatus::Invalid;
    TArray<int64> Revisions; // Same order as mutations; replay returns original revisions.
};

/** Storage port for server-authorized values. Implementations must atomically
 * validate all revisions, write all records AND store the idempotency receipt.
 * Implementing this interface does not authenticate a caller or authorize a command.
 * Blocking providers run on a bounded I/O executor, never the game thread.
 */
class PROJECT_JMMO_API IRepository
{
public:
    virtual ~IRepository() = default;
    virtual bool Read(const FAggregateKey& Key, FRecord& OutRecord) const = 0;
    virtual FCommitResult Commit(const FCommitRequest& Request) = 0;
};

/** Test/development adapter ONLY. No restart durability or cross-process safety.
 * Bounded receipts fail closed at capacity; never silently evict deduplication.
 */
class PROJECT_JMMO_API FMemoryRepository final : public IRepository
{
public:
    explicit FMemoryRepository(int32 InMaxRecords = 1024, int32 InMaxReceipts = 4096)
        : MaxRecords(FMath::Max(1, InMaxRecords)), MaxReceipts(FMath::Max(1, InMaxReceipts)) {}
    bool Read(const FAggregateKey& Key, FRecord& OutRecord) const override;
    FCommitResult Commit(const FCommitRequest& Request) override;
private:
    struct FReceipt { FName Operation; TArray<FMutation> Mutations; TArray<int64> Revisions; };
    mutable FCriticalSection Mutex;
    const int32 MaxRecords, MaxReceipts;
    TMap<FAggregateKey, FRecord> Records;
    TMap<FGuid, FReceipt> Receipts;
};

// Durable publication belongs in the same backing transaction as the commit.
// Consumers deduplicate EventId and use AggregateRevision to detect stale/gapped streams.
struct FDomainEvent
{
    FGuid EventId;
    FGuid CorrelationId;
    FAggregateKey Aggregate;
    int64 AggregateRevision = 0;
    FName Type;
    int32 SchemaVersion = 1;
    TArray<uint8> Payload;
};
}
