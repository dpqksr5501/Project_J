#pragma once

#include "CoreMinimal.h"
#include "Project_JCombatHitValidation.generated.h"

UENUM(BlueprintType)
enum class EProject_JCombatHitValidationFailure : uint8
{
	None,
	InvalidRequester,
	InvalidTarget,
	InvalidTimestamp,
	RequestTooOld,
	RequestFromFuture,
	InvalidTrace,
	TraceTooLong,
	TargetTooFar,
	TraceOriginTooFar,
	TargetOutsideAttackArc
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JCombatHitRequest
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Hit Validation")
	TObjectPtr<AActor> Target = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Hit Validation")
	float ClientTimestamp = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Hit Validation")
	FVector TraceStart = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Hit Validation")
	FVector TraceEnd = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JCombatHitValidationPolicy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Hit Validation", meta = (ClampMin = "0.0"))
	float MaxRequestAge = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Hit Validation", meta = (ClampMin = "0.0"))
	float MaxFutureTolerance = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Hit Validation", meta = (ClampMin = "0.0"))
	float MaxTraceLength = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Hit Validation", meta = (ClampMin = "0.0"))
	float MaxTargetDistance = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Hit Validation", meta = (ClampMin = "0.0"))
	float MaxTraceStartDistance = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Hit Validation", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float MinTargetFacingDot = 0.0f;

	EProject_JCombatHitValidationFailure ValidateRequestData(
		const FProject_JCombatHitRequest& Request,
		float ServerTimeSeconds) const;

	EProject_JCombatHitValidationFailure ValidateSpatialData(
		const FVector& RequesterLocation,
		const FVector& RequesterForward,
		const FVector& TargetLocation,
		const FProject_JCombatHitRequest& Request) const;

	EProject_JCombatHitValidationFailure ValidateActors(
		const AActor* Requester,
		const FProject_JCombatHitRequest& Request) const;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JCombatHitValidationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Hit Validation")
	bool bAccepted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Hit Validation")
	EProject_JCombatHitValidationFailure Failure = EProject_JCombatHitValidationFailure::InvalidRequester;

	static FProject_JCombatHitValidationResult Accepted()
	{
		FProject_JCombatHitValidationResult Result;
		Result.bAccepted = true;
		Result.Failure = EProject_JCombatHitValidationFailure::None;
		return Result;
	}

	static FProject_JCombatHitValidationResult Rejected(EProject_JCombatHitValidationFailure FailureReason)
	{
		FProject_JCombatHitValidationResult Result;
		Result.Failure = FailureReason;
		return Result;
	}
};
