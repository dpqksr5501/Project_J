#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
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
	TargetOutsideAttackArc,
	NoActiveAttack,
	AttackNodeMismatch,
	HitWindowClosed,
	DuplicateTarget,
	RequestRateLimited
};

inline const TCHAR* LexToString(EProject_JCombatHitValidationFailure Failure)
{
	switch (Failure)
	{
	case EProject_JCombatHitValidationFailure::None: return TEXT("None");
	case EProject_JCombatHitValidationFailure::InvalidRequester: return TEXT("InvalidRequester");
	case EProject_JCombatHitValidationFailure::InvalidTarget: return TEXT("InvalidTarget");
	case EProject_JCombatHitValidationFailure::InvalidTimestamp: return TEXT("InvalidTimestamp");
	case EProject_JCombatHitValidationFailure::RequestTooOld: return TEXT("RequestTooOld");
	case EProject_JCombatHitValidationFailure::RequestFromFuture: return TEXT("RequestFromFuture");
	case EProject_JCombatHitValidationFailure::InvalidTrace: return TEXT("InvalidTrace");
	case EProject_JCombatHitValidationFailure::TraceTooLong: return TEXT("TraceTooLong");
	case EProject_JCombatHitValidationFailure::TargetTooFar: return TEXT("TargetTooFar");
	case EProject_JCombatHitValidationFailure::TraceOriginTooFar: return TEXT("TraceOriginTooFar");
	case EProject_JCombatHitValidationFailure::TargetOutsideAttackArc: return TEXT("TargetOutsideAttackArc");
	case EProject_JCombatHitValidationFailure::NoActiveAttack: return TEXT("NoActiveAttack");
	case EProject_JCombatHitValidationFailure::AttackNodeMismatch: return TEXT("AttackNodeMismatch");
	case EProject_JCombatHitValidationFailure::HitWindowClosed: return TEXT("HitWindowClosed");
	case EProject_JCombatHitValidationFailure::DuplicateTarget: return TEXT("DuplicateTarget");
	case EProject_JCombatHitValidationFailure::RequestRateLimited: return TEXT("RequestRateLimited");
	default: return TEXT("Unknown");
	}
}

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Hit Validation")
	FGameplayTag AttackNodeTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Hit Validation")
	int32 RequestSequence = 0;
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
