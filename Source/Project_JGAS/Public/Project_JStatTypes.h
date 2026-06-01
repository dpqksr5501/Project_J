#pragma once

#include "CoreMinimal.h"
#include "Project_JStatTypes.generated.h"

UENUM(BlueprintType)
enum class EProject_JEquipmentStat : uint8
{
	MaxHealth UMETA(DisplayName = "Max Health"),
	MaxMana UMETA(DisplayName = "Max Mana"),
	AttackPower UMETA(DisplayName = "Attack Power"),
	Defense UMETA(DisplayName = "Defense")
};

USTRUCT(BlueprintType)
struct FProject_JEquipmentStatModifier
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Stats")
	EProject_JEquipmentStat Stat = EProject_JEquipmentStat::AttackPower;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment|Stats")
	float Value = 0.0f;
};
