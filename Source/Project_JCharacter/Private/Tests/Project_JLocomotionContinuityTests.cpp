#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/Project_JMovingTurnPolicy.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "Project_JPlayerCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLocomotionCandidateContinuityTest, "ProjectJ.LocomotionContinuity.CandidatesAndLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJLocomotionCandidateContinuityTest::RunTest(const FString&)
{
	auto* Set = NewObject<UProject_JMotionMatchingAssetSet>();
	auto* Turn = NewObject<UPoseSearchDatabase>(); auto* Cycle = NewObject<UPoseSearchDatabase>();
	auto* Schema = NewObject<UPoseSearchSchema>(); Turn->Schema = Cycle->Schema = Schema;
	auto* Normalization = NewObject<UPoseSearchNormalizationSet>(); Normalization->Databases = {Turn, Cycle};
	Turn->NormalizationSet = Cycle->NormalizationSet = Normalization;
	Set->RunDatabases.TurnRedirect = Turn; Set->RunDatabases.Cycle = Cycle;
	FProject_JMotionMatchingSelectionContext Selection;
	Selection.bMovingTurn180 = true; Selection.PhaseFamily = EProject_JLocomotionPhaseFamily::Turn;
	TestEqual(TEXT("Approved turn searches its dynamic Cycle too"), Set->FindTurnCycleCompanion(Selection, Turn), Cycle);
	Selection.bMovingTurn180 = false;
	TestNull(TEXT("Ordinary motion never opens the Turn PSD"), Set->FindTurnCycleCompanion(Selection, Turn));
	Selection.bMovingTurn180 = true; Cycle->Schema = NewObject<UPoseSearchSchema>();
	TestNull(TEXT("Different feature spaces cannot compete"), Set->FindTurnCycleCompanion(Selection, Turn));
	Cycle->Schema = Schema; Selection.RotationMode = EProject_JLocomotionRotationMode::Strafe;
	TestNull(TEXT("Combat requires its explicitly selected family"), Set->FindTurnCycleCompanion(Selection, Turn));
	Selection.bUseGenericFamiliesForNonOrientToMovement = true;
	TestEqual(TEXT("Qualified Combat uses its own Cycle"), Set->FindTurnCycleCompanion(Selection, Turn), Cycle);
	Set->RunDatabases.GeneralTurn = NewObject<UPoseSearchDatabase>();
	Set->RunDatabases.GeneralTurn->Schema = Schema; Set->RunDatabases.GeneralTurn->NormalizationSet = Normalization;
	Normalization->Databases.Add(Set->RunDatabases.GeneralTurn);
	Selection.bMovingTurn180 = false; Selection.bGeneralTurnCandidates = true;
	Selection.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	TestEqual(TEXT("General candidates accompany dynamic Cycle"), Set->FindTurnCycleCompanion(Selection, Cycle), Set->RunDatabases.GeneralTurn.Get());
	Selection.GaitIntent = EProject_JLocomotionGaitIntent::Sprint;
	TestNull(TEXT("General Run cannot leak into Sprint"), Set->FindTurnCycleCompanion(Selection, Cycle));
	Selection.GaitIntent = EProject_JLocomotionGaitIntent::Run;
	Selection.bGeneralTurnCandidates = false; Selection.bMovingTurn180 = true; Selection.PhaseFamily = EProject_JLocomotionPhaseFamily::Turn;
	Set->bEnableTurnCycleCandidates = false;
	TestNull(TEXT("Asset-set rollback retains single PSD"), Set->FindTurnCycleCompanion(Selection, Turn));

	FProject_JMovingTurnPolicy Policy; FProject_JMovingTurnPolicy::FInput Input; Input.bEligible = true;
	Policy.Update(Input); Input.NowSeconds = 0.01; Input.TargetFacingYaw = Input.MoveYaw = 180;
	TestTrue(TEXT("Turn entry is qualified"), Policy.Update(Input));
	Input.NowSeconds = 0.4; Input.ActorYaw = Input.VelocityYaw = 180;
	TestFalse(TEXT("Alignment closes new Turn candidates"), Policy.Update(Input));
	TestTrue(TEXT("Same completed motion can continue"), Policy.AllowsCompletedTurnContinuation());
	Input.MoveYaw = Input.TargetFacingYaw = 90; Input.NowSeconds = 0.41; Policy.Update(Input);
	TestFalse(TEXT("New target revokes continuing permission"), Policy.AllowsCompletedTurnContinuation());
	Policy.Reset(); Input = {}; Input.bEligible = true; Policy.Update(Input);
	Input.TargetFacingYaw = Input.MoveYaw = 180; Input.NowSeconds = 0.01; Policy.Update(Input);
	Input.NowSeconds = 1.0; Policy.Update(Input);
	TestFalse(TEXT("Timeout does not masquerade as successful completion"), Policy.AllowsCompletedTurnContinuation());

	FProject_JCharacterAnimInstanceProxy P; FProject_JAnimThreadSafeData Data;
	Data.LocomotionContext.bIsMotionMatchingMoving = true;
	Data.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	P.QueueGameThreadData(Data, Cycle, true, true, false);
	P.ConsumeQueuedGameThreadData();
	TestEqual(TEXT("Consumed Cycle snapshot has one candidate"), P.GetThreadSafeCandidateCount(), 1);
	P.ClearMotionMatchingReselects();
	Data.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Turn;
	Data.MotionMatching.SelectionContext.bMovingTurn180 = true;
	P.QueueGameThreadData(Data, Turn, true, true, false, true, Cycle);
	TestEqual(TEXT("New GT publication does not mutate the consumed snapshot"), P.GetThreadSafeCandidateCount(), 1);
	TestFalse(TEXT("Consumed context remains coherent until graph boundary"), P.GetThreadSafeData().MotionMatching.SelectionContext.bMovingTurn180);
	P.ConsumeQueuedGameThreadData();
	TestEqual(TEXT("Graph boundary consumes paired candidates together"), P.GetThreadSafeCandidateCount(), 2);
	TestTrue(TEXT("Graph boundary consumes matching Turn ownership"), P.GetThreadSafeData().MotionMatching.SelectionContext.bMovingTurn180);
	const uint64 Request = P.PendingReselectRevision;
	TestTrue(TEXT("Candidate expansion requests one search"), Request > 0);
	P.QueueGameThreadData(Data, Turn, true, true, false, true, Cycle);
	TestEqual(TEXT("Stable candidates do not repeatedly request searches"), P.PendingReselectRevision, Request);
	P.ThreadSafeData = Data; P.CacheMotionMatchingPolicyState(); P.ClearMotionMatchingReselects();
	Data.MotionMatching.SelectionContext.bMovingTurn180 = false;
	Data.MotionMatching.SelectionContext.bAllowTurnContinuation = true;
	Data.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	P.QueueGameThreadData(Data, Cycle, true, true, false);
	P.ThreadSafeData = Data;
	TestFalse(TEXT("Aligned return preserves normal search budget"), P.bForceMotionMatchingReselect);
	TestEqual(TEXT("Aligned return keeps the engine's continuing pose"), P.ResolveDatabaseChangeInterruptMode(), EPoseSearchInterruptMode::DoNotInterrupt);
	Data.MotionMatching.SelectionContext.bAllowTurnContinuation = false;
	P.QueueGameThreadData(Data, Cycle, true, true, false);
	TestTrue(TEXT("Revoking continuation searches even when primary PSD is unchanged"), P.bForceMotionMatchingReselect);
	TestEqual(TEXT("Cancellation retires the old Turn query pose"), P.ResolveReselectInterruptMode(), EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose);
	P.QueueGameThreadData(Data, nullptr, false, false, false);
	TestFalse(TEXT("Disabled owner clears continuation cancellation"), P.bRetireTurnContinuingPose);
	P.ClearMotionMatchingReselects();
	Data.MotionMatching.SelectionContext.bGeneralTurnCandidates = true;
	P.QueueGameThreadData(Data, Cycle, true, true, false, true, Turn);
	TestTrue(TEXT("General expansion requests a search"), P.bForceMotionMatchingReselect);
	TestEqual(TEXT("Candidate expansion preserves continuing Cycle"), P.ResolveReselectInterruptMode(), EPoseSearchInterruptMode::ForceInterrupt);
	P.ClearMotionMatchingReselects();
	Data.MotionMatching.SelectionContext.bGeneralTurnCandidates = false;
	Data.MotionMatching.SelectionContext.bAllowGeneralTurnContinuation = true;
	P.QueueGameThreadData(Data, Cycle, true, true, false);
	TestFalse(TEXT("Settled general closure does not force a search"), P.bForceMotionMatchingReselect);
	Data.MotionMatching.SelectionContext.bAllowGeneralTurnContinuation = false;
	P.QueueGameThreadData(Data, Cycle, true, true, false);
	TestEqual(TEXT("General permission cancellation retires query pose"), P.ResolveReselectInterruptMode(), EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose);
	P.ClearMotionMatchingReselects();
	Data.MotionMatching.SelectionContext.bGeneralTurnCandidates = true;
	P.QueueGameThreadData(Data, Cycle, true, true, false, true, Turn); P.ClearMotionMatchingReselects();
	Data.MotionMatching.SelectionContext.bGeneralTurnCandidates = false;
	P.QueueGameThreadData(Data, Cycle, true, true, false);
	TestEqual(TEXT("Cancelled open general window also retires query pose"), P.ResolveReselectInterruptMode(), EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose);
	P.ClearMotionMatchingReselects();
	Data.MotionMatching.SelectionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	Data.MotionMatching.SelectionContext.bGeneralTurnCandidates = true;
	P.QueueGameThreadData(Data, Cycle, true, true, false, true, Turn); P.ClearMotionMatchingReselects();
	P.LatestPostSelection.CaptureFrame = GFrameCounter;
	P.LatestPostSelection.SelectedDatabase = Cycle->GetFName();
	P.LatestPostSelection.CachedNodeWeight = 1;
	Data.MotionMatching.SelectionContext.bGeneralTurnCandidates = false;
	P.QueueGameThreadData(Data, Cycle, true, true, false);
	TestFalse(TEXT("A fresh Cycle winner is not forced to reselect when its Turn companion closes"), P.bForceMotionMatchingReselect);
	TestFalse(TEXT("Cycle handoff cannot invalidate the already winning Cycle pose"), P.bRetireTurnContinuingPose);
	P.QueueGameThreadData(Data, Cycle, true, true, true);
	TestTrue(TEXT("Cycle handoff must still honor an explicit fresh search request"), P.bForceMotionMatchingReselect);
	TestFalse(TEXT("Explicit search after Cycle handoff preserves the continuing pose"), P.bRetireTurnContinuingPose);
	FProject_JLocomotionContinuityTraceSampler Sampler;
	FProject_JLocomotionContinuityTraceKey Key;
	bool bEdge = false;
	TestFalse(TEXT("Continuity logging defaults to no samples"), Sampler.ShouldRecord(Key, 0, 0, bEdge));
	TestTrue(TEXT("Enabling logs captures the initial state"), Sampler.ShouldRecord(Key, 1, 1, bEdge));
	TestFalse(TEXT("Stable state respects the 5Hz logging limit"), Sampler.ShouldRecord(Key, 1.1, 1, bEdge));
	Key.Candidates = 2;
	TestTrue(TEXT("Candidate expansion is recorded immediately"), Sampler.ShouldRecord(Key, 1.11, 1, bEdge));
	TestTrue(TEXT("Detailed mode captures a 10Hz periodic sample"), Sampler.ShouldRecord(Key, 1.22, 2, bEdge));
	TestTrue(TEXT("A new PIE clock resets sampling"), Sampler.ShouldRecord(Key, 0, 1, bEdge));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLocomotionSteeringOwnershipTest, "ProjectJ.LocomotionContinuity.SteeringOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJLocomotionSteeringOwnershipTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* Player = World->SpawnActor<AProject_JCombatFacingFixture>();
	if (!TestNotNull(TEXT("Concrete test player"), Player)) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
	auto* Controller = World->SpawnActor<APlayerController>(); Controller->SetAsLocalPlayerController();
	auto* State = World->SpawnActor<APlayerState>(); State->SetOwner(Controller); Controller->SetPlayerState(State);
	Controller->Possess(Player);
	auto* Profile = NewObject<UProject_JCharacterAnimProfile>(); Profile->LocomotionProfile = NewObject<UProject_JLocomotionProfile>();
	Player->CharacterAnimProfile = Profile;
	Player->GetCharacterMovement()->SetUpdatedComponent(Player->GetCapsuleComponent());
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	auto* Anim = NewObject<UProject_JCharacterAnimInstance>(Player->GetMesh());
	Player->GetMesh()->AnimScriptInstance = Anim;
	Anim->OwningCharacter = Player; Anim->OwningPlayerCharacter = Player;
	TestTrue(TEXT("Fixture has a grounded movement component"), Player->GetCharacterMovement()->IsMovingOnGround());
	TestTrue(TEXT("Fixture has a player controller"), Player->IsPlayerControlled());
	FProject_JAnimThreadSafeData Data; Data.bIsLocallyControlled = true; Data.Input.bHasMoveInput = true;
	Data.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	Data.Movement.bHasTrajectory = Data.Movement.bTrajectoryPredictionUsable = true;
	Data.Movement.TrajectoryAgeSeconds = 0;
	for (float Time : {0.0f, 0.4f, 0.6f})
	{
		auto& S = Data.Movement.Trajectory.Samples.AddDefaulted_GetRef(); S.TimeInSeconds = Time;
		S.Facing = FRotator(0, Time < 0.5 ? 170 : -170, 0).Quaternion();
	}
	Anim->UpdateLocomotionSteeringData(Data);
	TestTrue(TEXT("Grounded local continuous movement has a visual owner"), Data.bLocomotionSteeringEnabled);
	TestEqual(TEXT("Trace captures the exact successful gate"), Data.LocomotionSteeringGate, EProject_JLocomotionSteeringGate::Enabled);
	TestTrue(TEXT("Future-facing interpolation follows shortest path across yaw wrap"),
		FMath::Abs(Data.LocomotionSteeringTarget.Rotator().Yaw) > 179);
	Data.Movement.GroundSpeed = 350; Data.Movement.Velocity = FVector(350, 0, 0);
	Data.LocomotionContext.RequestedMoveWorldDirection = FRotator(0, 45, 0).Vector();
	Data.MotionMatching.SelectionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	Data.MotionMatching.SelectionContext.bUseSettledCycle = true;
	Anim->UpdateGeneralTurnData(Data);
	TestFalse(TEXT("Missing candidate data cannot open a partial general window"), Data.MotionMatching.SelectionContext.bGeneralTurnCandidates);
	TestTrue(TEXT("Missing candidate data retains original settled routing"), Data.MotionMatching.SelectionContext.bUseSettledCycle);
	TestEqual(TEXT("Data fallback is diagnosed explicitly"), Data.GeneralTurnReason, FName(TEXT("CandidateDataUnavailable")));
	for (auto Phase : {EProject_JLocomotionPhaseFamily::Start, EProject_JLocomotionPhaseFamily::Landing})
	{
		Data.LocomotionContext.PhaseFamily = Phase; Data.OneShotPresentation.bShouldOverrideMotionMatching = true;
		Data.OneShotPresentation.PresentationState = Phase == EProject_JLocomotionPhaseFamily::Start ?
			EProject_JStateControllerPresentationState::TransitionToLocomotion : EProject_JStateControllerPresentationState::TransitionToLand;
		Anim->UpdateLocomotionSteeringData(Data); TestTrue(TEXT("Moving Start/Land share the latest facing"), Data.bLocomotionSteeringEnabled);
	}
	const auto Reject = [&]() { Anim->UpdateLocomotionSteeringData(Data); TestFalse(TEXT("Another owner or invalid prediction disables steering"), Data.bLocomotionSteeringEnabled); };
	Data.Combat.bIsAttacking = true; Reject(); Data.Combat.bIsAttacking = false;
	Data.Input.bHasMoveInput = false; Reject(); Data.Input.bHasMoveInput = true;
	Data.bIsLocallyControlled = false; Reject(); Data.bIsLocallyControlled = true;
	Data.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Pivot; Reject();
	Data.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Landing;
	Data.Air.bIsInAir = true; Reject(); Data.Air.bIsInAir = false;
	Data.Movement.TrajectoryAgeSeconds = 0.2f; Reject();
	TestEqual(TEXT("Trace distinguishes stale prediction"), Data.LocomotionSteeringGate, EProject_JLocomotionSteeringGate::StaleTrajectory);
	Data.Movement.TrajectoryAgeSeconds = 0;
	Data.Movement.Trajectory.Samples.SetNum(1); Reject();
	TestEqual(TEXT("Trace distinguishes incomplete prediction"), Data.LocomotionSteeringGate, EProject_JLocomotionSteeringGate::IncompletePrediction);
	World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGeneralTurningLifetimeTest, "ProjectJ.GeneralTurning.Lifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGeneralTurningLifetimeTest::RunTest(const FString&)
{
	using Policy = FProject_JGeneralTurnPolicy;
	for (int32 Hz : {30, 60, 144})
	{
		Policy P; Policy::FInput I; Policy::FSettings S; I.bEligible = true; I.Speed = 350;
		double Entry = -1; bool bContinuation = false;
		for (int32 Frame = 0; Frame < Hz * 2; ++Frame)
		{
			I.Now = double(Frame) / Hz;
			I.FacingYaw = I.MoveYaw = FMath::Min(float(I.Now * 180), 90.0f);
			I.ActorYaw = I.VelocityYaw = I.MoveYaw - (I.Now >= .2 && I.Now < .5 ? 50 : 0);
			if (P.Update(I, S) && Entry < 0) Entry = I.Now;
			bContinuation |= P.AllowsContinuation();
		}
		TestTrue(TEXT("Sustained correction opens after confirmation at multiple frame rates"), Entry >= .24 && Entry <= .30);
		TestTrue(TEXT("Quiet finish permits continuation"), bContinuation);
		TestFalse(TEXT("Completed Turn cannot remain privileged indefinitely"), P.AllowsContinuation());
		I.bEligible = false; I.Now += .01; P.Update(I, S);
		TestFalse(TEXT("Ownership revokes permission"), P.AllowsContinuation());
	}
	Policy P; Policy::FInput I; Policy::FSettings S; I.bEligible = true; I.Speed = 350;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		I.Now = double(Frame) / 60; I.ActorYaw = I.VelocityYaw = I.FacingYaw = I.MoveYaw = Frame % 2 ? 2 : -2;
		TestFalse(TEXT("Small input noise does not accumulate into a turn"), P.Update(I, S));
	}
	P.Reset(); I = {}; I.bEligible = true; I.Speed = 350; P.Update(I, S);
	I.Now = .02; I.MoveYaw = I.FacingYaw = 45;
	TestFalse(TEXT("One path-lag sample cannot open Turn"), P.Update(I, S));
	I.Now = .04; P.Update(I, S); I.Now = .06;
	TestTrue(TEXT("Sustained path lag qualifies a forward turn"), P.Update(I, S));
	I.Now = .08; I.Speed = 20; I.VelocityYaw = I.ActorYaw = 45;
	TestTrue(TEXT("Entry speed does not abort the same turn while braking"), P.Update(I, S));
	I.Now = .10; I.bEligible = false;
	TestFalse(TEXT("Action handoff closes candidates immediately"), P.Update(I, S));
	TestFalse(TEXT("Action cancellation cannot grant completion"), P.AllowsContinuation());
	P.Reset(); I = {}; I.bEligible = true; I.Speed = 350; I.Mode = EProject_JLocomotionRotationMode::Strafe;
	I.MoveYaw = I.VelocityYaw = 180;
	TestFalse(TEXT("Combat backward travel cannot use forward turns"), P.Update(I, S));
	I.MoveYaw = I.VelocityYaw = 90;
	TestFalse(TEXT("Combat sideways travel cannot use forward turns"), P.Update(I, S));
	P.Reset(); I = {}; I.bEligible = true; I.Speed = 350; I.ActorYaw = I.MoveYaw = I.FacingYaw = I.VelocityYaw = 170;
	P.Update(I, S); I.Now = .04; I.MoveYaw = I.ActorYaw = I.FacingYaw = I.VelocityYaw = -175; P.Update(I, S);
	TestTrue(TEXT("Yaw wrap observes fifteen degrees, not 345"), FMath::Abs(P.GetRecentHeading() - 15) < .1);
	I.Now = .05; I.MoveYaw = -70;
	TestFalse(TEXT("Abrupt new target rejects general continuation"), P.Update(I, S));
	TestFalse(TEXT("Abrupt target cannot grant completion"), P.AllowsContinuation());
	P.Reset(); I = {}; I.bEligible = true; I.Speed = 350; I.MoveYaw = I.FacingYaw = 45; P.Update(I, S);
	I.Now = .2;
	TestFalse(TEXT("Missing samples reset the envelope rather than retaining a stale event"), P.Update(I, S));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGeneralTurningContinuousCurvesTest, "ProjectJ.GeneralTurning.ContinuousCurves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGeneralTurningContinuousCurvesTest::RunTest(const FString&)
{
	using Policy = FProject_JGeneralTurnPolicy;
	for (auto Mode : {EProject_JLocomotionRotationMode::OrientToMovement, EProject_JLocomotionRotationMode::Strafe})
	for (int32 Hz : {30, 60, 144})
	for (float Rate : {-90.f, -60.f, -20.f, 20.f, 60.f, 90.f})
	{
		Policy P; Policy::FInput I; Policy::FSettings S; I.bEligible = true; I.Speed = 350; I.Mode = Mode;
		bool bTurn = false, bDynamic = false;
		for (int32 Frame = 0; Frame < Hz * 3; ++Frame)
		{
			I.Now = double(Frame) / Hz;
			I.ActorYaw = I.VelocityYaw = I.FacingYaw = I.MoveYaw = FMath::UnwindDegrees(170 + Rate * float(I.Now));
			bTurn |= P.Update(I, S); bDynamic |= P.RequiresDynamicCycle();
		}
		TestFalse(TEXT("Continuous aligned curves do not become Turn events from accumulated angle"), bTurn);
		TestTrue(TEXT("Continuous curves keep the rich Cycle searchable"), bDynamic);
		for (int32 Frame = 1; Frame <= Hz; ++Frame) { I.Now += 1.0 / Hz; P.Update(I, S); }
		TestFalse(TEXT("Straight travel eventually returns to settled routing"), P.RequiresDynamicCycle());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStrafeForwardCurveTest, "ProjectJ.GeneralTurning.StrafeForwardCurve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJStrafeForwardCurveTest::RunTest(const FString&)
{
	using Policy = FProject_JGeneralTurnPolicy;
	for (int32 Hz : {30, 60, 144})
	for (float Rate : {-320.f, -240.f, -211.f, -180.f, 180.f, 211.f, 240.f, 320.f})
	{
		Policy P; Policy::FInput I; I.bEligible = true; I.Speed = 350;
		I.Mode = EProject_JLocomotionRotationMode::Strafe;
		bool bTurn = false, bDynamic = false;
		for (int32 Frame = 0; Frame < Hz * 3; ++Frame)
		{
			I.Now = double(Frame) / Hz;
			I.MoveYaw = I.FacingYaw = I.ActorYaw = FMath::UnwindDegrees(170 + Rate * float(I.Now));
			I.VelocityYaw = I.MoveYaw - FMath::Sign(Rate) * 14;
			bTurn |= P.Update(I, {}); bDynamic |= P.RequiresDynamicCycle();
		}
		TestFalse(TEXT("Forward Strafe with normal path lag stays in Cycle through yaw wrap"), bTurn);
		TestTrue(TEXT("Smooth forward Strafe retains rich Cycle"), bDynamic);
		TestFalse(TEXT("Smooth forward Strafe cannot inherit Turn continuation"), P.AllowsContinuation());
	}
	for (int32 Hz : {30, 60, 144})
	{
		Policy P; Policy::FInput I; I.bEligible = true; I.Speed = 350;
		I.Mode = EProject_JLocomotionRotationMode::Strafe;
		bool bFastTurn = false, bContinued = false, bLateTurn = false;
		for (int32 Frame = 0; Frame < Hz * 2; ++Frame)
		{
			I.Now = double(Frame) / Hz;
			I.MoveYaw = I.FacingYaw = I.ActorYaw = FMath::UnwindDegrees(
				float(I.Now < .5 ? I.Now * 300 : 150 + (I.Now - .5) * 180));
			I.VelocityYaw = I.MoveYaw - (I.Now >= .1 && I.Now < .5 ? 50 : 14);
			const bool bTurn = P.Update(I, {});
			if (I.Now < .5) bFastTurn |= bTurn;
			if (I.Now > 1) bLateTurn |= bTurn;
			bContinued |= P.AllowsContinuation();
		}
		TestTrue(TEXT("Fast forward Strafe keeps Turn eligibility"), bFastTurn);
		TestTrue(TEXT("Fast-to-curve transition respects completion grace"), bContinued);
		TestFalse(TEXT("Fast Turn finishes into a curve without requiring the mouse to stop"), bLateTurn);
		TestFalse(TEXT("Continuing permission expires during the curve"), P.AllowsContinuation());
		TestTrue(TEXT("Curve remains searchable after Turn completion"), P.RequiresDynamicCycle());
		// A genuine large path error still qualifies below the smooth-curve rate.
		P.Reset();
		bool bAlignmentTurn = false;
		for (int32 Frame = 0; Frame < Hz / 2; ++Frame)
		{
			I.Now = double(Frame) / Hz; I.MoveYaw = I.FacingYaw = I.ActorYaw = Frame * 180.f / Hz;
			I.VelocityYaw = I.MoveYaw - 50;
			bAlignmentTurn |= P.Update(I, {});
		}
		TestTrue(TEXT("Real Strafe path misalignment still opens Turn"), bAlignmentTurn);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGeneralTurningCorrectionTest, "ProjectJ.GeneralTurning.CorrectionAdmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGeneralTurningCorrectionTest::RunTest(const FString&)
{
	using Policy = FProject_JGeneralTurnPolicy;
	for (auto Mode : {EProject_JLocomotionRotationMode::OrientToMovement, EProject_JLocomotionRotationMode::Strafe})
	for (int32 Hz : {30, 60, 144})
	{
		Policy P; Policy::FInput I; I.bEligible = true; I.Speed = 350; I.Mode = Mode;
		bool bSpikeTurn = false, bCorrectionTurn = false, bHysteresis = false, bLateTurn = false;
		for (int32 Frame = 0; Frame < Hz * 2; ++Frame)
		{
			I.Now = double(Frame) / Hz;
			I.MoveYaw = I.FacingYaw = I.ActorYaw = FMath::UnwindDegrees(180 * float(I.Now));
			const float Error = I.Now < .5 ? (Frame % (Hz / 4) == 0 ? 50.f : 14.f) :
				I.Now < .8 ? 50.f : I.Now < 1 ? 35.f : 10.f;
			I.VelocityYaw = I.MoveYaw - Error;
			const bool bTurn = P.Update(I, {});
			if (I.Now < .5) bSpikeTurn |= bTurn;
			if (I.Now >= .6 && I.Now < .8) bCorrectionTurn |= bTurn;
			if (I.Now >= .85 && I.Now < 1) bHysteresis |= bTurn;
			if (I.Now > 1.5) bLateTurn |= bTurn;
		}
		TestFalse(TEXT("Repeated single-frame path-error spikes do not admit Turn"), bSpikeTurn);
		TestTrue(TEXT("Confirmed correction admits Turn in both modes"), bCorrectionTurn);
		TestTrue(TEXT("Below-entry error does not flicker an active correction off"), bHysteresis);
		TestFalse(TEXT("Recovered continuous curve closes Turn without stopping"), bLateTurn);
		TestFalse(TEXT("Recovered curve cannot retain continuing permission indefinitely"), P.AllowsContinuation());
		P.Reset(); bool bAlternatingTurn = false;
		for (int32 Frame = 0; Frame < Hz; ++Frame)
		{
			I.Now = double(Frame) / Hz; I.MoveYaw = I.FacingYaw = I.ActorYaw = Frame * 180.f / Hz;
			I.VelocityYaw = I.MoveYaw + (Frame % 2 ? 50 : -50);
			bAlternatingTurn |= P.Update(I, {});
		}
		TestFalse(TEXT("Alternating correction directions cannot accumulate confirmation"), bAlternatingTurn);
		P.Reset(); int32 Entries = 0;
		for (int32 Frame = 0; Frame < Hz * 4; ++Frame)
		{
			I.Now = double(Frame) / Hz; I.MoveYaw = I.FacingYaw = I.ActorYaw = FMath::UnwindDegrees(float(I.Now * 180));
			I.VelocityYaw = I.MoveYaw - 50; P.Update(I, {});
			Entries += FCString::Strcmp(P.GetReason(), TEXT("Enter")) == 0;
		}
		TestEqual(TEXT("Unrecovered same-direction event cannot repeatedly reopen after timeout"), Entries, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGeneralTurningModeSemanticsTest, "ProjectJ.GeneralTurning.ModeSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGeneralTurningModeSemanticsTest::RunTest(const FString&)
{
	using Policy = FProject_JGeneralTurnPolicy;
	for (int32 Case = 0; Case < 3; ++Case)
	{
		Policy P; Policy::FInput I; Policy::FSettings S; I.bEligible = true; I.Speed = 350;
		I.Mode = EProject_JLocomotionRotationMode::Strafe;
		bool bTurn = false, bDynamic = false;
		for (int32 Frame = 0; Frame < 90; ++Frame)
		{
			I.Now = double(Frame) / 60;
			I.FacingYaw = I.ActorYaw = Case == 1 ? 0 : Frame * 3.0f;
			I.MoveYaw = I.VelocityYaw = Case == 0 ? 0 : Case == 1 ? FMath::Min(Frame * 3.0f, 40.f) : I.FacingYaw + 90;
			bTurn |= P.Update(I, S); bDynamic |= P.RequiresDynamicCycle();
		}
		TestFalse(TEXT("Camera-only, directional-input-only, and lateral Strafe cannot borrow forward Turn"), bTurn);
		TestTrue(TEXT("Those Strafe changes retain directional Dynamic Cycle"), bDynamic);
	}
	// OTM ignores camera swivels when world travel stays straight.
	Policy P; Policy::FInput I; Policy::FSettings S; I.bEligible = true; I.Speed = 350;
	P.Update(I, S); I.Now = .02; I.MoveYaw = I.FacingYaw = 60;
	TestFalse(TEXT("OTM first misalignment sample is pending"), P.Update(I, S));
	I.Now = .04; P.Update(I, S); I.Now = .06;
	TestTrue(TEXT("OTM confirmed travel/body misalignment enters promptly"), P.Update(I, S));
	I.Now += .02; I.Mode = EProject_JLocomotionRotationMode::Strafe;
	TestFalse(TEXT("Mode handoff revokes the previous turn window"), P.Update(I, S));
	TestFalse(TEXT("Mode handoff cannot inherit Turn continuation"), P.AllowsContinuation());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGeneralTurningCommittedAnglesTest, "ProjectJ.GeneralTurning.CommittedAngles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGeneralTurningCommittedAnglesTest::RunTest(const FString&)
{
	using Policy = FProject_JGeneralTurnPolicy;
	for (auto Mode : {EProject_JLocomotionRotationMode::OrientToMovement, EProject_JLocomotionRotationMode::Strafe})
	for (int32 Hz : {30, 60, 144})
	for (float Angle : {-135.f, -90.f, -45.f, 45.f, 90.f, 135.f})
	{
		Policy P; Policy::FInput I; Policy::FSettings S; I.bEligible = true; I.Speed = 350; I.Mode = Mode;
		bool bEntered = false, bContinued = false;
		for (int32 Frame = 0; Frame < Hz * 3; ++Frame)
		{
			I.Now = double(Frame) / Hz;
			const float Yaw = FMath::Sign(Angle) * FMath::Min(FMath::Max(float(I.Now - .2) * 240, 0.f), FMath::Abs(Angle));
			I.ActorYaw = I.VelocityYaw = I.FacingYaw = I.MoveYaw = Yaw;
			bEntered |= P.Update(I, S); bContinued |= P.AllowsContinuation();
		}
		TestFalse(TEXT("Fully aligned 45/90/135 turns stay in Cycle in both modes and directions"), bEntered);
		TestFalse(TEXT("Aligned travel cannot grant Turn privilege"), bContinued);
		TestFalse(TEXT("Completion handoff expires while straight travel continues"), P.AllowsContinuation());
		TestFalse(TEXT("Straight travel does not repeatedly reopen the turn"), P.Update(I, S));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGeneralTurningSmallBurstsTest, "ProjectJ.GeneralTurning.SmallBursts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGeneralTurningSmallBurstsTest::RunTest(const FString&)
{
	using Policy = FProject_JGeneralTurnPolicy;
	for (auto Mode : {EProject_JLocomotionRotationMode::OrientToMovement, EProject_JLocomotionRotationMode::Strafe})
	for (int32 Hz : {30, 60, 144})
	for (float Angle : {20.f, 30.f, 45.f})
	{
		Policy P; Policy::FInput I; Policy::FSettings S; I.bEligible = true; I.Speed = 500; I.Mode = Mode;
		bool bEntered = false;
		for (int32 Frame = 0; Frame < Hz * 4; ++Frame)
		{
			I.Now = double(Frame) / Hz;
			const float LocalTime = FMath::Fmod(float(I.Now), 1.f);
			const float Direction = int32(I.Now) % 2 ? -1.f : 1.f;
			const float Burst = LocalTime < .45f ? FMath::Min(LocalTime * 240, Angle) :
				FMath::Max(Angle - (LocalTime - .45f) * 240, 0.f);
			I.MoveYaw = I.FacingYaw = FMath::UnwindDegrees(175 + Direction * Burst);
			I.ActorYaw = I.FacingYaw;
			// Match the user's small 9..14-degree path lag without inventing a
			// direction-change event from normal mouse-speed peaks.
			I.VelocityYaw = I.MoveYaw - Direction * 12;
			bEntered |= P.Update(I, S);
		}
		TestFalse(TEXT("Repeated small fast mouse bursts with normal path lag do not open Turn"), bEntered);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGeneralTurningCycleHandoffTest, "ProjectJ.GeneralTurning.CycleHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGeneralTurningCycleHandoffTest::RunTest(const FString&)
{
	using Policy = FProject_JGeneralTurnPolicy;
	for (auto Mode : {EProject_JLocomotionRotationMode::OrientToMovement, EProject_JLocomotionRotationMode::Strafe})
	{
		Policy P; Policy::FInput I; Policy::FSettings S; I.bEligible = true; I.Speed = 350; I.Mode = Mode;
		for (int32 Frame = 0; Frame <= 20; ++Frame)
		{
			I.Now = double(Frame) / 60; I.ActorYaw = I.MoveYaw = I.FacingYaw = Frame * 4.f;
			I.VelocityYaw = I.MoveYaw - 50;
			P.Update(I, S);
		}
		I.Now += .01; I.SelectionFrame = 1; I.bSelectedCycle = true;
		TestTrue(TEXT("Initial Cycle competition does not close an unused Turn window"), P.Update(I, S));
		I.Now += .01; I.SelectionFrame = 2; I.bSelectedCycle = false; I.bSelectedGeneralTurn = true;
		TestTrue(TEXT("A fresh relevant GeneralTurn result marks use of this event"), P.Update(I, S));
		I.Now += .01; I.bSelectedGeneralTurn = false; I.bSelectedCycle = true;
		TestTrue(TEXT("A stale cached Cycle result cannot close the event"), P.Update(I, S));
		I.Now += .01; I.SelectionFrame = 3;
		TestFalse(TEXT("Fresh Cycle after actual Turn use closes the extra pool"), P.Update(I, S));
		TestTrue(TEXT("Cycle winner latches same-event re-entry suppression"), P.IsCycleHandoffHeld());
		TestFalse(TEXT("Cycle winner does not need continuing GeneralTurn permission"), P.AllowsContinuation());
		for (int32 Frame = 1; Frame <= 12; ++Frame)
		{
			I.Now += 1.0 / 60; I.SelectionFrame++;
			I.ActorYaw = I.MoveYaw = I.FacingYaw = 80 + Frame * 2.f;
			I.VelocityYaw = I.MoveYaw - 50;
			TestFalse(TEXT("Same event cannot reopen Turn from retained recent heading"), P.Update(I, S));
		}
		bool bRearmed = false;
		for (int32 Frame = 1; Frame <= 25; ++Frame)
		{
			I.Now += 1.0 / 60; I.SelectionFrame++;
			I.ActorYaw = I.MoveYaw = I.FacingYaw = 104 - Frame * 4.f;
			I.VelocityYaw = I.MoveYaw + 50;
			bRearmed |= P.Update(I, S);
		}
		TestTrue(TEXT("A substantial reversed target can re-arm without requiring a stop"), bRearmed);
		I.bEligible = false; I.Now += .01; P.Update(I, S);
		TestFalse(TEXT("Owner cancellation resets handoff suppression"), P.IsCycleHandoffHeld());
	}
	return true;
}
#endif
