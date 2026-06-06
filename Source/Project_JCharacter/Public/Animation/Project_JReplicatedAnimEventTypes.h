#pragma once

#include "CoreMinimal.h"
#include "Project_JReplicatedAnimEventTypes.generated.h"

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JReplicatedAnimEventState
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 MoveStartCounter = 0;

	UPROPERTY()
	bool bMoveStartWasSprinting = false;

	UPROPERTY()
	uint8 MoveStopCounter = 0;

	UPROPERTY()
	uint8 JumpStartCounter = 0;

	UPROPERTY()
	uint8 FallOffStartCounter = 0;

	UPROPERTY()
	uint8 LandingCancelCounter = 0;
};
