#include "Combat/Project_JCombatStyleDefinition.h"

#if WITH_EDITOR
#include "Validation/Project_JDataValidation.h"
#endif

#if WITH_EDITOR
EDataValidationResult UProject_JCombatStyleDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bHasError = Result == EDataValidationResult::Invalid;
	if (!CombatStyleTag.IsValid())
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJCombatStyle", "MissingStyleTag", "CombatStyleTag is required."));
	}
	if (!WeaponAnimationProfile)
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJCombatStyle", "MissingAnimProfile", "WeaponAnimationProfile is required."));
	}
	if (!ComboDefinition)
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJCombatStyle", "MissingCombo", "ComboDefinition is required."));
	}
	if (!AttackSet)
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJCombatStyle", "MissingAttackSet", "AttackSet is required."));
	}
	return Project_J::DataValidation::MakeResult(Result, bHasError);
}
#endif
