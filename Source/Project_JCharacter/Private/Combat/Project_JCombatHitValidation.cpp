#include "Combat/Project_JCombatHitValidation.h"

#include "Combat/Project_JMultiPartHitComponent.h"
#include "GameFramework/Actor.h"

EProject_JCombatHitValidationFailure FProject_JCombatHitValidationPolicy::ValidateRequestData(
	const FProject_JCombatHitRequest& Request,
	double ServerTimeSeconds) const
{
	if (!FMath::IsFinite(Request.ClientTimestamp) || !FMath::IsFinite(ServerTimeSeconds))
	{
		return EProject_JCombatHitValidationFailure::InvalidTimestamp;
	}

	const double RequestAge = ServerTimeSeconds - Request.ClientTimestamp;
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

	// Account for large boss capsule/multi-part extents so attacking a limb or tail
	// does not fail the actor-origin distance check.
	float TargetExtentAllowance = 0.0f;
	if (const UProject_JMultiPartHitComponent* MultiPart = Request.Target->FindComponentByClass<UProject_JMultiPartHitComponent>())
	{
		for (const FProject_JBossHitPartSpec& Part : MultiPart->HitParts)
		{
			TargetExtentAllowance = FMath::Max(TargetExtentAllowance, static_cast<float>(Part.LocalOffset.Size()) + Part.SphereRadius);
		}
		TargetExtentAllowance = FMath::Clamp(TargetExtentAllowance, 0.0f, 1500.0f);
	}
	else
	{
		float ColRadius = 0.0f, ColHalfHeight = 0.0f;
		Request.Target->GetSimpleCollisionCylinder(ColRadius, ColHalfHeight);
		if (ColRadius > 80.0f)
		{
			TargetExtentAllowance = FMath::Clamp(ColRadius, 0.0f, 600.0f);
		}
	}

	if (TargetExtentAllowance > 0.0f)
	{
		FProject_JCombatHitValidationPolicy AdjustedPolicy = *this;
		AdjustedPolicy.MaxTargetDistance = FMath::Max(0.0f, MaxTargetDistance) + TargetExtentAllowance;
		return AdjustedPolicy.ValidateSpatialData(
			Requester->GetActorLocation(),
			Requester->GetActorForwardVector(),
			Request.Target->GetActorLocation(),
			Request);
	}

	return ValidateSpatialData(
		Requester->GetActorLocation(),
		Requester->GetActorForwardVector(),
		Request.Target->GetActorLocation(),
		Request);
}

EProject_JCombatHitValidationFailure FProject_JCombatHitValidationPolicy::ValidateSpatialData(
	const FVector& RequesterLocation,
	const FVector& RequesterForward,
	const FVector& TargetLocation,
	const FProject_JCombatHitRequest& Request) const
{
	if (FVector::DistSquared(RequesterLocation, TargetLocation) >
		FMath::Square(FMath::Max(0.0f, MaxTargetDistance)))
	{
		return EProject_JCombatHitValidationFailure::TargetTooFar;
	}

	if (FVector::DistSquared(RequesterLocation, Request.TraceStart) >
		FMath::Square(FMath::Max(0.0f, MaxTraceStartDistance)))
	{
		return EProject_JCombatHitValidationFailure::TraceOriginTooFar;
	}

	const FVector TargetDirection = (TargetLocation - RequesterLocation).GetSafeNormal();
	const FVector ForwardDirection = RequesterForward.GetSafeNormal();
	if (!TargetDirection.IsNearlyZero() &&
		!ForwardDirection.IsNearlyZero() &&
		FVector::DotProduct(ForwardDirection, TargetDirection) < FMath::Clamp(MinTargetFacingDot, -1.0f, 1.0f))
	{
		return EProject_JCombatHitValidationFailure::TargetOutsideAttackArc;
	}

	return EProject_JCombatHitValidationFailure::None;
}
