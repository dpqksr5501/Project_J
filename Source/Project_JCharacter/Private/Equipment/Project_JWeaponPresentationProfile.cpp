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
	if (DrawnSocketName.IsNone() && (DrawnAttachmentMode == EProject_JDrawnAttachmentMode::Socket || MotionPresentation.bSupportsIndependentMotion))
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJWeaponPresentationProfile", "MissingSocket", "DrawnSocketName is required."));
	}
	if (DrawnAttachmentMode == EProject_JDrawnAttachmentMode::PrimaryGripContact && MotionPresentation.PrimaryGripSocketName.IsNone())
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJWeaponPresentationProfile", "MissingMountGrip", "Primary Grip attachment requires PrimaryGripSocketName, including weapons without attack IK."));
	}
	if (SheathedSocketName.IsNone())
	{
		Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJWeaponPresentationProfile", "MissingSheathedSocket", "SheathedSocketName is required."));
	}

	if (MotionPresentation.bSupportsIndependentMotion)
	{
		if (!FMath::IsFinite(MotionPresentation.DefaultAttackPrimaryIKAlpha) ||
			MotionPresentation.DefaultAttackPrimaryIKAlpha < 0.0f || MotionPresentation.DefaultAttackPrimaryIKAlpha > 1.0f ||
			!FMath::IsFinite(MotionPresentation.DefaultAttackSecondaryIKAlpha) ||
			MotionPresentation.DefaultAttackSecondaryIKAlpha < 0.0f || MotionPresentation.DefaultAttackSecondaryIKAlpha > 1.0f)
		{
			Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJWeaponPresentationProfile", "InvalidAttackGripAlpha", "Default attack grip IK alphas must be between zero and one."));
		}
		if (MotionPresentation.PrimaryGripSocketName.IsNone())
		{
			Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJWeaponPresentationProfile", "MissingPrimaryGrip", "Independent-motion weapons require PrimaryGripSocketName."));
		}
		if (!FMath::IsFinite(MotionPresentation.AttackEntryBlendSeconds) || MotionPresentation.AttackEntryBlendSeconds < 0.0f)
		{
			Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJWeaponPresentationProfile", "InvalidAttackEntryBlend", "AttackEntryBlendSeconds must be finite and non-negative."));
		}
		if (!FMath::IsFinite(MotionPresentation.ContactRecoverySeconds) ||
			MotionPresentation.ContactRecoverySeconds < 0.0f || MotionPresentation.ContactRecoverySeconds > 1.0f)
		{
			Project_J::DataValidation::AddError(Context, bHasError, NSLOCTEXT("ProjectJWeaponPresentationProfile", "InvalidContactRecovery", "ContactRecoverySeconds must be finite and between zero and one."));
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
