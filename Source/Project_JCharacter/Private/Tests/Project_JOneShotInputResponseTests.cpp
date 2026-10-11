#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JOneShotInputResponse.h"
#include "Animation/Project_JGeneralTurnPolicy.h"
#include "Animation/Project_JMovingTurnPolicy.h"
#include "Animation/Project_JOneShotCurvePolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOneShotCurveTest, "ProjectJ.Animation.OneShotInput.ContinuousCurve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJOneShotCurveTest::RunTest(const FString&)
{
	using P = FProject_JOneShotCurvePolicy;
	for (int32 Mode : {0, 1}) for (float Sign : {-1.f, 1.f}) for (float Rate : {5.f, 30.f, 60.f})
	{
		P Policy; P::FInput I; I.bEligible = true; I.Mode = Mode;
		bool bYielded = false;
		for (int32 N = 0; N < 360; ++N)
		{
			I.Now = N / 60.; const float Yaw = Sign * Rate * I.Now;
			I.Velocity = FRotator(0, Yaw, 0).RotateVector(FVector(400, 0, 0));
			I.FutureVelocity = FRotator(0, Sign * 2, 0).RotateVector(I.Velocity);
			I.Position += I.Velocity / 60;
			I.bCanExit = N >= 30;
			const auto R = Policy.Update(I, {});
			if (!I.bCanExit) TestFalse(TEXT("Curve cannot bypass protected Start/Land phase"), R.bYield);
			bYielded |= R.bYield;
		}
		TestTrue(TEXT("Actual consistent curve yields at fast and slow rates, in both modes and directions"), bYielded);
	}
	for (int32 Case = 0; Case < 7; ++Case)
	{
		P Policy; P::FInput I; I.bEligible = I.bCanExit = true;
		for (int32 N = 0; N < 180; ++N)
		{
			I.Now = N / 60.;
			const float Yaw = Case == 0 ? 0.f : Case == 1 ? (N % 2 ? 2.f : -2.f) :
				Case == 2 ? FMath::Min(N * .5f, 5.f) : N * .5f;
			I.Velocity = FRotator(0, Yaw, 0).RotateVector(FVector(400, 0, 0));
			I.FutureVelocity = Case == 3 ? FRotator(0, -5, 0).RotateVector(I.Velocity) : I.Velocity;
			if (Case != 4) I.Position += I.Velocity / 60; // blocked motion
			if (Case == 5) ++I.OwnerRevision;
			if (Case == 6) ++I.TrajectoryRevision;
			TestFalse(TEXT("Camera-only/straight, alternating noise, transient, opposite forecast, blocked motion and new owner/history do not yield"), Policy.Update(I, {}).bYield);
		}
	}
	P Policy; P::FInput I; I.bEligible = I.bCanExit = true; I.Velocity = I.FutureVelocity = FVector(400, 0, 0);
	Policy.Update(I, {}); I.Now = .016; I.Position.X = 10000;
	TestFalse(TEXT("Teleport cannot create curved travel evidence"), Policy.Update(I, {}).bYield);
	I.bEligible = false;
	TestFalse(TEXT("Air, action, unsupported direction and stale trajectory discard evidence"), Policy.Update(I, {}).bYield);
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
