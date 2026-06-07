#include "Network/Project_JNetObjectFilter_Distance.h"

#include "GameFramework/Actor.h"

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

FProject_JReplicationPolicyDecision UProject_JNetObjectFilter_Distance::BuildReplicationDecision(const AActor* TargetActor, const FVector& ViewerLocation) const
{
	return BuildReplicationDecisionWithSettings(TargetActor, ViewerLocation, PolicySettings);
}

FProject_JReplicationPolicyDecision UProject_JNetObjectFilter_Distance::BuildReplicationDecisionWithSettings(
	const AActor* TargetActor,
	const FVector& ViewerLocation,
	const FProject_JReplicationPolicySettings& Settings) const
{
	FProject_JReplicationPolicyDecision Decision;
	if (!TargetActor)
	{
		return Decision;
	}

	Decision.DistanceSquared = GetReplicationDistanceSquared(TargetActor, ViewerLocation);

	if (Settings.bAlwaysReplicateOwnerOrInstigator && (TargetActor->GetOwner() || TargetActor->GetInstigator()))
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
