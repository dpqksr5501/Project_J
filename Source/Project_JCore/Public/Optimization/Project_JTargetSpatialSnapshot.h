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
	/** Takes ownership of the input value allocation and filters invalid entries in place, retaining stable order. */
	bool Build(TArray<FSpatialTarget> Input, double InCellSize = CellSize);
	/** XY grid broad phase, exact 3D sphere filter, stable Id order. Oversized queries use a bounded full scan. */
	bool Query(const FVector& Origin, double Radius, int32 ExcludedTeam, TArray<int32>& OutIndices, FSpatialQueryStats& Stats) const;
	/** Exact presence query for activation consumers. Stops at the first match without allocating/sorting an array. */
	bool HasAny(const FVector& Origin, double Radius, int32 ExcludedTeam, bool& OutFound, FSpatialQueryStats& Stats) const;
	const TArray<FSpatialTarget>& GetTargets() const { return Targets; }
private:
	bool TryCell(const FVector& Position, FIntPoint& OutCell) const;
	bool QueryCells(const FVector& Origin, double Radius, FIntPoint& MinCell, FIntPoint& MaxCell) const;
	double GridCellSize = CellSize;
	TArray<FSpatialTarget> Targets;
	struct FCellIndices { int32 First = INDEX_NONE, Last = INDEX_NONE; };
	TMap<FIntPoint, FCellIndices> Cells;
	// Snapshot-local indices, not UObject links. One bounded allocation replaces
	// an independently allocated array per cell; append order is preserved.
	TArray<int32> NextInCell;
	TArray<int32> Unindexed;
};
}
