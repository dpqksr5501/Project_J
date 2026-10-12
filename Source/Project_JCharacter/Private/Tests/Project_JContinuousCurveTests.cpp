#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JContinuousCurvePolicy.h"
#include "Animation/Project_JGeneralTurnPolicy.h"
#include "Animation/Project_JOneShotCurvePolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJContinuousCurveEvidenceTest, "ProjectJ.Animation.OneShotInput.SustainedEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJContinuousCurveEvidenceTest::RunTest(const FString&)
{
	for (int32 Hz : {30, 60, 120, 144}) for (float Sign : {-1.f, 1.f}) for (bool bPackets : {false, true})
	{
		FProject_JContinuousCurvePolicy P; FProject_JContinuousCurvePolicy::FInput I;
		I.bEligible = I.bHasRequestedHeading = true;
		double First = -1;
		for (int32 N = 0; N <= Hz * 2; ++N)
		{
			I.Now = double(N) / Hz;
			const double Time = bPackets ? FMath::FloorToDouble(I.Now * 20) / 20 : I.Now;
			I.RequestedHeading = FMath::UnwindDegrees(179 + Sign * 5 * Time);
			I.Velocity = FRotator(0, I.RequestedHeading, 0).Vector() * 400;
			I.Position += I.Velocity / Hz;
			if (P.Update(I, {}).bActive && First < 0) First = I.Now;
		}
		TestTrue(TEXT("Slow physical curves including sparse mouse packets confirm around 0.6 seconds at every tick rate"), First >= .55 && First <= .75);
		I.HistoryRevision++; I.Now += 1. / Hz; I.Position += I.Velocity / Hz;
		TestFalse(TEXT("New movement history revokes accumulated proof"), P.Update(I, {}).bActive);
	}
	for (int32 Case = 0; Case < 5; ++Case)
	{
		FProject_JContinuousCurvePolicy P; FProject_JContinuousCurvePolicy::FInput I;
		I.bEligible = I.bHasRequestedHeading = true;
		FProject_JGeneralTurnPolicy Routing;
		bool bActive = false, bDynamic = false;
		for (int32 N = 0; N < 240; ++N)
		{
			I.Now = N / 120.;
			// Isolated tiny nudge, repeated jitter, camera-only, blocked and straight travel.
			I.RequestedHeading = Case == 0 ? FMath::Min(N * .1f, 1.f) : Case == 1 ? (N % 2 ? .15f : -.15f) : Case == 2 ? N * .1f : 0;
			I.Velocity = FRotator(0, Case == 2 ? 0 : I.RequestedHeading, 0).Vector() * 400;
			if (Case != 3) I.Position += I.Velocity / 120;
			const auto R = P.Update(I, {}); bActive |= R.bActive;
			FProject_JGeneralTurnPolicy::FInput G;
			G.bEligible = true; G.Mode = EProject_JLocomotionRotationMode::Strafe; G.Speed = 400; G.Now = I.Now;
			G.ActorYaw = G.FacingYaw = G.MoveYaw = G.VelocityYaw = Case == 2 ? 0 : I.RequestedHeading;
			G.bContinuousCurve = R.bActive; Routing.Update(G, {}); bDynamic |= Routing.RequiresDynamicCycle();
		}
		TestFalse(TEXT("Tiny nudge, jitter, camera-only, blocked and straight travel do not prove a curve"), bActive);
		TestFalse(TEXT("Tiny rapid samples cannot bypass Strafe routing through raw yaw rate"), bDynamic);
	}
	FProject_JOneShotCurvePolicy Shot; FProject_JOneShotCurvePolicy::FInput I;
	I.bEligible = I.bHasRequestedHeading = true;
	for (int32 N = 0; N < 120; ++N)
	{
		I.Now = N / 60.; I.RequestedHeading = N / 12.f;
		I.Velocity = I.FutureVelocity = FRotator(0, I.RequestedHeading, 0).Vector() * 400;
		I.Position += I.Velocity / 60;
		TestFalse(TEXT("Confirmed slow curve cannot bypass a protected Start or Land"), Shot.Update(I, {}).bYield);
	}
	I.Now += 1. / 60; I.Position += I.Velocity / 60; I.bCanExit = true;
	TestTrue(TEXT("Protected shot can yield as soon as its semantic exit becomes ready"), Shot.Update(I, {}).bYield);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCurveRoutingReversalTest, "ProjectJ.Animation.OneShotInput.CurveRoutingReversal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCurveRoutingReversalTest::RunTest(const FString&)
{
	for (int32 Hz : {30, 60, 120}) for (float Rate : {5.f, 21.f, 90.f}) for (bool bPackets : {false, true})
	{
		FProject_JContinuousCurvePolicy P; FProject_JContinuousCurvePolicy::FInput I;
		I.bEligible = I.bHasRequestedHeading = true;
		bool bConfirmed = false; int32 LostFrames = 0, UnconfirmedBridgeFrames = 0;
		for (int32 N = 0; N < Hz * 4; ++N)
		{
			I.Now = double(N) / Hz;
			const double T = bPackets ? FMath::FloorToDouble(I.Now * 20) / 20 : I.Now;
			I.RequestedHeading = float(Rate * (T <= 2 ? T : 4 - T));
			I.Velocity = FRotator(0, I.RequestedHeading, 0).Vector() * 500;
			I.Position += I.Velocity / Hz;
			const auto R = P.Update(I, {});
			bConfirmed |= R.bActive;
			if (I.Now >= 1 && !R.bRoutingActive) ++LostFrames;
			if (I.Now >= 2 && R.bRoutingActive && !R.bActive) ++UnconfirmedBridgeFrames;
		}
		TestTrue(TEXT("A genuine curve confirms before its direction reverses"), bConfirmed);
		TestEqual(TEXT("Coherent opposite travel cannot force a settled-only candidate pool"), LostFrames, 0);
		TestTrue(TEXT("Routing bridge does not replace the strict authored-exit proof"), UnconfirmedBridgeFrames > 0);
		for (int32 N = 0; N < Hz * 2; ++N)
		{
			I.Now += 1. / Hz; I.RequestedHeading = N % 2 ? .15f : -.15f;
			I.Velocity = FRotator(0, I.RequestedHeading, 0).Vector() * 500; I.Position += I.Velocity / Hz;
			const auto R = P.Update(I, {});
			if (N > Hz * 1.5) TestFalse(TEXT("Pointer noise cannot preserve a stale confirmed curve"), R.bRoutingActive);
		}
		I.Now += 1. / Hz; I.HistoryRevision++;
		TestFalse(TEXT("History reset clears routing protection immediately"), P.Update(I, {}).bRoutingActive);
	}
	return true;
}
#endif
