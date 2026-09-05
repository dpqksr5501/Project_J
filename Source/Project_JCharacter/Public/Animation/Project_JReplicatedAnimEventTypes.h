#pragma once

#include "CoreMinimal.h"
#include "Project_JReplicatedAnimEventTypes.generated.h"

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JReplicatedAnimEventState
{
	GENERATED_BODY()

	UPROPERTY()
	int32 MoveSequence = 0;

	/** Monotonic server-side ordering shared by movement and landing boundaries. */
	UPROPERTY()
	int32 MoveEventOrder = 0;

	UPROPERTY()
	bool bIsMoving = false;

	UPROPERTY()
	bool bIsSprinting = false;

	UPROPERTY()
	float MoveServerTimeSeconds = 0.0f;

	/** Physical landing identity. A revision can also mark that landing cancelled. */
	UPROPERTY()
	int32 LandingSequence = 0;

	UPROPERTY()
	int32 LandingRevision = 0;

	UPROPERTY()
	int32 LandingEventOrder = 0;

	UPROPERTY()
	float LandingServerTimeSeconds = 0.0f;

	UPROPERTY()
	float LandingImpactSpeed = 0.0f;

	UPROPERTY()
	bool bLandingActive = false;

	UPROPERTY()
	bool bLandingWasMoving = false;

	UPROPERTY()
	bool bLandingWasSprinting = false;

	UPROPERTY()
	bool bLandingWasHeavy = false;

	UPROPERTY()
	int32 FallOffStartCounter = 0;

	UPROPERTY()
	int32 FallOffEventOrder = 0;

	UPROPERTY()
	uint8 LandingCancelCounter = 0;

	/** Monotonic identity for a server-confirmed stationary combat turn. */
	UPROPERTY()
	int32 TurnInPlaceSequence = 0;

	/** Shares the event ordering domain with movement, landing, and fall events. */
	UPROPERTY()
	int32 TurnInPlaceEventOrder = 0;

	/** Server world time at which the visual turn was accepted. */
	UPROPERTY()
	float TurnInPlaceServerTimeSeconds = 0.0f;

	/** Chooser-compatible bucket: 1=L90, 2=L180, 3=R90, 4=R180. */
	UPROPERTY()
	uint8 TurnInPlaceDirectionBucket = 0;

	/** Absolute local facing target captured at the semantic TIP edge. */
	UPROPERTY()
	float TurnInPlaceTargetFacingYaw = 0.0f;
};
