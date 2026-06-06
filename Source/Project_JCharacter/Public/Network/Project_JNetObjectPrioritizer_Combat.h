#pragma once

#include "CoreMinimal.h"
#include "Project_JMMOTypes.h"
#include "UObject/NoExportTypes.h"
#include "Project_JNetObjectPrioritizer_Combat.generated.h"

class AActor;

/**
 * Project_J Custom Net Object Prioritizer based on Combat State.
 * Ensures that actors currently engaged in combat have higher replication frequency and priority.
 * 
 * The policy calculation is kept independent from the Iris adapter layer.
 */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JNetObjectPrioritizer_Combat : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Network|Prioritization")
	float CalculatePriorityMultiplier(const AActor* TargetActor) const;

	UFUNCTION(BlueprintPure, Category = "Network|Prioritization")
	bool HasCombatPriority(const AActor* TargetActor) const;

	FProject_JReplicationPolicyDecision ApplyCombatPriority(const AActor* TargetActor, FProject_JReplicationPolicyDecision Decision) const;

	// Setup prioritizer parameters
	// virtual void Init(FNetObjectPrioritizerInitParams& Params) override;

	// Perform prioritization per object
	// virtual void Prioritize(FNetObjectPrioritizationParams& Params) override;

protected:
	UPROPERTY(EditAnywhere, Category = "Network|Prioritization", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float ActiveCombatPriorityMultiplier = 3.0f;

	UPROPERTY(EditAnywhere, Category = "Network|Prioritization", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float CombatModePriorityMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Network|Prioritization", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DeadPriorityMultiplier = 0.25f;
};
