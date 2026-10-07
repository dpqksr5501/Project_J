#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/Project_JMovingTurnPolicy.h"
#include "Animation/Project_JLocomotionContextBuilder.h"
#include "Animation/Project_JMotionMatchingSelectionPolicy.h"
#include "Misc/AutomationTest.h"
#include "Animation/Project_JMotionMatchingCVars.h"
#include "HAL/IConsoleManager.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMovingTurnGeometryTest, "ProjectJ.MovingTurn.Geometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMovingTurnGeometryTest::RunTest(const FString&)
{
	for (const auto Mode : {EProject_JLocomotionRotationMode::OrientToMovement, EProject_JLocomotionRotationMode::Strafe})
	{
		for (const float Sign : {-1.0f, 1.0f})
		{
			FProject_JMovingTurnPolicy Policy;
			FProject_JMovingTurnPolicy::FInput I; I.bEligible = true; I.RotationMode = Mode;
			TestFalse(TEXT("Aligned run primes but does not turn"), Policy.Update(I));
			I.TargetFacingYaw = Sign * 179.0f; I.MoveYaw = I.TargetFacingYaw;
			I.NowSeconds = 0.016;
			TestTrue(TEXT("Left/right body and path reversal enters in either mode"), Policy.Update(I));
		}
	}
	const auto Reject = [&](float Actor, float Velocity, float Move, float Facing)
	{
		FProject_JMovingTurnPolicy Policy;
		FProject_JMovingTurnPolicy::FInput I; I.bEligible = true; I.RotationMode = EProject_JLocomotionRotationMode::Strafe;
		Policy.Update(I);
		I.ActorYaw = Actor; I.VelocityYaw = Velocity; I.MoveYaw = Move; I.TargetFacingYaw = Facing;
		return !Policy.Update(I);
	};
	TestTrue(TEXT("S reversal without a camera turn belongs to Pivot"), Reject(0, 0, 180, 0));
	TestTrue(TEXT("Facing-only rotation with an unchanged world path does not fit Turn"), Reject(0, 0, 0, 180));
	TestTrue(TEXT("Backpedal-to-backpedal body turn does not fit forward Turn assets"), Reject(0, 180, 0, 180));
	TestTrue(TEXT("Small mouse turn remains Cycle"), Reject(0, 0, 30, 30));
	TestTrue(TEXT("135-degree mouse turn remains outside the Strafe 180 family"), Reject(0, 0, 135, 135));
	TestTrue(TEXT("Yaw wrap at 179/-179 is a two-degree turn"), Reject(179, 179, -179, -179));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMovingTurnLifetimeTest, "ProjectJ.MovingTurn.Lifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMovingTurnLifetimeTest::RunTest(const FString&)
{
	for (const int32 FPS : {30, 60, 120})
	{
		FProject_JMovingTurnPolicy Policy;
		FProject_JMovingTurnPolicy::FInput I; I.bEligible = true;
		Policy.Update(I);
		I.TargetFacingYaw = I.MoveYaw = 180; I.NowSeconds = 1.0 / FPS;
		TestTrue(TEXT("One forward reversal begins"), Policy.Update(I));
		I.ActorYaw = 40; I.VelocityYaw = I.MoveYaw;
		I.NowSeconds += 1.0 / FPS;
		TestTrue(TEXT("Velocity reaching the new direction does not prematurely end the same turn"), Policy.Update(I));
		I.NowSeconds = 0.9;
		TestFalse(TEXT("Unfinished turn cannot hold a restricted PSD indefinitely"), Policy.Update(I));
		I.NowSeconds = 2;
		TestFalse(TEXT("Timeout cannot retrigger from the same stale angle"), Policy.Update(I));
		I.ActorYaw = 180; Policy.Update(I); // Re-arm on a coherent running pose.
		I.TargetFacingYaw = I.MoveYaw = 0; I.VelocityYaw = 180; I.NowSeconds = 2.1;
		TestTrue(TEXT("A later opposite reversal can begin"), Policy.Update(I));
		I.bEligible = false;
		TestFalse(TEXT("Stop/air/landing/pivot/montage immediately cancels ownership"), Policy.Update(I));
		I.bEligible = true; I.NowSeconds = 2.4;
		TestFalse(TEXT("Resuming cannot revive a pre-interruption turn"), Policy.Update(I));
		I.ActorYaw = I.VelocityYaw = I.MoveYaw = I.TargetFacingYaw = 0; Policy.Update(I);
		I.RotationMode = EProject_JLocomotionRotationMode::Strafe; I.TargetFacingYaw = I.MoveYaw = 180;
		TestFalse(TEXT("OTM to Strafe is not a fresh mouse-turn event"), Policy.Update(I));
		TestFalse(TEXT("Persistent mode catch-up cannot create a delayed stale turn"), Policy.Update(I));
	}
	FProject_JMovingTurnPolicy Policy;
	FProject_JMovingTurnPolicy::FInput I; I.bEligible = true; Policy.Update(I);
	I.TargetFacingYaw = I.MoveYaw = 180;
	I.EntryAngle = I.ExitAngle = std::numeric_limits<float>::quiet_NaN();
	TestTrue(TEXT("Invalid tuning falls back to finite thresholds"), Policy.Update(I));
	I.NowSeconds = std::numeric_limits<double>::quiet_NaN();
	TestFalse(TEXT("Invalid sample cannot preserve a request"), Policy.Update(I));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMovingTurnOwnershipTest, "ProjectJ.MovingTurn.PhaseAndSearchEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMovingTurnOwnershipTest::RunTest(const FString&)
{
	FProject_JLocomotionContextBuilder::FPhaseInput I;
	I.bMoving = I.bHasMoveInput = I.bMovingTurn180 = I.bCombatFacingRedirect = true;
	I.GroundMode = EProject_JGroundMotionMode::Locomotion;
	I.GroundSpeed = 500; I.TurnMinSpeed = 180; I.TurnAngleThreshold = 45;
	for (const bool bStrafe : {false, true})
	{
		I.bCombatStrafe = bStrafe;
		TestEqual(TEXT("World reversal needs no WASD input-angle edge"), FProject_JLocomotionContextBuilder::ResolvePhaseFamily(I), EProject_JLocomotionPhaseFamily::Turn);
		I.bStarting = true;
		TestEqual(TEXT("Start owns its existing direct path"), FProject_JLocomotionContextBuilder::ResolvePhaseFamily(I), EProject_JLocomotionPhaseFamily::Start); I.bStarting = false;
		I.bPivoting = true;
		TestEqual(TEXT("Pivot owns its existing direct path"), FProject_JLocomotionContextBuilder::ResolvePhaseFamily(I), EProject_JLocomotionPhaseFamily::Pivot); I.bPivoting = false;
		I.bLanding = true;
		TestEqual(TEXT("Land owns its existing direct path"), FProject_JLocomotionContextBuilder::ResolvePhaseFamily(I), EProject_JLocomotionPhaseFamily::Landing); I.bLanding = false;
		I.GroundMode = EProject_JGroundMotionMode::Stop;
		TestEqual(TEXT("Stop releases moving-turn ownership"), FProject_JLocomotionContextBuilder::ResolvePhaseFamily(I), EProject_JLocomotionPhaseFamily::Stop); I.GroundMode = EProject_JGroundMotionMode::Locomotion;
	}
	FProject_JMotionMatchingSelectionPolicy Selection;
	FProject_JMotionMatchingSelectionPolicy::FKey Key; Key.Phase = EProject_JLocomotionPhaseFamily::Turn;
	Selection.Publish(Key);
	Key.bMovingTurn180 = true;
	TestTrue(TEXT("Entering large Turn inside an existing OTM Turn is still a semantic edge"), Selection.Publish(Key));
	TestFalse(TEXT("Keeping that request does not publish again every frame"), Selection.Publish(Key));
	Key.bMovingTurn180 = false;
	TestTrue(TEXT("Release is a separate semantic boundary"), Selection.Publish(Key));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMovingTurnDecelerationTest, "ProjectJ.MovingTurn.DecelerationLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMovingTurnDecelerationTest::RunTest(const FString&)
{
	for (auto Mode : {EProject_JLocomotionRotationMode::OrientToMovement, EProject_JLocomotionRotationMode::Strafe})
	{
		FProject_JMovingTurnPolicy Policy;
		FProject_JMovingTurnPolicy::FInput I;
		I.bEligible = true;
		I.RotationMode = Mode;
		Policy.Update(I);
		I.MoveYaw = I.TargetFacingYaw = 180;
		TestTrue(TEXT("Qualified moving reversal enters"), Policy.Update(I));
		I.bEntryQualified = false;
		I.VelocityYaw = 0; // At zero speed the velocity heading is undefined.
		I.ActorYaw = 50;
		I.NowSeconds = 0.1;
		TestTrue(TEXT("Normal braking does not cancel active ownership"), Policy.Update(I));
		TestEqual(TEXT("Trace uses the same active lifetime"), FString(Policy.DescribeNextUpdate(I)), FString(TEXT("Active")));
		FProject_JLocomotionContextBuilder::FPhaseInput Phase;
		Phase.GroundMode = EProject_JGroundMotionMode::Locomotion;
		Phase.bHasMoveInput = true;
		Phase.bCombatStrafe = Mode == EProject_JLocomotionRotationMode::Strafe;
		Phase.bMovingTurn180 = Policy.IsActive();
		Phase.TurnMinSpeed = 180;
		for (float Speed : {179.0f, 80.0f, 0.0f, 100.0f})
		{
			Phase.GroundSpeed = Speed;
			TestEqual(TEXT("Authorized Turn PSD survives the velocity minimum"),
				FProject_JLocomotionContextBuilder::ResolvePhaseFamily(Phase), EProject_JLocomotionPhaseFamily::Turn);
		}
		Phase.GroundMode = EProject_JGroundMotionMode::Stop;
		TestEqual(TEXT("Real Stop preempts the active request"), FProject_JLocomotionContextBuilder::ResolvePhaseFamily(Phase), EProject_JLocomotionPhaseFamily::Stop);
		I.bEligible = false;
		TestFalse(TEXT("Input release/owner interruption still cancels while braking"), Policy.Update(I));
		I.bEligible = true;
		TestFalse(TEXT("Low-speed input cannot revive the interrupted request"), Policy.Update(I));
		Policy.Reset(); I.ActorYaw = I.TargetFacingYaw = I.MoveYaw = 0;
		Policy.Update(I); I.MoveYaw = I.TargetFacingYaw = 180;
		TestFalse(TEXT("Low-speed context cannot create a new running turn"), Policy.Update(I));
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMovingTurnTraceTest, "ProjectJ.MovingTurn.TraceReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMovingTurnTraceTest::RunTest(const FString&)
{
	FProject_JMovingTurnPolicy Baseline, Observed;
	FProject_JMovingTurnPolicy::FInput I; I.bEligible = true;
	const auto Step = [&](const TCHAR* Expected)
	{
		TestEqual(TEXT("Diagnosis reports the actual upcoming gate"), FString(Observed.DescribeNextUpdate(I)), FString(Expected));
		TestEqual(TEXT("Reading diagnostics does not change the next decision"), Observed.Update(I), Baseline.Update(I));
		TestEqual(TEXT("Published diagnosis uses the completed update"), FString(Observed.GetLastUpdateReason()), FString(Expected));
		TestEqual(TEXT("Reading diagnostics does not change arming"), Observed.IsArmed(), Baseline.IsArmed());
	};
	Step(TEXT("FacingBelowEntry"));
	I.TargetFacingYaw = 180; Step(TEXT("NewTravelNotForward"));
	I.TargetFacingYaw = 0; Observed.Update(I); Baseline.Update(I); // Observe the new aligned owner before another event.
	I.TargetFacingYaw = 180;
	I.MoveYaw = 180; Step(TEXT("Enter"));
	I.NowSeconds = 0.1; Step(TEXT("Active"));
	I.NowSeconds = 0.8; Step(TEXT("Timeout"));
	I.NowSeconds = 1.0; Step(TEXT("NotArmed"));
	I.bEligible = false; Step(TEXT("Ineligible"));
	I.RotationMode = EProject_JLocomotionRotationMode::Strafe; Step(TEXT("ModeChanged"));
	auto* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ProjectJ.MovingTurnTrace"));
	if (TestNotNull(TEXT("Turn trace console switch registered"), CVar))
	{
		TestEqual(TEXT("Accessor reads the active diagnostic switch"), Project_J::MotionMatchingCVars::GetMovingTurnTraceMode(), CVar->GetInt());
	}
	return true;
}
#endif
