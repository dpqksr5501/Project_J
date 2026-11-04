#include "Optimization/Project_JTargetScoring.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace ProjectJ::TargetScoring
{
namespace
{
TAutoConsoleVariable<int32> ParallelMinCandidates(TEXT("ProjectJ.TargetScoring.ParallelMinCandidates"), 4096,
    TEXT("Minimum candidate values before subdividing a worker query/batch. 1 restores previous subdivision for comparison. Root-task admission and GT ownership are unchanged."));
int32 ParallelThreshold() { return FMath::Clamp(ParallelMinCandidates.GetValueOnAnyThread(), 1, MaxCandidates); }
}
FBatchResult EvaluateBatch(TConstArrayView<FSnapshot> Snapshots, bool bParallel, const std::atomic<bool>& Cancelled)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_TargetScoring_QueryBatch);
	FBatchResult Batch;
	const double Started = FPlatformTime::Seconds();
	if (Snapshots.IsEmpty() || Snapshots.Num() > MaxQueriesPerBatch) { return Batch; }
	int32 TotalCandidates = 0;
	for (const FSnapshot& Snapshot : Snapshots)
	{
		if (Snapshot.Candidates.Num() > MaxCandidates - TotalCandidates) { return Batch; }
		TotalCandidates += Snapshot.Candidates.Num();
	}
	Batch.Results.SetNum(Snapshots.Num());
	if (bParallel && Snapshots.Num() > 1 && TotalCandidates >= ParallelThreshold())
	{
		ParallelFor(TEXT("ProjectJ_TargetScoring_Queries"), Snapshots.Num(), 1, [&](int32 Index)
		{
			Batch.Results[Index] = Evaluate(Snapshots[Index], false, Cancelled);
		}, EParallelForFlags::Unbalanced);
	}
	else
	{
		for (int32 Index = 0; Index < Snapshots.Num(); ++Index)
		{
			Batch.Results[Index] = Evaluate(Snapshots[Index], bParallel, Cancelled);
		}
	}
	Batch.ComputeMicroseconds = (FPlatformTime::Seconds() - Started) * 1.e6;
	return Batch;
}

FResult Evaluate(const FSnapshot& Snapshot, bool bParallel, const std::atomic<bool>& Cancelled)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_TargetScoring_Compute);
	const double Started = FPlatformTime::Seconds();
	FResult Result;
	const auto Finish = [&]() { Result.ComputeMicroseconds = (FPlatformTime::Seconds() - Started) * 1.e6; return Result; };
	if (Cancelled.load(std::memory_order_relaxed))
	{
		Result.Status = EStatus::Cancelled;
		return Finish();
	}
	if (Snapshot.Candidates.Num() > MaxCandidates || Snapshot.Origin.ContainsNaN() || Snapshot.Forward.ContainsNaN()
		|| !FMath::IsFinite(Snapshot.Range) || Snapshot.Range <= 0.0 || Snapshot.Range > 1.e9
		|| !FMath::IsFinite(Snapshot.DistanceWeight) || Snapshot.DistanceWeight < 0.0 || Snapshot.DistanceWeight > 1000.0
		|| !FMath::IsFinite(Snapshot.DirectionWeight) || Snapshot.DirectionWeight < 0.0 || Snapshot.DirectionWeight > 1000.0)
	{
		return Finish();
	}
	// Zero forward has a neutral direction score; it does not invalidate a proximity query.
	const FVector Forward = Snapshot.Forward.GetSafeNormal();
	struct FReduction
	{
		int32 BestId = INDEX_NONE;
		double BestScore = 0.0;
		int32 Eligible = 0;
		void Include(int32 Id, double Score)
		{
			if (BestId == INDEX_NONE || Score > BestScore || (Score == BestScore && Id < BestId))
			{ BestId = Id; BestScore = Score; }
		}
	};
	const auto EvaluateRange = [&](int32 Begin, int32 End)
	{
		FReduction Partial;
		// Retain the existing 128-candidate cooperative cancellation granularity.
		if (Cancelled.load(std::memory_order_relaxed)) { return Partial; }
		for (int32 Index = Begin; Index < End; ++Index)
		{
			const FCandidate& Candidate = Snapshot.Candidates[Index];
			if (Candidate.Id < 0 || Candidate.Position.ContainsNaN()) { continue; }
			const FVector Offset = Candidate.Position - Snapshot.Origin;
			const double Distance = Offset.Size();
			if (!FMath::IsFinite(Distance) || Distance > Snapshot.Range) { continue; }
			const double Facing = FMath::Clamp(FVector::DotProduct(Forward, Offset.GetSafeNormal()), -1.0, 1.0);
			const double Score = Snapshot.DistanceWeight * (1.0 - Distance / Snapshot.Range)
				+ Snapshot.DirectionWeight * (Facing + 1.0) * 0.5;
			++Partial.Eligible;
			Partial.Include(Candidate.Id, Score);
		}
		return Partial;
	};
	FReduction Reduced;
	const auto Merge = [&Reduced](const FReduction& Partial)
	{
		Reduced.Eligible += Partial.Eligible;
		if (Partial.BestId != INDEX_NONE) { Reduced.Include(Partial.BestId, Partial.BestScore); }
	};
	const int32 NumBatches = FMath::DivideAndRoundUp(Snapshot.Candidates.Num(), BatchSize);
	if (bParallel && NumBatches > 1 && Snapshot.Candidates.Num() >= ParallelThreshold())
	{
		// Only one final write per chunk; query-parallel batches never enter this branch.
		// Capacity is the existing MaxCandidates / BatchSize bound, not candidate-count scratch.
		TArray<FReduction, TInlineAllocator<(MaxCandidates + BatchSize - 1) / BatchSize>> Partials;
		Partials.SetNum(NumBatches);
		ParallelFor(TEXT("ProjectJ_TargetScoring_Batches"), NumBatches, 1, [&](int32 Batch)
		{
			Partials[Batch] = EvaluateRange(Batch * BatchSize, FMath::Min((Batch + 1) * BatchSize, Snapshot.Candidates.Num()));
		}, EParallelForFlags::Unbalanced);
		for (const auto& Partial : Partials) { Merge(Partial); }
	}
	else
	{
		// NPC query batches parallelize across queries: each one now streams once without heap scratch.
		for (int32 Batch = 0; Batch < NumBatches; ++Batch)
		{
			Merge(EvaluateRange(Batch * BatchSize, FMath::Min((Batch + 1) * BatchSize, Snapshot.Candidates.Num())));
		}
	}
	if (Cancelled.load(std::memory_order_relaxed))
	{
		Result.Status = EStatus::Cancelled;
		return Finish();
	}
	Result.Status = EStatus::Completed;
	Result.BestId = Reduced.BestId;
	Result.BestScore = Reduced.BestScore;
	Result.EligibleCandidates = Reduced.Eligible;
	return Finish();
}
}
