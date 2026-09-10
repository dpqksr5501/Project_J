#include "Mass/Project_JMassMovementProcessor.h"
#include "MassExecutionContext.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
// Covers two opposing 100 cm steps plus the supported capsule diameters.
constexpr double CrowdCellSize = 400;
constexpr int32 MaxCellOccupants = 32;
struct FCrowdSample { FVector Position; uint64 Id; float Radius; float HalfHeight; };
struct FCrowdCell { TArray<int32, TInlineAllocator<8>> Indices; bool bOverflow = false; };
FIntPoint CrowdCell(const FVector& P)
{ return FIntPoint(FMath::FloorToInt(P.X / CrowdCellSize), FMath::FloorToInt(P.Y / CrowdCellSize)); }
}

UProject_JMassMovementProcessor::UProject_JMassMovementProcessor() : Query(*this)
{
	bAutoRegisterWithProcessingPhases = false;
	ExecutionFlags = int32(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Standalone);
}

void UProject_JMassMovementProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	Query.AddRequirement<FProjectJMassMovementFragment>(EMassFragmentAccess::ReadWrite);
}

void UProject_JMassMovementProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_MassMovement_DispatchAndJoin);
	// Complete the read snapshot before any chunk mutates positions. The const grid is shared only until join.
	TArray<FCrowdSample> Samples;
	TMap<FIntPoint, FCrowdCell> Grid;
	if (bUseCrowdSpacing)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_MassCrowd_Snapshot);
		Query.ForEachEntityChunk(Context, [&Samples, &Grid](FMassExecutionContext& Chunk)
		{
			for (const auto& Agent : Chunk.GetMutableFragmentView<FProjectJMassMovementFragment>())
			{
				if (Agent.Position.ContainsNaN()) { continue; }
				auto& Cell = Grid.FindOrAdd(CrowdCell(Agent.Position));
				if (Cell.Indices.Num() == MaxCellOccupants) { Cell.bOverflow = true; continue; }
				Cell.Indices.Add(Samples.Add({Agent.Position, Agent.StableId, Agent.Radius, Agent.HalfHeight}));
			}
		});
	}
	const auto& ReadGrid = Grid;
	const auto& ReadSamples = Samples;
	const bool bCrowd = bUseCrowdSpacing;
	const auto MoveChunk = [&ReadGrid, &ReadSamples, bCrowd](FMassExecutionContext& Chunk)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_MassMovement_Chunk);
		const float Delta = FMath::Clamp(Chunk.GetDeltaTimeSeconds(), 0.f, 0.1f);
		for (auto& Agent : Chunk.GetMutableFragmentView<FProjectJMassMovementFragment>())
		{
			Agent.NeighborVisits = 0;
			if (!Agent.bMassOwnsMovement || !Agent.bRouteValid || Agent.bNeedsCharacterTraversal) { continue; }
			double Remaining = Agent.Speed * Delta;
			if (bCrowd && Agent.NextPoint < Agent.RouteCount)
			{
				const FVector Direction = (Agent.Route[Agent.NextPoint] - Agent.Position).GetSafeNormal2D();
				double Clearance = CrowdCellSize;
				const auto Center = CrowdCell(Agent.Position);
				for (int32 Y = -1; Y <= 1; ++Y) for (int32 X = -1; X <= 1; ++X)
				{
					const auto* Cell = ReadGrid.Find(Center + FIntPoint(X, Y));
					if (!Cell) { continue; }
					// Dense overflow cannot silently omit blockers: request Character traversal instead.
					if (Cell->bOverflow) { Agent.bNeedsCharacterTraversal = true; }
					for (const int32 Index : Cell->Indices)
					{
						++Agent.NeighborVisits;
						const auto& Other = ReadSamples[Index];
						if (Other.Id == Agent.StableId || FMath::Abs(Other.Position.Z - Agent.Position.Z) > Agent.HalfHeight + Other.HalfHeight) { continue; }
						const FVector Offset = Other.Position - Agent.Position;
						const double Ahead = FVector::DotProduct(Offset, Direction);
						const double Radii = Agent.Radius + Other.Radius + 10;
						if (Ahead >= 0 && (Offset - Direction * Ahead).SizeSquared2D() < FMath::Square(Radii))
						{ Clearance = FMath::Min(Clearance, FMath::Max(0.0, Ahead - Radii)); }
					}
				}
				Remaining = FMath::Min(Remaining, Clearance * FMath::Min(1.0, double(Delta) / 0.25));
				Agent.BlockedSeconds = Remaining < 0.01 ? Agent.BlockedSeconds + Delta : 0;
				Agent.bNeedsCharacterTraversal |= Agent.BlockedSeconds > 0.75f;
				if (Agent.bNeedsCharacterTraversal) { continue; }
			}
			while (Remaining > 0 && Agent.NextPoint < Agent.RouteCount)
			{
				const FVector Offset = Agent.Route[Agent.NextPoint] - Agent.Position;
				const double Distance = Offset.Size();
				if (Distance <= Remaining)
				{
					Agent.Position = Agent.Route[Agent.NextPoint++]; Remaining -= Distance;
					if (bCrowd) { break; } // Recheck neighbors along the new segment next frame.
				}
				else { Agent.Position += Offset * (Remaining / Distance); break; }
			}
		}
	};
	if (bParallel) { Query.ParallelForEachEntityChunk(Context, MoveChunk, FMassEntityQuery::EParallelExecutionFlags::Force); }
	else { Query.ForEachEntityChunk(Context, MoveChunk); }
}
