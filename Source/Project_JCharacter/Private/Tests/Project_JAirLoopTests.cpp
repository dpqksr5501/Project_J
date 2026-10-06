#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/AnimSequence.h"
#include "Project_JGreatswordCharacter.h"
#include "Chooser.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAirLoopOutputTest, "ProjectJ.Animation.Naturalness.AirLoopOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAirLoopOutputTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* Player = World->SpawnActor<AProject_JGreatswordCharacter>();
	auto* Profile = NewObject<UProject_JCharacterAnimProfile>();
	auto* Locomotion = NewObject<UProject_JLocomotionProfile>();
	Profile->LocomotionProfile = Locomotion;
	Player->CharacterAnimProfile = Profile;
	Locomotion->MotionMatchingSearchPolicy.StateControllerAnimationChooserTable = LoadObject<UChooserTable>(nullptr,
		TEXT("/Game/Animation_Logic/Chooser/CHT_Player_StateControllerAnimations.CHT_Player_StateControllerAnimations"));
	auto* Anim = NewObject<UProject_JCharacterAnimInstance>(Player->GetMesh());
	Anim->OwningCharacter = Player;
	Anim->OwningPlayerCharacter = Player;
	FProject_JAnimThreadSafeData Data;
	Data.Air.bIsInAir = true;
	Data.OneShotPresentation.bEnabled = true;
	Data.OneShotPresentation.PhaseFamily = EProject_JLocomotionPhaseFamily::Fall;
	Data.OneShotPresentation.PresentationState = EProject_JStateControllerPresentationState::InAirLoop;
	const auto Evaluate = [&]()
	{
		// NativeUpdateAnimation normally publishes these command-owned columns.
		Anim->StateControllerPresentationStateForChooser = Data.OneShotPresentation.PresentationState;
		Anim->RotationModeForChooser = Data.LocomotionContext.RotationMode;
		Anim->EvaluateStateControllerAnimationChooserOnGameThread(Data);
	};
	Evaluate();
	TestTrue(TEXT("Actual authored FallLoop chooser returns an asset"), Data.OneShotPresentation.bHasSelectedAnimation);
	TestTrue(TEXT("FallLoop plays as a loop"), Data.OneShotPresentation.bSelectedAnimationShouldLoop);
	TestTrue(TEXT("Selected airborne loop owns the external output"), Data.OneShotPresentation.bShouldOverrideMotionMatching);
	// An authored FallOff may finish before the component's broader 2s flag.
	// Exercise the real hold resolver, including repeated fresh snapshots.
	Anim->StateControllerRuntime.BeginHold(EProject_JStateControllerPresentationState::TransitionToInAir, 0);
	Data.Air.bIsFallOffStart = true;
	Data.MotionMatchingSearchPolicy.ExperimentalFallOffMaxHoldTime = 0.65f;
	Anim->AnimationClock.Advance(0.8f, false);
	Anim->ResolveStateControllerPresentationStateWithPlaybackHold(Data, Data.OneShotPresentation);
	TestEqual(TEXT("Completed FallOff releases to its loop before the semantic timer ends"), Data.OneShotPresentation.PresentationState, EProject_JStateControllerPresentationState::InAirLoop);
	for (int32 Frame = 0; Frame < 20; ++Frame)
	{
		Anim->AnimationClock.Advance(1.0f / 60, false);
		Anim->ResolveStateControllerPresentationStateWithPlaybackHold(Data, Data.OneShotPresentation);
		TestEqual(TEXT("Same FallOff flag cannot reacquire the completed entry"), Data.OneShotPresentation.PresentationState, EProject_JStateControllerPresentationState::InAirLoop);
	}
	Data.Air.bIsJumping = true;
	Data.OneShotPresentation.PhaseFamily = EProject_JLocomotionPhaseFamily::JumpStart;
	Anim->ResolveStateControllerPresentationStateWithPlaybackHold(Data, Data.OneShotPresentation);
	TestEqual(TEXT("A real renewed JumpStart can acquire its own air entry"), Data.OneShotPresentation.PresentationState, EProject_JStateControllerPresentationState::TransitionToInAir);
	Data.Air.bIsJumping = Data.Air.bIsFallOffStart = false;
	Data.OneShotPresentation.PhaseFamily = EProject_JLocomotionPhaseFamily::Fall;
	Data.OneShotPresentation.PresentationState = EProject_JStateControllerPresentationState::InAirLoop;
	Evaluate();
	const auto* Asset = Data.OneShotPresentation.SelectedAnimation.Get();
	const int32 Revision = Data.OneShotPresentation.SelectionRevision;
	for (int32 Frame = 0; Frame < 180; ++Frame)
	{
		Anim->AnimationClock.Advance(1.0f / 60.0f, false);
		// Live feet, input and mode columns must not restart the same airborne loop.
		Data.Combat.bIsCombatMode = Frame % 2 != 0;
		Data.LocomotionContext.RotationMode = Data.Combat.bIsCombatMode
			? EProject_JLocomotionRotationMode::Strafe : EProject_JLocomotionRotationMode::OrientToMovement;
		Data.OneShotPresentation.Foot = Frame % 2 ? EProject_JStateControllerFoot::Right : EProject_JStateControllerFoot::Left;
		Data.Movement.GroundSpeed = float(Frame);
		Evaluate();
		TestTrue(TEXT("Same fall retains its selected loop"), Data.OneShotPresentation.SelectedAnimation.Get() == Asset);
		TestEqual(TEXT("Same fall retains its selection revision"), Data.OneShotPresentation.SelectionRevision, Revision);
		TestFalse(TEXT("Same fall emits no replay pulse"), Data.OneShotPresentation.bForceBlendNextUpdate);
	}
	// The existing worker contract must retain the return request through budgets.
	FProject_JCharacterAnimInstanceProxy Proxy;
	Proxy.QueueGameThreadData(Data, nullptr, true, false, false);
	Data.OneShotPresentation.PresentationState = EProject_JStateControllerPresentationState::LocomotionLoop;
	Data.Air.bIsInAir = false;
	Data.LocomotionContext.bIsMotionMatchingMoving = true;
	Evaluate();
	TestFalse(TEXT("Ground locomotion remains owned by MM"), Data.OneShotPresentation.bShouldOverrideMotionMatching);
	Proxy.QueueGameThreadData(Data, nullptr, true, false, false);
	TestTrue(TEXT("Air external exit requests a history-based MM return"), Proxy.IsReturnQueryForTrace());
	for (auto State : {EProject_JStateControllerPresentationState::IdleLoop,
		EProject_JStateControllerPresentationState::Disabled})
	{
		Data.OneShotPresentation.PresentationState = State;
		Evaluate();
		TestFalse(TEXT("Idle/disabled output cannot become external loop ownership"), Data.OneShotPresentation.bShouldOverrideMotionMatching);
	}
	Data.OneShotPresentation.PresentationState = EProject_JStateControllerPresentationState::InAirLoop;
	Data.Landing.bIsLanding = true;
	Evaluate();
	TestFalse(TEXT("Landing's presentation air flag cannot grant stale FallLoop output"), Data.OneShotPresentation.bShouldOverrideMotionMatching);
	Data.Air.bIsInAir = true;
	Evaluate();
	TestTrue(TEXT("Renewed physical fall wins over a stale landing event"), Data.OneShotPresentation.bShouldOverrideMotionMatching);
	Data.Landing.bIsLanding = false;
	Data.LocomotionMode = EProject_JAnimationLocomotionMode::Mounted;
	Evaluate();
	TestFalse(TEXT("Mounted output cannot acquire an on-foot air loop"), Data.OneShotPresentation.bShouldOverrideMotionMatching);
	Data.OneShotPresentation.bEnabled = false;
	Evaluate();
	TestFalse(TEXT("Disabled presentation releases selected output"), Data.OneShotPresentation.bShouldOverrideMotionMatching);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
