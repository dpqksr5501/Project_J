#if WITH_DEV_AUTOMATION_TESTS
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
#endif
