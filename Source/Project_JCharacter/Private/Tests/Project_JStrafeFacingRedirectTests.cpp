#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "AIController.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Components/Project_JCombatStateComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MotionTrajectoryLibrary.h"
#include <limits>

namespace
{
struct FFacingWorld
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AProject_JCombatFacingFixture* Player = nullptr;
	AAIController* Controller = nullptr;
	UProject_JCombatAnimProfile* Combat = nullptr;
	FFacingWorld()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Player = World->SpawnActor<AProject_JCombatFacingFixture>();
		Controller = World->SpawnActor<AAIController>(); Controller->Possess(Player);
		auto* Profile = NewObject<UProject_JCharacterAnimProfile>();
		Combat = NewObject<UProject_JCombatAnimProfile>(); Profile->CombatAnimProfile = Combat;
		Player->CharacterAnimProfile = Profile;
		auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Player); ASC->RegisterComponent();
		ASC->InitAbilityActorInfo(Player, Player);
		Player->FindComponentByClass<UProject_JCombatStateComponent>()->BindToAbilitySystem(ASC);
		ASC->AddProjectJLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode, false);
		Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Player->GetCharacterMovement()->Velocity = FVector(-300.0f, 0.0f, 0.0f);
		Controller->SetControlRotation(FRotator::ZeroRotator);
		Player->SetActorRotation(FRotator(0.0f, 180.0f, 0.0f));
		Player->UpdateTestMovementPolicy(); Player->ApplyTestRotationMode(true);
	}
	~FFacingWorld() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStrafeFacingTrajectoryTest, "ProjectJ.StrafeFacingRedirect.TrajectoryPrediction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJStrafeFacingTrajectoryTest::RunTest(const FString&)
{
	FFacingWorld W;
	auto* C = W.Player->GetMotionMatchingTrajectoryComponent();
	auto* Movement = W.Player->GetCharacterMovement();
	for (const int32 FPS : {30, 60, 120})
	{
		W.Player->SetActorRotation(FRotator(0.0f, 180.0f, 0.0f));
		C->GenerateTrajectory(*W.Player, 1.0f / FPS);
		const FQuat Present = C->CharacterTrajectoryData.Facing;
		bool bFutureTurnFound = false;
		for (const auto& Sample : C->Trajectory.Samples)
		{
			if (Sample.TimeInSeconds <= 0.0f) { continue; }
			const double ActorYaw = FMath::FixedTurn(180.0, 0.0, 360.0 * Sample.TimeInSeconds);
			const FQuat Expected = FRotator(0.0, FMath::FindDeltaAngleDegrees(180.0, ActorYaw), 0.0).Quaternion() * Present;
			TestTrue(TEXT("Fixed-camera future facing predicts the actual bounded CMC catch-up"), Sample.Facing.Equals(Expected, 0.001f));
			bFutureTurnFound |= !Sample.Facing.Equals(Present, 0.001f);
		}
		TestTrue(TEXT("Holding S with no camera/input change still predicts a turn"), bFutureTurnFound);
		for (int32 Frame = 0; Frame < FPS / 2; ++Frame) { Movement->PhysicsRotation(1.0f / FPS); }
		TestTrue(TEXT("Physical CMC converges to the same predicted half-second target"),
			FMath::Abs(FMath::FindDeltaAngleDegrees(W.Player->GetActorRotation().Yaw, 0.0)) < 0.01);
	}

	W.Player->SetActorRotation(FRotator(0.0f, 170.0f, 0.0f));
	W.Controller->SetControlRotation(FRotator(0.0f, -170.0f, 0.0f));
	C->CharacterTrajectoryData.UpdateDataFromCharacter(1.0f / 60.0f, W.Player);
	C->CharacterTrajectoryData.ControllerYawRateClamped = 0.0f;
	FMotionTrajectoryLibrary::UpdatePrediction_SimulateCharacterMovement(C->Trajectory, C->CharacterTrajectoryData, C->SamplingData);
	const FTransformTrajectory Before = C->Trajectory;
	C->PredictCombatStrafeFacing(*W.Player);
	for (int32 Index = 0; Index < C->Trajectory.Samples.Num(); ++Index)
	{
		const auto& S = C->Trajectory.Samples[Index];
		TestTrue(TEXT("Facing prediction never bends translation"), S.Position.Equals(Before.Samples[Index].Position));
		if (S.TimeInSeconds <= 0.0f)
		{
			TestTrue(TEXT("Past/present facing is preserved"), S.Facing.Equals(Before.Samples[Index].Facing));
		}
		else
		{
			const float Delta = FMath::FindDeltaAngleDegrees(Before.Samples[C->SamplingData.NumHistorySamples].Facing.Rotator().Yaw, S.Facing.Rotator().Yaw);
			TestTrue(TEXT("170 to -170 uses the short turn"), Delta >= -0.01f && Delta <= 20.01f);
		}
	}
	const FTransformTrajectory Retained = C->Trajectory;
	C->LastGeneratedWorldTimeSeconds = 0.0;
	C->NotifyRotationModeChanged();
	TestEqual(TEXT("Policy change invalidates freshness until regeneration"), C->GetTrajectoryAgeSeconds(), -1.0f);
	TestEqual(TEXT("Policy change retains history sample count"), C->Trajectory.Samples.Num(), Retained.Samples.Num());
	for (int32 Index = 0; Index < Retained.Samples.Num(); ++Index)
	{
		TestTrue(TEXT("Tab policy notification does not flatten entry history"),
			C->Trajectory.Samples[Index].GetTransform().Equals(Retained.Samples[Index].GetTransform()));
	}
	W.Player->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	C->GenerateTrajectory(*W.Player, 1.0f / 60.0f);
	for (int32 Index = 0; Index < Retained.Samples.Num(); ++Index)
	{
		if (Retained.Samples[Index].TimeInSeconds >= 0.0f) { continue; }
		TestTrue(TEXT("Same-frame policy rebuild does not advance history twice"),
			C->Trajectory.Samples[Index].GetTransform().Equals(Retained.Samples[Index].GetTransform()));
	}
	TestTrue(TEXT("Same-frame policy rebuild still updates the current/future query"),
		!C->Trajectory.Samples[C->SamplingData.NumHistorySamples].Position.Equals(Retained.Samples[C->SamplingData.NumHistorySamples].Position));
	const auto CheckUnchanged = [&]()
	{
		C->Trajectory = Before; C->PredictCombatStrafeFacing(*W.Player);
		for (int32 I = 0; I < Before.Samples.Num(); ++I)
		{
			TestTrue(TEXT("Non-facing owner preserves the engine trajectory"), C->Trajectory.Samples[I].Facing.Equals(Before.Samples[I].Facing));
		}
	};
	W.Combat->bEnableStrafeFacingRedirect = false; CheckUnchanged(); W.Combat->bEnableStrafeFacingRedirect = true;
	Movement->SetMovementMode(MOVE_Falling); CheckUnchanged(); Movement->SetMovementMode(MOVE_Walking);
	W.Player->bIsDodging = true; CheckUnchanged(); W.Player->bIsDodging = false;
	W.Player->bIsAttacking = true; CheckUnchanged(); W.Player->bIsAttacking = false;
	W.Player->bIsHitReacting = true; CheckUnchanged(); W.Player->bIsHitReacting = false;
	Movement->bUseControllerDesiredRotation = false; CheckUnchanged(); Movement->bUseControllerDesiredRotation = true;
	W.Controller->UnPossess(); CheckUnchanged();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStrafeConsumedInputTest, "ProjectJ.StrafeFacingRedirect.ConsumedInputAtBrakingMinimum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJStrafeConsumedInputTest::RunTest(const FString&)
{
	FFacingWorld W;
	auto* State = W.Player->GetLocomotionAnimStateComponent();
	auto* Movement = W.Player->GetCharacterMovement();
	State->RefreshCachedReferences();
	for (const float Speed : {8.f, 0.f, 9.99f})
	{
		State->SetMoveInput(FVector2D(0, 1));
		W.Player->AddMovementInput(FVector::ForwardVector);
		W.Player->ConsumeMovementInputVector();
		Movement->Velocity = FVector(Speed, 0, 0);
		TestTrue(TEXT("Repro uses consumed pending input"), W.Player->GetPendingMovementInputVector().IsNearlyZero());
		TestTrue(TEXT("Raw local input remains held after consumption"), State->HasHeldLocalMoveInput());
		W.Player->ApplyTestRotationMode(true);
		TestTrue(TEXT("CMC facing owner survives the sub-10 braking minimum"), Movement->bUseControllerDesiredRotation);
		for (const float Yaw : {-170.f, 170.f})
		{
			W.Player->SetActorRotation(FRotator(0, Yaw, 0));
			Movement->PhysicsRotation(1.f / 120);
			const float Delta = FMath::FindDeltaAngleDegrees(Yaw, W.Player->GetActorRotation().Yaw);
			TestTrue(TEXT("Physical yaw still catches up at the configured CMC rate"),
				FMath::IsNearlyEqual(FMath::Abs(Delta), 3.f, .01f) && Delta * Yaw < 0);
		}
		State->ClearMoveInput();
		W.Player->ApplyTestRotationMode(true);
		TestFalse(TEXT("Release clears raw input without waiting for animation update"), State->HasHeldLocalMoveInput());
		TestFalse(TEXT("Released low-speed character gives facing back to idle TIP"), Movement->bUseControllerDesiredRotation);
	}
	State->SetMoveInput(FVector2D(0, .01f)); W.Player->ApplyTestRotationMode(true);
	TestFalse(TEXT("Input below the dead zone cannot claim idle rotation"), Movement->bUseControllerDesiredRotation);
	State->SetMoveInput(FVector2D(0, 1));
	W.Combat->bUseCombatRotationMode = false; W.Player->ApplyTestRotationMode(true);
	TestFalse(TEXT("Profile opt-out still preempts held input"), Movement->bUseControllerDesiredRotation);
	W.Combat->bUseCombatRotationMode = true;
	Movement->SetMovementMode(MOVE_Falling); W.Player->ApplyTestRotationMode(true);
	TestFalse(TEXT("Air retains its separate rotation owner"), Movement->bUseControllerDesiredRotation);
	Movement->SetMovementMode(MOVE_Walking);
	W.Player->ApplyTestRotationMode(false);
	TestFalse(TEXT("OTM does not claim controller facing"), Movement->bUseControllerDesiredRotation);
	TestTrue(TEXT("OTM retains orient-to-movement"), Movement->bOrientRotationToMovement);
	W.Controller->UnPossess(); W.Player->ApplyTestRotationMode(true);
	TestFalse(TEXT("An unpossessed character cannot use cached local input"), State->HasHeldLocalMoveInput());
	TestFalse(TEXT("No local input is applied to the non-local rotation owner"), Movement->bUseControllerDesiredRotation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStrafeFacingSelectionTest, "ProjectJ.StrafeFacingRedirect.SelectionAndLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJStrafeFacingSelectionTest::RunTest(const FString&)
{
	FFacingWorld W;
	auto* S = W.Player->GetLocomotionAnimStateComponent(); S->RefreshCachedReferences();
	auto* Set = NewObject<UProject_JMotionMatchingAssetSet>(); W.Combat->CombatStrafeMotionMatchingAssetSet = Set;
	Set->IdlePoseSearchDatabase = NewObject<UPoseSearchDatabase>(); Set->DefaultPoseSearchDatabase = Set->IdlePoseSearchDatabase;
	Set->RunDatabases.Cycle = NewObject<UPoseSearchDatabase>(); Set->RunDatabases.SettledCycle = NewObject<UPoseSearchDatabase>();
	S->bUsingLocalInputState = true; S->AuthoritativeContext.bCombatMode = true;
	S->AuthoritativeContext.RotationMode = EProject_JLocomotionRotationMode::Strafe;
	S->AuthoritativeContext.GaitIntent = EProject_JLocomotionGaitIntent::Run;
	S->SetMoveInput(FVector2D(0.0f, -1.0f)); S->bHasMoveInput = true; S->GroundSpeed = 300.0f;
	S->KinematicContext.bHasMoveInput = true; S->KinematicContext.GroundSpeed = 300.0f;
	S->GroundMotionMode = EProject_JGroundMotionMode::Locomotion;
	S->KinematicContext.HorizontalVelocity = FVector(-300, 0, 0);
	S->KinematicContext.MoveWorldDirection = FVector(-1, 0, 0);
	// Movement faces the actor already, while the unchanged camera is opposite.
	S->KinematicContext.DesiredFacingDeltaYaw = 0.0f;
	FProject_JDerivedLocomotionContext D; D.bIsMoving = true; D.bIsMotionMatchingMoving = true;
	S->DerivedLocomotionContext = D; S->DerivedLocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	S->UpdateMotionMatchingSelectionState(*W.Player);
	const auto RefreshTurn = [&]()
	{
		D.bIsMovingTurn180 = S->UpdateMovingTurnPolicy(S->AuthoritativeContext, S->KinematicContext, D);
	};
	RefreshTurn();
	TestEqual(TEXT("Held S / fixed camera catch-up cannot open a forward Turn PSD"), S->ResolvePhaseFamily(D), EProject_JLocomotionPhaseFamily::Cycle);
	W.Controller->SetControlRotation(FRotator(0, 180, 0)); RefreshTurn(); // Coherent forward run primes the owner.
	W.Controller->SetControlRotation(FRotator::ZeroRotator);
	S->KinematicContext.MoveWorldDirection = FVector(1, 0, 0); RefreshTurn();
	TestEqual(TEXT("Camera and forward path reversal selects a moving Turn"), S->ResolvePhaseFamily(D), EProject_JLocomotionPhaseFamily::Turn);
	S->DerivedLocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Turn;
	S->DerivedLocomotionContext.bIsMovingTurn180 = D.bIsMovingTurn180;
	S->UpdateMotionMatchingSelectionState(*W.Player);
	TestTrue(TEXT("Facing semantic edge forces one search without an input edge"), S->bForceMotionMatchingReselect);
	S->UpdateMotionMatchingSelectionState(*W.Player);
	TestFalse(TEXT("Sustained redirect does not force a search every frame"), S->bForceMotionMatchingReselect);
	S->PreviousDerivedPhaseFamily = EProject_JLocomotionPhaseFamily::Turn;
	W.Player->SetActorRotation(FRotator(0.0f, 15.0f, 0.0f));
	RefreshTurn();
	TestEqual(TEXT("Exit hysteresis holds the turn below the entry threshold"), S->ResolvePhaseFamily(D), EProject_JLocomotionPhaseFamily::Turn);
	S->SetMoveInput(FVector2D(0, 1));
	for (float Speed : {179.0f, 80.0f, 0.0f, 100.0f})
	{
		W.Player->ConsumeMovementInputVector();
		W.Player->GetCharacterMovement()->Velocity = FVector(Speed, 0, 0);
		W.Player->ApplyTestRotationMode(true);
		S->KinematicContext.GroundSpeed = Speed;
		S->KinematicContext.HorizontalVelocity = FVector(Speed, 0, 0);
		D.bIsMoving = Speed > 10;
		RefreshTurn();
		TestTrue(TEXT("Real producer keeps the same Turn during reversal braking"), D.bIsMovingTurn180);
		TestEqual(TEXT("Real phase resolver retains its Turn PSD at low speed"), S->ResolvePhaseFamily(D), EProject_JLocomotionPhaseFamily::Turn);
	}
	S->KinematicContext.GroundSpeed = 300;
	S->KinematicContext.HorizontalVelocity = FVector(-300, 0, 0);
	D.bIsMoving = true;
	W.Player->SetActorRotation(FRotator(0.0f, 4.0f, 0.0f));
	RefreshTurn();
	TestEqual(TEXT("Facing alone cannot end a still-reversing travel correction"), S->ResolvePhaseFamily(D), EProject_JLocomotionPhaseFamily::Turn);
	S->KinematicContext.HorizontalVelocity = FVector(300, 0, 0);
	RefreshTurn();
	TestEqual(TEXT("Facing and travel alignment return to Cycle"), S->ResolvePhaseFamily(D), EProject_JLocomotionPhaseFamily::Cycle);
	W.Player->SetActorRotation(FRotator(0.0f, 180.0f, 0.0f));
	D.bIsStarting = true; TestEqual(TEXT("Start retains its direct owner"), S->ResolvePhaseFamily(D), EProject_JLocomotionPhaseFamily::Start); D.bIsStarting = false;
	D.bIsPivoting = true; TestEqual(TEXT("Pivot retains its direct owner"), S->ResolvePhaseFamily(D), EProject_JLocomotionPhaseFamily::Pivot); D.bIsPivoting = false;
	S->GroundMotionMode = EProject_JGroundMotionMode::Stop;
	TestEqual(TEXT("Released input's Stop preempts a facing redirect"), S->ResolvePhaseFamily(D), EProject_JLocomotionPhaseFamily::Stop);
	S->GroundMotionMode = EProject_JGroundMotionMode::Locomotion;
	S->bIsInAir = true; TestEqual(TEXT("Air preempts a facing redirect"), S->ResolvePhaseFamily(D), EProject_JLocomotionPhaseFamily::Fall); S->bIsInAir = false;
	W.Combat->bEnableStrafeFacingRedirect = false;
	RefreshTurn();
	TestEqual(TEXT("Authored opt-out keeps ordinary combat Cycle"), S->ResolvePhaseFamily(D), EProject_JLocomotionPhaseFamily::Cycle);
	W.Combat->bEnableStrafeFacingRedirect = true;
	W.Player->bIsAttacking = true;
	RefreshTurn();
	TestEqual(TEXT("Attacks do not request a moving facing redirect"), S->ResolvePhaseFamily(D), EProject_JLocomotionPhaseFamily::Cycle); W.Player->bIsAttacking = false;
	W.Combat->StrafeFacingRedirectEntryAngle = std::numeric_limits<float>::quiet_NaN();
	W.Combat->StrafeFacingRedirectExitAngle = std::numeric_limits<float>::quiet_NaN();
	S->MovingTurnPolicy.Reset();
	W.Controller->SetControlRotation(FRotator(0, 180, 0));
	S->KinematicContext.HorizontalVelocity = FVector(-300, 0, 0);
	S->KinematicContext.MoveWorldDirection = FVector(-1, 0, 0); RefreshTurn();
	W.Controller->SetControlRotation(FRotator::ZeroRotator);
	S->KinematicContext.MoveWorldDirection = FVector(1, 0, 0); RefreshTurn();
	TestEqual(TEXT("Invalid tuning uses finite default thresholds"), S->ResolvePhaseFamily(D), EProject_JLocomotionPhaseFamily::Turn);

	auto* Anim = NewObject<UProject_JCharacterAnimInstance>(W.Player->GetMesh());
	Anim->OwningCharacter = W.Player; Anim->OwningPlayerCharacter = W.Player;
	FProject_JAnimThreadSafeData Data; Data.Combat.bIsCombatMode = true;
	Data.LocomotionContext.RotationMode = EProject_JLocomotionRotationMode::Strafe;
	Data.MotionMatching.SelectionContext = S->MotionMatchingSelectionContext;
	Data.MotionMatching.SelectionContext.bMovingTurn180 = true;
	Data.MotionMatching.SelectionContext.bUseSettledCycle = true; // Deliberately stale; Turn fallback must stay Dynamic.
	TestTrue(TEXT("Empty optional TurnRedirect falls back to Dynamic Cycle, never Idle or Settled"),
		Anim->EvaluatePoseSearchDatabaseOnGameThread(Data) == Set->RunDatabases.Cycle);
	Set->RunDatabases.TurnRedirect = NewObject<UPoseSearchDatabase>();
	TestTrue(TEXT("An authored combat TurnRedirect becomes searchable"), Anim->EvaluatePoseSearchDatabaseOnGameThread(Data) == Set->RunDatabases.TurnRedirect);
	Data.MotionMatching.SelectionContext.bMovingTurn180 = false;
	TestTrue(TEXT("An unqualified Turn cannot open the restricted Strafe PSD"), Anim->EvaluatePoseSearchDatabaseOnGameThread(Data) == Set->RunDatabases.Cycle);
	Data.MotionMatching.SelectionContext.bUseSettledCycle = false;
	Data.MotionMatching.SelectionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	TestTrue(TEXT("Turn completion restores Dynamic Cycle"), Anim->EvaluatePoseSearchDatabaseOnGameThread(Data) == Set->RunDatabases.Cycle);
	Data.MotionMatching.SelectionContext.bUseSettledCycle = true;
	TestTrue(TEXT("Settled Cycle remains an explicit later selection"), Anim->EvaluatePoseSearchDatabaseOnGameThread(Data) == Set->RunDatabases.SettledCycle);

	// Exercise the real component's OTM world-input path, not just a Boolean
	// phase request: a camera turn has zero keyboard MoveInputTurnAngle.
	S->AuthoritativeContext.bCombatMode = false;
	S->AuthoritativeContext.RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
	const auto PrimeOTMTurn = [&]()
	{
		S->MovingTurnPolicy.Reset();
		S->AuthoritativeContext.GaitIntent = EProject_JLocomotionGaitIntent::Run;
		W.Player->SetActorRotation(FRotator::ZeroRotator);
		S->KinematicContext.HorizontalVelocity = FVector(300, 0, 0);
		S->KinematicContext.MoveInputTurnAngle = 0;
		S->KinematicContext.MoveWorldDirection = FVector(1, 0, 0); RefreshTurn();
		S->KinematicContext.MoveWorldDirection = FVector(-1, 0, 0); RefreshTurn();
	};
	PrimeOTMTurn();
	TestTrue(TEXT("Real OTM component recognizes a world-heading reversal without a key edge"), D.bIsMovingTurn180);
	S->DerivedLocomotionContext = D;
	S->DerivedLocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Turn;
	S->DerivedLocomotionContext.bIsMovingTurn180 = false; S->UpdateMotionMatchingSelectionState(*W.Player);
	S->DerivedLocomotionContext.bIsMovingTurn180 = true; S->UpdateMotionMatchingSelectionState(*W.Player);
	TestTrue(TEXT("OTM large reversal inside an existing Turn still forces one search"), S->bForceMotionMatchingReselect);
	S->UpdateMotionMatchingSelectionState(*W.Player);
	TestFalse(TEXT("OTM held Turn does not force every update"), S->bForceMotionMatchingReselect);
	S->PreviousDerivedPhaseFamily = EProject_JLocomotionPhaseFamily::Turn;
	S->DerivedPhaseFamilyElapsedTime = 0;
	W.Player->bIsDodging = true; RefreshTurn();
	D.PhaseFamily = S->ResolvePhaseFamily(D); S->ApplyLocomotionPhaseStability(0.01f, D);
	TestEqual(TEXT("OTM's old minimum hold cannot resurrect a cancelled large Turn"), D.PhaseFamily, EProject_JLocomotionPhaseFamily::Cycle);
	W.Player->bIsDodging = false; RefreshTurn();
	TestFalse(TEXT("Action return cannot revive the previous Turn request"), D.bIsMovingTurn180);
	PrimeOTMTurn();
	S->AuthoritativeContext.GaitIntent = EProject_JLocomotionGaitIntent::Sprint; RefreshTurn();
	TestFalse(TEXT("Run window does not leak into Sprint"), D.bIsMovingTurn180);
	PrimeOTMTurn();
	S->bUsingLocalInputState = false; RefreshTurn();
	TestFalse(TEXT("A simulated owner cannot infer a local camera-turn request"), D.bIsMovingTurn180);
	S->bUsingLocalInputState = true; RefreshTurn();
	TestFalse(TEXT("Ownership handoff does not revive a stale request"), D.bIsMovingTurn180);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStrafeFacingSearchTest, "ProjectJ.StrafeFacingRedirect.SearchContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJStrafeFacingSearchTest::RunTest(const FString&)
{
	FProject_JCharacterAnimInstanceProxy P;
	P.ThreadSafeData.Combat.bIsCombatMode = true;
	P.ThreadSafeData.LocomotionContext.RotationMode = EProject_JLocomotionRotationMode::Strafe;
	P.ThreadSafeData.LocomotionContext.bIsMotionMatchingMoving = true;
	P.ThreadSafeData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	P.CacheMotionMatchingPolicyState();
	P.ThreadSafeData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Turn;
	TestEqual(TEXT("Moving facing family can interrupt when its PSD changes"), P.ResolveDatabaseChangeInterruptMode(), EPoseSearchInterruptMode::InterruptOnDatabaseChange);
	TestTrue(TEXT("Facing redirect preserves the continuing pose's foot-phase features"),
		P.ResolveDatabaseChangeInterruptMode() != EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose);
	P.CacheMotionMatchingPolicyState();
	TestEqual(TEXT("Stable turn does not repeatedly interrupt"), P.ResolveDatabaseChangeInterruptMode(), EPoseSearchInterruptMode::DoNotInterrupt);
	P.ThreadSafeData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	TestEqual(TEXT("Completion can restore the Cycle PSD without invalidating continuing pose"), P.ResolveDatabaseChangeInterruptMode(), EPoseSearchInterruptMode::InterruptOnDatabaseChange);
	P.CacheMotionMatchingPolicyState();
	TestEqual(TEXT("Stable Cycle resumes normal continuous search"), P.ResolveDatabaseChangeInterruptMode(), EPoseSearchInterruptMode::DoNotInterrupt);
	P.ThreadSafeData.Combat.bIsCombatMode = false;
	P.ThreadSafeData.LocomotionContext.RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
	P.CacheMotionMatchingPolicyState();
	P.ThreadSafeData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Turn;
	TestEqual(TEXT("OTM camera-driven Turn also interrupts on its database boundary"), P.ResolveDatabaseChangeInterruptMode(), EPoseSearchInterruptMode::InterruptOnDatabaseChange);
	P.CacheMotionMatchingPolicyState();
	TestEqual(TEXT("OTM sustained Turn resumes normal budgeted search"), P.ResolveDatabaseChangeInterruptMode(), EPoseSearchInterruptMode::DoNotInterrupt);
	return true;
}
#endif
