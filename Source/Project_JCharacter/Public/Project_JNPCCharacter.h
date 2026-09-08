// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Optimization/Project_JNPCUpdateBudget.h"
#include "Project_JBaseCharacter.h"
#include "Project_JNPCCharacter.generated.h"

/**
 * Base character class for NPCs and Monsters in Project J.
 * Does NOT inherit from MotionMatchingCharacter, as they do not use motion matching.
 */
UCLASS()
class PROJECT_JCHARACTER_API AProject_JNPCCharacter : public AProject_JBaseCharacter
{
	GENERATED_BODY()

public:
	AProject_JNPCCharacter();

	/** Returns policy data only; no AI is throttled until its owner opts in. */
	float GetRecommendedAIUpdateInterval() const;
	/** Read-only policy access for consumer-local distance tiers; does not change global significance. */
	float GetRecommendedAIUpdateIntervalForTier(EProject_JNPCUpdateBudgetTier Tier) const;
	EProject_JNPCUpdateBudgetTier GetDecisionTierForDistance(double Distance, EProject_JNPCUpdateBudgetTier PreviousTier) const;
	EProject_JNPCUpdateBudgetTier GetNPCUpdateBudgetTier() const;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Optimization")
	bool bApplyDefaultNPCOptimizationPolicy = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Optimization", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float NPCNetCullDistance = 8000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Optimization", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float NPCNetUpdateFrequency = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Optimization", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float NPCMinNetUpdateFrequency = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Optimization")
	FProject_JNPCUpdateBudgetSettings NPCUpdateBudget;

	void ApplyDefaultNPCOptimizationPolicy();
};
