#include "Optimization/Project_JTargetSpatialSnapshot.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace ProjectJ::TargetScoring
{
bool FTargetSpatialSnapshot::TryCell(const FVector& Position, FIntPoint& OutCell) const
{
	if (Position.ContainsNaN()) { return false; }
	const double X = FMath::FloorToDouble(Position.X / GridCellSize);
	const double Y = FMath::FloorToDouble(Position.Y / GridCellSize);
	if (X < MIN_int32 || X > MAX_int32 || Y < MIN_int32 || Y > MAX_int32) { return false; }
	OutCell = FIntPoint(static_cast<int32>(X), static_cast<int32>(Y));
	return true;
}

bool FTargetSpatialSnapshot::Build(TArray<FSpatialTarget> Input, double InCellSize)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_TargetSpatial_Build);
	Targets.Reset(); Cells.Reset(); NextInCell.Reset(); Unindexed.Reset();
	if (Input.Num() > MaxEntries || !FMath::IsFinite(InCellSize) || InCellSize <= 0) { return false; }
	GridCellSize = InCellSize;
	TSet<int32> Ids;
	// Callers already own a value snapshot. Adopt that allocation instead of allocating/copying it again.
	Targets = MoveTemp(Input);
	NextInCell.Reserve(Targets.Num());
	int32 ValidCount = 0;
	for (int32 Read = 0; Read < Targets.Num(); ++Read)
	{
		const auto& Target = Targets[Read];
		if (Target.Id < 0 || Target.Team < 0 || Target.Position.ContainsNaN()) { continue; }
		if (Ids.Contains(Target.Id)) { Targets.Reset(); Cells.Reset(); NextInCell.Reset(); Unindexed.Reset(); return false; }
		Ids.Add(Target.Id);
		const int32 Index = ValidCount++;
		if (Index != Read) { Targets[Index] = Target; }
		NextInCell.Add(INDEX_NONE);
		FIntPoint Cell;
		if (TryCell(Target.Position, Cell))
		{
			auto& Indices = Cells.FindOrAdd(Cell);
			if (Indices.Last == INDEX_NONE) { Indices.First = Index; }
			else { NextInCell[Indices.Last] = Index; }
			Indices.Last = Index;
		}
		else { Unindexed.Add(Index); } // Extreme finite coordinates remain queryable without integer overflow.
	}
	Targets.SetNum(ValidCount, EAllowShrinking::No);
	return true;
}

bool FTargetSpatialSnapshot::QueryCells(const FVector& Origin, double Radius, FIntPoint& MinCell, FIntPoint& MaxCell) const
{
	if (!TryCell(Origin - FVector(Radius, Radius, 0), MinCell) || !TryCell(Origin + FVector(Radius, Radius, 0), MaxCell)) return false;
	const int64 Width = int64(MaxCell.X) - MinCell.X + 1;
	const int64 Height = int64(MaxCell.Y) - MinCell.Y + 1;
	// Check dimensions before multiplying so extreme ranges cannot overflow the product.
	return Width > 0 && Height > 0 && Width <= MaxQueryCells && Height <= MaxQueryCells && Width * Height <= MaxQueryCells;
}

bool FTargetSpatialSnapshot::Query(const FVector& Origin, double Radius, int32 ExcludedTeam,
	TArray<int32>& OutIndices, FSpatialQueryStats& Stats) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_TargetSpatial_Query);
	OutIndices.Reset(); Stats = {};
	if (Origin.ContainsNaN() || !FMath::IsFinite(Radius) || Radius < 0.0 || Radius > 1.e9) return false;
	const double RadiusSquared = FMath::Square(Radius);
	const auto Consider = [&](int32 Index)
	{
		++Stats.CandidatesVisited;
		const auto& Target = Targets[Index];
		if (Target.Team != ExcludedTeam && FVector::DistSquared(Target.Position, Origin) <= RadiusSquared) OutIndices.Add(Index);
	};
	FIntPoint MinCell, MaxCell;
	if (!QueryCells(Origin, Radius, MinCell, MaxCell))
	{
		Stats.bLinearFallback = true;
		for (int32 Index = 0; Index < Targets.Num(); ++Index) Consider(Index);
	}
	else
	{
		for (int64 X = MinCell.X; X <= int64(MaxCell.X); ++X)
			for (int64 Y = MinCell.Y; Y <= int64(MaxCell.Y); ++Y)
			{
				++Stats.CellsVisited;
				if (const auto* Indices = Cells.Find(FIntPoint(static_cast<int32>(X), static_cast<int32>(Y))))
					for (int32 Index = Indices->First; Index != INDEX_NONE; Index = NextInCell[Index]) Consider(Index);
			}
		for (int32 Index : Unindexed) Consider(Index);
	}
	// Preserve the equal-score tie break without adding per-candidate virtual calls.
	OutIndices.Sort([&](int32 A, int32 B) { return Targets[A].Id < Targets[B].Id; });
	return true;
}

bool FTargetSpatialSnapshot::HasAny(const FVector& Origin, double Radius, int32 ExcludedTeam,
	bool& OutFound, FSpatialQueryStats& Stats) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_TargetSpatial_Presence);
	OutFound = false; Stats = {};
	if (Origin.ContainsNaN() || !FMath::IsFinite(Radius) || Radius < 0.0 || Radius > 1.e9) return false;
	const double RadiusSquared = FMath::Square(Radius);
	const auto Consider = [&](int32 Index)
	{
		++Stats.CandidatesVisited;
		const auto& Target = Targets[Index];
		return Target.Team != ExcludedTeam && FVector::DistSquared(Target.Position, Origin) <= RadiusSquared;
	};
	FIntPoint MinCell, MaxCell, OriginCell;
	if (!QueryCells(Origin, Radius, MinCell, MaxCell))
	{
		Stats.bLinearFallback = true;
		for (int32 Index = 0; Index < Targets.Num(); ++Index)
			if (Consider(Index)) { OutFound = true; return true; }
		return true;
	}
	const bool bOriginIndexed = TryCell(Origin, OriginCell);
	if (bOriginIndexed)
	{
		++Stats.CellsVisited;
		if (const auto* Indices = Cells.Find(OriginCell))
			for (int32 Index = Indices->First; Index != INDEX_NONE; Index = NextInCell[Index]) if (Consider(Index)) { OutFound = true; return true; }
	}
	for (int64 X = MinCell.X; X <= int64(MaxCell.X); ++X)
		for (int64 Y = MinCell.Y; Y <= int64(MaxCell.Y); ++Y)
		{
			if (bOriginIndexed && X == OriginCell.X && Y == OriginCell.Y) continue;
			++Stats.CellsVisited;
			if (const auto* Indices = Cells.Find(FIntPoint(static_cast<int32>(X), static_cast<int32>(Y))))
				for (int32 Index = Indices->First; Index != INDEX_NONE; Index = NextInCell[Index]) if (Consider(Index)) { OutFound = true; return true; }
		}
	for (int32 Index : Unindexed) if (Consider(Index)) { OutFound = true; return true; }
	return true;
}
}
