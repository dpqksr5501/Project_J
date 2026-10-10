#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuthoredEarlyTransitionTest, "ProjectJ.Animation.EarlyTransition.AuthoredConditionAndOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAuthoredEarlyTransitionTest::RunTest(const FString&)
{
	auto* Mesh = NewObject<USkeletalMeshComponent>();
	auto* Anim = NewObject<UProject_JCharacterAnimInstance>(Mesh);
	auto* Held = NewObject<UAnimSequence>(); auto* Outgoing = NewObject<UAnimSequence>();
	Anim->CachedStateControllerSelectedAnimation = Held;
	Anim->ThreadSafeData.OneShotPresentation.bEnabled = true;
	Anim->RequestAuthoredEarlyTransition(Outgoing, true, EProject_JLocomotionGaitIntent::Run);
	TestFalse(TEXT("Other/outgoing clip cannot authorize held state's exit"), Anim->IsAuthoredEarlyTransitionAllowed(EProject_JLocomotionGaitIntent::Walk));
	Anim->RequestAuthoredEarlyTransition(Held, true, EProject_JLocomotionGaitIntent::Run);
	TestFalse(TEXT("Original Run condition preserves Run playback"), Anim->IsAuthoredEarlyTransitionAllowed(EProject_JLocomotionGaitIntent::Run));
	TestTrue(TEXT("Walk change enables re-evaluation"), Anim->IsAuthoredEarlyTransitionAllowed(EProject_JLocomotionGaitIntent::Walk));
	TestTrue(TEXT("Sprint change enables re-evaluation"), Anim->IsAuthoredEarlyTransitionAllowed(EProject_JLocomotionGaitIntent::Sprint));
	Anim->CachedStateControllerSelectedAnimation = Outgoing;
	TestFalse(TEXT("New selected asset rejects old permission"), Anim->IsAuthoredEarlyTransitionAllowed(EProject_JLocomotionGaitIntent::Walk));
	Anim->CachedStateControllerSelectedAnimation = Held;
	++Anim->EarlyTransitionSnapshotRevision;
	TestTrue(TEXT("Budgeted next animation update retains permission"), Anim->IsAuthoredEarlyTransitionAllowed(EProject_JLocomotionGaitIntent::Walk));
	++Anim->EarlyTransitionSnapshotRevision;
	TestFalse(TEXT("Window expires without another notify tick"), Anim->IsAuthoredEarlyTransitionAllowed(EProject_JLocomotionGaitIntent::Walk));
	Anim->bAuthoredEarlyTransitionPending = false;
	Anim->ThreadSafeData.OneShotPresentation.bEnabled = false;
	Anim->RequestAuthoredEarlyTransition(Held, true, EProject_JLocomotionGaitIntent::Run);
	TestFalse(TEXT("Motion Matching only owner creates no held permission"), Anim->bAuthoredEarlyTransitionPending);
	return true;
}
#endif
