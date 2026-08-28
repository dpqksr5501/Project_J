#pragma once

#include "CoreMinimal.h"
#include "Project_JNPCUpdateBudget.generated.h"

UENUM(BlueprintType)
enum class EProject_JNPCUpdateBudgetTier : uint8
{
	Near,
	Mid,
	Far,
	Hidden
};

/**
 * Read-only policy data for future NPC AI. NPC, boss, and monster animation stays
 * on the low-cost non-Motion-Matching path; this structure budgets only AI work.
 */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JNPCUpdateBudgetSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Optimization", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float NearUpdateInterval = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Optimization", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float MidUpdateInterval = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Optimization", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float FarUpdateInterval = 0.33f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Optimization", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float HiddenUpdateInterval = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Optimization")
	bool bAllowPerceptionBeyondNear = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|Optimization")
	bool bAllowPathRefreshBeyondMid = false;
};
