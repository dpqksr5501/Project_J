#pragma once

#include "CoreMinimal.h"
#include "Project_JGameplayAsyncTypes.generated.h"

/**
 * Identifies one data-only gameplay job. The worker owns only copied plain data;
 * UObject, Actor, Component, and World access belongs to the game-thread apply step.
 */
USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JGameplayAsyncRequestToken
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gameplay|Async")
	int64 Value = 0;

	bool IsValid() const { return Value != 0; }
};

UENUM(BlueprintType)
enum class EProject_JGameplayAsyncWorkKind : uint8
{
	None,
	TargetQuery,
	PathPreparation,
	CrowdSteering,
	DataPreprocess
};

/**
 * Contract for future gameplay jobs. It intentionally does not start threads yet:
 * a concrete user must provide copied input, cancellation/epoch handling, and a
 * game-thread-only result-application path before dispatching work.
 */
USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JGameplayAsyncWorkContract
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gameplay|Async")
	EProject_JGameplayAsyncWorkKind WorkKind = EProject_JGameplayAsyncWorkKind::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gameplay|Async")
	bool bRequiresGameThreadApply = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gameplay|Async")
	bool bMayDiscardStaleResult = true;
};
