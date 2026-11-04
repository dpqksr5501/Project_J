#include "Combat/Project_JCombatHitValidation.h"

#include "GameFramework/Actor.h"
#include "GameFramework/PlayerState.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Project_JSocialStateInterface.h"
#include "UObject/StrongObjectPtr.h"

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

	if (const auto Failure = Project_J::Combat::ValidatePlayerMeleeTarget(Requester, Request.Target);
		Failure != EProject_JCombatHitValidationFailure::None) return Failure;

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


bool Project_J::Combat::CanPlayerMeleeAttack(const AActor* Attacker)
{
	if (!IsValid(Attacker) || Attacker->IsActorBeingDestroyed()) return false;
	const auto* Player = Cast<AProject_JPlayerCharacter>(Attacker);
	if (!Player) return true; // Existing NPC paths keep their own eligibility contract.
	const auto* ASC = Player->GetAbilitySystemComponent();
	const auto* Attributes = IsValid(ASC) ? ASC->GetSet<UProject_JAttributeSet>() : nullptr;
	return Player->IsPlayerLifeActive() && Attributes && ASC->GetAvatarActor() == Player && FMath::IsFinite(Attributes->GetHealth())
		&& Attributes->GetHealth() > 0.f;
}

EProject_JCombatHitValidationFailure Project_J::Combat::ValidatePlayerMeleeTarget(const AActor* Attacker, const AActor* Target)
{
	if (!CanPlayerMeleeAttack(Attacker)) return EProject_JCombatHitValidationFailure::AttackerIneligible;
	if (!IsValid(Target) || Target->IsActorBeingDestroyed() || Attacker == Target)
		return EProject_JCombatHitValidationFailure::InvalidTarget;
	const auto* SourcePlayer = Cast<AProject_JPlayerCharacter>(Attacker);
	const auto* TargetPlayer = Cast<AProject_JPlayerCharacter>(Target);
	if (!SourcePlayer || !TargetPlayer) return EProject_JCombatHitValidationFailure::None;
	TStrongObjectPtr<APlayerState> SourceState(SourcePlayer->GetPlayerState());
	TStrongObjectPtr<APlayerState> TargetState(TargetPlayer->GetPlayerState());
	if (!IsValid(SourceState.Get()) || !IsValid(TargetState.Get()) ||
		!SourceState->Implements<UProject_JSocialStateInterface>() || !TargetState->Implements<UProject_JSocialStateInterface>())
		return EProject_JCombatHitValidationFailure::None;
	FName SourceParty, TargetParty;
	const auto* SourceSocial = Cast<IProject_JSocialStateInterface>(SourceState.Get());
	const auto* TargetSocial = Cast<IProject_JSocialStateInterface>(TargetState.Get());
	if (!SourceSocial || !TargetSocial || !SourceSocial->TryGetAuthoritativePartyId(SourceParty)
		|| !TargetSocial->TryGetAuthoritativePartyId(TargetParty)) return EProject_JCombatHitValidationFailure::None;
	// Consume only native authority snapshots; Blueprint display overrides cannot authorize damage.
	if (!CanPlayerMeleeAttack(Attacker) || !IsValid(Target) || Target->IsActorBeingDestroyed() ||
		SourcePlayer->GetPlayerState() != SourceState.Get() || TargetPlayer->GetPlayerState() != TargetState.Get())
		return EProject_JCombatHitValidationFailure::AttackerIneligible;
	return !SourceParty.IsNone() && SourceParty == TargetParty
		? EProject_JCombatHitValidationFailure::FriendlyPlayerTarget : EProject_JCombatHitValidationFailure::None;
}
