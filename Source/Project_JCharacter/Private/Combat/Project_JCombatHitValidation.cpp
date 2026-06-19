#include "Combat/Project_JCombatHitValidation.h"

#include "GameFramework/Actor.h"

EProject_JCombatHitValidationFailure FProject_JCombatHitValidationPolicy::ValidateRequestData(
	const FProject_JCombatHitRequest& Request,
	float ServerTimeSeconds) const
{
	if (!FMath::IsFinite(Request.ClientTimestamp) || !FMath::IsFinite(ServerTimeSeconds))
	{
		return EProject_JCombatHitValidationFailure::InvalidTimestamp;
	}

	const float RequestAge = ServerTimeSeconds - Request.ClientTimestamp;
	if (RequestAge < -FMath::Max(0.0f, MaxFutureTolerance))
	{
		return EProject_JCombatHitValidationFailure::RequestFromFuture;
	}
	if (RequestAge > FMath::Max(0.0f, MaxRequestAge))
	{
		return EProject_JCombatHitValidationFailure::RequestTooOld;
	}

	if (Request.TraceStart.ContainsNaN() || Request.TraceEnd.ContainsNaN())
	{
		return EProject_JCombatHitValidationFailure::InvalidTrace;
	}

	if (FVector::DistSquared(Request.TraceStart, Request.TraceEnd) > FMath::Square(FMath::Max(0.0f, MaxTraceLength)))
	{
		return EProject_JCombatHitValidationFailure::TraceTooLong;
	}

	return EProject_JCombatHitValidationFailure::None;
}

EProject_JCombatHitValidationFailure FProject_JCombatHitValidationPolicy::ValidateActors(
	const AActor* Requester,
	const FProject_JCombatHitRequest& Request) const
{
	if (!Requester || !Requester->HasAuthority())
	{
		return EProject_JCombatHitValidationFailure::InvalidRequester;
	}

	if (!IsValid(Request.Target) || Request.Target == Requester)
	{
		return EProject_JCombatHitValidationFailure::InvalidTarget;
	}

	if (FVector::DistSquared(Requester->GetActorLocation(), Request.Target->GetActorLocation()) >
		FMath::Square(FMath::Max(0.0f, MaxTargetDistance)))
	{
		return EProject_JCombatHitValidationFailure::TargetTooFar;
	}

	return EProject_JCombatHitValidationFailure::None;
}
