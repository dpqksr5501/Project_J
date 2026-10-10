#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/Project_JMovingTurnPolicy.h"
#include "Animation/Project_JGeneralTurnPolicy.h"
#include "Animation/Project_JTurnRequestSample.h"
#include "Animation/Project_JLocomotionContextBuilder.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Misc/DataValidation.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTurnHandoffTest, "ProjectJ.MovingTurn.CoordinatedHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTurnHandoffTest::RunTest(const FString&)
{
	for (auto Mode : {EProject_JLocomotionRotationMode::OrientToMovement, EProject_JLocomotionRotationMode::Strafe})
	for (auto Gait : {EProject_JLocomotionGaitIntent::Run, EProject_JLocomotionGaitIntent::Sprint})
	for (const int32 FPS : {30, 60, 144})
	for (const float Sign : {-1.f, 1.f})
	{
		FProject_JMovingTurnPolicy Acute;
		FProject_JGeneralTurnPolicy General;
		FProject_JMovingTurnPolicy::FInput A; A.bEligible = true; A.RotationMode = Mode;
		FProject_JGeneralTurnPolicy::FInput G; G.bEligible = true; G.Mode = Mode; G.Speed = 350;
		A.Gait = G.Gait = Gait;
		FProject_JGeneralTurnPolicy::FSettings Settings;
		bool bUsedGeneral = false, bUsedAcute = false, bUsedBridge = false;
		for (int32 Frame = 0; Frame <= FPS / 2; ++Frame)
		{
			A.NowSeconds = G.Now = double(Frame) / FPS;
			// A real correction ramps from an aligned forward run. The actor and
			// velocity deliberately lag together; this is not an aligned curve.
			A.MoveYaw = A.TargetFacingYaw = Sign * FMath::Min(Frame * 420.f / FPS, 180.f);
			const bool bActive = Acute.Update(A);
			G.MoveYaw = G.FacingYaw = A.MoveYaw;
			G.bAcuteApproach = Acute.AllowsGeneralTurnHandoff(A);
			G.bEligible = !bActive;
			const bool bGeneral = General.Update(G, Settings);
			bUsedGeneral |= bGeneral; bUsedAcute |= bActive;
			if (G.bAcuteApproach && bUsedGeneral && !bUsedAcute)
			{
				bUsedBridge = true;
				TestTrue(TEXT("Confirmed correction bridges until acute admission"), bGeneral);
			}
			TestFalse(TEXT("General and acute never own three search pools together"), bGeneral && bActive);
		}
		TestTrue(TEXT("Correction began with general data"), bUsedGeneral);
		TestTrue(TEXT("Same reversal reached acute data"), bUsedAcute);
		TestTrue(TEXT("Approach exercised at this sample rate"), bUsedBridge);
	}
	// A pending approach is not an early 180 entry and cannot keep general data
	// after real Cycle handoff, or reopen it without prior correction.
	FProject_JMovingTurnPolicy A;
	FProject_JMovingTurnPolicy::FInput I; I.bEligible = true; A.Update(I);
	I.NowSeconds = .01; I.MoveYaw = I.TargetFacingYaw = 140;
	TestFalse(TEXT("140 degrees does not lower the acute entry threshold"), A.Update(I));
	TestTrue(TEXT("Forward geometry can advertise approach without acute ownership"), A.AllowsGeneralTurnHandoff(I));
	I.MoveYaw = 180; I.TargetFacingYaw = 0;
	TestFalse(TEXT("Backpedal cannot advertise a forward handoff"), A.AllowsGeneralTurnHandoff(I));
	I.MoveYaw = I.TargetFacingYaw = 140; I.VelocityYaw = 0; I.bEntryQualified = false;
	TestTrue(TEXT("The qualified pending event survives its braking minimum"), A.AllowsGeneralTurnHandoff(I));
	A.Reset(); A.Update(I);
	TestFalse(TEXT("Low speed cannot originate a handoff without a running origin"), A.AllowsGeneralTurnHandoff(I));
	FProject_JGeneralTurnPolicy P; FProject_JGeneralTurnPolicy::FInput G;
	FProject_JGeneralTurnPolicy::FSettings S; G.bEligible = true; G.Speed = 350;
	for (int32 N = 0; N <= 20; ++N) { G.Now = N / 60.; G.MoveYaw = G.FacingYaw = N * 4; P.Update(G, S); }
	G.MoveYaw = G.FacingYaw = 140; G.Now += .01; G.bAcuteApproach = true;
	TestTrue(TEXT("Existing correction bridges a partial forward reversal"), P.Update(G, S));
	G.Now += .07;
	TestTrue(TEXT("Same physical correction bridges beyond the old 0.06-second timer"), P.Update(G, S));
	G.Now += .01; G.SelectionFrame = 1; G.bSelectedGeneralTurn = true; P.Update(G, S);
	G.Now += .01; G.SelectionFrame = 2; G.bSelectedGeneralTurn = false; G.bSelectedCycle = true;
	TestFalse(TEXT("Actual Cycle handoff closes the bridge even while preparation is advertised"), P.Update(G, S));
	G.Now += .01;
	TestFalse(TEXT("The same correction cannot reopen after Cycle won"), P.Update(G, S));
	P.Reset();
	TestFalse(TEXT("Approach alone cannot start general Turn"), P.Update(G, S));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTurnSampleTest, "ProjectJ.MovingTurn.SharedSampleValidity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTurnSampleTest::RunTest(const FString&)
{
	FProject_JTurnRequestSample S; S.bValid = true; S.Frame = 100; S.Seconds = 2;
	TestTrue(TEXT("One physical publication can cross the animation boundary"), S.IsUsable(102, 2.02, S.Mode));
	TestFalse(TEXT("Old frame cannot retain candidate ownership"), S.IsUsable(103, 2.02, S.Mode));
	TestFalse(TEXT("Future frame is rejected"), S.IsUsable(99, 2.02, S.Mode));
	TestFalse(TEXT("Stale time is rejected"), S.IsUsable(100, 2.11, S.Mode));
	TestFalse(TEXT("Clock reversal is rejected"), S.IsUsable(100, 1.99, S.Mode));
	TestFalse(TEXT("Mode handoff cannot reuse old sample"), S.IsUsable(100, 2, EProject_JLocomotionRotationMode::Strafe));
	S.Speed = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("Invalid geometry is rejected"), S.IsUsable(100, 2, S.Mode));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTurnPreparedTest, "ProjectJ.MovingTurn.PreparedCorrection",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTurnPreparedTest::RunTest(const FString&)
{
 for (auto Mode : {EProject_JLocomotionRotationMode::OrientToMovement, EProject_JLocomotionRotationMode::Strafe})
 for (const int32 FPS : {30, 60, 144})
 for (const float Sign : {-1.f, 1.f})
 {
  FProject_JMovingTurnPolicy P;
  FProject_JMovingTurnPolicy::FInput I; I.bEligible = true; I.RotationMode = Mode;
  P.Update(I);
  // The running origin qualifies before deceleration. By commitment the
  // capsule has partially followed and the instantaneous speed gate fails.
  bool Prepared = false, Entered = false;
  for (int32 N = 1; N <= FPS; ++N)
  {
   I.NowSeconds = double(N) / FPS;
   I.MoveYaw = I.TargetFacingYaw = Sign * FMath::Min(180.f, 240.f * N / FPS);
   I.ActorYaw = Sign * FMath::Min(70.f, FMath::Abs(I.MoveYaw) * .45f);
   I.bEntryQualified = FMath::Abs(I.MoveYaw) < 70;
   const bool Active = P.Update(I);
   Prepared |= P.IsPreparing(); Entered |= Active;
   if (Active) TestFalse(TEXT("Prepared correction does not require simultaneous entry speed"), I.bEntryQualified);
  }
  TestTrue(TEXT("Gradual request records a qualified running origin"), Prepared);
  TestTrue(TEXT("Delayed commitment works in both directions, modes and sample rates"), Entered);
  I.ActorYaw = I.VelocityYaw = I.MoveYaw; I.NowSeconds += 1. / FPS;
  TestFalse(TEXT("Geometric recovery ends correction without waiting for watchdog"), P.Update(I));
  TestFalse(TEXT("Recovery clears preparation"), P.IsPreparing());
  P.Reset(); I = {}; I.bEligible = true; I.RotationMode = Mode; P.Update(I);
  for (int32 N = 1; N <= 28; ++N)
  {
   I.NowSeconds = N / 60.; I.ActorYaw = I.MoveYaw = I.TargetFacingYaw = Sign * N * 10.f;
   I.VelocityYaw = I.MoveYaw - Sign * 25.f; P.Update(I);
  }
  I.NowSeconds += .1; I.MoveYaw = I.TargetFacingYaw = Sign * 335.f;
  I.bEntryQualified = false; P.Update(I);
  I.NowSeconds += .1; I.MoveYaw = I.TargetFacingYaw = Sign * 430.f;
  I.ActorYaw = Sign * 360.f; I.VelocityYaw = Sign * 300.f;
  TestTrue(TEXT("Ordinary curved running refreshes the origin for the next delayed correction"), P.Update(I));
  P.Reset();
  // Multiple complete camera revolutions with coherent physical travel must
  // never manufacture a 180 event from accumulated input yaw.
  for (int32 N = 0; N < FPS * 4; ++N)
  {
   I.NowSeconds = double(N) / FPS; I.bEntryQualified = true;
   I.ActorYaw = I.VelocityYaw = I.MoveYaw = I.TargetFacingYaw = Sign * N * 180.f / FPS;
   TestFalse(TEXT("Already aligned curves never borrow acute Turn"), P.Update(I));
   TestFalse(TEXT("Aligned curves retain no preparation debt"), P.IsPreparing());
  }
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTurnRecoveryTest, "ProjectJ.MovingTurn.VisualRecoveryAndSelection",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTurnRecoveryTest::RunTest(const FString&)
{
 FProject_JMovingTurnPolicy P;
 FProject_JMovingTurnPolicy::FInput I; I.bEligible = true; I.bHasVisualFacing = true; P.Update(I);
 I.NowSeconds = .1; I.ActorYaw = I.VelocityYaw = I.MoveYaw = I.TargetFacingYaw = 70;
 I.VisualFacingYaw = 0; P.Update(I);
 TestEqual(TEXT("Aligned travel with an evaluated facing debt uses the separate recovery route"),
  P.GetDemand(), FProject_JMovingTurnPolicy::EDemand::FacingRecovery);
 I.NowSeconds = .4; I.ActorYaw = I.VelocityYaw = I.MoveYaw = I.TargetFacingYaw = 160;
 I.VisualFacingYaw = 70;
 TestTrue(TEXT("A qualified visual recovery can search authored Turn without capsule reversal"), P.Update(I));
 I.SelectionFrame = 10; I.bSelectedCycle = true; I.NowSeconds = .42;
 TestTrue(TEXT("Cycle competition before Turn actually wins is not a completion signal"), P.Update(I));
 I.SelectionFrame = 11; I.bSelectedCycle = false; I.bSelectedTurn = true; I.NowSeconds = .44; P.Update(I);
 I.SelectionFrame = 12; I.bSelectedTurn = false; I.bSelectedCycle = true; I.NowSeconds = .46;
 TestFalse(TEXT("Fresh completed Cycle selection after Turn releases ownership"), P.Update(I));
 TestFalse(TEXT("The completed event does not reopen from the same visual debt"), P.Update(I));
 P.Reset(); I = {}; I.bEligible = true; I.bHasVisualFacing = true; P.Update(I);
 I.NowSeconds = .1; I.MoveYaw = I.TargetFacingYaw = 180; P.Update(I);
 I.NowSeconds = .2; I.ActorYaw = I.VelocityYaw = 180; I.VisualFacingYaw = 140;
 TestFalse(TEXT("Physical alignment and an ordinary steering residual end the turn"), P.Update(I));
 TestTrue(TEXT("Geometric completion allows the selected pose to finish naturally"), P.AllowsCompletedTurnContinuation());
 FProject_JTurnSelectionFeedback F; F.Frame = 100; F.Seconds = 2;
 TestTrue(TEXT("Completed feedback is usable across the animation frame boundary"), F.IsFresh(102, 2.02, F.Mode));
 TestFalse(TEXT("Old completed frame is rejected"), F.IsFresh(103, 2.02, F.Mode));
 TestFalse(TEXT("Old completed timestamp is rejected"), F.IsFresh(100, 2.11, F.Mode));
 TestFalse(TEXT("Future feedback is rejected"), F.IsFresh(99, 2.02, F.Mode));
 TestFalse(TEXT("A mode change cannot borrow old visual recovery"), F.IsFresh(100, 2, EProject_JLocomotionRotationMode::Strafe));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTurnCoverageTest, "ProjectJ.MovingTurn.CoverageAndInterruption",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTurnCoverageTest::RunTest(const FString&)
{
 FProject_JMovingTurnPolicy P;
 FProject_JMovingTurnPolicy::FInput I; I.bEligible = true;
 const auto Prepare = [&]() { P.Reset(); I = {}; I.bEligible = true; P.Update(I);
  I.NowSeconds = .1; I.MoveYaw = I.TargetFacingYaw = 140; P.Update(I); };
 Prepare(); I.NowSeconds = .2; I.MoveYaw = I.TargetFacingYaw = 179; TestTrue(TEXT("Qualified event enters"), P.Update(I));
 I.NowSeconds = .21; I.MoveYaw = I.TargetFacingYaw = -179;
 TestTrue(TEXT("Signed continuity crosses +/-180 without a target-change cancellation"), P.Update(I));
 I.NowSeconds = .3; I.MoveYaw = I.TargetFacingYaw = -130;
 TestFalse(TEXT("Request beyond authored forward coverage releases the event"), P.Update(I));
 Prepare(); I.MoveYaw = I.TargetFacingYaw = 95; I.NowSeconds = .2;
 TestFalse(TEXT("Meaningful reversal cancels the old preparation"), P.Update(I));
 TestFalse(TEXT("Reversed request clears its running origin"), P.IsArmed());
 Prepare(); I.MoveYaw = 50; I.NowSeconds = .2;
 TestFalse(TEXT("A/S/D request cancels rather than borrowing the forward family"), P.Update(I));
 TestFalse(TEXT("Side request clears preparation"), P.IsPreparing());
 Prepare(); I.bEligible = false; P.Update(I); I.bEligible = true; I.bEntryQualified = false;
 I.MoveYaw = I.TargetFacingYaw = 180; I.NowSeconds = .3;
 TestFalse(TEXT("Action/release cannot revive an old origin at low speed"), P.Update(I));
 P.Reset(); I = {}; I.bEligible = true; P.Update(I);
 I.NowSeconds = .1; I.MoveYaw = I.TargetFacingYaw = 12; I.ActorYaw = I.VelocityYaw = -28; P.Update(I);
 I.NowSeconds = .2; I.MoveYaw = I.TargetFacingYaw = 15; I.ActorYaw = I.VelocityYaw = -35; P.Update(I);
 I.NowSeconds = .4; I.MoveYaw = I.TargetFacingYaw = 135; I.ActorYaw = I.VelocityYaw = 80;
 TestFalse(TEXT("Opposite velocity lag cannot inflate an ordinary 135-degree correction into acute Turn"), P.Update(I));
 TestTrue(TEXT("Ordinary correction retains its requested coverage"), FMath::Abs(P.GetRequestedSweep()) < 150);
 P.Reset(); I = {}; I.bEligible = true; P.Update(I);
 I.NowSeconds = .1; I.bEntryQualified = false; P.Update(I); // Blocked/slow while still aligned.
 I.NowSeconds = .2; I.MoveYaw = I.TargetFacingYaw = 180;
 TestFalse(TEXT("An old run at a wall cannot originate a new Turn later"), P.Update(I));
 TestFalse(TEXT("An old blocked run cannot retain preparation"), P.IsPreparing());
 Prepare(); I.NowSeconds = -.1; P.Update(I);
 I.NowSeconds = 0; I.ActorYaw = I.VelocityYaw = I.MoveYaw = I.TargetFacingYaw = 0; P.Update(I);
 TestTrue(TEXT("Clock reset can observe a new coherent origin immediately"), P.IsArmed());
 FProject_JTurnEventSettings S; S.MinimumRemainingFacingAngle = 100;
 P.Reset(); I = {}; I.bEligible = true; P.Update(I, S);
 I.NowSeconds = .1; I.MoveYaw = I.TargetFacingYaw = 140; P.Update(I, S);
 I.NowSeconds = .2; I.MoveYaw = I.TargetFacingYaw = 160; I.ActorYaw = 80;
 TestFalse(TEXT("Profile coverage can withhold a Turn with insufficient remaining correction"), P.Update(I, S));
 S.MinimumRemainingFacingAngle = 60;
 TestTrue(TEXT("Changing the profile coverage admits the same qualified correction"), P.Update(I, S));
 S.MinimumRemainingFacingAngle = std::numeric_limits<float>::quiet_NaN();
 TestTrue(TEXT("Corrupt profile input resolves to finite safe tuning"), FMath::IsFinite(S.Resolved().MinimumRemainingFacingAngle));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTurnPreparationPhaseTest, "ProjectJ.MovingTurn.PreparationPhaseOwnership",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTurnPreparationPhaseTest::RunTest(const FString&)
{
 FProject_JLocomotionContextBuilder::FPhaseInput I;
 I.bPreparingForwardTurn = I.bHasMoveInput = true; I.GroundMode = EProject_JGroundMotionMode::Locomotion;
 for (bool Strafe : {false, true})
 {
  I.bCombatStrafe = Strafe;
  TestEqual(TEXT("Zero-speed preparation keeps Cycle presentation"), FProject_JLocomotionContextBuilder::ResolvePhaseFamily(I), EProject_JLocomotionPhaseFamily::Cycle);
  I.bStarting = true;
  TestEqual(TEXT("Explicit Start preempts preparation"), FProject_JLocomotionContextBuilder::ResolvePhaseFamily(I), EProject_JLocomotionPhaseFamily::Start); I.bStarting = false;
  I.GroundMode = EProject_JGroundMotionMode::Stop;
  TestEqual(TEXT("Explicit Stop preempts preparation"), FProject_JLocomotionContextBuilder::ResolvePhaseFamily(I), EProject_JLocomotionPhaseFamily::Stop); I.GroundMode = EProject_JGroundMotionMode::Locomotion;
  I.bFallOffOrInAir = true;
  TestEqual(TEXT("Air preempts preparation"), FProject_JLocomotionContextBuilder::ResolvePhaseFamily(I), EProject_JLocomotionPhaseFamily::Fall); I.bFallOffOrInAir = false;
 }
 return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTurnProfileValidationTest, "ProjectJ.MovingTurn.ProfileValidation",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTurnProfileValidationTest::RunTest(const FString&)
{
 auto* P = NewObject<UProject_JLocomotionProfile>();
 FDataValidationContext Valid;
 TestTrue(TEXT("Default forward-turn profiles pass authoring validation"), P->IsDataValid(Valid) != EDataValidationResult::Invalid);
 P->OTMForwardTurn.MinimumRemainingFacingAngle = std::numeric_limits<float>::quiet_NaN();
 FDataValidationContext Invalid;
 TestTrue(TEXT("Invalid OTM tuning is visible to asset validation"), P->IsDataValid(Invalid) == EDataValidationResult::Invalid);
 P->OTMForwardTurn = {};
 P->StrafeForwardTurn.SettledPathAngle = P->StrafeForwardTurn.PreparationPathAngle;
 FDataValidationContext Hysteresis;
 TestTrue(TEXT("Strafe recovery cannot equal its preparation threshold"), P->IsDataValid(Hysteresis) == EDataValidationResult::Invalid);
 P->StrafeForwardTurn.PreparationPathAngle = 1;
 P->StrafeForwardTurn.SettledPathAngle = std::numeric_limits<float>::quiet_NaN();
 const auto Safe = P->StrafeForwardTurn.Resolved();
 TestTrue(TEXT("Fallback tuning still respects dependent hysteresis bounds"), Safe.SettledPathAngle < Safe.PreparationPathAngle);
 return true;
}
#endif
#endif
