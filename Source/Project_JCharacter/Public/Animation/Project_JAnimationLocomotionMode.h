#pragma once

#include "CoreMinimal.h"
#include "Project_JAnimationLocomotionMode.generated.h"

/**
 * Selects the persistent full-body locomotion context. This is deliberately
 * coarse-grained: mount species, vehicle seats, and swim styles belong to the
 * linked-layer profile/state machine inside a context, not to this enum.
 */
UENUM(BlueprintType)
enum class EProject_JAnimationLocomotionMode : uint8
{
	OnFoot UMETA(DisplayName = "On Foot"),
	Mounted UMETA(DisplayName = "Mounted"),
	Swimming UMETA(DisplayName = "Swimming"),
	Vehicle UMETA(DisplayName = "Vehicle"),
	Transformed UMETA(DisplayName = "Transformed")
};
