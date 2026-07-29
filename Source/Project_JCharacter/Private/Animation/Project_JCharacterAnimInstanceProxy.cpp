#include "Animation/Project_JCharacterAnimInstanceProxy.h"

#include "Animation/AnimClassInterface.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JMotionMatchingCVars.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "UObject/UnrealType.h"

namespace
{
FFloatProperty* GetMotionMatchingSearchThrottleTimeProperty()
{
	static FFloatProperty* Property = FindFProperty<FFloatProperty>(
		FAnimNode_MotionMatching::StaticStruct(),
		TEXT("SearchThrottleTime"));
	return Property;
}

void SetMotionMatchingSearchThrottleTime(FAnimNode_MotionMatching& Node, float SearchThrottleTime)
{
	if (FFloatProperty* Property = GetMotionMatchingSearchThrottleTimeProperty())
	{
		Property->SetPropertyValue_InContainer(&Node, SearchThrottleTime);
	}
}

float GetMotionMatchingSearchThrottleTime(const FAnimNode_MotionMatching& Node)
{
	if (const FFloatProperty* Property = GetMotionMatchingSearchThrottleTimeProperty())
	{
		return Property->GetPropertyValue_InContainer(&Node);
	}

	return 0.0f;
}

}

FProject_JCharacterAnimInstanceProxy::FProject_JCharacterAnimInstanceProxy()
{
	LinkNativeGraph();
}

FProject_JCharacterAnimInstanceProxy::FProject_JCharacterAnimInstanceProxy(UAnimInstance* InAnimInstance)
	: FAnimInstanceProxy(InAnimInstance)
{
	LinkNativeGraph();
}

void FProject_JCharacterAnimInstanceProxy::QueueGameThreadData(
	const FProject_JAnimThreadSafeData& InData,
	UPoseSearchDatabase* InSelectedDatabase,
	bool bInMotionMatchingEnabled,
	bool bInUpdateMotionMatchingThisFrame,
	bool bInForceMotionMatchingReselect)
{
	PendingGameThreadData = InData;
	CurrentActiveDatabase = InSelectedDatabase;
	bMotionMatchingEnabled = bInMotionMatchingEnabled;
	bUpdateMotionMatchingThisFrame = bInUpdateMotionMatchingThisFrame;
	bForceMotionMatchingReselect = bInForceMotionMatchingReselect;
}

void FProject_JCharacterAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	ThreadSafeData = PendingGameThreadData;
	ThreadSafeData.DeltaTime = DeltaSeconds;
}

void FProject_JCharacterAnimInstanceProxy::UpdateAnimationNode_WithRoot(
	const FAnimationUpdateContext& InContext,
	FAnimNode_Base* InRootNode,
	FName InLayerName)
{
	NativePoseHistoryNode.TransformTrajectory = ThreadSafeData.Movement.Trajectory;

	if (bUpdateMotionMatchingThisFrame || !bMotionMatchingEnabled)
	{
		ApplySelectedDatabaseToNativeNode();
	}
	if (bForceMotionMatchingReselect && bMotionMatchingEnabled && CurrentActiveDatabase)
	{
		ForceReselectMotionMatchingNodes();
	}

	ApplyMotionMatchingSearchPolicy();
	FAnimInstanceProxy::UpdateAnimationNode_WithRoot(InContext, InRootNode, InLayerName);
	CapturePivotDebugTrace();
}

