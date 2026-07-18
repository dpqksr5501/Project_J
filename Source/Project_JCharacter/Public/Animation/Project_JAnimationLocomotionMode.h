#pragma once

#include "CoreMinimal.h"
#include "Project_JAnimationLocomotionMode.generated.h"

/**
 * Selects the player's full-body locomotion animation layer. This represents
 * persistent movement context, not a short interaction montage.
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
