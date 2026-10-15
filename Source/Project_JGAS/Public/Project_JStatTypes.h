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

/** Fixed additive equipment tuple shared by authoring, preview and runtime.
 * Add a new stat here and to the native equipment GE's attribute/tag mapping together. */
struct FProject_JEquipmentStatValues
{
	static constexpr int32 NumStats = static_cast<int32>(EProject_JEquipmentStat::Defense) + 1;
	float Values[NumStats] = {};

	static bool TryResolve(TConstArrayView<FProject_JEquipmentStatModifier> Modifiers, FProject_JEquipmentStatValues& Out)
	{
		double Totals[NumStats] = {};
		for (const auto& Modifier : Modifiers)
		{
			const int32 Index = static_cast<int32>(Modifier.Stat);
			if (Index < 0 || Index >= NumStats || !FMath::IsFinite(Modifier.Value)) { return false; }
			if (!FMath::IsNearlyZero(Modifier.Value)) { Totals[Index] += Modifier.Value; }
		}
		FProject_JEquipmentStatValues Candidate;
		for (int32 Index = 0; Index < NumStats; ++Index)
		{
			if (!FMath::IsFinite(Totals[Index]) || FMath::Abs(Totals[Index]) > MAX_flt) { return false; }
			Candidate.Values[Index] = static_cast<float>(Totals[Index]);
		}
		Out = Candidate;
		return true;
	}
};
