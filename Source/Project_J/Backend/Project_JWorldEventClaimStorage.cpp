#include "Backend/Project_JWorldEventClaimStorage.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
namespace ProjectJ::WorldEventClaims
{
ProjectJ::MMO::FAggregateKey Key(FGuid Character) { return {TEXT("LocalProfileWorldEventClaims"), Character}; }
bool Encode(const TArray<FClaim>& Claims, TArray<uint8>& Out)
{
    if (Claims.IsEmpty() || Claims.Num() > MaxClaims) return false;
    TArray<uint8> Bytes; FMemoryWriter W(Bytes); int32 Count = Claims.Num(); W << Count;
    TSet<FGuid> Seen;
    for (auto Claim : Claims)
    {
        if (!Claim.Round.IsValid() || Claim.Experience <= 0 || Seen.Contains(Claim.Round)) return false;
        Seen.Add(Claim.Round); W << Claim.Round << Claim.Experience;
    }
    Out = MoveTemp(Bytes); return true;
}
bool Decode(const ProjectJ::MMO::FRecord& Record, TArray<FClaim>& Out)
{
    if (Record.Revision == 0 && Record.Payload.IsEmpty() && !Record.bDeleted) { Out.Reset(); return true; }
    if (Record.Revision <= 0 || Record.SchemaVersion != 1 || Record.bDeleted || Record.Payload.Num() < 4 ||
        Record.Payload.Num() > 4 + MaxClaims * 24) return false;
    FMemoryReader R(Record.Payload); int32 Count = 0; R << Count;
    if (Count <= 0 || Count > MaxClaims || Record.Payload.Num() != 4 + Count * 24) return false;
    TArray<FClaim> Claims; TSet<FGuid> Seen;
    for (int32 N = 0; N < Count; ++N)
    {
        FClaim Claim; R << Claim.Round << Claim.Experience;
        if (!Claim.Round.IsValid() || Claim.Experience <= 0 || Seen.Contains(Claim.Round)) return false;
        Seen.Add(Claim.Round); Claims.Add(Claim);
    }
    if (R.IsError()) return false;
    Out = MoveTemp(Claims); return true;
}
bool IsAppend(const ProjectJ::MMO::FRecord& Before, const TArray<uint8>& After)
{
    TArray<FClaim> Old, Next;
    ProjectJ::MMO::FRecord Proposed; Proposed.Revision = 1; Proposed.Payload = After;
    if (!Decode(Before, Old) || !Decode(Proposed, Next) || Next.Num() != Old.Num() + 1) return false;
    for (int32 N = 0; N < Old.Num(); ++N)
        if (Old[N].Round != Next[N].Round || Old[N].Experience != Next[N].Experience) return false;
    return true;
}
}
