#include "Animation/AnimNode_ProjectJLocomotion.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/AnimInertializationSyncScope.h"
#include "Animation/AnimNode_Inertialization.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Animation/Project_JMotionMatchingEngineAdapter.h"
using ProjectJ::Animation::FScopedMotionMatchingReturn;
#include "Animation/Project_JMotionMatchingCVars.h"

namespace
{
class FScopedPlaybackPresentation
{
public:
	FScopedPlaybackPresentation(FProject_JCharacterAnimInstanceProxy& InProxy,
		const FProject_JAnimOneShotPresentationThreadSafeData* Data, int32 SampleIndex) : Proxy(InProxy)
	{
		Previous = Proxy.SwapExternalPlaybackOverride(Data);
		PreviousIndex = Proxy.SwapExternalPlaybackSampleIndex(SampleIndex);
	}
	~FScopedPlaybackPresentation()
	{
		Proxy.SwapExternalPlaybackOverride(Previous);
		Proxy.SwapExternalPlaybackSampleIndex(PreviousIndex);
	}
private:
	FProject_JCharacterAnimInstanceProxy& Proxy;
	const FProject_JAnimOneShotPresentationThreadSafeData* Previous = nullptr;
	int32 PreviousIndex = INDEX_NONE;
};

}

void FAnimNode_ProjectJLocomotion::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Presentation = {};
	AuthoredPoseLinkIndex = INDEX_NONE;
	bPlayingOneShot = bRootTurnEpisode = false;
	InertialReturn.Reset();
	PlaybackUpdateCounter.Reset();
	FAnimNode_MotionMatching::Initialize_AnyThread(Context);
}

void FAnimNode_ProjectJLocomotion::UpdateAssetPlayer(const FAnimationUpdateContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_UnifiedLocomotion);
	if (!Context.AnimInstanceProxy->GetAnimInstanceObject()->IsA<UProject_JCharacterAnimInstance>())
	{
		FAnimNode_MotionMatching::UpdateAssetPlayer(Context);
		return;
	}
	auto& Proxy = *static_cast<FProject_JCharacterAnimInstanceProxy*>(Context.AnimInstanceProxy);
	const auto& Data = Proxy.GetThreadSafeData();
	const auto& Shot = Data.OneShotPresentation;
	const bool bRelevantAgain = PlaybackUpdateCounter.HasEverBeenUpdated() &&
		!PlaybackUpdateCounter.WasSynchronizedCounter(Proxy.GetUpdateCounter());
	if (bRelevantAgain)
	{
		FAnimNode_BlendStack_Standalone::Reset();
		Presentation = {};
		AuthoredPoseLinkIndex = INDEX_NONE;
		bPlayingOneShot = bRootTurnEpisode = false;
		InertialReturn.Reset();
	}
	PlaybackUpdateCounter.SynchronizeWith(Proxy.GetUpdateCounter());
	const bool bWasOneShot = bPlayingOneShot;
	bPlayingOneShot = Shot.bShouldOverrideMotionMatching && Shot.SelectedAnimation != nullptr;
	if (bPlayingOneShot)
	{
		InertialReturn.Reset();
		GetEvaluateGraphExposedInputs().Execute(Context);
		const bool bNewCommand = !bWasOneShot || Presentation.SelectionRevision != Shot.SelectionRevision ||
			Presentation.SelectedAnimation != Shot.SelectedAnimation || AnimPlayers.IsEmpty();
		const bool bRootTurnCommand = Shot.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot ||
			Shot.PresentationState == EProject_JStateControllerPresentationState::TurnInPlace;
		bRootTurnEpisode = bRootTurnCommand || (!bNewCommand && bRootTurnEpisode);
		Presentation = Shot;
		if (bNewCommand)
		{
			const float EntryBlend = FMath::Max(0.f, bWasOneShot ? Shot.SelectedAnimationOutput.BlendTime : OneShotEntryBlendTime);
			BlendTo(Context, Shot.SelectedAnimation, Shot.SelectedAnimationOutput.StartTime,
				Shot.bSelectedAnimationShouldLoop, false, nullptr, EntryBlend,
				Shot.SelectedAnimationOutput.BlendProfile, EAlphaBlendOption::Linear, !bWasOneShot);
			AuthoredPoseLinkIndex = AnimPlayers.IsEmpty() ? INDEX_NONE : AnimPlayers[0].GetPoseLinkIndex();
		}
		FScopedPlaybackPresentation Playback(Proxy, &Presentation, AuthoredPoseLinkIndex);
		UE::Anim::TOptionalScopedGraphMessage<UE::Anim::FAnimInertializationSyncScope> Sync(bNewCommand && !bWasOneShot, Context);
		// No MM query or MM state update while an authored command owns playback.
		FAnimNode_BlendStack_Standalone::UpdateAssetPlayer(Context);
		return;
	}
	Presentation.bForceBlendNextUpdate = false;
	Presentation.StrafeDirectionAngle = Shot.StrafeDirectionAngle;
	bool bInterruptedReturn = false;
	if (!bWasOneShot && IsReturning() && !ProjectJ::Animation::CanOverlapAuthoredReturn(Data, Presentation, bRootTurnEpisode, Project_J::MotionMatchingCVars::ShouldUseLiveOneShotReturn()))
	{
		if (auto* Requester = Context.GetMessage<UE::Anim::IInertializationRequester>())
		{
			FInertializationRequest Request;
			Request.Duration = FMath::Max(0.f, Data.MotionMatchingSearchPolicy.TransitionBlendTime);
			Requester->RequestInertialization(Request);
			Requester->AddDebugRecord(*Context.AnimInstanceProxy, Context.GetCurrentNodeId());
			// Keep the incoming player's clock; remove only preempted outgoing samples.
			AnimPlayers.RemoveAt(1, AnimPlayers.Num() - 1, EAllowShrinking::No);
			bInterruptedReturn = true;
		}
	}
	FScopedPlaybackPresentation Playback(Proxy, Presentation.SelectedAnimation ? &Presentation : nullptr, AuthoredPoseLinkIndex);
	UE::Anim::TOptionalScopedGraphMessage<UE::Anim::FAnimInertializationSyncScope> ReturnSync(bInterruptedReturn, Context);
	// A forward landing return and its subsequent MM jumps must use one blend
	// strategy. Starting a live stack fade underneath a still-decaying inertial
	// offset changes the target twice. This envelope follows requested blend
	// durations; it does not delay a search or lock a candidate.
	if (!ProjectJ::Animation::CanContinueInertialReturn(Data, GetMotionMatchingState().SearchResult.bIsInteraction))
		InertialReturn.Cancel();
	if (bWasOneShot)
	{
		const float AuthoredReturn = Presentation.SelectedAnimationOutput.ReturnBlendTime;
		const float ReturnTime = FMath::IsFinite(AuthoredReturn) && AuthoredReturn >= 0.f ? AuthoredReturn :
			FMath::Max(0.f, Data.MotionMatchingSearchPolicy.TransitionBlendTime);
		const bool bLive = ProjectJ::Animation::CanOverlapAuthoredReturn(Data, Presentation, bRootTurnEpisode, Project_J::MotionMatchingCVars::ShouldUseLiveOneShotReturn());
		FScopedMotionMatchingReturn Settings(*this, ReturnTime, !bLive);
		SetInterruptMode(EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose);
		FAnimNode_MotionMatching::UpdateAssetPlayer(Context);
		if (!bLive && Data.MotionMatchingSearchPolicy.bUseInertialLandingInterruptReturn &&
			Presentation.PresentationState == EProject_JStateControllerPresentationState::TransitionToLand &&
			Data.MotionMatching.SelectionContext.bUseForwardCycle && AnyNewBlendToThisFrame() &&
			Context.GetMessage<UE::Anim::IInertializationRequester>())
		{
			InertialReturn.Begin(ReturnTime);
		}
	}
	else if (InertialReturn.IsActive())
	{
		// Keep the regular search interval, continuation bias and candidate set.
		// The engine handles repeated-request deficits and pose velocities.
		FScopedMotionMatchingReturn Settings(*this, InertialReturn.GetDuration(), true, false);
		FAnimNode_MotionMatching::UpdateAssetPlayer(Context);
		if (AnyNewBlendToThisFrame()) InertialReturn.OnReselection();
	}
	else
	{
		FAnimNode_MotionMatching::UpdateAssetPlayer(Context);
	}
	InertialReturn.Advance(Context.GetDeltaTime());
	if (!IsReturning() && GetAnimAsset() != Presentation.SelectedAnimation)
	{ Presentation = {}; AuthoredPoseLinkIndex = INDEX_NONE; }
}

