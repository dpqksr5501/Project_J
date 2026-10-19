#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JOneShotInputResponse.h"
#include "Animation/Project_JGeneralTurnPolicy.h"
#include "Animation/Project_JMovingTurnPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOneShotCurveTest, "ProjectJ.Animation.OneShotInput.ContinuousCurve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJOneShotCurveTest::RunTest(const FString&)
{
	using P = FProject_JOneShotInputResponse;
	for (int32 Mode : {0, 1}) for (float Sign : {-1.f, 1.f}) for (float Rate : {5.f, 30.f, 60.f})
	{
		P::FInput I; I.bCanSteer = I.bHasMoveReference = I.bHasMoveInput = true;
		for (int32 N = 0; N < 360; ++N)
		{
			I.ControlYaw = I.MoveYaw = FMath::UnwindDegrees(Sign * Rate * N / 60.f);
			I.FacingError = 3; I.PathError = 2;
			TestFalse(TEXT("A compatible held key stays in its authored phase through accumulated curves and yaw wrap"), P::Resolve(I).bRelease);
		}
		I.FacingError = 90;
		TestTrue(TEXT("Real steering demand beyond capacity still releases immediately"), P::Resolve(I).bRelease);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOneShotInputCompatibilityTest, "ProjectJ.Animation.OneShotInput.Compatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJOneShotInputCompatibilityTest::RunTest(const FString&)
{
	using P = FProject_JOneShotInputResponse;
	P::FInput I; I.bCanSteer = I.bHasMoveReference = I.bHasMoveInput = true;
	for (float KeyYaw : {0.f, 90.f, -90.f, 180.f})
	{
		I.EntryMoveYaw = KeyYaw; I.MoveYaw = KeyYaw + 40; I.ControlYaw = 40;
		I.FacingError = 12; I.PathError = 10;
		TestFalse(TEXT("Same key through a camera sweep keeps the authored command"), P::Resolve(I).bRelease);
	}
	I.EntryMoveYaw = 0; I.ControlYaw = 0; I.MoveYaw = 90;
	TestTrue(TEXT("New lateral key releases even if the body is already aligned"), P::Resolve(I).bRelease);
	I.MoveYaw = I.ControlYaw = 180; I.FacingError = 150;
	TestTrue(TEXT("Fast coherent camera/input reversal exceeds live steering capacity"), P::Resolve(I).bRelease);
	I.MoveYaw = I.ControlYaw = 40; I.FacingError = 12;
	I.bCanSteer = false;
	TestTrue(TEXT("Disabled/stale/unavailable steering retains the legacy cancellation"), P::Resolve(I).bRelease);
	I.bCanSteer = true; I.FacingError = 0; I.PathError = 80;
	TestTrue(TEXT("Incompatible momentum releases even when facing already caught up"), P::Resolve(I).bRelease);
	I.PathError = 0; I.ControlYaw = 179; I.EntryControlYaw = -179; I.MoveYaw = 179; I.EntryMoveYaw = -179;
	TestFalse(TEXT("Yaw wrap is not a new keyboard command"), P::Resolve(I).bRelease);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOneShotObservedGeneralTurnTest, "ProjectJ.Animation.OneShotInput.ObservedGeneralTurn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJOneShotObservedGeneralTurnTest::RunTest(const FString&)
{
	for (auto Mode : {EProject_JLocomotionRotationMode::OrientToMovement, EProject_JLocomotionRotationMode::Strafe})
	{
		FProject_JGeneralTurnPolicy P; FProject_JGeneralTurnPolicy::FInput I;
		I.bEligible = I.bObserveOnly = true; I.Mode = Mode; I.Speed = 400;
		for (int32 N = 0; N < 20; ++N)
		{
			I.Now = N / 60.; I.MoveYaw = I.FacingYaw = N * 4; I.ActorYaw = I.VelocityYaw = 0;
			TestFalse(TEXT("Observation never opens a candidate pool while the shot owns the pose"), P.Update(I, {}));
		}
		I.bObserveOnly = false; I.Now += 1. / 60.;
		TestTrue(TEXT("Confirmed correction is admitted on the very first MM return sample"), P.Update(I, {}));
		TestTrue(TEXT("Physical turn history survives the one-shot"), P.GetMoveYawRate() > 100);
		I.bEligible = false; I.Now += 1. / 60.;
		TestFalse(TEXT("Ownership/action/input loss discards observed admission"), P.Update(I, {}));
	}
	FProject_JGeneralTurnPolicy P; FProject_JGeneralTurnPolicy::FInput I;
	I.bEligible = I.bObserveOnly = true; I.Mode = EProject_JLocomotionRotationMode::Strafe; I.Speed = 400;
	for (int32 N = 0; N < 20; ++N) { I.Now = N / 60.; I.FacingYaw = N * 4; I.MoveYaw = 180; P.Update(I, {}); }
	I.bObserveOnly = false; I.Now += 1. / 60.;
	TestFalse(TEXT("Backward Strafe cannot borrow forward Turn data at return"), P.Update(I, {}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOneShotObservedAcuteTurnTest, "ProjectJ.Animation.OneShotInput.ObservedAcuteTurn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJOneShotObservedAcuteTurnTest::RunTest(const FString&)
{
	for (auto Mode : {EProject_JLocomotionRotationMode::OrientToMovement, EProject_JLocomotionRotationMode::Strafe})
	{
		FProject_JMovingTurnPolicy P; FProject_JMovingTurnPolicy::FInput I;
		I.bEligible = I.bDeferAdmission = true; I.RotationMode = Mode; I.NowSeconds = 1;
		TestFalse(TEXT("A qualified authored forward run is only observed"), P.Update(I));
		I.NowSeconds += .016; I.MoveYaw = I.TargetFacingYaw = 180;
		TestFalse(TEXT("A reversal cannot activate MM beneath the shot"), P.Update(I));
		TestTrue(TEXT("The reversal keeps its proven running origin"), P.IsPreparing());
		auto Return = P; I.bDeferAdmission = false;
		TestTrue(TEXT("First return can admit the same physical reversal without losing its origin"), Return.Update(I));
		TestFalse(TEXT("The read-only return probe does not advance the live policy"), P.IsActive());
		I.bEligible = false;
		TestFalse(TEXT("Actions, air, input loss, or unsupported Sprint direction cancel observation"), P.Update(I));
	}
	return true;
}
#endif
