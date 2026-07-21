#include "Equipment/Project_JWeaponPresentationProfile.h"

#if WITH_EDITOR
#include "Validation/Project_JDataValidation.h"

EDataValidationResult UProject_JWeaponPresentationProfile::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	bool bHasError = Result == EDataValidationResult::Invalid;

	if (!WeaponActorClass)
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJWeaponPresentationProfile", "MissingActor", "WeaponActorClass is required."));
	}
	if (DrawnSocketName.IsNone())
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJWeaponPresentationProfile", "MissingSocket", "DrawnSocketName is required."));
	}
	if (SheathedSocketName.IsNone())
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJWeaponPresentationProfile", "MissingSheathedSocket", "SheathedSocketName is required."));
	}

	return Project_J::DataValidation::MakeResult(Result, bHasError);
}
#endif