void FAnimNode_ProjectJLocomotion::Evaluate_AnyThread(FPoseContext& Output)
{
	if (!Output.AnimInstanceProxy->GetAnimInstanceObject()->IsA<UProject_JCharacterAnimInstance>())
	{
		FAnimNode_MotionMatching::Evaluate_AnyThread(Output);
		return;
	}
	auto& Proxy = *static_cast<FProject_JCharacterAnimInstanceProxy*>(Output.AnimInstanceProxy);
	FScopedPlaybackPresentation Playback(Proxy, Presentation.SelectedAnimation ? &Presentation : nullptr, AuthoredPoseLinkIndex);
	if (bPlayingOneShot) FAnimNode_BlendStack_Standalone::Evaluate_AnyThread(Output);
	else FAnimNode_MotionMatching::Evaluate_AnyThread(Output);
}

float FAnimNode_ProjectJLocomotion::GetOutgoingWeight() const
{
	if (bPlayingOneShot || !Presentation.SelectedAnimation) return 0.f;
	float RemainingWeight = 1.f, Result = 0.f;
	for (int32 Index = 0; Index < AnimPlayers.Num(); ++Index)
	{
		const auto& Player = AnimPlayers[Index];
		const float Fraction = Index == AnimPlayers.Num() - 1 ? 1.f : Player.GetBlendInWeight();
		if (Index > 0 && Player.GetPoseLinkIndex() == AuthoredPoseLinkIndex &&
			Player.GetAnimationAsset() == Presentation.SelectedAnimation) Result += RemainingWeight * Fraction;
		RemainingWeight *= 1.f - Fraction;
	}
	return Result;
}

bool FAnimNode_ProjectJLocomotion::IsReturning() const
{
	return GetOutgoingWeight() > ZERO_ANIMWEIGHT_THRESH;
}

float FAnimNode_ProjectJLocomotion::GetOneShotSecondsRemaining() const
{
	if (!bPlayingOneShot || AnimPlayers.IsEmpty()) return -1.f;
	const auto& Player = AnimPlayers[0];
	return Player.IsLooping() ? -1.f : FMath::Max(0.f,
		(Player.GetCurrentAssetLength() - Player.GetCurrentAssetTime()) / FMath::Max(UE_SMALL_NUMBER, Player.GetPlayRate()));
}
