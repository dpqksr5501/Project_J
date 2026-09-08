#pragma once
#include "CoreMinimal.h"

namespace ProjectJ
{
// Pure value policy: bounded exponential delay with deterministic per-consumer dispersion.
inline double RetryDelay(uint32 Attempt, uint32 Seed, double BaseSeconds, double MaxSeconds)
{
	const uint32 Mix = (Seed ^ (Attempt * 747796405u)) * 2891336453u;
	const double Jitter = 0.8 + (Mix % 401) * 0.001;
	return FMath::Min(MaxSeconds, BaseSeconds * double(1u << FMath::Min(Attempt, 5u)) * Jitter);
}
}