FString FProject_JCharacterAnimInstanceProxy::GetPivotTraceSummary() const
{
	FString Summary = FString::Printf(TEXT("==== Motion Matching Pivot / BlendStack Trace (%d entries) ====\n"), PivotDebugTrace.Num());
	for (const FProject_JMotionMatchingPivotTraceEntry& Entry : PivotDebugTrace)
	{
		Summary += FString::Printf(
			TEXT("Frame=%llu Phase=%d RequestedPSD=%s NativePSD=%s Anim=%s AssetTime=%.3f SearchElapsed=%.3f Cost=%.3f PlayRate=%.2f Continuing=%s NewBlend=%s Players=%d\n"),
			Entry.FrameNumber,
			static_cast<int32>(Entry.PhaseFamily),
			*Entry.RequestedDatabase.ToString(),
			*Entry.NativeSelectedDatabase.ToString(),
			*Entry.SelectedAnimation.ToString(),
			Entry.SelectedAnimationTime,
			Entry.ElapsedPoseSearchTime,
			Entry.SearchCost,
			Entry.WantedPlayRate,
			Entry.bContinuingPoseSearch ? TEXT("true") : TEXT("false"),
			Entry.bNewBlendThisFrame ? TEXT("true") : TEXT("false"),
			Entry.BlendPlayers.Num());

		for (int32 PlayerIndex = 0; PlayerIndex < Entry.BlendPlayers.Num(); ++PlayerIndex)
		{
			const FProject_JMotionMatchingBlendStackPlayerDebug& Player = Entry.BlendPlayers[PlayerIndex];
			Summary += FString::Printf(
				TEXT("  Stack[%d] Anim=%s AssetTime=%.3f/%.3f Rate=%.2f Weight=%.3f Blend=%.3f/%.3f Active=%s Loop=%s Mirror=%s\n"),
				PlayerIndex,
				*Player.Animation.ToString(),
				Player.AssetTime,
				Player.AssetLength,
				Player.PlayRate,
				Player.BlendWeight,
				Player.BlendElapsed,
				Player.BlendTime,
				Player.bActive ? TEXT("true") : TEXT("false"),
				Player.bLooping ? TEXT("true") : TEXT("false"),
				Player.bMirrored ? TEXT("true") : TEXT("false"));
		}
	}
	return Summary;
}

void FProject_JCharacterAnimInstanceProxy::ApplyMotionMatchingSearchPolicy()
{
	if (!bHasNativeDefaultSearchThrottleTime)
	{
		NativeDefaultSearchThrottleTime = GetMotionMatchingSearchThrottleTime(NativeMotionMatchingNode);
		bHasNativeDefaultSearchThrottleTime = true;
	}
	const bool bNativeDatabaseChanged =
		NativeMotionMatchingNode.GetMotionMatchingState().SearchResult.SelectedDatabase != CurrentActiveDatabase;
	SetMotionMatchingSearchThrottleTime(
		NativeMotionMatchingNode,
		ThreadSafeData.MotionMatchingSearchPolicy.ResolveSearchThrottleTime(
			ThreadSafeData.LocomotionContext.PhaseFamily,
			ThreadSafeData.Air.bIsFallOffStart,
			NativeDefaultSearchThrottleTime,
			bNativeDatabaseChanged));

	const IAnimClassInterface* AnimClass = GetAnimClassInterface();
	if (!AnimClass)
	{
		return;
	}

	const TArray<FStructProperty*>& AnimNodeProperties = AnimClass->GetAnimNodeProperties();
	for (int32 NodeIndex = 0; NodeIndex < AnimNodeProperties.Num(); ++NodeIndex)
	{
		const FAnimNode_MotionMatching* MotionMatchingNode =
			GetNodeFromIndex<FAnimNode_MotionMatching>(NodeIndex);
		if (!MotionMatchingNode)
		{
			continue;
		}

		float* DefaultSearchThrottleTime = DefaultSearchThrottleTimes.Find(NodeIndex);
		if (!DefaultSearchThrottleTime)
		{
			DefaultSearchThrottleTime = &DefaultSearchThrottleTimes.Add(
				NodeIndex,
				GetMotionMatchingSearchThrottleTime(*MotionMatchingNode));
		}
		const bool bDatabaseChanged =
			MotionMatchingNode->GetMotionMatchingState().SearchResult.SelectedDatabase != CurrentActiveDatabase;
		SetMotionMatchingSearchThrottleTime(
			*const_cast<FAnimNode_MotionMatching*>(MotionMatchingNode),
			ThreadSafeData.MotionMatchingSearchPolicy.ResolveSearchThrottleTime(
				ThreadSafeData.LocomotionContext.PhaseFamily,
				ThreadSafeData.Air.bIsFallOffStart,
				*DefaultSearchThrottleTime,
				bDatabaseChanged));
	}
}

