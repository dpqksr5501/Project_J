#pragma once
#include "GameplayEffect.h"
#include "Project_JStatTypes.h"
#include "Project_JGameplayEffect_EquipmentStats.generated.h"

/** Stable native definition: compatibility StatModifiers own a removable GE, never base writes. */
UCLASS(NotBlueprintable)
class UProject_JGameplayEffect_EquipmentStats : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UProject_JGameplayEffect_EquipmentStats();
	static FActiveGameplayEffectHandle ApplyBonuses(UAbilitySystemComponent& ASC,
		TConstArrayView<FProject_JEquipmentStatModifier> Modifiers, const UObject* SourceObject);
};
