#pragma once

#include "CoreMinimal.h"
#include "Project_JMMOTypes.h"
#include "UObject/NoExportTypes.h"
#include "Project_JNetObjectFilter_Distance.generated.h"

class AActor;

/**
 * Project_J Custom Net Object Filter based on Distance and Visibility.
 * Used with Unreal Engine 5 Iris Replication System to filter replicated objects for large-scale MMORPG environments.
 *
 * The class intentionally keeps the policy calculation independent from Iris hooks so the
 * same logic can be reused by tests, debug commands, and future UNetObjectFilter glue.
 */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JNetObjectFilter_Distance : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Network|Filtering")
	bool ShouldReplicateActorForViewer(const AActor* TargetActor, const FVector& ViewerLocation) const;

	UFUNCTION(BlueprintPure, Category = "Network|Filtering")
	float GetReplicationDistanceSquared(const AActor* TargetActor, const FVector& ViewerLocation) const;

	FProject_JReplicationPolicyDecision BuildReplicationDecision(const AActor* TargetActor, const FVector& ViewerLocation, const AActor* ViewerActor = nullptr) const;
	FProject_JReplicationPolicyDecision BuildReplicationDecisionWithSettings(
		const AActor* TargetActor,
		const FVector& ViewerLocation,
		const FProject_JReplicationPolicySettings& Settings,
		const AActor* ViewerActor = nullptr) const;

	UFUNCTION(BlueprintCallable, Category = "Network|Filtering")
	void SetPolicySettings(const FProject_JReplicationPolicySettings& InPolicySettings);

	// Setup filter parameters (overridden from UNetObjectFilter)
	// virtual void OnInit(FNetObjectFilterInitParams& Params) override;
	
	// Update filtering status per object or globally
	// virtual void UpdateObjects(FNetObjectFilterUpdateParams& Params) override;

	// Perform actual filtering logic
	// virtual void PreFilter(FNetObjectPreFilterParams& Params) override;
	// virtual void Filter(FNetObjectFilterParams& Params) override;

protected:
	UPROPERTY(EditAnywhere, Category = "Network|Filtering")
	FProject_JReplicationPolicySettings PolicySettings;
};
