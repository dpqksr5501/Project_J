#pragma once

#include "CoreMinimal.h"
#include "Project_JAnimationBudgetTypes.generated.h"

UENUM(BlueprintType)
enum class EProject_JAnimBudgetTier : uint8
{
	Local,
	Near,
	Mid,
	Far,
	Hidden
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimOptimizationPolicy
{
	GENERATED_BODY()

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Optimization")
	EProject_JAnimBudgetTier Tier = EProject_JAnimBudgetTier::Local;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Optimization")
	bool bUpdateAnimationData = true;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Optimization")
	bool bUseFullChooserRows = true;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Optimization")
	bool bUseFarChooserRowsOnly = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Optimization")
	float MotionMatchingUpdateInterval = 0.0f;

	/** Whether Post-Retarget Hand IK should be evaluated. (On for Local/Near, Off for Mid/Far/Hidden). */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Optimization")
	bool bEnableHandIK = true;

	/** Whether Foot IK / Leg ground alignment should be evaluated. (On for Local/Near/Mid, Off for Far/Hidden). */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Optimization")
	bool bEnableFootIK = true;

	/** Whether Runtime Retarget IK is enabled on follower mesh. (On for Local/Near, Off for Mid/Far). */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Optimization")
	bool bEnableRetargetIK = true;

	/** Whether visual Follower Mesh evaluation should proceed or be throttled/bypassed. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Optimization")
	bool bEnableFollowerRetarget = true;

	/** Maximum LOD threshold for Follower Retarget IK nodes. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Optimization")
	int32 RetargetIKLODThreshold = 0;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimationBudgetSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Budget", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float NearDistance = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Budget", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MidDistance = 6000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Budget", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FarDistance = 12000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Budget", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float MidUpdateInterval = 0.033f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Budget", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float FarUpdateInterval = 0.083f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Budget", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float HiddenUpdateInterval = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Budget")
	bool bDisableMotionMatchingBeyondFarDistance = false;
};
