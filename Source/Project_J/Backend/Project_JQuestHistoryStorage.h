#pragma once

#include "Backend/Project_JLocalProfileSnapshot.h"
#include "MMO/Repository.h"

class FProject_JSQLiteRepository;

/** Local profile record composition, not another live state owner or I/O scheduler. */
namespace ProjectJ::LocalProfileHistory
{
inline constexpr int32 LocalProfileRecordLimit = 4096;
using FProfileValues = TSharedPtr<const FProject_JLocalProfileSnapshot, ESPMode::ThreadSafe>;

ProjectJ::MMO::FAggregateKey PageKey(FGuid Character, int32 Index);
int32 PageIndex(const ProjectJ::MMO::FAggregateKey& Key);

/** Worker-only. Returns a complete immutable value or null; never publishes a core-only history. */
FProfileValues ReadProfileValues(FProject_JSQLiteRepository& Repository, FGuid Character,
    const TArray<uint8>& CoreBytes, const FProfileValues& Known = {}, int64 ExpectedCoreRevision = -1);
/** Pure value hydrate for a coherently captured offline inspection. Pages start at Known.Count/64. */
FProfileValues HydrateProfileValues(FGuid Character, const TArray<uint8>& CoreBytes,
    const TArray<ProjectJ::MMO::FRecord>& Pages, const FProfileValues& Known = {});

/** Worker-only. Reuses the prefix; reads only affected tail/new page revisions.
 * Failure leaves Request unchanged. A page cannot commit without its character's core and lease. */
bool PrepareHistoryMutations(FProject_JSQLiteRepository& Repository, FGuid Character,
    const FProject_JLocalProfileSnapshot& Before, const FProject_JLocalProfileSnapshot& After,
    ProjectJ::MMO::FCommitRequest& Request, FString& Failure);

/** Transaction-local validation for CommitProfile, supplied already-read CAS records.
 * Missing records have Revision0. This does not authenticate/authorize a domain command. */
bool ValidateCommit(const ProjectJ::MMO::FCommitRequest& Request, FGuid Character,
    const ProjectJ::MMO::FRecord* OldCore, const TMap<int32, ProjectJ::MMO::FRecord>& OldPages,
    FString& Failure);
}
