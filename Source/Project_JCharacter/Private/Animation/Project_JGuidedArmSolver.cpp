#include "Animation/Project_JGuidedArmSolver.h"
#include "TwoBoneIK.h"

FVector Project_J::Animation::StabilizeGuidedArmBend(const FVector& RawBend,
	const FVector& TargetAim, const FVector& FallbackBend, double BendRatio, double DeltaTime,
	const FGuidedArmBendSettings& Settings, FGuidedArmBendState& State)
{
	const double Hold = FMath::Max(0.0, Settings.HoldBendRatio);
	const double Reliable = FMath::Max(Hold + UE_KINDA_SMALL_NUMBER, Settings.ReliableBendRatio);
	if (!FMath::IsFinite(DeltaTime) || DeltaTime < 0.0 || DeltaTime > Settings.MaxHistorySeconds)
	{
		State.Reset();
	}
	FVector Bend = RawBend;
	if (!State.bValid)
	{
		if (BendRatio < Reliable)
		{
			const FVector Fallback = (FallbackBend - TargetAim * FVector::DotProduct(FallbackBend, TargetAim)).GetSafeNormal();
			if (!Fallback.IsNearlyZero()) { Bend = Fallback; }
			State.bReacquiring = true;
		}
	}
	else
	{
		// Parallel transport removes changes due solely to the moving wrist axis.
		const FQuat Transport = FVector::DotProduct(State.Aim, TargetAim) < -0.9999
			? FQuat(State.Bend, PI) : FQuat::FindBetweenNormals(State.Aim, TargetAim);
		FVector Previous = Transport.RotateVector(State.Bend);
		Previous = (Previous - TargetAim * FVector::DotProduct(Previous, TargetAim)).GetSafeNormal();
		if (Previous.IsNearlyZero()) { Previous = RawBend; }
		if (BendRatio < Reliable) { State.bReacquiring = true; }
		if (State.bReacquiring)
		{
			const double Dot = FMath::Clamp(FVector::DotProduct(Previous, RawBend), -1.0, 1.0);
			const double Angle = FMath::Acos(Dot);
			const double Confidence = FMath::Clamp((BendRatio - Hold) / (Reliable - Hold), 0.0, 1.0);
			const double SmoothConfidence = Confidence * Confidence * (3.0 - 2.0 * Confidence);
			const double MaxStep = FMath::DegreesToRadians(FMath::Max(0.0, Settings.ReacquireDegreesPerSecond)) *
				DeltaTime * SmoothConfidence;
			if (Angle <= MaxStep + UE_KINDA_SMALL_NUMBER)
			{
				Bend = RawBend;
				State.bReacquiring = BendRatio < Reliable;
			}
			else
			{
				// Rotate on the bend circle, including an exact 180-degree reversal;
				// a linear vector blend would collapse to zero in that case.
				const double SignedSine = FVector::DotProduct(TargetAim, FVector::CrossProduct(Previous, RawBend));
				Bend = FQuat(TargetAim, SignedSine < 0.0 ? -MaxStep : MaxStep).RotateVector(Previous);
			}
		}
	}
	State.Aim = TargetAim;
	State.Bend = Bend.GetSafeNormal();
	State.bValid = true;
	return State.Bend;
}

