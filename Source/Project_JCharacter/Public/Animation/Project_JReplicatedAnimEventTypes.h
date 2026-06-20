#pragma once

#include "CoreMinimal.h"
#include "Project_JReplicatedAnimEventTypes.generated.h"

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JReplicatedAnimEventState
{
	GENERATED_BODY()

	UPROPERTY()
	int32 MoveSequence = 0;

	UPROPERTY()
	bool bIsMoving = false;

	UPROPERTY()
	bool bIsSprinting = false;

	UPROPERTY()
	uint8 FallOffStartCounter = 0;

	UPROPERTY()
	uint8 LandingCancelCounter = 0;
};
