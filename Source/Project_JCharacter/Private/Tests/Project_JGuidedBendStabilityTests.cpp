#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JGuidedArmSolver.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGuidedBendStabilityTest,
	"ProjectJ.Animation.GuidedBendStability", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJGuidedBendStabilityTest::RunTest(const FString&)
{
	using namespace Project_J::Animation;
	FGuidedArmBendSettings Settings;
	FGuidedArmBendState State;
	const FVector Aim = FVector::XAxisVector;
	const FVector Fallback = FVector::YAxisVector;
	StabilizeGuidedArmBend(Fallback, Aim, Fallback, 0.5, 1.0 / 60.0, Settings, State);
	TestTrue(TEXT("Weak reversed input retains its stable bend plane"),
		StabilizeGuidedArmBend(-Fallback, Aim, Fallback, 0.04, 1.0 / 60.0, Settings, State).Equals(Fallback));
	const FVector Reacquired = StabilizeGuidedArmBend(-Fallback, Aim, Fallback, 0.5, 1.0 / 60.0, Settings, State);
	TestTrue(TEXT("A newly reliable 180-degree reversal remains finite and bounded"),
		!Reacquired.ContainsNaN() && FMath::IsNearlyEqual(Reacquired.Size(), 1.0, 0.00001) &&
		FVector::DotProduct(Fallback, Reacquired) > 0.97);
	TestTrue(TEXT("Repeated evaluation without elapsed time does not advance reacquisition"),
		StabilizeGuidedArmBend(-Fallback, Aim, Fallback, 0.5, 0.0, Settings, State).Equals(Reacquired, 0.00001));
	for (int32 I = 0; I < 20; ++I)
	{
		StabilizeGuidedArmBend(-Fallback, Aim, Fallback, 0.5, 1.0 / 60.0, Settings, State);
	}
	TestTrue(TEXT("Reliable input is fully reacquired without persistent guide lag"),
		State.Bend.Equals(-Fallback, 0.00001) && !State.bReacquiring);
	TestTrue(TEXT("A long cadence gap discards stale history"),
		StabilizeGuidedArmBend(Fallback, Aim, Fallback, 0.5, 0.5, Settings, State).Equals(Fallback));
	State.Reset();
	TestTrue(TEXT("A new weak chain starts from its body fallback, not an old solve"),
		StabilizeGuidedArmBend(-Fallback, Aim, Fallback, 0.0, 1.0 / 30.0, Settings, State).Equals(Fallback));
	TestTrue(TEXT("A moving wrist axis transports the retained guide on its perpendicular plane"),
		StabilizeGuidedArmBend(Fallback, FVector::ZAxisVector, Fallback, 0.0, 1.0 / 30.0, Settings, State).Equals(Fallback));

	struct FSample { FVector Upper, Elbow, Hand, Target; };
	// Geometry from the user's second late-LMB2 pop (2026-10-02), rounded by
	// the trace to 0.01 cm. No asset, actor name or network state is needed.
	const FSample Samples[] = {
		{FVector(-43.49, 2.65, 94.98), FVector(-31.93, -11.94, 76.85), FVector(-46.12, -23.27, 60.19), FVector(-35.92, -29.18, 64.68)},
		{FVector(-42.96, 3.44, 96.46), FVector(-32.69, -11.40, 77.75), FVector(-48.28, -20.84, 61.17), FVector(-38.21, -27.35, 65.40)},
		{FVector(-42.21, 3.91, 97.92), FVector(-33.42, -10.87, 78.43), FVector(-50.36, -18.34, 62.17), FVector(-40.66, -25.52, 66.06)},
		{FVector(-40.37, 4.03, 100.01), FVector(-33.76, -10.29, 79.36), FVector(-52.19, -15.63, 63.89), FVector(-43.67, -23.56, 67.31)},
		{FVector(-37.60, 3.74, 102.55), FVector(-33.81, -9.63, 80.59), FVector(-53.73, -12.66, 66.41), FVector(-47.10, -21.12, 69.44)},
		{FVector(-33.77, 3.35, 105.72), FVector(-33.56, -8.63, 82.66), FVector(-54.79, -9.25, 70.17), FVector(-50.53, -17.89, 73.04)},
		{FVector(-29.10, 2.62, 109.19), FVector(-32.99, -7.32, 85.50), FVector(-55.26, -5.52, 75.11), FVector(-53.58, -13.57, 78.22)},
		{FVector(-24.60, 1.93, 112.39), FVector(-32.39, -5.69, 88.80), FVector(-55.09, -1.50, 80.17), FVector(-55.45, -8.70, 84.16)},
		{FVector(-20.27, 1.23, 115.34), FVector(-31.63, -3.79, 92.51), FVector(-54.29, 2.67, 85.31), FVector(-56.10, -3.37, 90.57)},
		{FVector(-17.17, 0.46, 117.36), FVector(-31.24, -1.99, 95.65), FVector(-53.59, 6.23, 89.32), FVector(-55.90, 0.95, 95.54)},
		{FVector(-15.03, -0.20, 118.73), FVector(-31.17, -0.27, 98.37), FVector(-53.12, 9.25, 92.45), FVector(-55.45, 4.23, 99.21)},
		{FVector(-13.78, -0.91, 119.57), FVector(-31.69, 1.38, 100.88), FVector(-53.26, 11.90, 95.30), FVector(-55.15, 6.40, 101.73)},
		{FVector(-13.24, -1.57, 120.02), FVector(-32.67, 2.91, 103.35), FVector(-53.93, 14.20, 98.07), FVector(-55.16, 7.63, 103.32)},
		{FVector(-12.76, -2.16, 120.60), FVector(-33.67, 4.63, 106.76), FVector(-54.62, 16.64, 101.81), FVector(-55.13, 8.78, 105.15)},
		{FVector(-12.32, -2.68, 121.32), FVector(-34.36, 6.48, 111.04), FVector(-54.98, 19.16, 106.41), FVector(-54.91, 9.93, 107.18)},
		{FVector(-12.09, -3.07, 122.15), FVector(-34.52, 8.48, 115.94), FVector(-54.97, 21.48, 111.41), FVector(-54.66, 10.90, 109.37)},
		{FVector(-12.03, -3.36, 123.07), FVector(-33.87, 10.60, 121.17), FVector(-54.35, 23.51, 116.57), FVector(-54.23, 11.68, 111.65)},
		{FVector(-12.24, -3.51, 124.04), FVector(-33.08, 11.99, 124.90), FVector(-53.59, 25.15, 121.19), FVector(-53.80, 12.08, 113.91)},
		{FVector(-12.67, -3.55, 125.04), FVector(-32.62, 12.94, 127.35), FVector(-52.90, 26.79, 125.25), FVector(-53.60, 12.38, 116.03)},
		{FVector(-13.47, -3.47, 126.04), FVector(-32.58, 13.85, 129.28), FVector(-52.55, 28.26, 128.34), FVector(-53.91, 12.57, 117.83)},
		{FVector(-14.61, -3.28, 127.04), FVector(-32.96, 14.72, 130.83), FVector(-52.59, 29.62, 130.69), FVector(-54.67, 12.61, 119.41)},
		{FVector(-16.18, -3.01, 128.05), FVector(-33.87, 15.60, 132.06), FVector(-53.15, 30.94, 132.39), FVector(-55.91, 12.60, 120.73)},
		{FVector(-18.15, -2.64, 129.04), FVector(-35.28, 16.49, 133.03), FVector(-54.20, 32.27, 133.55), FVector(-57.55, 12.58, 121.82)},
		{FVector(-20.37, -2.25, 129.94), FVector(-37.97, 16.55, 133.44), FVector(-55.62, 33.71, 134.59), FVector(-59.36, 12.50, 122.87)},
		{FVector(-22.80, -1.83, 130.74), FVector(-41.68, 15.83, 133.41), FVector(-57.04, 35.00, 135.38), FVector(-60.92, 12.27, 123.82)},
		{FVector(-25.32, -1.43, 131.40), FVector(-45.24, 15.13, 133.43), FVector(-58.27, 35.89, 136.01), FVector(-62.23, 11.87, 124.74)},
		{FVector(-27.91, -1.05, 131.93), FVector(-48.68, 14.48, 133.50), FVector(-59.39, 36.47, 136.55), FVector(-63.38, 11.38, 125.65)},
		{FVector(-30.49, -0.68, 132.32), FVector(-51.97, 13.90, 133.49), FVector(-60.50, 36.76, 136.98), FVector(-64.45, 10.79, 126.53)},
		{FVector(-33.06, -0.31, 132.58), FVector(-55.13, 13.39, 133.39), FVector(-61.62, 36.83, 137.36), FVector(-65.48, 10.18, 127.43)},
		{FVector(-35.49, 0.02, 132.68), FVector(-58.10, 12.82, 133.08), FVector(-62.75, 36.58, 137.70), FVector(-66.40, 9.46, 128.33)},
		{FVector(-37.80, 0.31, 132.65), FVector(-60.90, 12.20, 132.59), FVector(-63.90, 36.05, 138.01), FVector(-67.23, 8.68, 129.28)},
		{FVector(-39.83, 0.52, 132.50), FVector(-63.39, 11.47, 131.86), FVector(-65.04, 35.20, 138.31), FVector(-67.91, 7.80, 130.27)},
		{FVector(-41.63, 0.67, 132.25), FVector(-65.59, 10.64, 130.92), FVector(-66.18, 34.05, 138.60), FVector(-68.46, 6.82, 131.34)},
		{FVector(-43.04, 0.71, 131.92), FVector(-67.34, 9.67, 129.80), FVector(-67.26, 32.58, 138.90), FVector(-68.79, 5.72, 132.49)},
		{FVector(-44.12, 0.66, 131.53), FVector(-68.69, 8.57, 128.53), FVector(-68.31, 30.80, 139.18), FVector(-68.95, 4.50, 133.74)},
		{FVector(-44.96, 0.51, 130.81), FVector(-69.63, 7.40, 126.38), FVector(-69.50, 28.70, 138.78), FVector(-69.06, 3.10, 134.83)},
		{FVector(-45.60, 0.29, 129.80), FVector(-70.13, 6.13, 123.53), FVector(-70.85, 26.26, 137.74), FVector(-69.11, 1.48, 135.74)},
		{FVector(-46.17, 0.02, 128.27), FVector(-70.66, 3.90, 120.49), FVector(-72.84, 23.25, 135.60), FVector(-69.43, -0.38, 136.21)},
		{FVector(-46.66, -0.27, 126.33), FVector(-71.07, 0.82, 117.48), FVector(-74.97, 19.82, 132.69), FVector(-69.81, -2.17, 136.42)},
		{FVector(-47.00, -0.53, 124.10), FVector(-70.84, -1.80, 113.83), FVector(-77.03, 16.35, 129.31), FVector(-70.38, -3.64, 136.39)},
		{FVector(-47.20, -0.76, 121.65), FVector(-70.06, -4.11, 109.76), FVector(-78.97, 12.65, 125.48), FVector(-71.08, -5.01, 136.03)},
		{FVector(-47.22, -0.90, 119.12), FVector(-69.18, -6.55, 106.42), FVector(-80.64, 8.85, 121.88), FVector(-71.89, -5.92, 135.45)},
		{FVector(-47.10, -0.94, 116.53), FVector(-68.26, -9.02, 103.79), FVector(-81.96, 5.00, 118.73), FVector(-72.70, -6.40, 134.74)},
		{FVector(-46.84, -0.83, 113.99), FVector(-67.39, -10.71, 101.53), FVector(-82.72, 1.66, 116.34), FVector(-73.36, -6.23, 133.90)},
	};
	for (const int32 Stride : { 1, 2 })
	{
		State.Reset();
		double RawMaxStep = 0.0, StableMaxStep = 0.0;
		FVector PreviousRaw, PreviousStable;
		for (int32 I = 0; I < UE_ARRAY_COUNT(Samples); I += Stride)
		{
			const FSample& S = Samples[I];
			const FTransform Upper(S.Upper), Elbow(S.Elbow), Hand(S.Hand);
			const FTransform Target(FRotator(10.0, 20.0, 30.0), S.Target);
			FTransform RawUpper, RawElbow, RawHand, StableUpper, StableElbow, StableHand;
			FGuidedArmSolveDiagnostics Diagnostics;
			TestTrue(TEXT("Raw captured solve succeeds"), SolveGuidedArm(Upper, Elbow, Hand, Target,
				Fallback, nullptr, true, RawUpper, RawElbow, RawHand));
			TestTrue(TEXT("Stabilized captured solve succeeds"), SolveGuidedArm(Upper, Elbow, Hand, Target,
				Fallback, nullptr, true, StableUpper, StableElbow, StableHand, &Diagnostics,
				&State, &Settings, Stride / 60.0));
			TestTrue(TEXT("Stabilization preserves reachable wrist contact and rotation"),
				StableHand.Equals(Target, 0.001f) && !Diagnostics.bClampedMaximum && !Diagnostics.bClampedMinimum);
			TestTrue(TEXT("Stabilization preserves the imported body's actual segment lengths"),
				FMath::IsNearlyEqual(FVector::Distance(StableUpper.GetLocation(), StableElbow.GetLocation()),
					FVector::Distance(S.Upper, S.Elbow), 0.001) &&
				FMath::IsNearlyEqual(FVector::Distance(StableElbow.GetLocation(), StableHand.GetLocation()),
					FVector::Distance(S.Elbow, S.Hand), 0.001));
			if (I > 0)
			{
				RawMaxStep = FMath::Max(RawMaxStep, FVector::Distance(PreviousRaw, RawElbow.GetLocation()));
				StableMaxStep = FMath::Max(StableMaxStep, FVector::Distance(PreviousStable, StableElbow.GetLocation()));
			}
			PreviousRaw = RawElbow.GetLocation();
			PreviousStable = StableElbow.GetLocation();
		}
		AddInfo(FString::Printf(TEXT("Captured replay %d Hz: raw max elbow step %.3f cm, stabilized %.3f cm"),
			60 / Stride, RawMaxStep, StableMaxStep));
		TestTrue(TEXT("Captured near-straight elbow spike is reduced at both evaluation cadences"),
			RawMaxStep > 15.0 && StableMaxStep < RawMaxStep * 0.6);
	}
	return true;
}
#endif
