#pragma once

#include "CoreMinimal.h"

/** Animation-domain elapsed time. UE supplies dilated/URO accumulated delta;
 * do not multiply world dilation again or advance from a real-time watchdog. */
struct FProject_JAnimationClock
{
	double Seconds = 0.0;
	void Advance(float AnimationDelta, bool bPaused)
	{
		if (!bPaused && FMath::IsFinite(AnimationDelta) && AnimationDelta > 0.0f) { Seconds += AnimationDelta; }
	}
};
