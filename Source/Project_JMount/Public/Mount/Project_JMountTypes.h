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
