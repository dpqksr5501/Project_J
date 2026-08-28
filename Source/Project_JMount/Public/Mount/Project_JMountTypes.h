#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Project_JMountTypes.generated.h"

/** Stable reason returned by both interaction UI and authority validation. */
UENUM(BlueprintType)
enum class EProject_JMountEligibilityFailure : uint8
{
	None,
	InvalidRider,
	Occupied,
	MountUnavailable,
	MissingController,
	AlreadyMounted,
	RiderInCombat,
	RiderBusy,
	RiderDead,
	TooFar
};

namespace Project_J::Mount
{
	/** Pure policy shared by the actor validation path and automation tests. */
	PROJECT_JMOUNT_API EProject_JMountEligibilityFailure EvaluateRiderGameplayTags(
		const FGameplayTagContainer& RiderTags);
}

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
