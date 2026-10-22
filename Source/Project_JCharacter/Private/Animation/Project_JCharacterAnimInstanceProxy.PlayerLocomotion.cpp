#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/AnimClassInterface.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "Animation/AnimationAsset.h"

const FProject_JAnimPlaybackSnapshot& FProject_JCharacterAnimInstanceProxy::GetLatestPostSelection() const
{
 check(IsInGameThread());
 if (CapturedPlaybackRevision!=PlaybackUpdateRevision)
 {
  CapturePlayerLocomotionPostSelection();
  CapturedPlaybackRevision=PlaybackUpdateRevision;
 }
 return LatestPostSelection;
}

void FProject_JCharacterAnimInstanceProxy::CapturePlayerLocomotionPostSelection() const
{
	const auto* Interface = GetAnimClassInterface();
	const FAnimNode_BlendStack_Standalone* Selected = nullptr;
	int32 SelectedIndex = INDEX_NONE;
	if (Interface)
	{
		for (int32 Index = 0; Index < Interface->GetAnimNodeProperties().Num(); ++Index)
		{
			const auto* Candidate = GetNodeFromIndex<FAnimNode_BlendStack_Standalone>(Index);
			if (!Candidate || !Candidate->GetAnimAsset() || Candidate->GetCachedBlendWeight() <= ZERO_ANIMWEIGHT_THRESH) continue;
			if (!Selected || Candidate->GetCachedBlendWeight() > Selected->GetCachedBlendWeight())
			{
				Selected = Candidate;
				SelectedIndex = Index;
			}
		}
	}
	if (!Selected) { LatestPostSelection = {}; return; }
	const FName Animation = GetFNameSafe(Selected->GetAnimAsset());
	const bool bChanged = LatestPostSelection.ProducerNodeIndex != SelectedIndex || LatestPostSelection.SelectedAnimation != Animation;
	LatestPostSelection.ProducerNodeIndex = SelectedIndex;
	LatestPostSelection.CaptureFrame = PlaybackUpdateFrame;
	LatestPostSelection.CachedNodeWeight = Selected->GetCachedBlendWeight();
	LatestPostSelection.SelectedAnimation = Animation;
	LatestPostSelection.SelectedAnimationTime = Selected->GetCurrentAssetTime();
	LatestPostSelection.SelectedAnimationLength = Selected->GetCurrentAssetLength();
	LatestPostSelection.WantedPlayRate = Selected->AnimPlayers.IsEmpty() ? 1.f : Selected->AnimPlayers[0].GetPlayRate();
	LatestPostSelection.bNewBlend = Selected->AnyNewBlendToThisFrame();
	LatestPostSelection.bResultChangedSincePreviousCapture = bChanged;
	if (bChanged) LatestPostSelection.ResultLastChangedFrame = PlaybackUpdateFrame;
}
