#pragma once

#include "CoreMinimal.h"
#include <atomic>

namespace ProjectJ::TargetScoring
{
inline constexpr int32 MaxCandidates = 16384;
inline constexpr int32 BatchSize = 128;

struct FCandidate
{
	int32 Id = INDEX_NONE;
	FVector Position = FVector::ZeroVector;
};

/** Copied on the game thread, immutable after dispatch. No object references. */
struct FSnapshot
{
	FVector Origin = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	double Range = 2000.0;
	double DistanceWeight = 1.0;
	double DirectionWeight = 1.0;
	TArray<FCandidate> Candidates;
};

enum class EStatus : uint8 { Completed, InvalidInput, Cancelled, Expired };

struct FResult
{
	EStatus Status = EStatus::InvalidInput;
	int32 BestId = INDEX_NONE;
	double BestScore = 0.0;
	int32 EligibleCandidates = 0;
	double ComputeMicroseconds = 0.0;
};

/** Parallel mode writes disjoint array elements and reduces in stable input order. */
PROJECT_JCORE_API FResult Evaluate(const FSnapshot& Snapshot, bool bParallel,
	const std::atomic<bool>& Cancelled);

/** Module unload only: cancel and join data-only tasks before their code is unloaded. */
PROJECT_JCORE_API void DrainTasksForModuleShutdown();
}
