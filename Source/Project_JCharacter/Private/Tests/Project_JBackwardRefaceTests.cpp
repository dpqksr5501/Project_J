#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JBackwardRefacePolicy.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJBackwardRefaceLifetime, "ProjectJ.BackwardReface.Lifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJBackwardRefaceLifetime::RunTest(const FString&)
{
	for (int32 FPS : {30,60,120})
	{
		FProject_JBackwardRefacePolicy P;
		FProject_JBackwardRefacePolicy::FInput I;
		I.bEligible = I.bVisualFresh = true; I.Speed = 300; I.MoveYaw = I.VelocityYaw = 180;
		TestFalse(TEXT("Straight backward Run retains its Cycle"), P.Update(I));
		I.Now = 1. / FPS; I.FacingYaw = 170; I.MoveYaw = -10;
		TestTrue(TEXT("Held S with real facing/path reversal opens authored candidates"), P.Update(I));
		I.SelectionFrame = 1; I.bSelectedReface = true; I.Now += 1. / FPS;
		TestTrue(TEXT("Selected Reface can continue inside the window"), P.Update(I));
		I.bSelectedReface = false; I.bSelectedCycle = true; I.SelectionFrame = 2; I.Now += 1. / FPS;
		TestFalse(TEXT("A fresh Cycle winner closes Reface competition"), P.Update(I));
		I.Now += 1. / FPS;
		TestFalse(TEXT("An unchanged large error cannot reopen the same event"), P.Update(I));
		I.VisualYaw = I.FacingYaw; I.VelocityYaw = I.MoveYaw; I.bSelectedCycle = false;
		for (int N=0;N<FPS/4;++N) { I.Now += 1. / FPS; P.Update(I); }
		I.FacingYaw = 0; I.MoveYaw = 180; I.Now += 1. / FPS;
		TestTrue(TEXT("Coherent Run re-arms a later opposite camera reversal"), P.Update(I));
		I.bEligible = false; I.Now += 1. / FPS;
		TestFalse(TEXT("Action/montage/air/one-shot/remote cancellation is immediate"), P.Update(I));
		TestFalse(TEXT("Cancelled owner cannot retain a Reface continuing pose"), P.AllowsContinuation());
	}
	FProject_JBackwardRefacePolicy P;
	FProject_JBackwardRefacePolicy::FInput I;
	I.bEligible = I.bVisualFresh = true; I.Speed = 300; I.FacingYaw = 180; I.MoveYaw = 0; I.VelocityYaw = 180;
	TestTrue(TEXT("A pre-existing physical reversal is eligible without an input revision change"), P.Update(I));
	I.Now = .01; I.SelectionFrame = 1; I.bSelectedReface = true; P.Update(I);
	I.bSelectedReface = false; I.VisualYaw = 180; I.VelocityYaw = 0;
	for (int N=0;N<40;++N) { I.Now += .01; P.Update(I); }
	TestTrue(TEXT("Aligned used Reface receives bounded completion grace"), P.AllowsContinuation());
	I.Now += .01; I.FacingYaw = 140; P.Update(I);
	TestFalse(TEXT("A new target revokes completion grace"), P.AllowsContinuation());
	I.bVisualFresh = false;
	TestFalse(TEXT("Stale evaluated root cannot open candidates"), P.Update(I));
	P.Reset(); I = {}; I.bEligible = I.bVisualFresh = true; I.Speed = 149; I.FacingYaw = 180;
	TestFalse(TEXT("Slow movement is not a running Reface"), P.Update(I));
	I.Speed = 300; I.Now = .01; TestTrue(TEXT("Running speed opens the same correction"), P.Update(I));
	for(int N=0;N<150;++N) { I.Now += .01; P.Update(I); }
	TestFalse(TEXT("Timeout closes an unfinished correction"), P.Update(I));
	TestFalse(TEXT("Timeout does not grant successful continuation"), P.AllowsContinuation());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJBackwardRefaceCandidates, "ProjectJ.BackwardReface.CandidatesAndInterrupt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJBackwardRefaceCandidates::RunTest(const FString&)
{
	auto* Set = NewObject<UProject_JMotionMatchingAssetSet>();
	auto* Cycle = NewObject<UPoseSearchDatabase>(); auto* Reface = NewObject<UPoseSearchDatabase>();
	auto* Schema = NewObject<UPoseSearchSchema>(); Cycle->Schema = Reface->Schema = Schema;
	auto* Norm = NewObject<UPoseSearchNormalizationSet>(); Norm->Databases = {Cycle,Reface};
	Cycle->NormalizationSet = Reface->NormalizationSet = Norm;
	Set->RunDatabases.Cycle = Cycle; Set->RunDatabases.BackwardRefaceCycle = Cycle; Set->RunDatabases.BackwardReface = Reface;
	FProject_JMotionMatchingSelectionContext C;
	C.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle; C.bBackwardRefaceCandidates = true;
	TestNull(TEXT("OTM cannot borrow backward Reface"), Set->FindTurnCycleCompanion(C,Cycle));
	C.RotationMode = EProject_JLocomotionRotationMode::Strafe; C.bUseGenericFamiliesForNonOrientToMovement = true;
	TestEqual(TEXT("Eligible Combat Run searches Reface beside Cycle"), Set->FindTurnCycleCompanion(C,Cycle),Reface);
	C.GaitIntent = EProject_JLocomotionGaitIntent::Sprint;
	TestNull(TEXT("Backward Sprint never opens Run Reface"), Set->FindTurnCycleCompanion(C,Cycle));
	C.GaitIntent = EProject_JLocomotionGaitIntent::Run; Reface->NormalizationSet = NewObject<UPoseSearchNormalizationSet>();
	TestNull(TEXT("Incompatible normalization refuses competition"), Set->FindTurnCycleCompanion(C,Cycle));
	Reface->NormalizationSet = Norm; Reface->Schema = NewObject<UPoseSearchSchema>();
	TestNull(TEXT("Incompatible schema refuses competition"), Set->FindTurnCycleCompanion(C,Cycle));
	FProject_JCharacterAnimInstanceProxy Proxy; FProject_JAnimThreadSafeData D;
	D.LocomotionContext.PhaseFamily = D.MotionMatching.SelectionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	D.LocomotionContext.bIsMotionMatchingMoving = true;
	Proxy.QueueGameThreadData(D,Cycle,true,true,false); Proxy.ConsumeQueuedGameThreadData(); Proxy.ClearMotionMatchingReselects();
	D.MotionMatching.SelectionContext.bBackwardRefaceCandidates = true;
	Proxy.QueueGameThreadData(D,Cycle,true,true,false,true,Reface);
	TestEqual(TEXT("New normalization retires the previous cost context once"),Proxy.ResolveReselectInterruptMode(),EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose);
	Proxy.ClearMotionMatchingReselects();
	D.MotionMatching.SelectionContext.bBackwardRefaceCandidates = false;
	D.MotionMatching.SelectionContext.bAllowBackwardRefaceContinuation = true;
	Proxy.QueueGameThreadData(D,Cycle,true,true,false);
	TestFalse(TEXT("Successful closure preserves the continuing pose"),Proxy.bForceMotionMatchingReselect);
	D.MotionMatching.SelectionContext.bAllowBackwardRefaceContinuation = false;
	Proxy.QueueGameThreadData(D,Cycle,true,true,false);
	TestEqual(TEXT("Revoking grace retires continuing Reface even with unchanged primary PSD"),Proxy.ResolveReselectInterruptMode(),EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose);
	return true;
}
#endif