void FProject_JCharacterAnimInstanceProxy::ForceReselectMotionMatchingNodes()
{
	const EPoseSearchInterruptMode InterruptMode =
		EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose;
	NativeMotionMatchingNode.SetInterruptMode(InterruptMode);

	const IAnimClassInterface* AnimClass = GetAnimClassInterface();
	if (!AnimClass)
	{
		return;
	}

	const TArray<FStructProperty*>& AnimNodeProperties = AnimClass->GetAnimNodeProperties();
	for (int32 NodeIndex = 0; NodeIndex < AnimNodeProperties.Num(); ++NodeIndex)
	{
		if (FAnimNode_MotionMatching* MotionMatchingNode =
			const_cast<FAnimNode_MotionMatching*>(GetNodeFromIndex<FAnimNode_MotionMatching>(NodeIndex)))
		{
			MotionMatchingNode->SetInterruptMode(InterruptMode);
		}
	}
}

void FProject_JCharacterAnimInstanceProxy::CapturePivotDebugTrace()
{
	if (!Project_J::MotionMatchingCVars::ShouldCapturePivotDebugTrace())
	{
		bWasPivotPhaseForDebug = false;
		return;
	}

	const bool bIsPivotPhase = ThreadSafeData.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot;
	const bool bNewBlendThisFrame = NativeMotionMatchingNode.AnyNewBlendToThisFrame();
	if (!bIsPivotPhase && !bWasPivotPhaseForDebug && !bNewBlendThisFrame)
	{
		return;
	}

	const FMotionMatchingState& State = NativeMotionMatchingNode.GetMotionMatchingState();
	const FPoseSearchBlueprintResult& SearchResult = State.SearchResult;
	FProject_JMotionMatchingPivotTraceEntry& Entry = PivotDebugTrace.AddDefaulted_GetRef();
	Entry.FrameNumber = GFrameCounter;
	Entry.PhaseFamily = ThreadSafeData.LocomotionContext.PhaseFamily;
	Entry.RequestedDatabase = CurrentActiveDatabase ? CurrentActiveDatabase->GetFName() : NAME_None;
	Entry.NativeSelectedDatabase = SearchResult.SelectedDatabase ? SearchResult.SelectedDatabase->GetFName() : NAME_None;
	Entry.SelectedAnimation = SearchResult.SelectedAnim ? SearchResult.SelectedAnim->GetFName() : NAME_None;
	Entry.SelectedAnimationTime = SearchResult.SelectedTime;
	Entry.SearchCost = SearchResult.SearchCost;
	Entry.WantedPlayRate = SearchResult.WantedPlayRate;
	Entry.ElapsedPoseSearchTime = State.ElapsedPoseSearchTime;
	Entry.bContinuingPoseSearch = SearchResult.bIsContinuingPoseSearch;
	Entry.bNewBlendThisFrame = bNewBlendThisFrame;

	for (const FBlendStackAnimPlayer& Player : NativeMotionMatchingNode.AnimPlayers)
	{
		FProject_JMotionMatchingBlendStackPlayerDebug& PlayerEntry = Entry.BlendPlayers.AddDefaulted_GetRef();
		if (const UAnimationAsset* Animation = Player.GetAnimationAsset())
		{
			PlayerEntry.Animation = Animation->GetFName();
		}
		PlayerEntry.AssetTime = Player.GetCurrentAssetTime();
		PlayerEntry.AssetLength = Player.GetCurrentAssetLength();
		PlayerEntry.PlayRate = Player.GetPlayRate();
		PlayerEntry.BlendWeight = Player.GetBlendInWeight();
		PlayerEntry.BlendTime = Player.GetTotalBlendInTime();
		PlayerEntry.BlendElapsed = Player.GetCurrentBlendInTime();
		PlayerEntry.bActive = Player.IsActive();
		PlayerEntry.bLooping = Player.IsLooping();
		PlayerEntry.bMirrored = Player.GetMirror();
	}

	constexpr int32 MaxPivotTraceEntries = 180;
	if (PivotDebugTrace.Num() > MaxPivotTraceEntries)
	{
		PivotDebugTrace.RemoveAt(0, PivotDebugTrace.Num() - MaxPivotTraceEntries, EAllowShrinking::No);
	}
	bWasPivotPhaseForDebug = bIsPivotPhase;
}

