#include "Animation/AnimNode_ProjectJOneShotHandoff.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "HAL/IConsoleManager.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimInertializationSyncScope.h"

namespace
{
TAutoConsoleVariable<int32> CVarLiveReturn(TEXT("p.ProjectJ.LiveOneShotReturn"), 1,
	TEXT("One Shot Handoff node: 0=inertial return baseline, 1=profile-controlled live Start/Stop/Land overlap."));
TAutoConsoleVariable<int32> CVarReturnDebug(TEXT("p.ProjectJ.OneShotReturnDebug"), 0,
	TEXT("0=off, 1=log each live return blend frame and completion. Does not change playback."));
}

void FAnimNode_ProjectJOneShotHandoff::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	OutgoingPresentation = {};
	bWasOverride = bLiveReturn = bRootTurnEpisode = bHasUpdated = false;
	FAnimNode_BlendListByBool::Initialize_AnyThread(Context);
}

void FAnimNode_ProjectJOneShotHandoff::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	if (!Context.AnimInstanceProxy->GetAnimInstanceObject()->IsA<UProject_JCharacterAnimInstance>())
	{
		FAnimNode_BlendListByBool::Update_AnyThread(Context); return;
	}
	auto& Proxy = *static_cast<FProject_JCharacterAnimInstanceProxy*>(Context.AnimInstanceProxy);
	const auto& Data = Proxy.GetThreadSafeData();
	const auto& Shot = Data.OneShotPresentation;
	const bool bPreviousLiveReturn = bLiveReturn;
	if (Shot.bShouldOverrideMotionMatching)
	{
		if (!bWasOverride || OutgoingPresentation.SelectionRevision != Shot.SelectionRevision) bRootTurnEpisode = false;
		bRootTurnEpisode |= Shot.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot ||
			Shot.PresentationState == EProject_JStateControllerPresentationState::TurnInPlace;
		OutgoingPresentation = Shot;
		bLiveReturn = false;
	}
	else if (bWasOverride)
	{
		using P = EProject_JStateControllerPresentationState;
		// TIP/Pivot own root-turn completion; retain their existing inertial path.
		bLiveReturn = OutgoingPresentation.SelectedAnimation && !bRootTurnEpisode &&
			((OutgoingPresentation.PresentationState == P::TransitionToLocomotion &&
				OutgoingPresentation.PhaseFamily != EProject_JLocomotionPhaseFamily::Pivot) ||
			 OutgoingPresentation.PresentationState == P::TransitionToIdle ||
			 OutgoingPresentation.PresentationState == P::TransitionToLand);
	}
	// A live overlap is presentation only. Gameplay replacements preempt it.
	bLiveReturn &= CVarLiveReturn.GetValueOnAnyThread() != 0 && Data.MotionMatchingSearchPolicy.bEnableLiveOneShotReturn &&
		Data.LocomotionMode == EProject_JAnimationLocomotionMode::OnFoot &&
		!Data.Air.bIsInAir && !Data.Combat.bIsAttacking && !Data.Combat.bIsDodging && !Data.Combat.bIsHitReacting &&
		Data.ProceduralIK.FullBodyMontageWeight <= UE_SMALL_NUMBER &&
		Data.LocomotionContext.RotationMode == OutgoingPresentation.RotationMode;
	const bool bChangedBranch = bWasOverride != Shot.bShouldOverrideMotionMatching;
	bool bRequestedInertialization = false;
	if ((bHasUpdated && bChangedBranch && !bLiveReturn) ||
		(bPreviousLiveReturn && !bLiveReturn && !Shot.bShouldOverrideMotionMatching))
	{
		// A mode/action replacement must not change the outgoing player's asset
		// halfway through a live blend. Release it through the existing inertializer.
		if (auto* Requester = Context.GetMessage<UE::Anim::IInertializationRequester>())
		{
			FInertializationRequest Request;
			const int32 Destination = Shot.bShouldOverrideMotionMatching ? 1 : 0;
			Request.Duration = GetBlendTimes().IsValidIndex(Destination) ? GetBlendTimes()[Destination] : 0.f;
			Request.BlendProfile = GetBlendProfile(); Request.bUseBlendMode = true;
			Request.BlendMode = GetBlendType(); Request.CustomBlendCurve = GetCustomBlendCurve();
#if ANIM_TRACE_ENABLED
			Request.NodeId = Context.GetCurrentNodeId();
			Request.AnimInstance = Context.AnimInstanceProxy->GetAnimInstanceObject();
#endif
			Requester->RequestInertialization(Request);
			Requester->AddDebugRecord(*Context.AnimInstanceProxy, Context.GetCurrentNodeId());
			bRequestedInertialization = true;
		}
		Initialize();
	}
	OutgoingPresentation.bForceBlendNextUpdate = false;
	// Keep the asset's warp eligibility, but use the current travel direction.
	OutgoingPresentation.StrafeDirectionAngle = Shot.StrafeDirectionAngle;
	const auto* PreviousPlayback = Proxy.SwapExternalPlaybackOverride(bLiveReturn ? &OutgoingPresentation : nullptr);
	UE::Anim::TOptionalScopedGraphMessage<UE::Anim::FAnimInertializationSyncScope> InertializationSync(bRequestedInertialization, Context);
	FAnimNode_BlendListByBool::Update_AnyThread(Context);
	Proxy.SwapExternalPlaybackOverride(PreviousPlayback);
#if !UE_BUILD_SHIPPING
	if (bLiveReturn && CVarReturnDebug.GetValueOnAnyThread() != 0)
	{
		UE_LOG(LogAnimation, Display, TEXT("ProjectJOneShotReturn Frame=%llu Instance=%s Clip=%s MMWeight=%.4f ExternalWeight=%.4f Complete=%d"),
			GFrameCounter, *GetNameSafe(Proxy.GetAnimInstanceObject()), *GetNameSafe(OutgoingPresentation.SelectedAnimation.Get()),
			PerBlendData.IsValidIndex(0) ? PerBlendData[0].Weight : 0.f, GetOutgoingWeight(), GetOutgoingWeight() <= ZERO_ANIMWEIGHT_THRESH);
	}
#endif
	bWasOverride = Shot.bShouldOverrideMotionMatching;
	bHasUpdated = true;
	if (!bWasOverride && GetOutgoingWeight() <= ZERO_ANIMWEIGHT_THRESH)
	{
		bLiveReturn = false;
		OutgoingPresentation = {};
	}
}
