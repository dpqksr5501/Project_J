#include "Optimization/Project_JTargetSpatialSnapshot.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace ProjectJ::TargetScoring
{
bool FTargetSpatialSnapshot::TryCell(const FVector& Position, FIntPoint& OutCell)
{
	if (Position.ContainsNaN()) { return false; }
	const double X = FMath::FloorToDouble(Position.X / CellSize);
	const double Y = FMath::FloorToDouble(Position.Y / CellSize);
	if (X < MIN_int32 || X > MAX_int32 || Y < MIN_int32 || Y > MAX_int32) { return false; }
	OutCell = FIntPoint(static_cast<int32>(X), static_cast<int32>(Y));
	return true;
}

bool FTargetSpatialSnapshot::Build(TArray<FSpatialTarget> Input)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_TargetSpatial_Build);
	Targets.Reset(); Cells.Reset(); Unindexed.Reset();
	if (Input.Num() > MaxEntries) { return false; }
	TSet<int32> Ids;
	Targets.Reserve(Input.Num());
	for (const auto& Target : Input)
	{
		if (Target.Id < 0 || Target.Team < 0 || Target.Position.ContainsNaN()) { continue; }
		if (Ids.Contains(Target.Id)) { Targets.Reset(); Cells.Reset(); Unindexed.Reset(); return false; }
		Ids.Add(Target.Id);
		const int32 Index = Targets.Add(Target);
		FIntPoint Cell;
		if (TryCell(Target.Position, Cell)) { Cells.FindOrAdd(Cell).Add(Index); }
		else { Unindexed.Add(Index); } // Extreme finite coordinates remain queryable without integer overflow.
	}
	return true;
}

bool FTargetSpatialSnapshot::Query(const FVector& Origin, double Radius, int32 ExcludedTeam,
	TArray<int32>& OutIndices, FSpatialQueryStats& Stats) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_TargetSpatial_Query);
	OutIndices.Reset(); Stats = {};
	if (Origin.ContainsNaN() || !FMath::IsFinite(Radius) || Radius < 0.0 || Radius > 1.e9) { return false; }
	const double RadiusSquared = FMath::Square(Radius);
	const auto Consider = [&](int32 Index)
	{
		++Stats.CandidatesVisited;
		const auto& Target = Targets[Index];
		if (Target.Team != ExcludedTeam && FVector::DistSquared(Target.Position, Origin) <= RadiusSquared) { OutIndices.Add(Index); }
	};
	FIntPoint MinCell = FIntPoint::ZeroValue;
	FIntPoint MaxCell = FIntPoint::ZeroValue;
	bool bUseGrid = TryCell(Origin - FVector(Radius, Radius, 0), MinCell)
		&& TryCell(Origin + FVector(Radius, Radius, 0), MaxCell);
	if (bUseGrid)
	{
		const int64 Width = int64(MaxCell.X) - MinCell.X + 1;
		const int64 Height = int64(MaxCell.Y) - MinCell.Y + 1;
		// Check dimensions before multiplying so extreme ranges cannot overflow the product.
		bUseGrid = Width > 0 && Height > 0 && Width <= MaxQueryCells && Height <= MaxQueryCells
			&& Width * Height <= MaxQueryCells;
	}
	if (!bUseGrid)
	{
		Stats.bLinearFallback = true;
		for (int32 Index = 0; Index < Targets.Num(); ++Index) { Consider(Index); }
	}
	else
	{
		for (int64 X = MinCell.X; X <= int64(MaxCell.X); ++X)
		{
			for (int64 Y = MinCell.Y; Y <= int64(MaxCell.Y); ++Y)
			{
				++Stats.CellsVisited;
				if (const auto* Indices = Cells.Find(FIntPoint(static_cast<int32>(X), static_cast<int32>(Y))))
				{
					for (int32 Index : *Indices) { Consider(Index); }
				}
			}
		}
		for (int32 Index : Unindexed) { Consider(Index); }
	}
	// Grid traversal order must not change the existing component's equal-score tie break.
	OutIndices.Sort([&](int32 A, int32 B) { return Targets[A].Id < Targets[B].Id; });
	return true;
}
}
