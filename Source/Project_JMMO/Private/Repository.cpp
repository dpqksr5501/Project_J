#include "MMO/Repository.h"
#include "Misc/ScopeLock.h"

namespace ProjectJ::MMO
{
bool FMutation::operator==(const FMutation& Other) const
{
    return Key == Other.Key && ExpectedRevision == Other.ExpectedRevision && SchemaVersion == Other.SchemaVersion &&
        Payload == Other.Payload && bDelete == Other.bDelete;
}
bool FMemoryRepository::Read(const FAggregateKey& Key, FRecord& OutRecord) const
{
    FScopeLock Lock(&Mutex);
    OutRecord = {};
    const auto* Record = Records.Find(Key);
    if (!Record) { return false; }
    OutRecord = *Record; return true; // Includes tombstones for revision checks.
}
FCommitResult FMemoryRepository::Commit(const FCommitRequest& Request)
{
    if (!Request.IdempotencyKey.IsValid() || Request.Operation.IsNone() ||
        Request.Mutations.IsEmpty() || Request.Mutations.Num() > 64) { return {}; }
    TSet<FAggregateKey> Keys;
    int64 Bytes = 0;
    for (const auto& Mutation : Request.Mutations)
    {
        Bytes += Mutation.Payload.Num();
        if (!Mutation.Key.IsValid() || Keys.Contains(Mutation.Key) || Mutation.ExpectedRevision < 0 ||
            Mutation.ExpectedRevision == MAX_int64 || Mutation.SchemaVersion < 1 || Mutation.Payload.Num() > 65536 ||
            (Mutation.bDelete && !Mutation.Payload.IsEmpty()) || Bytes > 262144) { return {}; }
        Keys.Add(Mutation.Key);
    }
    FScopeLock Lock(&Mutex);
    if (const auto* Receipt = Receipts.Find(Request.IdempotencyKey))
    {
        if (Receipt->Operation != Request.Operation || Receipt->Mutations != Request.Mutations)
        { return {ECommitStatus::KeyReused, {}}; }
        return {ECommitStatus::Replayed, Receipt->Revisions};
    }
    if (Receipts.Num() >= MaxReceipts) { return {ECommitStatus::Capacity, {}}; }
    int32 NewRecords = 0;
    for (const auto& Mutation : Request.Mutations)
    {
        const auto* Existing = Records.Find(Mutation.Key);
        if ((Existing ? Existing->Revision : 0) != Mutation.ExpectedRevision) { return {ECommitStatus::Conflict, {}}; }
        NewRecords += !Existing;
    }
    if (int64(Records.Num()) + NewRecords > MaxRecords) { return {ECommitStatus::Capacity, {}}; }
    FCommitResult Result { ECommitStatus::Committed, {} };
    for (const auto& Mutation : Request.Mutations)
    {
        const int64 Revision = Mutation.ExpectedRevision + 1;
        Records.Add(Mutation.Key, FRecord { Revision, Mutation.SchemaVersion, Mutation.Payload, Mutation.bDelete });
        Result.Revisions.Add(Revision);
    }
    Receipts.Add(Request.IdempotencyKey, FReceipt { Request.Operation, Request.Mutations, Result.Revisions });
    return Result;
}
}
