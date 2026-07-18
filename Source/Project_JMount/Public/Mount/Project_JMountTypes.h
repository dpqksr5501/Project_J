#pragma once

#include "CoreMinimal.h"
#include "Project_JMountTypes.generated.h"

/** Shared mount lifecycle used by player and mount actors. */
UENUM(BlueprintType)
enum class EProject_JMountState : uint8
{
	Unmounted,
	Mounting,
	Mounted,
	Dismounting
};

/** Replicated flight phase. Persistent phases drive animation and input rules. */
UENUM(BlueprintType)
enum class EProject_JMountFlightState : uint8
{
	Grounded,
	TakingOff,
	AutoAscending,
	Flying,
	Landing
};

/** Animation-authored cue shared by flight sequences and server phase logic. */
UENUM(BlueprintType)
enum class EProject_JMountFlightAnimationCue : uint8
{
	TakeOffImpulse,
	LandingTouchdown
};
