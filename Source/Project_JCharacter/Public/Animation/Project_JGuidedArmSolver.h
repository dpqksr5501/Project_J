#pragma once

#include "CoreMinimal.h"

namespace Project_J::Animation
{
	/** Dimensionless body-relative thresholds; settings belong to the rig, not the weapon/job. */
	struct FGuidedArmBendSettings
	{
		double HoldBendRatio = 0.15;
		double ReliableBendRatio = 0.25;
		double ReacquireDegreesPerSecond = 720.0;
		double MaxHistorySeconds = 0.25;
	};

	/** Only a guide direction is remembered. Never seed it with the final IK elbow. */
	struct FGuidedArmBendState
	{
		FVector Aim = FVector::ZeroVector;
		FVector Bend = FVector::ZeroVector;
		bool bValid = false;
		bool bReacquiring = false;
		void Reset() { *this = {}; }
	};

	PROJECT_JCHARACTER_API FVector StabilizeGuidedArmBend(const FVector& RawBend,
		const FVector& TargetAim, const FVector& FallbackBend, double BendRatio, double DeltaTime,
		const FGuidedArmBendSettings& Settings, FGuidedArmBendState& State);

	/** Optional observational data; never used to choose a different pose. */
	struct FGuidedArmSolveDiagnostics
	{
		double UpperLength = 0.0, LowerLength = 0.0, TargetDistance = 0.0, InputBendHeight = 0.0;
		bool bUsedFallback = false, bUsedBestAxis = false, bAntipodal = false;
		bool bClampedMinimum = false, bClampedMaximum = false;
		FVector BendDirection = FVector::ZeroVector;
		FVector ReachableTarget = FVector::ZeroVector;
		FVector RawBendDirection = FVector::ZeroVector;
		bool bGuideHistory = false, bGuideReacquiring = false;
		double GuideCorrectionDegrees = 0.0;
	};
	/** Value-only solve. All transforms and guides are in the same component space. */
	PROJECT_JCHARACTER_API bool SolveGuidedArm(const FTransform& InputUpperArm,
		const FTransform& InputForearm, const FTransform& InputHand, const FTransform& WristTarget,
		const FVector& FallbackBendDirection, const FVector* ExplicitElbowGuide,
		bool bMatchWristRotation, FTransform& OutUpperArm, FTransform& OutForearm, FTransform& OutHand,
		FGuidedArmSolveDiagnostics* Diagnostics = nullptr,
		FGuidedArmBendState* BendState = nullptr, const FGuidedArmBendSettings* BendSettings = nullptr,
		double DeltaTime = 0.0);
}
