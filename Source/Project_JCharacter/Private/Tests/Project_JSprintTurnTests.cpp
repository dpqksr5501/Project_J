#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/Project_JMovingTurnPolicy.h"
#include "Animation/Project_JGeneralTurnPolicy.h"
#include "Animation/Project_JTurnRequestSample.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSprintTurnTest, "ProjectJ.MovingTurn.SprintFamilyOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJSprintTurnTest::RunTest(const FString&)
{
	auto* Profile = NewObject<UProject_JLocomotionProfile>();
	TestEqual(TEXT("Run qualification preserved"), Profile->OTMForwardTurn.RunningQualificationSpeed, 180.f);
	TestEqual(TEXT("Sprint has independent qualification"), Profile->OTMSprintForwardTurn.RunningQualificationSpeed, 300.f);
	FProject_JTurnSelectionFeedback Feedback; Feedback.Frame = 10; Feedback.Seconds = 1;
	TestFalse(TEXT("Run evaluation cannot complete Sprint"), Feedback.IsFresh(11, 1.01, Feedback.Mode, EProject_JLocomotionGaitIntent::Sprint));
	FProject_JTurnRequestSample Sample; Sample.bValid = true; Sample.Frame = 10; Sample.Seconds = 1;
	TestFalse(TEXT("Run sample cannot drive Sprint"), Sample.IsUsable(11, 1.01, Sample.Mode, EProject_JLocomotionGaitIntent::Sprint));
	for (auto Mode : {EProject_JLocomotionRotationMode::OrientToMovement, EProject_JLocomotionRotationMode::Strafe})
	{
		FProject_JMovingTurnPolicy P; FProject_JMovingTurnPolicy::FInput I; I.bEligible = true; I.RotationMode = Mode;
		P.Update(I); I.NowSeconds = .01; I.TargetFacingYaw = I.MoveYaw = 160;
		TestTrue(TEXT("Run reversal entered"), P.Update(I));
		I.NowSeconds += .01; I.SelectionFrame = 10; I.bSelectedTurn = true; P.Update(I);
		I.Gait = EProject_JLocomotionGaitIntent::Sprint; I.bSelectedTurn = false; I.bSelectedCycle = true; I.SelectionFrame = 11;
		I.NowSeconds += .01;
		TestTrue(TEXT("Coherent physical correction survives family switch"), P.Update(I, Profile->GetForwardTurnSettings(Mode, I.Gait)));
		TestFalse(TEXT("Old family cycle cannot grant continuation"), P.AllowsCompletedTurnContinuation());
		I.NowSeconds += .01; I.SelectionFrame = 12; I.bSelectedCycle = false; I.bSelectedTurn = true; P.Update(I);
		I.NowSeconds += .01; I.SelectionFrame = 13; I.bSelectedCycle = true; I.bSelectedTurn = false;
		TestFalse(TEXT("New Sprint selection hands back to Cycle"), P.Update(I));
		TestEqual(TEXT("Completed from actual Sprint selection"), FString(P.GetLastUpdateReason()), FString(TEXT("CycleHandoff")));
	}
	for (float Offset : {-45.f, 0.f, 45.f})
	{
		FProject_JMovingTurnPolicy P; FProject_JMovingTurnPolicy::FInput I;
		I.bEligible = true; I.Gait = EProject_JLocomotionGaitIntent::Sprint; I.RotationMode = EProject_JLocomotionRotationMode::Strafe;
		I.MoveYaw = I.VelocityYaw = Offset;
		const auto S = Profile->StrafeSprintForwardTurn;
		P.Update(I, S);
		TestTrue(TEXT("Forward diagonal can establish an origin"), P.IsArmed());
		I.NowSeconds = .01; I.MoveYaw = Offset + 160; I.TargetFacingYaw = 160;
		TestTrue(TEXT("Forward diagonal reversal can offer Sprint Turn"), P.Update(I, S));
		I.NowSeconds += .01; I.TargetFacingYaw = I.MoveYaw + 90;
		TestFalse(TEXT("Lateral request cancels forward coverage"), P.Update(I, S));
	}
	FProject_JGeneralTurnPolicy G; FProject_JGeneralTurnPolicy::FInput I;
	I.bEligible = true; I.Speed = 650; I.Gait = EProject_JLocomotionGaitIntent::Sprint;
	for (int32 N = 0; N < 120; ++N)
	{
		I.Now = N / 60.; I.MoveYaw = I.FacingYaw = I.ActorYaw = I.VelocityYaw = N * 2.f;
		TestFalse(TEXT("Aligned Sprint curve stays Cycle even beyond 180 degrees"), G.Update(I, {}));
	}
	I.Gait = EProject_JLocomotionGaitIntent::Run; I.Now += .01;
	TestFalse(TEXT("General history resets at gait boundary"), G.Update(I, {}));
	TestEqual(TEXT("Reset reason"), FString(G.GetReason()), FString(TEXT("OwnerOrSampleChanged")));
	return true;
}
#endif
