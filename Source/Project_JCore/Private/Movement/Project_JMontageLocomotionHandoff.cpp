#include "Movement/Project_JMontageLocomotionHandoff.h"

#if WITH_EDITOR
#include "Animation/AnimMontage.h"
#include "Movement/Project_JMontageVerticalMotion.h"

FString UProject_JMontageLocomotionHandoff::GetValidationFailure(const UAnimMontage& Montage) const
{
	const float End = Montage.GetPlayLength();
	if (!FMath::IsFinite(RecoveryStart) || !FMath::IsFinite(RecoveryBlendTime)
		|| !FMath::IsFinite(StationaryPoseWindow.X) || !FMath::IsFinite(StationaryPoseWindow.Y)
		|| !FMath::IsFinite(StationaryPoseWeight) || StationaryPoseWeight < 0.f || StationaryPoseWeight > 1.f)
	{
		return TEXT("Locomotion handoff values must be finite and the stationary weight must be in [0, 1].");
	}
	if (StationaryPoseWeight > 0.f && (RecoveryStart >= 0.f || Montage.HasRootMotion()
		|| StationaryPoseWindow.X < 0.f || StationaryPoseWindow.Y <= StationaryPoseWindow.X || StationaryPoseWindow.Y > End))
	{
		return TEXT("Stationary cast legs require an in-place montage and a valid stationary pose interval.");
	}
	if (RecoveryStart < 0.f) { return {}; }
	if (!Montage.HasRootMotion() || Montage.FindMetaDataByClass<UProject_JMontageVerticalMotion>()
		|| RecoveryBlendTime <= 0.f || RecoveryStart + RecoveryBlendTime >= End
		|| Montage.CompositeSections.Num() != 1 || !Montage.CompositeSections[0].NextSectionName.IsNone())
	{
		return TEXT("Recovery handoff requires a non-looping, single-section root-motion montage with room to blend.");
	}
	const int32 Samples = FMath::CeilToInt((End - RecoveryStart) * 120.f);
	for (int32 Index = 1; Index <= Samples; ++Index)
	{
		const float Position = FMath::Lerp(RecoveryStart, End, static_cast<float>(Index) / Samples);
		const FTransform Delta = Montage.ExtractRootMotionFromTrackRange(RecoveryStart, Position, FAnimExtractContext());
		if (Delta.GetTranslation().SizeSquared() > FMath::Square(.1f)
			|| Delta.GetRotation().AngularDistance(FQuat::Identity) > FMath::DegreesToRadians(.1f))
		{
			return TEXT("Recovery handoff would cut authored root motion.");
		}
	}
	return {};
}
#endif
