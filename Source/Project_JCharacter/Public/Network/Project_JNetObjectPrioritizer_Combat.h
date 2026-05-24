#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Project_JNetObjectPrioritizer_Combat.generated.h"

/**
 * Project_J Custom Net Object Prioritizer based on Combat State.
 * Ensures that actors currently engaged in combat have higher replication frequency and priority.
 * 
 * Note: Inherits from UObject as a placeholder until full Iris SDK is exposed to module.
 */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JNetObjectPrioritizer_Combat : public UObject
{
	GENERATED_BODY()

public:
	// Setup prioritizer parameters
	// virtual void Init(FNetObjectPrioritizerInitParams& Params) override;

	// Perform prioritization per object
	// virtual void Prioritize(FNetObjectPrioritizationParams& Params) override;

protected:
	// Base priority for an actor in combat
	UPROPERTY(EditAnywhere, Category = "Network|Prioritization")
	float CombatPriorityMultiplier = 3.0f;
};
