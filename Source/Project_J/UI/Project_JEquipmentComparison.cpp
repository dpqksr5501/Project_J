#include "UI/Project_JEquipmentComparison.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "GameplayEffect.h"

namespace
{
EProject_JEquipmentBonusPreview PreviewMode(const UProject_JEquipmentItemDefinition *Definition)
{
	if (!Definition || Definition->StatApplicationPolicy == EProject_JEquipmentStatApplicationPolicy::StatModifiersOnly)
		return EProject_JEquipmentBonusPreview::Fixed;
	const bool bHasRemovableEffect = Definition->EquipmentEffects.ContainsByPredicate([](const auto &Effect)
	{
		return Effect && Effect->template GetDefaultObject<UGameplayEffect>()->DurationPolicy != EGameplayEffectDurationType::Instant;
	});
	if (!bHasRemovableEffect) return EProject_JEquipmentBonusPreview::Fixed;
	return Definition->StatApplicationPolicy == EProject_JEquipmentStatApplicationPolicy::GameplayEffectsOnly
		? EProject_JEquipmentBonusPreview::GameplayEffects : EProject_JEquipmentBonusPreview::ConditionalFallback;
}
float FixedBonus(const UProject_JEquipmentItemDefinition *Definition, EProject_JEquipmentStat Stat)
{
	if (!Definition || Definition->StatApplicationPolicy == EProject_JEquipmentStatApplicationPolicy::GameplayEffectsOnly) return 0;
	double Value = 0;
	for (const auto &Modifier : Definition->StatModifiers)
		if (Modifier.Stat == Stat && FMath::IsFinite(Modifier.Value) && !FMath::IsNearlyZero(Modifier.Value)) Value += Modifier.Value;
	return static_cast<float>(Value);
}
}
FProject_JEquipmentComparison FProject_JEquipmentComparison::Build(const UProject_JEquipmentItemDefinition *Candidate,
	const UProject_JEquipmentItemDefinition *Equipped)
{
	FProject_JEquipmentComparison Result;
	Result.CandidateMode = PreviewMode(Candidate);
	Result.EquippedMode = PreviewMode(Equipped);
	Result.bCanCompare = Result.CandidateMode == EProject_JEquipmentBonusPreview::Fixed &&
		Result.EquippedMode == EProject_JEquipmentBonusPreview::Fixed;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FProject_JEquipmentComparisonRow Row;
		Row.Stat = static_cast<EProject_JEquipmentStat>(Index);
		Row.Candidate = FixedBonus(Candidate, Row.Stat);
		Row.Equipped = FixedBonus(Equipped, Row.Stat);
		Row.Difference = Row.Candidate - Row.Equipped;
		if (!FMath::IsFinite(Row.Candidate) || !FMath::IsFinite(Row.Equipped) || !FMath::IsFinite(Row.Difference))
		{
			Result.bCanCompare = false;
			continue;
		}
		if (Row.Candidate != 0 || Row.Equipped != 0) Result.Rows.Add(Row);
	}
	Result.Explanation = Result.bCanCompare
		? NSLOCTEXT("ProjectJUI", "BonusCompareScope", "고정 가산 보너스 비교 · 최종 능력치는 다른 효과와 상한에 따라 달라집니다.")
		: NSLOCTEXT("ProjectJUI", "ConditionalCompareScope", "효과·패시브·조건에 따라 달라져 확정 차이를 표시하지 않습니다. 고정 대체 보너스는 장비 효과가 모두 적용되지 않았을 때만 사용됩니다.");
	if ((Candidate && Candidate->AbilitySet) || (Equipped && Equipped->AbilitySet))
		Result.Explanation = FText::Format(NSLOCTEXT("ProjectJUI", "BonusPassiveScope", "{0}\n장비가 부여하는 스킬·패시브는 이 수치 비교에 포함하지 않습니다."), Result.Explanation);
	return Result;
}
FText FProject_JEquipmentComparison::StatName(EProject_JEquipmentStat Stat)
{
	switch (Stat)
	{
	case EProject_JEquipmentStat::MaxHealth: return NSLOCTEXT("ProjectJUI", "StatMaxHealth", "최대 체력");
	case EProject_JEquipmentStat::MaxMana: return NSLOCTEXT("ProjectJUI", "StatMaxMana", "최대 마나");
	case EProject_JEquipmentStat::AttackPower: return NSLOCTEXT("ProjectJUI", "StatAttack", "공격력");
	default: return NSLOCTEXT("ProjectJUI", "StatDefense", "방어력");
	}
}
