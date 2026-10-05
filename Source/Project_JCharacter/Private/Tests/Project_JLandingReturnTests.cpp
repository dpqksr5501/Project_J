#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JGreatswordCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
struct FLandingWorld
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FLandingWorld() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
	~FLandingWorld() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLandingReturnContextTest,
	"ProjectJ.Animation.LandingReturnContext", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJLandingReturnContextTest::RunTest(const FString&)
{
	FLandingWorld Fixture;
	auto* Player = Fixture.World->SpawnActor<AProject_JGreatswordCharacter>();
	if (!TestNotNull(TEXT("Landing character"), Player)) return false;
	auto* Component = Player->GetLocomotionAnimStateComponent();
	if (!TestNotNull(TEXT("Landing component"), Component)) return false;
	Component->GroundSpeed = 500.0f;
	Component->bHasMoveInput = true;
	Component->BeginLandingState(*Player, 600.0f);
	TestTrue(TEXT("Real landing retains the legacy air presentation flag"), Component->bIsInAir);
	TestFalse(TEXT("Real landing has physical ground contact"), Component->bIsPhysicallyInAir);
	FProject_JLocomotionKinematicContext Movement;
	Movement.GroundSpeed = 500.0f;
	Movement.bHasMoveInput = true;
	Movement.bHasFutureTrajectoryVelocity = true;
	Movement.FutureTrajectorySpeed = 500.0f;
	Component->bUsingLocalInputState = true;
	const bool bGroundIntent = Component->HasGroundMovementIntentForContext(Movement);
	TestTrue(TEXT("Running intent survives the landing presentation gate"), bGroundIntent);
	FProject_JLocomotionAuthoritativeContext Authority;
	auto Derived = Component->BuildDerivedLocomotionContext(Authority, Movement);
	TestTrue(TEXT("Real landing producer publishes ground movement intent"), Derived.bHasGroundMovementIntent);
	TestFalse(TEXT("Authored landing still suppresses moving MM before release"), Derived.bIsMotionMatchingMoving);
	TestEqual(TEXT("Authored landing retains its phase before release"), Derived.PhaseFamily, EProject_JLocomotionPhaseFamily::Landing);
	Movement.bHasMoveInput = false;
	TestFalse(TEXT("Local input release still exits moving MM despite residual speed"), Component->HasGroundMovementIntentForContext(Movement));
	Component->bUsingLocalInputState = false;
	TestTrue(TEXT("Remote movement can use replicated velocity without local keys"), Component->HasGroundMovementIntentForContext(Movement));
	Component->UpdateLocalAirState(true);
	TestTrue(TEXT("A renewed fall can precede stale landing cleanup"), Component->IsLandingStateActive());
	TestFalse(TEXT("Actual air overrides an outstanding landing"), Component->HasGroundMovementIntentForContext(Movement));
	Component->ClearActiveLandingState();
	TestFalse(TEXT("Physical air still wins if the presentation air flag clears"), Component->HasGroundMovementIntentForContext(Movement));
	Component->bIsPhysicallyInAir = false;
	Component->bIsInAir = true;
	TestFalse(TEXT("Pending jump air without landing cannot become ground intent"), Component->HasGroundMovementIntentForContext(Movement));
	Component->BeginLandingState(*Player, 600.0f);
	Component->bUsingLocalInputState = true;
	Movement.bHasMoveInput = true;

	auto* Mesh = NewObject<USkeletalMeshComponent>();
	auto* Anim = NewObject<UProject_JCharacterAnimInstance>(Mesh);
	FProject_JAnimThreadSafeData Original;
	Original.Landing.bIsLanding = true;
	Original.Landing.PresentationRevision = 42;
	Original.Input.bHasMoveInput = true;
	Original.Movement.GroundSpeed = 500.0f;
	Original.LocomotionContext.bHasGroundMovementIntent = Component->BuildDerivedLocomotionContext(Authority, Movement).bHasGroundMovementIntent;
	Original.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Landing;
	Original.MotionMatching.SelectionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Landing;
	Original.OneShotPresentation.bEnabled = true;
	Original.OneShotPresentation.bRequested = true;
	Original.OneShotPresentation.PhaseFamily = EProject_JLocomotionPhaseFamily::Landing;
	FProject_JStateControllerRuntime::FIntent Intent;
	Intent.bHasMoveInput = true;
	Intent.LandingPresentationRevision = 42;
	Anim->StateControllerRuntime.BeginDesiredHold(EProject_JStateControllerPresentationState::TransitionToLand, Intent, 0);
	auto Data = Original;
	Anim->UpdateReleasedLandingMotionContext(Data);
	TestEqual(TEXT("An active authored Land keeps landing semantics"), Data.LocomotionContext.PhaseFamily, EProject_JLocomotionPhaseFamily::Landing);
	Anim->UpdateReleasedLandingMotionContext(Data, true);
	TestEqual(TEXT("Moving Land cancellation immediately requests Cycle"), Data.MotionMatching.SelectionContext.PhaseFamily, EProject_JLocomotionPhaseFamily::Cycle);
	TestTrue(TEXT("Physical landing remains active"), Data.Landing.bIsLanding);
	TestEqual(TEXT("Physical landing identity is preserved"), Data.Landing.PresentationRevision, 42);
	Anim->StateControllerRuntime.SetFallbackHold(EProject_JStateControllerPresentationState::LocomotionLoop, 0.1);
	for (auto Rotation : { EProject_JLocomotionRotationMode::OrientToMovement, EProject_JLocomotionRotationMode::Strafe })
	{
		Data = Original; // A fresh producer still reports physical Landing and MM-moving=false.
		Derived = Component->BuildDerivedLocomotionContext(Authority, Movement);
		Data.LocomotionContext.bHasGroundMovementIntent = Derived.bHasGroundMovementIntent;
		Data.LocomotionContext.bIsMotionMatchingMoving = Derived.bIsMotionMatchingMoving;
		Data.LocomotionContext.PhaseFamily = Derived.PhaseFamily;
		Data.LocomotionContext.RotationMode = Rotation;
		Anim->UpdateReleasedLandingMotionContext(Data);
		TestTrue(TEXT("Fresh landing snapshots retain ground MM intent"), Data.LocomotionContext.bIsMotionMatchingMoving);
		TestEqual(TEXT("Neither OTM nor Strafe regresses to Idle"), Data.MotionMatching.SelectionContext.PhaseFamily, EProject_JLocomotionPhaseFamily::Cycle);
		Anim->ResolveStateControllerPresentationStateWithPlaybackHold(Data, Data.OneShotPresentation);
		TestEqual(TEXT("Consumed Land is not replayed after a mode change"), Data.OneShotPresentation.PresentationState, EProject_JStateControllerPresentationState::LocomotionLoop);
	}
	Data = Original;
	Data.LocomotionContext.bHasGroundMovementIntent = false;
	Data.Input.bHasMoveInput = false;
	Anim->UpdateReleasedLandingMotionContext(Data);
	TestEqual(TEXT("A standing or released landing may return to Idle"), Data.MotionMatching.SelectionContext.PhaseFamily, EProject_JLocomotionPhaseFamily::Idle);
	Data = Original;
	Data.Landing.PresentationRevision = 43;
	Anim->UpdateReleasedLandingMotionContext(Data);
	TestEqual(TEXT("A new physical landing keeps its own presentation"), Data.LocomotionContext.PhaseFamily, EProject_JLocomotionPhaseFamily::Landing);
	Data = Original;
	Data.Air.bIsInAir = true;
	Data.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Fall;
	Anim->UpdateReleasedLandingMotionContext(Data, true);
	TestEqual(TEXT("A real fall is not replaced by a ground return"), Data.LocomotionContext.PhaseFamily, EProject_JLocomotionPhaseFamily::Fall);
	Data = Original;
	Data.LocomotionMode = EProject_JAnimationLocomotionMode::Mounted;
	Anim->UpdateReleasedLandingMotionContext(Data, true);
	TestEqual(TEXT("On-foot landing repair does not modify mounted context"), Data.LocomotionContext.PhaseFamily, EProject_JLocomotionPhaseFamily::Landing);
	return true;
}
#endif
