#include "Animation/Project_JAnimationQualityPolicy.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Components/SceneComponent.h"
#include "Project_JPlayerCharacter.h"
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

bool IsClothExecutionProtected(const APawn* Pawn)
{
	if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !Pawn->HasActorBegunPlay() ||
		Pawn->GetLocalRole() == ROLE_None || Pawn->GetLocalRole() == ROLE_AutonomousProxy || IsLocalPlayerAvatar(Pawn)) return true;
	// Reuse CombatPresentation's owner/instigator/attachment locally-viewed policy.
	// This also protects a spectator view while physical control remains elsewhere.
	const USceneComponent* Current = Pawn->GetRootComponent();
	if(!IsValid(Current)) return true;
	for (int32 Depth = 0; IsValid(Current) && Depth < 16; ++Depth, Current = Current->GetAttachParent())
	{
		const AActor* ObservedOwner = Current->GetOwner();
		if (!IsValid(ObservedOwner)) return true;
		const APawn* ObservedPawn = Cast<APawn>(ObservedOwner);
		const APawn* ObservedInstigator = ObservedOwner->GetInstigator();
		if ((ObservedPawn && ObservedPawn->IsLocallyViewed()) ||
			(IsValid(ObservedInstigator) && ObservedInstigator->IsLocallyViewed())) return true;
	}
	if (Current) return true; // Unresolved/malformed deep attachment chain.
	const auto* RiderComponent = Pawn->FindComponentByClass<UProject_JMountComponent>();
	const auto* ObservedMount = RiderComponent ? RiderComponent->GetMountedMount() : nullptr;
	if (!ObservedMount) ObservedMount = Cast<AProject_JMountCharacter>(Pawn->GetAttachParentActor());
	if (ObservedMount)
	{
		if (!IsValid(ObservedMount) || ObservedMount->IsActorBeingDestroyed() || ObservedMount->GetRider() != Pawn ||
			ObservedMount->GetWorld() != Pawn->GetWorld() || ObservedMount->GetMountState() != EProject_JMountState::Mounted)
			return true;
		const auto* MountController = ObservedMount->GetController();
		if (MountController) return !IsValid(MountController) || MountController->IsActorBeingDestroyed() ||
			MountController->GetPawn() != ObservedMount;
		// Foreign controllers are intentionally absent on simulated clients.
		const auto* MountState = ObservedMount->GetPlayerState();
		return Pawn->GetNetMode() != NM_Client || Pawn->GetLocalRole() != ROLE_SimulatedProxy ||
			ObservedMount->GetLocalRole() != ROLE_SimulatedProxy || !IsValid(MountState) || MountState->IsActorBeingDestroyed() ||
			MountState->GetWorld()!=Pawn->GetWorld() || MountState->GetPawn() != ObservedMount;
	}
	const auto* Controller=Pawn->GetController();
	if(Controller && (!IsValid(Controller) || Controller->IsActorBeingDestroyed() ||
		Controller->GetWorld()!=Pawn->GetWorld() || Controller->GetPawn()!=Pawn)) return true;
	const auto* State = Pawn->GetPlayerState();
	if(!State && Pawn->IsA<AProject_JPlayerCharacter>()) return true; // Player identity not replicated/resolved yet.
	if (State && (!IsValid(State) || State->IsActorBeingDestroyed() || State->GetWorld() != Pawn->GetWorld() || State->GetPawn() != Pawn)) return true;
	// Missing foreign PC and ordinary AI/no-controller NPCs are not automatically unknown.
	return false;
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
bool ResolveSecondaryMotionQuality(const FSecondaryMotionObservation& I)
{
	// Keep an already unavailable route unavailable; this optional budget only
	// narrows accessory simulation admitted by the existing body graph policy.
	if (!I.bRouteAllows) return false;
	if (!I.bEnabled || !I.bAssetOptIn || I.bLocal || I.bOptOut || !I.DistanceTier.IsSet()) return true;
	const float Tier = I.DistanceTier.GetValue();
	if (!FMath::IsFinite(Tier) || Tier < 0.f || Tier > 3.f) return true;
	return Tier <= 1.f; // Existing local/Near/Mid quality remains full; remote Far is optional accessory work.
}
float ResolveVisualInterval(bool bProtected, bool bHidden, float Significance, bool bSourceThrottled)
{
	if (bProtected || bHidden) return 0.f;
	return Significance > 1.f ? 1.f / 15 : Significance > 0.f ? 1.f / 30 : bSourceThrottled ? 1.f / 30 : 0.f;
}
}
