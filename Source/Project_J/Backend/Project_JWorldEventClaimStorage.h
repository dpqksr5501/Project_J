#pragma once
#include "MMO/Repository.h"

/** Development claim ledger, independent of bounded session receipts. Never evict claims.
 * Capacity stops new rewards rather than weakening once-per-character/round semantics. */
namespace ProjectJ::WorldEventClaims
{
constexpr int32 MaxClaims = 1024;
struct FClaim { FGuid Round; int64 Experience = 0; };
ProjectJ::MMO::FAggregateKey Key(FGuid Character);
bool Decode(const ProjectJ::MMO::FRecord& Record, TArray<FClaim>& Out);
bool Encode(const TArray<FClaim>& Claims, TArray<uint8>& Out);
bool IsAppend(const ProjectJ::MMO::FRecord& Before, const TArray<uint8>& After);
}
