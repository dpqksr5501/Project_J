#pragma once

#include "CoreMinimal.h"
#include "Combat/Project_JCombatTypes.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Project_JAttackDefinition.generated.h"

class UAnimMontage;
class UGameplayEffect;

/** How an authored attack is allowed to move the character. */
UENUM(BlueprintType)
enum class EProject_JAttackMovementPolicy : uint8
{
	InPlace UMETA(DisplayName = "In Place"),
	RootMotionMontage UMETA(DisplayName = "Root Motion Montage"),
	RootMotionWarped UMETA(DisplayName = "Root Motion + Motion Warping")
};

/** How an aerial root-motion attack resolves movement after normal completion. */
UENUM(BlueprintType)
enum class EProject_JRootMotionEndMovementPolicy : uint8
{
	Land UMETA(DisplayName = "Land / Walking"),
	Fall UMETA(DisplayName = "Fall"),
	KeepFlying UMETA(DisplayName = "Keep Flying")
};

/**
 * One reusable, server-resolved attack. Combo graphs, direct skills and AI may
 * all reference the same definition without duplicating damage or movement data.
 */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JAttackDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FGameplayTag AttackTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity", meta = (ClampMin = "1"))
	int32 SchemaVersion = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	FName MontageSectionName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float PlayRate = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
	EProject_JAttackMovementPolicy MovementPolicy = EProject_JAttackMovementPolicy::InPlace;

	/**
	 * Temporarily uses Flying while this root-motion attack is active, allowing
	 * authored vertical root motion to lift the character off the floor.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (EditCondition = "MovementPolicy != EProject_JAttackMovementPolicy::InPlace", EditConditionHides))
	bool bUseFlyingMovementModeForRootMotion = false;

	/** Movement state after this aerial root-motion montage completes normally. Interruptions always fall. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (EditCondition = "bUseFlyingMovementModeForRootMotion", EditConditionHides))
	EProject_JRootMotionEndMovementPolicy RootMotionEndMovementPolicy = EProject_JRootMotionEndMovementPolicy::Land;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hit")
	FProject_JComboHitSpec HitSpec;

	/** The server selects this effect from the active attack; clients never choose it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage")
	TSubclassOf<UGameplayEffect> DamageEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Conditions")
	FGameplayTagContainer RequiredOwnerTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Conditions")
	FGameplayTagContainer BlockedOwnerTags;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

/** Collection owned by one combat style. The tag is the stable runtime identity. */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JAttackSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attacks")
	TArray<TObjectPtr<UProject_JAttackDefinition>> Attacks;

	const UProject_JAttackDefinition* FindAttack(FGameplayTag AttackTag) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
