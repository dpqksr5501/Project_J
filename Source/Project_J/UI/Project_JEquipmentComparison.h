#pragma once
#include "CoreMinimal.h"
#include "Project_JStatTypes.h"
#include "Project_JEquipmentComparison.generated.h"

class UProject_JEquipmentItemDefinition;

UENUM(BlueprintType)
enum class EProject_JEquipmentBonusPreview : uint8 { Fixed, ConditionalFallback, GameplayEffects };

USTRUCT(BlueprintType)
struct FProject_JEquipmentComparisonRow
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) EProject_JEquipmentStat Stat = EProject_JEquipmentStat::AttackPower;
	UPROPERTY(BlueprintReadOnly) float Candidate = 0;
	UPROPERTY(BlueprintReadOnly) float Equipped = 0;
	UPROPERTY(BlueprintReadOnly) float Difference = 0;
};

/** Authored additive bonuses, never a prediction of final GAS attributes or item-level scaling. */
USTRUCT(BlueprintType)
struct FProject_JEquipmentComparison
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) EProject_JEquipmentBonusPreview CandidateMode = EProject_JEquipmentBonusPreview::Fixed;
	UPROPERTY(BlueprintReadOnly) EProject_JEquipmentBonusPreview EquippedMode = EProject_JEquipmentBonusPreview::Fixed;
	UPROPERTY(BlueprintReadOnly) bool bCanCompare = true;
	UPROPERTY(BlueprintReadOnly) TArray<FProject_JEquipmentComparisonRow> Rows;
	UPROPERTY(BlueprintReadOnly) FText Explanation;
	static FProject_JEquipmentComparison Build(const UProject_JEquipmentItemDefinition *Candidate,
		const UProject_JEquipmentItemDefinition *Equipped);
	static FText StatName(EProject_JEquipmentStat Stat);
};
