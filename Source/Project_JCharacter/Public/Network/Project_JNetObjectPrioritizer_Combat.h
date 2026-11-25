#pragma once

#include "CoreMinimal.h"
#include "Project_JMMOTypes.h"
#include "UObject/NoExportTypes.h"
#include "Project_JNetObjectPrioritizer_Combat.generated.h"

class AActor;

/**
 * Project_J Combat Network Priority Policy & Production Iris Prioritizer Dispatcher.
 * Ensures actors actively attacking/dodging/hit-reacting and giant bosses receive higher
 * Iris spatial priority while dead actors yield bandwidth in 100-200+ player battles.
 *
 * Driven event-driven from GAS GameplayTag transitions onto UE 5.8 SIMD
 * USphereWithOwnerBoostNetObjectPrioritizer tiers (zero per-connection tag lookups).
 */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JNetObjectPrioritizer_Combat : public UObject
{
	GENERATED_BODY()

public:
	static const FName PrioritizerName_DefaultMover;
	static const FName PrioritizerName_CombatActive;
	static const FName PrioritizerName_CombatMode;
	static const FName PrioritizerName_CombatDead;
	static const FName PrioritizerName_CombatBoss;

	static bool IsIrisCombatPrioritizerEnabled();

	/** Resolves the production Iris prioritizer tier name for TargetActor (single source of truth). */
	static FName ResolveIrisPrioritizerName(const AActor* TargetActor);

	/**
	 * Applies the resolved Iris prioritizer tier to TargetActor on an authoritative Iris server.
	 * Returns true when Iris accepts the tier or it is already assigned.
	 */
	static bool SyncActorIrisPrioritizer(AActor* TargetActor, FName& InOutLastAppliedName, bool bForceMoverTier = false);
	static void RefreshIrisPrioritizersAfterConsoleChange();

	UFUNCTION(BlueprintPure, Category = "Network|Prioritization")
	float CalculatePriorityMultiplier(const AActor* TargetActor) const;

	UFUNCTION(BlueprintPure, Category = "Network|Prioritization")
	bool HasCombatPriority(const AActor* TargetActor) const;

	FProject_JReplicationPolicyDecision ApplyCombatPriority(const AActor* TargetActor, FProject_JReplicationPolicyDecision Decision) const;

protected:
	UPROPERTY(EditAnywhere, Category = "Network|Prioritization", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float ActiveCombatPriorityMultiplier = 3.0f;

	UPROPERTY(EditAnywhere, Category = "Network|Prioritization", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float CombatModePriorityMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Network|Prioritization", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DeadPriorityMultiplier = 0.25f;
};
