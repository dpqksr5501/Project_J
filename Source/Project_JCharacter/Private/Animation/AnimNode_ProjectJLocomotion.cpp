#include "Animation/AnimNode_ProjectJLocomotion.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/AnimInertializationSyncScope.h"
#include "Animation/AnimNode_Inertialization.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/UnrealType.h"
#include "HAL/IConsoleManager.h"

namespace
{
bool CanOverlapReturn(const FProject_JAnimThreadSafeData& Data,
	const FProject_JAnimOneShotPresentationThreadSafeData& Presentation, bool bRootTurnEpisode)
{
	static const auto* LiveCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ProjectJ.LiveOneShotReturn"));
	using P = EProject_JStateControllerPresentationState;
	const bool bEligible = (Presentation.PresentationState == P::TransitionToLocomotion &&
		Presentation.PhaseFamily != EProject_JLocomotionPhaseFamily::Pivot) ||
		Presentation.PresentationState == P::TransitionToIdle || Presentation.PresentationState == P::TransitionToLand;
	return bEligible && (!LiveCVar || LiveCVar->GetInt() != 0) && !bRootTurnEpisode && Data.MotionMatchingSearchPolicy.bEnableLiveOneShotReturn &&
		Data.LocomotionMode == EProject_JAnimationLocomotionMode::OnFoot && !Data.Air.bIsInAir &&
		!Data.Combat.bIsAttacking && !Data.Combat.bIsDodging && !Data.Combat.bIsHitReacting &&
		Data.ProceduralIK.FullBodyMontageWeight <= UE_SMALL_NUMBER &&
		Data.LocomotionContext.RotationMode == Presentation.RotationMode;
}
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
// The engine's MM relevance counter is not updated during direct playback.
// Suppress its return-time stack reset; this node tracks actual graph relevance.
class FScopedMMReturnSettings
{
public:
	FScopedMMReturnSettings(FAnimNode_MotionMatching& InNode, float ReturnTime, bool bInertial)
		: Node(InNode)
	{
		Reset = FindFProperty<FBoolProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("bResetOnBecomingRelevant"));
		Inertial = FindFProperty<FBoolProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("bUseInertialBlend"));
		Blend = FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("BlendTime"));
		Throttle = FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("SearchThrottleTime"));
		check(Reset && Inertial && Blend && Throttle);
		bReset = Reset->GetPropertyValue_InContainer(&Node);
		bOldInertial = Inertial->GetPropertyValue_InContainer(&Node);
		OldBlend = Blend->GetPropertyValue_InContainer(&Node);
		OldThrottle = Throttle->GetPropertyValue_InContainer(&Node);
		Reset->SetPropertyValue_InContainer(&Node, false);
		Inertial->SetPropertyValue_InContainer(&Node, bInertial);
		Blend->SetPropertyValue_InContainer(&Node, ReturnTime);
		Throttle->SetPropertyValue_InContainer(&Node, 0.f);
	}
	~FScopedMMReturnSettings()
	{
		Reset->SetPropertyValue_InContainer(&Node, bReset);
		Inertial->SetPropertyValue_InContainer(&Node, bOldInertial);
		Blend->SetPropertyValue_InContainer(&Node, OldBlend);
		Throttle->SetPropertyValue_InContainer(&Node, OldThrottle);
	}
private:
	FAnimNode_MotionMatching& Node;
	FBoolProperty* Reset = nullptr;
	FBoolProperty* Inertial = nullptr;
	FFloatProperty* Blend = nullptr;
	FFloatProperty* Throttle = nullptr;
	bool bReset = false, bOldInertial = false;
	float OldBlend = 0.f, OldThrottle = 0.f;
};
}

void FAnimNode_ProjectJLocomotion::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Presentation = {};
	AuthoredPoseLinkIndex = INDEX_NONE;
	bPlayingOneShot = bRootTurnEpisode = false;
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
	}
	PlaybackUpdateCounter.SynchronizeWith(Proxy.GetUpdateCounter());
	const bool bWasOneShot = bPlayingOneShot;
	bPlayingOneShot = Shot.bShouldOverrideMotionMatching && Shot.SelectedAnimation != nullptr;
	if (bPlayingOneShot)
	{
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
	if (!bWasOneShot && IsReturning() && !CanOverlapReturn(Data, Presentation, bRootTurnEpisode))
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
	if (bWasOneShot)
	{
		const float AuthoredReturn = Presentation.SelectedAnimationOutput.ReturnBlendTime;
		const float ReturnTime = FMath::IsFinite(AuthoredReturn) && AuthoredReturn >= 0.f ? AuthoredReturn :
			FMath::Max(0.f, Data.MotionMatchingSearchPolicy.TransitionBlendTime);
		const bool bLive = CanOverlapReturn(Data, Presentation, bRootTurnEpisode);
		FScopedMMReturnSettings Settings(*this, ReturnTime, !bLive);
		SetInterruptMode(EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose);
		FAnimNode_MotionMatching::UpdateAssetPlayer(Context);
	}
	else
	{
		FAnimNode_MotionMatching::UpdateAssetPlayer(Context);
	}
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
