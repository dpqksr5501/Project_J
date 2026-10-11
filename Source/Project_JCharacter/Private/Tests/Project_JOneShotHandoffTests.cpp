#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimNode_ProjectJOneShotHandoff.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/UnrealType.h"

namespace
{
struct FPlaybackSpy : FAnimNode_Base
{
	TFunction<void(const FAnimationUpdateContext&)> Inspect;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override { Inspect(Context); }
};
struct FTestHandoff : FAnimNode_ProjectJOneShotHandoff
{
	void Configure(FAnimNode_Base* MM, FAnimNode_Base* External, float Duration = .2f)
	{
		AddPose(); AddPose(); BlendPose[0].SetLinkNode(MM); BlendPose[1].SetLinkNode(External);
		const auto* Times = FindFProperty<FArrayProperty>(FAnimNode_BlendListBase::StaticStruct(), TEXT("BlendTime"));
		auto* Values = Times->ContainerPtrToValuePtr<TArray<float>>(this); (*Values)[0] = (*Values)[1] = Duration;
	}
	void SetMM(bool bMM)
	{
		FindFProperty<FBoolProperty>(FAnimNode_BlendListByBool::StaticStruct(), TEXT("bActiveValue"))->SetPropertyValue_InContainer(this, bMM);
	}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOneShotHandoffOwnershipTest, "ProjectJ.Animation.OneShotReturn.CommandOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJOneShotHandoffOwnershipTest::RunTest(const FString&)
{
	auto* Mesh = NewObject<USkeletalMeshComponent>();
	auto* Anim = NewObject<UProject_JCharacterAnimInstance>(Mesh);
	FProject_JCharacterAnimInstanceProxy Proxy(Anim);
	FAnimationUpdateSharedContext Shared;
	FAnimationUpdateContext Context(&Proxy, 1.f / 60.f, &Shared);
	auto* Start = NewObject<UAnimSequence>(); auto* Loop = NewObject<UAnimSequence>(); auto* Replacement = NewObject<UAnimSequence>();
	FPlaybackSpy MM, External;
	FTestHandoff Node; Node.Configure(&MM, &External);
	Node.Initialize_AnyThread(FAnimationInitializeContext(&Proxy));
	auto& Data = Proxy.ThreadSafeData;
	Data.LocomotionMode = EProject_JAnimationLocomotionMode::OnFoot;
	Data.OneShotPresentation.SelectedAnimation = Start;
	Data.OneShotPresentation.PresentationState = EProject_JStateControllerPresentationState::TransitionToLocomotion;
	Data.OneShotPresentation.PhaseFamily = EProject_JLocomotionPhaseFamily::Start;
	Data.OneShotPresentation.bShouldOverrideMotionMatching = true;
	Data.OneShotPresentation.bForceBlendNextUpdate = true;
	Data.OneShotPresentation.SelectedAnimationOutput.StartTime = .3f;
	UAnimationAsset* ExpectedExternal = Start;
	bool bExpectLive = false;
	int32 ExternalUpdates = 0;
	MM.Inspect = [&](const FAnimationUpdateContext&)
	{
		TestTrue(TEXT("MM context remains the newest snapshot"), Proxy.GetThreadSafeData().OneShotPresentation.SelectedAnimation.Get() == Loop);
	};
	External.Inspect = [&](const FAnimationUpdateContext&)
	{
		++ExternalUpdates;
		TestEqual(TEXT("External player receives the correct playback command"), Proxy.GetExternalPlaybackData().SelectedAnimation.Get(), ExpectedExternal);
		TestEqual(TEXT("Only a live return installs a scoped playback view"), Proxy.IsLiveReturnTraversal(), bExpectLive);
		if (bExpectLive)
		{
			TestFalse(TEXT("Return never restarts the outgoing clip"), Proxy.GetExternalPlaybackData().bForceBlendNextUpdate);
			TestEqual(TEXT("Return retains authored entry time"), Proxy.GetExternalPlaybackData().SelectedAnimationOutput.StartTime, .3f);
		}
	};
	Node.SetMM(false); Node.Update_AnyThread(Context);
	Data.OneShotPresentation.bShouldOverrideMotionMatching = false;
	Data.OneShotPresentation.SelectedAnimation = Loop; bExpectLive = true;
	Node.SetMM(true); Node.Update_AnyThread(Context);
	TestTrue(TEXT("First return overlaps both pose branches"), Node.IsLiveReturnActive() && Node.GetOutgoingWeight() > 0.f && Node.GetOutgoingWeight() < 1.f);
	TestFalse(TEXT("Playback view cannot escape the node traversal"), Proxy.IsLiveReturnTraversal());
	for (int32 Frame = 0; Frame < 20; ++Frame) Node.Update_AnyThread(Context);
	TestFalse(TEXT("Engine weight completion releases the retained command"), Node.IsLiveReturnActive());
	TestEqual(TEXT("No outgoing weight remains"), Node.GetOutgoingWeight(), 0.f);
	const int32 CompletedUpdates = ExternalUpdates;
	Node.Update_AnyThread(Context);
	TestEqual(TEXT("Finished external branch does not keep ticking"), ExternalUpdates, CompletedUpdates);
	// Another return, interrupted by a new one-shot before its blend completes.
	Data.OneShotPresentation.SelectedAnimation = Start; Data.OneShotPresentation.bShouldOverrideMotionMatching = true;
	bExpectLive = false; Node.SetMM(false); Node.Update_AnyThread(Context);
	Data.OneShotPresentation.SelectedAnimation = Loop; Data.OneShotPresentation.bShouldOverrideMotionMatching = false;
	bExpectLive = true; Node.SetMM(true); Node.Update_AnyThread(Context);
	Data.OneShotPresentation.SelectedAnimation = Replacement; Data.OneShotPresentation.bShouldOverrideMotionMatching = true;
	ExpectedExternal = Replacement; bExpectLive = false;
	Node.SetMM(false); Node.Update_AnyThread(Context);
	TestFalse(TEXT("New one-shot preempts the old return"), Node.IsLiveReturnActive());
	// Mode change releases a live return without restarting any child graph.
	Data.OneShotPresentation.SelectedAnimation = Loop; Data.OneShotPresentation.bShouldOverrideMotionMatching = false;
	ExpectedExternal = Replacement; bExpectLive = true; Node.SetMM(true); Node.Update_AnyThread(Context);
	Data.LocomotionContext.RotationMode = EProject_JLocomotionRotationMode::Strafe;
	bExpectLive = false; Node.Update_AnyThread(Context);
	TestFalse(TEXT("Mode change preempts live overlap"), Node.IsLiveReturnActive());
	TestEqual(TEXT("Mode replacement hands ownership fully to MM"), Node.GetOutgoingWeight(), 0.f);
	TestFalse(TEXT("No scoped view leaks after replacement"), Proxy.IsLiveReturnTraversal());
	for (int32 Case = 0; Case < 9; ++Case)
	{
		Data = {};
		Data.LocomotionMode = EProject_JAnimationLocomotionMode::OnFoot;
		Data.OneShotPresentation.SelectedAnimation = Start;
		Data.OneShotPresentation.bShouldOverrideMotionMatching = true;
		Data.OneShotPresentation.PresentationState = EProject_JStateControllerPresentationState::TransitionToLocomotion;
		Data.OneShotPresentation.PhaseFamily = EProject_JLocomotionPhaseFamily::Start;
		Data.OneShotPresentation.SelectedAnimationOutput.StartTime = .3f;
		ExpectedExternal = Start; bExpectLive = false;
		FTestHandoff Guarded; Guarded.Configure(&MM, &External, Case == 8 ? 0.f : .2f);
		Guarded.Initialize_AnyThread(FAnimationInitializeContext(&Proxy));
		if (Case == 0) Data.OneShotPresentation.PresentationState = EProject_JStateControllerPresentationState::TurnInPlace;
		if (Case == 1) Data.OneShotPresentation.PhaseFamily = EProject_JLocomotionPhaseFamily::Pivot;
		Guarded.SetMM(false); Guarded.Update_AnyThread(Context);
		if (Case == 1)
		{
			// Semantic phase changes must not erase the root-turn episode.
			Data.OneShotPresentation.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
			Guarded.Update_AnyThread(Context);
		}
		Data.OneShotPresentation.bShouldOverrideMotionMatching = false;
		Data.OneShotPresentation.SelectedAnimation = Loop;
		if (Case == 2) Data.Air.bIsInAir = true;
		if (Case == 3) Data.Combat.bIsAttacking = true;
		if (Case == 4) Data.Combat.bIsDodging = true;
		if (Case == 5) Data.Combat.bIsHitReacting = true;
		if (Case == 6) Data.ProceduralIK.FullBodyMontageWeight = 1.f;
		if (Case == 7) Data.MotionMatchingSearchPolicy.bEnableLiveOneShotReturn = false;
		// Engine's zero-duration switch performs one zero-weight outgoing
		// update before releasing it; the retained command still protects it.
		bExpectLive = Case == 8;
		Guarded.SetMM(true); Guarded.Update_AnyThread(Context);
		TestFalse(FString::Printf(TEXT("Guard case %d releases live overlap"), Case), Guarded.IsLiveReturnActive());
		TestEqual(TEXT("Excluded return or zero duration leaves no outgoing weight"), Guarded.GetOutgoingWeight(), 0.f);
		TestFalse(TEXT("Guard leaves no scoped playback pointer"), Proxy.IsLiveReturnTraversal());
	}
	return true;
}
#endif
