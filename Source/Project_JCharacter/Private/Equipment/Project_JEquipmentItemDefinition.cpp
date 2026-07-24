#include "Equipment/Project_JEquipmentItemDefinition.h"

#if WITH_EDITOR
#include "GameplayEffect.h"
#include "Misc/DataValidation.h"
#include "Validation/Project_JDataValidation.h"

EDataValidationResult UProject_JEquipmentItemDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bHasError = Result == EDataValidationResult::Invalid;

	if (EquipmentSlot == EProject_JEquipmentSlot::None)
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJEquipmentItemDefinition", "MissingEquipmentSlot", "EquipmentSlot must not be None."));
	}

	if (StatApplicationPolicy == EProject_JEquipmentStatApplicationPolicy::GameplayEffectsOnly && EquipmentEffects.IsEmpty())
	{
		Project_J::DataValidation::AddWarning(Context, NSLOCTEXT("ProjectJEquipmentItemDefinition", "EffectsOnlyNoEffects", "StatApplicationPolicy is GameplayEffectsOnly, but no EquipmentEffects are assigned."));
	}

	if (StatApplicationPolicy == EProject_JEquipmentStatApplicationPolicy::StatModifiersOnly && StatModifiers.IsEmpty())
	{
		Project_J::DataValidation::AddWarning(Context, NSLOCTEXT("ProjectJEquipmentItemDefinition", "StatsOnlyNoStats", "StatApplicationPolicy is StatModifiersOnly, but no StatModifiers are assigned."));
	}
	if (StatApplicationPolicy == EProject_JEquipmentStatApplicationPolicy::StatModifiersOnly)
	{
		Project_J::DataValidation::AddWarning(Context, NSLOCTEXT("ProjectJEquipmentItemDefinition", "LegacyStatModifiers", "StatModifiersOnly is a compatibility path. Prefer authored infinite GameplayEffects for new equipment."));
	}

	for (int32 EffectIndex = 0; EffectIndex < EquipmentEffects.Num(); ++EffectIndex)
	{
		if (!EquipmentEffects[EffectIndex])
		{
			Project_J::DataValidation::AddError(Context, bHasError, FText::Format(
				NSLOCTEXT("ProjectJEquipmentItemDefinition", "MissingEquipmentEffect", "EquipmentEffects[{0}] has no GameplayEffect class."),
				FText::AsNumber(EffectIndex)));
		}
	}

	for (int32 ModifierIndex = 0; ModifierIndex < StatModifiers.Num(); ++ModifierIndex)
	{
		if (FMath::IsNearlyZero(StatModifiers[ModifierIndex].Value))
		{
			Project_J::DataValidation::AddWarning(Context, FText::Format(
				NSLOCTEXT("ProjectJEquipmentItemDefinition", "ZeroStatModifier", "StatModifiers[{0}] has a zero Value."),
				FText::AsNumber(ModifierIndex)));
		}
	}

	if (EquipmentSlot == EProject_JEquipmentSlot::Weapon && !CombatStyleDefinition)
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJEquipmentItemDefinition", "MissingCombatStyle", "Weapon equipment requires a CombatStyleDefinition."));
	}

	if (EquipmentSlot == EProject_JEquipmentSlot::Weapon && !WeaponPresentationProfile)
	{
		Project_J::DataValidation::AddWarning(Context, NSLOCTEXT("ProjectJEquipmentItemDefinition", "MissingPresentation", "Weapon equipment has no WeaponPresentationProfile and will not display a drawn weapon actor."));
	}

	return Project_J::DataValidation::MakeResult(Result, bHasError);
}
#endif
