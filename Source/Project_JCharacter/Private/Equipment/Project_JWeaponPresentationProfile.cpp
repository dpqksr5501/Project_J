#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "Equipment/Project_JWeaponPresentationActor.h"

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
	else if (!WeaponActorClass->IsChildOf(AProject_JWeaponPresentationActor::StaticClass()))
	{
		// Keep this a migration warning: existing profiles retain their visuals,
		// while new assets are guided to the common root/mesh contract.
		Project_J::DataValidation::AddWarning(Context, NSLOCTEXT("ProjectJWeaponPresentationProfile", "LegacyActorClass", "WeaponActorClass should derive from AProject_JWeaponPresentationActor so it supplies the common WeaponRoot and WeaponMesh contract."));
	}
	if (DrawnSocketName.IsNone())
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJWeaponPresentationProfile", "MissingSocket", "DrawnSocketName is required."));
	}
	if (SheathedSocketName.IsNone())
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJWeaponPresentationProfile", "MissingSheathedSocket", "SheathedSocketName is required."));
	}

	if (MotionPresentation.bSupportsIndependentMotion)
	{
		if (MotionPresentation.PrimaryGripSocketName.IsNone())
		{
			Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJWeaponPresentationProfile", "MissingPrimaryGrip", "Independent-motion weapons require PrimaryGripSocketName."));
		}

		const FProject_JWeaponGroundContactSettings& Ground = MotionPresentation.GroundContact;
		if (Ground.bEnableGroundContact && Ground.PrimaryProbeSocketName.IsNone())
		{
			Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJWeaponPresentationProfile", "IncompleteGroundContact", "Ground contact requires PrimaryProbeSocketName."));
		}
		if (Ground.bEnableGroundContact && Ground.TraceLength <= 0.0f)
		{
			Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJWeaponPresentationProfile", "InvalidGroundTraceLength", "Ground contact TraceLength must be greater than zero."));
		}
	}

	return Project_J::DataValidation::MakeResult(Result, bHasError);
}
#endif
