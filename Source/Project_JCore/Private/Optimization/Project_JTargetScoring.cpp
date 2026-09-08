#include "Optimization/Project_JTargetScoring.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace ProjectJ::TargetScoring
{
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
	TArray<double> Scores;
	Scores.Init(-1.0, Snapshot.Candidates.Num());
	const int32 NumBatches = FMath::DivideAndRoundUp(Scores.Num(), BatchSize);
	const auto EvaluateBatch = [&](int32 Batch)
	{
		if (Cancelled.load(std::memory_order_relaxed)) { return; }
		const int32 End = FMath::Min((Batch + 1) * BatchSize, Scores.Num());
		for (int32 Index = Batch * BatchSize; Index < End; ++Index)
		{
			const FCandidate& Candidate = Snapshot.Candidates[Index];
			if (Candidate.Id < 0 || Candidate.Position.ContainsNaN()) { continue; }
			const FVector Offset = Candidate.Position - Snapshot.Origin;
			const double Distance = Offset.Size();
			if (!FMath::IsFinite(Distance) || Distance > Snapshot.Range) { continue; }
			const double Facing = FMath::Clamp(FVector::DotProduct(Forward, Offset.GetSafeNormal()), -1.0, 1.0);
			Scores[Index] = Snapshot.DistanceWeight * (1.0 - Distance / Snapshot.Range)
				+ Snapshot.DirectionWeight * (Facing + 1.0) * 0.5;
		}
	};
	if (bParallel && NumBatches > 1)
	{
		ParallelFor(TEXT("ProjectJ_TargetScoring_Batches"), NumBatches, 1, EvaluateBatch, EParallelForFlags::Unbalanced);
	}
	else
	{
		for (int32 Batch = 0; Batch < NumBatches; ++Batch) { EvaluateBatch(Batch); }
	}
	if (Cancelled.load(std::memory_order_relaxed))
	{
		Result.Status = EStatus::Cancelled;
		return Finish();
	}
	Result.Status = EStatus::Completed;
	for (int32 Index = 0; Index < Scores.Num(); ++Index)
	{
		if (Scores[Index] < 0.0) { continue; }
		++Result.EligibleCandidates;
		const int32 Id = Snapshot.Candidates[Index].Id;
		if (Result.BestId == INDEX_NONE || Scores[Index] > Result.BestScore
			|| (Scores[Index] == Result.BestScore && Id < Result.BestId))
		{
			Result.BestId = Id;
			Result.BestScore = Scores[Index];
		}
	}
	return Finish();
}
}
