#include "Animation/Project_JHandGripProfile.h"

#if WITH_EDITOR
#include "Validation/Project_JDataValidation.h"

EDataValidationResult UProject_JHandGripProfile::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Result = Super::IsDataValid(Context);
	bool bHasError = Result == EDataValidationResult::Invalid;
	if (Calibration.PrimaryHandOffset.ContainsNaN() || Calibration.SecondaryHandOffset.ContainsNaN() ||
		Calibration.PrimaryElbowTarget.ContainsNaN() || Calibration.SecondaryElbowTarget.ContainsNaN() ||
		Calibration.PrimaryHandOffset.GetScale3D().GetAbs().GetMin() <= UE_KINDA_SMALL_NUMBER ||
		Calibration.SecondaryHandOffset.GetScale3D().GetAbs().GetMin() <= UE_KINDA_SMALL_NUMBER)
	{
		Project_J::DataValidation::AddError(Context, bHasError,
			NSLOCTEXT("ProjectJHandGripProfile", "InvalidTransform", "Grip offsets and elbow targets must contain finite values, with invertible grip scales."));
	}
	for (const FProject_JArmBendStability* Stability : { &Calibration.PrimaryBendStability, &Calibration.SecondaryBendStability })
	{
		if (!Stability->IsValid())
		{
			Project_J::DataValidation::AddError(Context, bHasError,
				NSLOCTEXT("ProjectJHandGripProfile", "InvalidStability", "Bend stability requires a finite nonzero pole axis, 0 <= hold < reliable <= 1, and positive finite speed/history duration."));
		}
	}
	for (const FProject_JGripArmBones* Arm : { &Calibration.PrimaryArm, &Calibration.SecondaryArm })
	{
		if (Arm->Shoulder.IsNone() != Arm->Elbow.IsNone() ||
			(!Arm->Shoulder.IsNone() && Arm->Shoulder == Arm->Elbow) ||
			(!Arm->Hand.IsNone() && (Arm->Elbow == Arm->Hand || Arm->Shoulder == Arm->Hand)))
		{
			Project_J::DataValidation::AddError(Context, bHasError,
				NSLOCTEXT("ProjectJHandGripProfile", "IncompleteArm", "Upper-arm and elbow roles must be set together and be distinct from the hand. Hand may be inferred from its palm socket. Empty upper/elbow roles retain immediate-parent discovery."));
		}
	}
	return Project_J::DataValidation::MakeResult(Result, bHasError);
}
#endif
