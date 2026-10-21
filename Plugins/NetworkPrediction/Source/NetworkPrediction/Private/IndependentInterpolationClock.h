// Project J patch to UE 5.8.3 NetworkPrediction. See the plugin's PROJECT_J_PATCH.md.
#pragma once
#include "CoreMinimal.h"

namespace UE::Net::Private
{
enum class EIndependentClockResult : uint8 { Waiting, Running, Initialized, Behind, InvalidTime };

inline EIndependentClockResult AdvanceIndependentInterpolationClock(float& TimeMS, int32 LatestMS, int32 DesiredMS, int32 MaxMS, float DeltaMS)
{
	if (LatestMS <= DesiredMS) return EIndependentClockResult::Waiting;
	EIndependentClockResult Result=EIndependentClockResult::Running;
	if (TimeMS==0.f)
	{
		// The first timestamp is an epoch, not accumulated buffered history.
		TimeMS=float(LatestMS-DesiredMS);
		Result=EIndependentClockResult::Initialized;
	}
	float BufferedMS=LatestMS-TimeMS;
	if (BufferedMS>MaxMS)
	{
		// Preserve the engine's exceptional catch-up policy and warning.
		TimeMS=float(LatestMS-DesiredMS); BufferedMS=float(DesiredMS);
		Result=EIndependentClockResult::Behind;
	}
	if (BufferedMS<0.f) return EIndependentClockResult::InvalidTime;
	// Small clock-rate adjustments rebuild the existing target buffer after
	// jitter. Neither the configured latency nor the maximum buffer grows.
	const float Rate=FMath::Clamp(BufferedMS/FMath::Max(1.f,float(DesiredMS)),.9f,1.1f);
	// A stalled frame must never consume a future state that has not arrived.
	// Keep a valid interpolation interval; resume monotonically on reception.
	const float AvailableMS=FMath::Max(0.f,BufferedMS-1.f);
	TimeMS+=FMath::Min(FMath::Max(0.f,DeltaMS)*Rate,AvailableMS);
	return Result;
}
}