bool Project_J::Animation::SolveGuidedArm(const FTransform& InputUpperArm,
	const FTransform& InputForearm, const FTransform& InputHand, const FTransform& WristTarget,
	const FVector& FallbackBendDirection, const FVector* ExplicitElbowGuide,
	bool bMatchWristRotation, FTransform& OutUpperArm, FTransform& OutForearm, FTransform& OutHand,
	FGuidedArmSolveDiagnostics* Diagnostics, FGuidedArmBendState* BendState,
	const FGuidedArmBendSettings* BendSettings, double DeltaTime)
{
	if (Diagnostics) { *Diagnostics = {}; }
	OutUpperArm = InputUpperArm;
	OutForearm = InputForearm;
	OutHand = InputHand;
	if (InputUpperArm.ContainsNaN() || InputForearm.ContainsNaN() || InputHand.ContainsNaN() ||
		WristTarget.ContainsNaN() || FallbackBendDirection.ContainsNaN() ||
		(ExplicitElbowGuide && ExplicitElbowGuide->ContainsNaN())) { return false; }

	const FVector Root = InputUpperArm.GetLocation();
	const FVector Joint = InputForearm.GetLocation();
	const FVector End = InputHand.GetLocation();
	const double UpperLength = FVector::Distance(Root, Joint);
	const double LowerLength = FVector::Distance(Joint, End);
	if (UpperLength <= UE_KINDA_SMALL_NUMBER || LowerLength <= UE_KINDA_SMALL_NUMBER) { return false; }

	FVector InputAim = (End - Root).GetSafeNormal();
	if (InputAim.IsNearlyZero()) { InputAim = (Joint - Root).GetSafeNormal(); }
	const FVector TargetDelta = WristTarget.GetLocation() - Root;
	FVector TargetAim = TargetDelta.GetSafeNormal();
	if (TargetAim.IsNearlyZero()) { TargetAim = InputAim; }

	FVector Bend;
	if (ExplicitElbowGuide)
	{
		Bend = *ExplicitElbowGuide - Root;
	}
	else
	{
		// Read the current PRE-IK pose. Reading last frame's final elbow would
		// make contact corrections feed back into their own guide.
		Bend = Joint - Root;
		Bend -= InputAim * FVector::DotProduct(Bend, InputAim);
		if (Diagnostics) { Diagnostics->InputBendHeight = Bend.Size(); }
		if (Bend.SizeSquared() > FMath::Square(UE_KINDA_SMALL_NUMBER))
		{
			Bend.Normalize();
			// At the antipodal case use the input bend axis, rather than an
			// arbitrary FindBetweenNormals axis that could reverse the elbow.
			const FQuat Transport = FVector::DotProduct(InputAim, TargetAim) < -0.9999
				? FQuat(Bend, PI) : FQuat::FindBetweenNormals(InputAim, TargetAim);
			if (Diagnostics) { Diagnostics->bAntipodal = FVector::DotProduct(InputAim, TargetAim) < -0.9999; }
			Bend = Transport.RotateVector(Bend);
		}
		else
		{
			Bend = FallbackBendDirection;
			if (Diagnostics) { Diagnostics->bUsedFallback = true; }
		}
	}
	Bend -= TargetAim * FVector::DotProduct(Bend, TargetAim);
	if (!Bend.Normalize())
	{
		if (Diagnostics) { Diagnostics->bUsedFallback = true; }
		Bend = FallbackBendDirection - TargetAim * FVector::DotProduct(FallbackBendDirection, TargetAim);
		if (!Bend.Normalize())
		{
			if (Diagnostics) { Diagnostics->bUsedBestAxis = true; }
			FVector OtherAxis;
			TargetAim.FindBestAxisVectors(Bend, OtherAxis);
		}
	}

	const FVector RawBend = Bend;
	const bool bHadGuideHistory = !ExplicitElbowGuide && BendState && BendState->bValid && DeltaTime >= 0.0 &&
		BendSettings && DeltaTime <= BendSettings->MaxHistorySeconds;
	if (BendState && BendSettings && !ExplicitElbowGuide)
	{
		const FVector InputBend = Joint - Root - InputAim * FVector::DotProduct(Joint - Root, InputAim);
		Bend = StabilizeGuidedArmBend(Bend, TargetAim, FallbackBendDirection,
			InputBend.Size() / FMath::Min(UpperLength, LowerLength), DeltaTime, *BendSettings, *BendState);
	}
	else if (BendState) { BendState->Reset(); }

	// Preserve this body's current segment lengths, including mesh scaling.
	// A target inside the minimum reach is also geometrically unreachable.
	const double MinReach = FMath::Abs(UpperLength - LowerLength) + UE_KINDA_SMALL_NUMBER;
	const double MaxReach = UpperLength + LowerLength;
	const FVector ReachableTarget = Root + TargetAim * FMath::Clamp(TargetDelta.Size(), MinReach, MaxReach);
	if (Diagnostics)
	{
		Diagnostics->UpperLength = UpperLength;
		Diagnostics->LowerLength = LowerLength;
		Diagnostics->TargetDistance = TargetDelta.Size();
		Diagnostics->bClampedMinimum = TargetDelta.Size() < MinReach;
		Diagnostics->bClampedMaximum = TargetDelta.Size() > MaxReach;
		Diagnostics->BendDirection = Bend;
		Diagnostics->ReachableTarget = ReachableTarget;
		Diagnostics->RawBendDirection = RawBend;
		Diagnostics->bGuideHistory = bHadGuideHistory;
		Diagnostics->bGuideReacquiring = BendState && BendState->bReacquiring;
		Diagnostics->GuideCorrectionDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector::DotProduct(RawBend, Bend), -1.0, 1.0)));
	}
	const FVector JointGuide = Root + Bend * MaxReach;
	AnimationCore::SolveTwoBoneIK(OutUpperArm, OutForearm, OutHand, JointGuide,
		ReachableTarget, UpperLength, LowerLength, false, 1.0, 1.0);
	if (bMatchWristRotation) { OutHand.SetRotation(WristTarget.GetRotation().GetNormalized()); }
	OutUpperArm.NormalizeRotation();
	OutForearm.NormalizeRotation();
	OutHand.NormalizeRotation();
	return !OutUpperArm.ContainsNaN() && !OutForearm.ContainsNaN() && !OutHand.ContainsNaN();
}
