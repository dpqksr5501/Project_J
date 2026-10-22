#include "Animation/Project_JAnimationQualityPolicy.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Mount/Project_JMountComponent.h"
#include "Mount/Project_JMountCharacter.h"

namespace ProjectJ::Animation
{
bool IsLocalPlayerAvatar(const APawn* Pawn)
{
	if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed()) return false;
	if (const auto* Controller=Pawn->GetController())
	{
		const auto* Player=Cast<APlayerController>(Controller);
		return IsValid(Player) && !Player->IsActorBeingDestroyed() && Player->IsLocalController();
	}
	const auto* Rider=Pawn->FindComponentByClass<UProject_JMountComponent>();
	const auto* Mount=Rider?Rider->GetMountedMount():nullptr;
	if (!Mount) Mount=Cast<AProject_JMountCharacter>(Pawn->GetAttachParentActor());
	if (!IsValid(Mount) || Mount->IsActorBeingDestroyed() || (Mount->GetRider() && Mount->GetRider()!=Pawn)) return false;
	const auto* Player=Cast<APlayerController>(Mount->GetController());
	return IsValid(Player) && !Player->IsActorBeingDestroyed() && Player->IsLocalController();
}

float ResolveAnimationDistanceTier(double DistanceSquared, const FProject_JAnimationBudgetSettings& S)
{
	// Malformed configuration preserves quality instead of silently reducing visual quality.
	if (!FMath::IsFinite(DistanceSquared) || DistanceSquared < 0 ||
		!FMath::IsFinite(S.NearDistance) || !FMath::IsFinite(S.MidDistance) || !FMath::IsFinite(S.FarDistance) ||
		S.NearDistance <= 0 || S.MidDistance <= S.NearDistance || S.FarDistance <= S.MidDistance) return 0.f;
	if (DistanceSquared < FMath::Square(double(S.NearDistance))) return 0.f;
	if (DistanceSquared < FMath::Square(double(S.MidDistance))) return 1.f;
	if (DistanceSquared < FMath::Square(double(S.FarDistance))) return 2.f;
	return 3.f; // Visible tier 3 still means Far, never actor hiding or network culling.
}

FProject_JAnimOptimizationPolicy ResolveSourceQuality(const FQualityObservation& I,
	const FProject_JAnimationBudgetSettings& S)
{
	FProject_JAnimOptimizationPolicy P;
	if (!I.bHasOwner || (!I.bLocal && !I.bGameplayPose && !I.bRecentlyRendered))
	{
		P.Tier = EProject_JAnimBudgetTier::Hidden;
		P.bEnableHandIK = P.bEnableFootIK = P.bEnableRetargetIK = P.bEnableFollowerRetarget = false;
		if (I.bHasOwner)
		{
			P.RetargetIKLODThreshold = P.RetargetLODThreshold = 0;
		}
		return P;
	}
	if (I.bLocal) { P.RetargetLODThreshold = -1; return P; }
	P.Tier = EProject_JAnimBudgetTier::Near;
	if (I.bGameplayPose) return P;
	if (!I.bHasSignificance || I.Significance <= 0.f)
	{
		P.RetargetIKLODThreshold = 1; P.RetargetLODThreshold = -1;
		return P;
	}
	P.bEnableHandIK = P.bEnableRetargetIK = false;
	if (I.Significance <= 1.f)
	{
		P.Tier = EProject_JAnimBudgetTier::Mid;
		P.RetargetLODThreshold = -1;
		return P;
	}
	P.Tier = EProject_JAnimBudgetTier::Far;
	P.bEnableFootIK = false;
	P.RetargetLODThreshold = 2;
	return P;
}
float ResolveVisualInterval(bool bProtected, bool bHidden, float Significance, bool bSourceThrottled)
{
	if (bProtected || bHidden) return 0.f;
	return Significance > 1.f ? 1.f / 15 : Significance > 0.f ? 1.f / 30 : bSourceThrottled ? 1.f / 30 : 0.f;
}
}
