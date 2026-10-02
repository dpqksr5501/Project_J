#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/Project_JGuidedArmSolver.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGuidedArmSolverTest,
	"ProjectJ.Animation.GuidedArmContact", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJGuidedArmSolverTest::RunTest(const FString&)
{
	const FTransform Upper(FQuat::Identity, FVector::ZeroVector);
	const FTransform Forearm(FQuat::Identity, FVector(30.0, 20.0, 0.0));
	const FTransform Hand(FQuat::Identity, FVector(60.0, 0.0, 0.0));
	FTransform SolvedUpper, SolvedForearm, SolvedHand;
	const auto Solve = [&](const FTransform& Target, const FVector* Guide = nullptr)
	{
		return Project_J::Animation::SolveGuidedArm(Upper, Forearm, Hand, Target,
			FVector::YAxisVector, Guide, true, SolvedUpper, SolvedForearm, SolvedHand);
	};
	TestTrue(TEXT("Input wrist contact is solvable"), Solve(Hand));
	TestTrue(TEXT("An already aligned input retains its natural elbow and hand"),
		SolvedUpper.Equals(Upper, 0.001f) && SolvedForearm.Equals(Forearm, 0.001f) && SolvedHand.Equals(Hand, 0.001f));

	const FTransform Target(FRotator(15.0, 35.0, 70.0), FVector(45.0, 20.0, 12.0));
	TestTrue(TEXT("Reachable contact is solvable"), Solve(Target));
	TestTrue(TEXT("Reachable wrist position and rotation match the calibrated contact"),
		SolvedHand.GetLocation().Equals(Target.GetLocation(), 0.001f) &&
		SolvedHand.GetRotation().Equals(Target.GetRotation(), 0.001f));
	const double UpperLength = FVector::Distance(Upper.GetLocation(), Forearm.GetLocation());
	const double LowerLength = FVector::Distance(Forearm.GetLocation(), Hand.GetLocation());
	TestTrue(TEXT("Contact preserves both segment lengths"),
		FMath::IsNearlyEqual(FVector::Distance(SolvedUpper.GetLocation(), SolvedForearm.GetLocation()), UpperLength, 0.001) &&
		FMath::IsNearlyEqual(FVector::Distance(SolvedForearm.GetLocation(), SolvedHand.GetLocation()), LowerLength, 0.001));
	const FVector NaturalBend = SolvedForearm.GetLocation();
	FVector Aim = (Target.GetLocation() - Upper.GetLocation()).GetSafeNormal();
	TestTrue(TEXT("Input elbow side guides the solve"),
		FVector::DotProduct(NaturalBend - Aim * FVector::DotProduct(NaturalBend, Aim), FVector::YAxisVector) > 0.0);

	// This geometry has a different upper/forearm ratio and is larger than the
	// first body. Nothing in the solve assumes mannequin bone lengths or names.
	const FTransform LongerForearm(FVector(40.0, 30.0, 0.0));
	const FTransform LongerHand(FVector(75.0, 0.0, 0.0));
	const FTransform LongerTarget(FVector(70.0, 25.0, 5.0));
	TestTrue(TEXT("A longer body with unequal segments reaches its own target"),
		Project_J::Animation::SolveGuidedArm(Upper, LongerForearm, LongerHand, LongerTarget,
			FVector::YAxisVector, nullptr, true, SolvedUpper, SolvedForearm, SolvedHand));
	TestTrue(TEXT("Longer body retains its actual segment lengths and contact"),
		SolvedHand.GetLocation().Equals(LongerTarget.GetLocation(), 0.001f) &&
		FMath::IsNearlyEqual(FVector::Distance(SolvedUpper.GetLocation(), SolvedForearm.GetLocation()), 50.0, 0.001) &&
		FMath::IsNearlyEqual(FVector::Distance(SolvedForearm.GetLocation(), SolvedHand.GetLocation()),
			FVector::Distance(LongerForearm.GetLocation(), LongerHand.GetLocation()), 0.001));

	const FVector ExplicitGuide(0.0, -100.0, 0.0);
	TestTrue(TEXT("A body-authored guide is supported"), Solve(FTransform(FVector(50.0, 0.0, 0.0)), &ExplicitGuide));
	TestTrue(TEXT("Explicit guide chooses the authored elbow side"), SolvedForearm.GetLocation().Y < 0.0);

	Solve(FTransform(FVector(200.0, 0.0, 0.0)));
	TestTrue(TEXT("Unreachable contact never stretches the arm or claims exact contact"),
		FMath::IsNearlyEqual(SolvedHand.GetLocation().Size(), UpperLength + LowerLength, 0.001) &&
		FVector::Distance(SolvedHand.GetLocation(), FVector(200.0, 0.0, 0.0)) > 100.0);
	TestTrue(TEXT("Minimum-reach target retains unequal segment lengths"),
		Project_J::Animation::SolveGuidedArm(Upper, LongerForearm, LongerHand, Upper,
			FVector::YAxisVector, nullptr, true, SolvedUpper, SolvedForearm, SolvedHand) &&
		FMath::IsNearlyEqual(FVector::Distance(SolvedForearm.GetLocation(), SolvedHand.GetLocation()),
			FVector::Distance(LongerForearm.GetLocation(), LongerHand.GetLocation()), 0.001));

	TestTrue(TEXT("A straight input pose has a finite fallback bend"),
		Project_J::Animation::SolveGuidedArm(Upper, FTransform(FVector(30.0, 0.0, 0.0)), Hand,
			FTransform(FVector(40.0, 0.0, 0.0)), FVector::YAxisVector, nullptr, true,
			SolvedUpper, SolvedForearm, SolvedHand) && !SolvedForearm.ContainsNaN() && SolvedForearm.GetLocation().Y > 0.0);

	// Move the target back to a fixed natural input pose without carrying a
	// stale final elbow snapshot or previous-solve state between evaluations.
	FVector PreviousElbow;
	for (int32 Index = 0; Index <= 60; ++Index)
	{
		const double T = Index / 60.0;
		FTransform ReturningTarget;
		ReturningTarget.Blend(Target, Hand, T);
		TestTrue(TEXT("Return contact remains finite"), Solve(ReturningTarget));
		if (Index > 0)
		{
			TestTrue(TEXT("Elbow guide does not flip during a gradual return"),
				FVector::Distance(PreviousElbow, SolvedForearm.GetLocation()) < 2.0);
		}
		PreviousElbow = SolvedForearm.GetLocation();
	}
	TestTrue(TEXT("Return ends at the original natural elbow"), SolvedForearm.GetLocation().Equals(Forearm.GetLocation(), 0.001f));
	const FTransform MirroredForearm(FVector(30.0, -20.0, 0.0));
	TestTrue(TEXT("Mirrored arm retains its own input elbow side"),
		Project_J::Animation::SolveGuidedArm(Upper, MirroredForearm, Hand, FTransform(FVector(50.0, 0.0, 0.0)),
			-FVector::YAxisVector, nullptr, true, SolvedUpper, SolvedForearm, SolvedHand) && SolvedForearm.GetLocation().Y < 0.0);
	TestTrue(TEXT("Antipodal target retains input bend instead of an arbitrary flipped plane"),
		Solve(FTransform(FVector(-50.0, 0.0, 0.0))) && SolvedForearm.GetLocation().Y > 0.0);
	Project_J::Animation::FGuidedArmSolveDiagnostics Diagnostics;
	const FTransform WithoutTraceUpper = SolvedUpper;
	const FTransform WithoutTraceForearm = SolvedForearm;
	const FTransform WithoutTraceHand = SolvedHand;
	TestTrue(TEXT("Enabling solver diagnostics preserves the exact antipodal result"),
		Project_J::Animation::SolveGuidedArm(Upper, Forearm, Hand, FTransform(FVector(-50.0, 0.0, 0.0)),
			FVector::YAxisVector, nullptr, true, SolvedUpper, SolvedForearm, SolvedHand, &Diagnostics) &&
		SolvedUpper.Equals(WithoutTraceUpper, 0.000001f) && SolvedForearm.Equals(WithoutTraceForearm, 0.000001f) &&
		SolvedHand.Equals(WithoutTraceHand, 0.000001f) && Diagnostics.bAntipodal && !Diagnostics.bUsedFallback);
	Project_J::Animation::SolveGuidedArm(Upper, Forearm, Hand, FTransform(FVector(200.0, 0.0, 0.0)),
		FVector::YAxisVector, nullptr, true, SolvedUpper, SolvedForearm, SolvedHand, &Diagnostics);
	TestTrue(TEXT("Trace distinguishes an unreachable target from a fallback bend"),
		Diagnostics.bClampedMaximum && !Diagnostics.bClampedMinimum && !Diagnostics.bUsedFallback &&
		Diagnostics.ReachableTarget.Equals(SolvedHand.GetLocation(), 0.001f));
	Project_J::Animation::SolveGuidedArm(Upper, FTransform(FVector(30.0, 0.0, 0.0)), Hand,
		FTransform(FVector(40.0, 0.0, 0.0)), FVector::YAxisVector, nullptr, true,
		SolvedUpper, SolvedForearm, SolvedHand, &Diagnostics);
	TestTrue(TEXT("Trace reports the straight-input fallback without inventing a plane"),
		Diagnostics.bUsedFallback && !Diagnostics.bUsedBestAxis && FMath::IsNearlyZero(Diagnostics.InputBendHeight));
	TestFalse(TEXT("Degenerate zero-length chain leaves input untouched"),
		Project_J::Animation::SolveGuidedArm(Upper, Upper, Hand, Target, FVector::YAxisVector, nullptr,
			true, SolvedUpper, SolvedForearm, SolvedHand));
	return true;
}

#endif
