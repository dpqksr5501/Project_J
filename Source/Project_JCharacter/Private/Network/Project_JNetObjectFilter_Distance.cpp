#include "Network/Project_JNetObjectFilter_Distance.h"
#include "Project_JSocialStateInterface.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

namespace
{
const UObject* ResolveSocialStateObject(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	if (Actor->Implements<UProject_JSocialStateInterface>())
	{
		return Actor;
	}

	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		if (const APlayerState* PlayerState = Pawn->GetPlayerState())
		{
			return PlayerState->Implements<UProject_JSocialStateInterface>() ? PlayerState : nullptr;
		}
	}

	if (const AController* Controller = Cast<AController>(Actor))
	{
		const APlayerState* PlayerState = Controller->GetPlayerState<APlayerState>();
		return PlayerState && PlayerState->Implements<UProject_JSocialStateInterface>() ? PlayerState : nullptr;
	}

	return nullptr;
}

bool SharesNonEmptySocialId(const UObject* ViewerState, const UObject* TargetState, bool bParty)
{
	if (!ViewerState || !TargetState)
	{
		return false;
	}

	const FName ViewerId = bParty
		? IProject_JSocialStateInterface::Execute_GetPartyId(ViewerState)
		: IProject_JSocialStateInterface::Execute_GetGuildId(ViewerState);
	const FName TargetId = bParty
		? IProject_JSocialStateInterface::Execute_GetPartyId(TargetState)
		: IProject_JSocialStateInterface::Execute_GetGuildId(TargetState);
	return !ViewerId.IsNone() && ViewerId == TargetId;
}

bool IsOwnedByViewer(const AActor* TargetActor, const AActor* ViewerActor)
{
	if (!TargetActor || !ViewerActor)
	{
		return false;
	}

	if (TargetActor == ViewerActor || TargetActor->IsOwnedBy(ViewerActor) || TargetActor->GetInstigator() == ViewerActor)
	{
		return true;
	}

	if (const APawn* ViewerPawn = Cast<APawn>(ViewerActor))
	{
		const AController* ViewerController = ViewerPawn->GetController();
		return ViewerController &&
			(TargetActor->IsOwnedBy(ViewerController) || TargetActor->GetInstigatorController() == ViewerController);
	}

	return false;
}
}

bool UProject_JNetObjectFilter_Distance::ShouldReplicateActorForViewer(const AActor* TargetActor, const FVector& ViewerLocation) const
{
	return BuildReplicationDecision(TargetActor, ViewerLocation).bShouldReplicate;
}

float UProject_JNetObjectFilter_Distance::GetReplicationDistanceSquared(const AActor* TargetActor, const FVector& ViewerLocation) const
{
	if (!TargetActor)
	{
		return TNumericLimits<float>::Max();
	}

	return FVector::DistSquared(TargetActor->GetActorLocation(), ViewerLocation);
}

FProject_JReplicationPolicyDecision UProject_JNetObjectFilter_Distance::BuildReplicationDecision(
	const AActor* TargetActor,
	const FVector& ViewerLocation,
	const AActor* ViewerActor) const
{
	return BuildReplicationDecisionWithSettings(TargetActor, ViewerLocation, PolicySettings, ViewerActor);
}

FProject_JReplicationPolicyDecision UProject_JNetObjectFilter_Distance::BuildReplicationDecisionWithSettings(
	const AActor* TargetActor,
	const FVector& ViewerLocation,
	const FProject_JReplicationPolicySettings& Settings,
	const AActor* ViewerActor) const
{
	FProject_JReplicationPolicyDecision Decision;
	if (!TargetActor)
	{
		return Decision;
	}

	Decision.DistanceSquared = GetReplicationDistanceSquared(TargetActor, ViewerLocation);

	const UObject* ViewerSocialState = ResolveSocialStateObject(ViewerActor);
	const UObject* TargetSocialState = ResolveSocialStateObject(TargetActor);
	if (SharesNonEmptySocialId(ViewerSocialState, TargetSocialState, true))
	{
		Decision.bShouldReplicate = true;
		Decision.AddReason(EProject_JReplicationRelevanceReason::Party);
		Decision.PriorityMultiplier = FMath::Max(Decision.PriorityMultiplier, Settings.PartyPriorityMultiplier);
	}
	if (SharesNonEmptySocialId(ViewerSocialState, TargetSocialState, false))
	{
		Decision.bShouldReplicate = true;
		Decision.AddReason(EProject_JReplicationRelevanceReason::Guild);
		Decision.PriorityMultiplier = FMath::Max(Decision.PriorityMultiplier, Settings.GuildPriorityMultiplier);
	}

	// 2. Owner Relevance Check
	if (Settings.bAlwaysReplicateOwnerOrInstigator && IsOwnedByViewer(TargetActor, ViewerActor))
	{
		Decision.bShouldReplicate = true;
		Decision.AddReason(EProject_JReplicationRelevanceReason::Owner);
		return Decision;
	}

	if (Decision.DistanceSquared <= Settings.GetMaxReplicationDistanceSquared())
	{
		Decision.bShouldReplicate = true;
		Decision.AddReason(EProject_JReplicationRelevanceReason::Distance);
	}

	return Decision;
}

void UProject_JNetObjectFilter_Distance::SetPolicySettings(const FProject_JReplicationPolicySettings& InPolicySettings)
{
	PolicySettings = InPolicySettings;
}
