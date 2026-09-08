#pragma once

#include "CoreMinimal.h"

namespace ProjectJ::TargetScoring
{
struct FSpatialTarget
{
	int32 Id = INDEX_NONE;
	FVector Position = FVector::ZeroVector;
	int32 Team = 0;
};

struct FSpatialQueryStats
{
	int32 CellsVisited = 0;
	int32 CandidatesVisited = 0;
	bool bLinearFallback = false;
};

/** Value-only snapshot. Build once, then query without modifying it. Returned indices belong to this snapshot. */
class PROJECT_JCORE_API FTargetSpatialSnapshot
{
public:
	static constexpr double CellSize = 1000.0;
	static constexpr int32 MaxQueryCells = 256;
	static constexpr int32 MaxEntries = 4096;
	bool Build(TArray<FSpatialTarget> Input);
	/** XY grid broad phase, exact 3D sphere filter, stable Id order. Oversized queries use a bounded full scan. */
	bool Query(const FVector& Origin, double Radius, int32 ExcludedTeam, TArray<int32>& OutIndices, FSpatialQueryStats& Stats) const;
	const TArray<FSpatialTarget>& GetTargets() const { return Targets; }
private:
	static bool TryCell(const FVector& Position, FIntPoint& OutCell);
	TArray<FSpatialTarget> Targets;
	TMap<FIntPoint, TArray<int32>> Cells;
	TArray<int32> Unindexed;
};
}