FAnimNode_Base* FProject_JCharacterAnimInstanceProxy::GetCustomRootNode()
{
	LinkNativeGraph();
	return &NativePoseHistoryNode;
}

void FProject_JCharacterAnimInstanceProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	LinkNativeGraph();
	OutNodes.Add(&NativePoseHistoryNode);
	OutNodes.Add(&NativeMotionMatchingNode);
}

void FProject_JCharacterAnimInstanceProxy::LinkNativeGraph()
{
	NativePoseHistoryNode.Source.SetLinkNode(&NativeMotionMatchingNode);
	NativePoseHistoryNode.bGenerateTrajectory = false;
	NativePoseHistoryNode.PoseCount = 10;
	NativePoseHistoryNode.SamplingInterval = 0.04f;
	NativePoseHistoryNode.TrajectorySpeedMultiplier = 1.0f;
}

void FProject_JCharacterAnimInstanceProxy::ApplySelectedDatabaseToNativeNode()
{
	if (!bMotionMatchingEnabled || !CurrentActiveDatabase)
	{
		if (AppliedDatabase)
		{
			NativeMotionMatchingNode.ResetDatabasesToSearch(
				EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose);
			AppliedDatabase = nullptr;
		}

		const IAnimClassInterface* AnimClass = GetAnimClassInterface();
		if (AnimClass)
		{
			const TArray<FStructProperty*>& AnimNodeProperties = AnimClass->GetAnimNodeProperties();
			for (int32 NodeIndex = 0; NodeIndex < AnimNodeProperties.Num(); ++NodeIndex)
			{
				if (FAnimNode_MotionMatching* MotionMatchingNode =
					const_cast<FAnimNode_MotionMatching*>(GetNodeFromIndex<FAnimNode_MotionMatching>(NodeIndex)))
				{
					if (AppliedGeneratedDatabases.Contains(NodeIndex))
					{
						MotionMatchingNode->ResetDatabasesToSearch(
							EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose);
						AppliedGeneratedDatabases.Remove(NodeIndex);
					}
				}
			}
		}
		return;
	}

	if (AppliedDatabase != CurrentActiveDatabase)
	{
		NativeMotionMatchingNode.SetDatabaseToSearch(
			CurrentActiveDatabase.Get(),
			EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose);
		AppliedDatabase = CurrentActiveDatabase;
	}

	// The visible AnimBP Motion Matching node may be the active graph in a linked
	// layer. Push the same C++-owned selection into every generated MM node so it
	// does not depend on an Anim Node Function mutating the database every update.
	const IAnimClassInterface* AnimClass = GetAnimClassInterface();
	if (!AnimClass)
	{
		return;
	}

	const TArray<FStructProperty*>& AnimNodeProperties = AnimClass->GetAnimNodeProperties();
	for (int32 NodeIndex = 0; NodeIndex < AnimNodeProperties.Num(); ++NodeIndex)
	{
		FAnimNode_MotionMatching* MotionMatchingNode =
			const_cast<FAnimNode_MotionMatching*>(GetNodeFromIndex<FAnimNode_MotionMatching>(NodeIndex));
		if (!MotionMatchingNode || AppliedGeneratedDatabases.FindRef(NodeIndex) == CurrentActiveDatabase)
		{
			continue;
		}

		MotionMatchingNode->SetDatabaseToSearch(
			CurrentActiveDatabase.Get(),
			EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose);
		AppliedGeneratedDatabases.Add(NodeIndex, CurrentActiveDatabase);
	}
}
