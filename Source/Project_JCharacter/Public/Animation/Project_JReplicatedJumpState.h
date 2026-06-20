#pragma once

#include "CoreMinimal.h"
#include "Project_JReplicatedJumpState.generated.h"

/**
 * Server-authoritative visual contract for one accepted jump.
 *
 * This intentionally contains presentation data only. CharacterMovement
 * remains authoritative for movement, prediction, correction, and physics.
 */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JReplicatedJumpState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Jump")
	int32 Sequence = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Jump")
	float ServerStartTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Jump")
	FVector_NetQuantize10 LaunchVelocity = FVector::ZeroVector;
};

/** Pure policy kept independent from components for testing and future movement implementations. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JRemoteJumpPredictionPolicy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Jump", meta = (ClampMin = "0.0"))
	float MinUpwardSpeed = 80.0f;

	bool ShouldPredict(
		bool bWasInAir,
		bool bIsInAir,
		float VerticalSpeed,
		bool bJumpStartActive,
		bool bLandingActive,
		bool bFallOffStartActive) const
	{
		return
			!bWasInAir &&
			bIsInAir &&
			VerticalSpeed >= MinUpwardSpeed &&
			!bJumpStartActive &&
			!bLandingActive &&
			!bFallOffStartActive;
	}
};
