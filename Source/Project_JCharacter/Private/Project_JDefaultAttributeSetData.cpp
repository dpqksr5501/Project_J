#include "Project_JDefaultAttributeSetData.h"

#if WITH_EDITOR
#include "Validation/Project_JDataValidation.h"

EDataValidationResult UProject_JDefaultAttributeSetData::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Result = Super::IsDataValid(Context);
	bool bHasError = Result == EDataValidationResult::Invalid;

	auto ValidateFiniteNonNegative = [&Context, &bHasError](float Value, const FText& DisplayName)
	{
		if (!FMath::IsFinite(Value) || Value < 0.0f)
		{
			Project_J::DataValidation::AddError(
				Context,
				bHasError,
				FText::Format(
					NSLOCTEXT("ProjectJDefaultAttributes", "InvalidNonNegativeValue", "{0} must be a finite value greater than or equal to 0."),
					DisplayName));
		}
	};

	ValidateFiniteNonNegative(MaxHealth, NSLOCTEXT("ProjectJDefaultAttributes", "MaxHealth", "MaxHealth"));
	ValidateFiniteNonNegative(Health, NSLOCTEXT("ProjectJDefaultAttributes", "Health", "Health"));
	ValidateFiniteNonNegative(MaxMana, NSLOCTEXT("ProjectJDefaultAttributes", "MaxMana", "MaxMana"));
	ValidateFiniteNonNegative(Mana, NSLOCTEXT("ProjectJDefaultAttributes", "Mana", "Mana"));
	ValidateFiniteNonNegative(AttackPower, NSLOCTEXT("ProjectJDefaultAttributes", "AttackPower", "AttackPower"));
	ValidateFiniteNonNegative(Defense, NSLOCTEXT("ProjectJDefaultAttributes", "Defense", "Defense"));

	if (!FMath::IsFinite(MaxHealth) || MaxHealth <= 0.0f)
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJDefaultAttributes", "InvalidMaxHealth", "MaxHealth must be a finite value greater than 0."));
	}
	if (FMath::IsFinite(Health) && FMath::IsFinite(MaxHealth) && Health > MaxHealth)
	{
		Project_J::DataValidation::AddWarning(Context, NSLOCTEXT("ProjectJDefaultAttributes", "HealthAboveMaximum", "Health exceeds MaxHealth and will be clamped during initialization."));
	}

	if (!FMath::IsFinite(MaxMana) || MaxMana <= 0.0f)
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJDefaultAttributes", "InvalidMaxMana", "MaxMana must be a finite value greater than 0."));
	}
	if (FMath::IsFinite(Mana) && FMath::IsFinite(MaxMana) && Mana > MaxMana)
	{
		Project_J::DataValidation::AddWarning(Context, NSLOCTEXT("ProjectJDefaultAttributes", "ManaAboveMaximum", "Mana exceeds MaxMana and will be clamped during initialization."));
	}

	return Project_J::DataValidation::MakeResult(Result, bHasError);
}
#endif
