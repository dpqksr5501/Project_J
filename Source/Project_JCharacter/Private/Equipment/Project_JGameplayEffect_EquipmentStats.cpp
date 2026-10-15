#include "Equipment/Project_JGameplayEffect_EquipmentStats.h"
#include "AbilitySystemComponent.h"
#include "NativeGameplayTags.h"
#include "Project_JAttributeSet.h"

namespace
{
UE_DEFINE_GAMEPLAY_TAG_STATIC(EquipmentMaxHealth, "SetByCaller.Equipment.MaxHealth");
UE_DEFINE_GAMEPLAY_TAG_STATIC(EquipmentMaxMana, "SetByCaller.Equipment.MaxMana");
UE_DEFINE_GAMEPLAY_TAG_STATIC(EquipmentAttackPower, "SetByCaller.Equipment.AttackPower");
UE_DEFINE_GAMEPLAY_TAG_STATIC(EquipmentDefense, "SetByCaller.Equipment.Defense");

FGameplayTag MagnitudeTag(EProject_JEquipmentStat Stat)
{
	switch (Stat)
	{
	case EProject_JEquipmentStat::MaxHealth: return EquipmentMaxHealth;
	case EProject_JEquipmentStat::MaxMana: return EquipmentMaxMana;
	case EProject_JEquipmentStat::AttackPower: return EquipmentAttackPower;
	case EProject_JEquipmentStat::Defense: return EquipmentDefense;
	default: checkNoEntry(); return FGameplayTag();
	}
}
FGameplayAttribute Attribute(EProject_JEquipmentStat Stat)
{
	switch (Stat)
	{
	case EProject_JEquipmentStat::MaxHealth: return UProject_JAttributeSet::GetMaxHealthAttribute();
	case EProject_JEquipmentStat::MaxMana: return UProject_JAttributeSet::GetMaxManaAttribute();
	case EProject_JEquipmentStat::AttackPower: return UProject_JAttributeSet::GetAttackPowerAttribute();
	case EProject_JEquipmentStat::Defense: return UProject_JAttributeSet::GetDefenseAttribute();
	default: checkNoEntry(); return FGameplayAttribute();
	}
}
}

UProject_JGameplayEffect_EquipmentStats::UProject_JGameplayEffect_EquipmentStats()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	for (int32 Index = 0; Index < FProject_JEquipmentStatValues::NumStats; ++Index)
	{
		const auto Stat = static_cast<EProject_JEquipmentStat>(Index);
		auto& Modifier = Modifiers.AddDefaulted_GetRef();
		Modifier.Attribute = Attribute(Stat);
		Modifier.ModifierOp = EGameplayModOp::Additive;
		FSetByCallerFloat Magnitude; Magnitude.DataTag = MagnitudeTag(Stat);
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Magnitude);
	}
}

FActiveGameplayEffectHandle UProject_JGameplayEffect_EquipmentStats::ApplyBonuses(UAbilitySystemComponent& ASC,
	TConstArrayView<FProject_JEquipmentStatModifier> Modifiers, const UObject* SourceObject)
{
	check(IsInGameThread());
	FProject_JEquipmentStatValues Values;
	if (!ASC.IsOwnerActorAuthoritative() || !FProject_JEquipmentStatValues::TryResolve(Modifiers, Values)) { return {}; }
	bool bHasBonus = false;
	for (const float Value : Values.Values) { bHasBonus |= Value != 0.f; }
	if (!bHasBonus) { return {}; }
	auto Context = ASC.MakeEffectContext(); Context.AddSourceObject(SourceObject);
	const auto Spec = ASC.MakeOutgoingSpec(StaticClass(), 1.f, Context);
	if (!Spec.IsValid()) { return {}; }
	for (int32 Index = 0; Index < FProject_JEquipmentStatValues::NumStats; ++Index)
	{
		Spec.Data->SetSetByCallerMagnitude(MagnitudeTag(static_cast<EProject_JEquipmentStat>(Index)), Values.Values[Index]);
	}
	return ASC.ApplyGameplayEffectSpecToSelf(*Spec.Data);
}
