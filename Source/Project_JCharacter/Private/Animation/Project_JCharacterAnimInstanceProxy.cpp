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
	CapturePostSelection();
	CapturePivotDebugTrace();
}

void FProject_JCharacterAnimInstanceProxy::CapturePostSelection()
{
	// The visible Motion Matching node can live in the generated AnimBP graph or a
	// linked layer. NativeMotionMatchingNode is only the fallback graph, so prefer
	// a generated node which actually produced a Pose Search result this frame.
	const FAnimNode_MotionMatching* ResultNode = nullptr;
	const IAnimClassInterface* AnimClass = GetAnimClassInterface();
	if (AnimClass)
	{
		const TArray<FStructProperty*>& AnimNodeProperties = AnimClass->GetAnimNodeProperties();
		for (int32 NodeIndex = 0; NodeIndex < AnimNodeProperties.Num(); ++NodeIndex)
		{
			const FAnimNode_MotionMatching* Candidate = GetNodeFromIndex<FAnimNode_MotionMatching>(NodeIndex);
			if (!Candidate)
			{
				continue;
			}

			const FPoseSearchBlueprintResult& CandidateResult = Candidate->GetMotionMatchingState().SearchResult;
			if (!CandidateResult.SelectedAnim)
			{
				continue;
			}

			ResultNode = Candidate;
			// Every generated node receives the same requested database. Prefer an
			// exact match in case an inactive graph still retains an older result.
			if (CandidateResult.SelectedDatabase == CurrentActiveDatabase)
			{
				break;
			}
		}
	}

	if (!ResultNode)
	{
		ResultNode = &NativeMotionMatchingNode;
	}

	const FMotionMatchingState& State = ResultNode->GetMotionMatchingState();
	const FPoseSearchBlueprintResult& Result = State.SearchResult;
	LatestPostSelection.SelectedDatabase = Result.SelectedDatabase ? Result.SelectedDatabase->GetFName() : NAME_None;
	LatestPostSelection.SelectedAnimation = Result.SelectedAnim ? Result.SelectedAnim->GetFName() : NAME_None;
	LatestPostSelection.SelectedAnimationTime = Result.SelectedTime;
	LatestPostSelection.SelectedAnimationLength = ResultNode->GetCurrentAssetLength();
	LatestPostSelection.WantedPlayRate = Result.WantedPlayRate;
	LatestPostSelection.SearchCost = Result.SearchCost;
	LatestPostSelection.bIsContinuingPoseSearch = Result.bIsContinuingPoseSearch;
	LatestPostSelection.DatabaseTags.Reset();
	if (Result.SelectedDatabase)
	{
		LatestPostSelection.DatabaseTags = Result.SelectedDatabase->Tags;
	}
}

FFloatProperty* FindMotionMatchingFloatProperty(const TCHAR* PropertyName)
{
	return FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(), PropertyName);
}

FStructProperty* GetMotionMatchingPlayRateProperty()
{
	static FStructProperty* Property = FindFProperty<FStructProperty>(
		FAnimNode_MotionMatching::StaticStruct(),
		TEXT("PlayRate"));
	return Property;
}

void SetMotionMatchingFloatProperty(FAnimNode_MotionMatching& Node, const TCHAR* PropertyName, float Value)
{
	if (FFloatProperty* Property = FindMotionMatchingFloatProperty(PropertyName))
	{
		Property->SetPropertyValue_InContainer(&Node, Value);
	}
}

void ApplyMotionMatchingPresentationPolicy(
	FAnimNode_MotionMatching& Node,
	const FProject_JMotionMatchingSearchPolicy& Policy,
	bool bIsInAir,
	bool bWasInAir,
	float VerticalSpeed)
{
	SetMotionMatchingFloatProperty(Node, TEXT("BlendTime"), Policy.ResolveBlendTime(bIsInAir, bWasInAir, VerticalSpeed));
	SetMotionMatchingFloatProperty(Node, TEXT("NotifyRecencyTimeOut"), FMath::Max(0.0f, Policy.NotifyRecencyTimeOut));
	SetMotionMatchingFloatProperty(Node, TEXT("MaxBlendInTimeToOverrideAnimation"), FMath::Max(0.0f, Policy.MaxBlendInTimeToOverrideAnimation));
	SetMotionMatchingFloatProperty(Node, TEXT("PlayerDepthBlendInTimeMultiplier"), FMath::Max(0.1f, Policy.PlayerDepthBlendInTimeMultiplier));
	Node.SetMaxActiveBlends(FMath::Clamp(Policy.MaxActiveBlends, 1, 8));
	if (FStructProperty* PlayRateProperty = GetMotionMatchingPlayRateProperty())
	{
		if (FFloatInterval* PlayRate = PlayRateProperty->ContainerPtrToValuePtr<FFloatInterval>(&Node))
		{
			PlayRate->Min = FMath::Clamp(Policy.MinPlayRate, 0.2f, 3.0f);
			PlayRate->Max = FMath::Clamp(FMath::Max(PlayRate->Min, Policy.MaxPlayRate), 0.2f, 3.0f);
		}
	}
}

EPoseSearchInterruptMode FProject_JCharacterAnimInstanceProxy::ResolveDatabaseChangeInterruptMode() const
{
	if (!bHasMotionMatchingPolicyState)
	{
		return EPoseSearchInterruptMode::InterruptOnDatabaseChange;
	}

	const bool bIsInAir = ThreadSafeData.Air.bIsInAir;
	// Project_J's broad bIsMoving intentionally remains true while decelerating.
	// That is useful for gameplay, but GASP's Movement State must transition to
	// non-moving at Stop so the Stop PSD can interrupt the continuing Cycle pose.
	const bool bIsMoving = ThreadSafeData.LocomotionContext.bIsMotionMatchingMoving;
	const bool bMovementModeChanged = bIsInAir != bLastPolicyWasInAir;
	const bool bMovementStateChanged = bIsMoving != bLastPolicyWasMoving;
	const bool bGaitChanged = ThreadSafeData.LocomotionContext.GaitIntent != LastPolicyGaitIntent;
	const bool bLocomotionStanceChanged =
		ThreadSafeData.Combat.bIsCombatMode != bLastPolicyWasCombat ||
		ThreadSafeData.LocomotionContext.RotationMode != LastPolicyRotationMode;

	// GASP Get_MMInterruptMode: default to DoNotInterrupt and only interrupt on
	// a core locomotion change. Project_J has no separate stance enum yet, so
	// combat stance and rotation family are its safe equivalent.
	const bool bInterrupt = bMovementModeChanged ||
		(!bIsInAir && (bMovementStateChanged || (!bIsMoving && bGaitChanged) || bLocomotionStanceChanged));
	return bInterrupt
		? EPoseSearchInterruptMode::InterruptOnDatabaseChange
		: EPoseSearchInterruptMode::DoNotInterrupt;
}

void FProject_JCharacterAnimInstanceProxy::CacheMotionMatchingPolicyState()
{
	bHasMotionMatchingPolicyState = true;
	bLastPolicyWasInAir = ThreadSafeData.Air.bIsInAir;
	bLastPolicyWasMoving = ThreadSafeData.LocomotionContext.bIsMotionMatchingMoving;
	bLastPolicyWasCombat = ThreadSafeData.Combat.bIsCombatMode;
	LastPolicyGaitIntent = ThreadSafeData.LocomotionContext.GaitIntent;
	LastPolicyRotationMode = ThreadSafeData.LocomotionContext.RotationMode;
}

FString FProject_JCharacterAnimInstanceProxy::GetPivotTraceSummary() const
{
	FString Summary = FString::Printf(TEXT("==== Motion Matching Pivot / One-Shot BlendStack Trace (%d entries) ====\n"), PivotDebugTrace.Num());
	for (const FProject_JMotionMatchingPivotTraceEntry& Entry : PivotDebugTrace)
	{
		Summary += FString::Printf(
			TEXT("Frame=%llu Phase=%d RequestedPSD=%s NativePSD=%s Anim=%s AssetTime=%.3f SearchElapsed=%.3f Cost=%.3f PlayRate=%.2f Interrupt=%d Continuing=%s NewBlend=%s Players=%d\n"),
			Entry.FrameNumber,
			static_cast<int32>(Entry.PhaseFamily),
			*Entry.RequestedDatabase.ToString(),
			*Entry.NativeSelectedDatabase.ToString(),
			*Entry.SelectedAnimation.ToString(),
			Entry.SelectedAnimationTime,
			Entry.ElapsedPoseSearchTime,
			Entry.SearchCost,
			Entry.WantedPlayRate,
			static_cast<int32>(Entry.AppliedInterruptMode),
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
	ApplyMotionMatchingPresentationPolicy(
		NativeMotionMatchingNode,
		ThreadSafeData.MotionMatchingSearchPolicy,
		ThreadSafeData.Air.bIsInAir,
		bLastPolicyWasInAir,
		ThreadSafeData.Movement.VerticalSpeed);
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
		ApplyMotionMatchingPresentationPolicy(
			*const_cast<FAnimNode_MotionMatching*>(MotionMatchingNode),
			ThreadSafeData.MotionMatchingSearchPolicy,
			ThreadSafeData.Air.bIsInAir,
			bLastPolicyWasInAir,
			ThreadSafeData.Movement.VerticalSpeed);

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

	CacheMotionMatchingPolicyState();
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
	const bool bCapturePivot = Project_J::MotionMatchingCVars::ShouldCapturePivotDebugTrace();
	const bool bCaptureTransition = Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace();
	if (!bCapturePivot && !bCaptureTransition)
	{
		bWasPivotPhaseForDebug = false;
		return;
	}

	const bool bIsPivotPhase = ThreadSafeData.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot;
	const EProject_JLocomotionPhaseFamily Phase = ThreadSafeData.LocomotionContext.PhaseFamily;
	const bool bIsTransitionPhase =
		Phase == EProject_JLocomotionPhaseFamily::Start ||
		Phase == EProject_JLocomotionPhaseFamily::Stop ||
		Phase == EProject_JLocomotionPhaseFamily::JumpStart ||
		Phase == EProject_JLocomotionPhaseFamily::Landing ||
		Phase == EProject_JLocomotionPhaseFamily::Pivot;
	const bool bShouldCapturePhase = bCapturePivot ? bIsPivotPhase : bIsTransitionPhase;

	// Match CapturePostSelection: the displayed graph's generated node is the
	// authoritative source for selection and Blend Stack diagnostics.
	const FAnimNode_MotionMatching* ResultNode = nullptr;
	if (const IAnimClassInterface* AnimClass = GetAnimClassInterface())
	{
		const TArray<FStructProperty*>& AnimNodeProperties = AnimClass->GetAnimNodeProperties();
		for (int32 NodeIndex = 0; NodeIndex < AnimNodeProperties.Num(); ++NodeIndex)
		{
			const FAnimNode_MotionMatching* Candidate = GetNodeFromIndex<FAnimNode_MotionMatching>(NodeIndex);
			if (!Candidate || !Candidate->GetMotionMatchingState().SearchResult.SelectedAnim)
			{
				continue;
			}

			ResultNode = Candidate;
			if (Candidate->GetMotionMatchingState().SearchResult.SelectedDatabase == CurrentActiveDatabase)
			{
				break;
			}
		}
	}
	if (!ResultNode)
	{
		ResultNode = &NativeMotionMatchingNode;
	}

	const bool bNewBlendThisFrame = ResultNode->AnyNewBlendToThisFrame();
	if (!bShouldCapturePhase && !bWasPivotPhaseForDebug && !bNewBlendThisFrame)
	{
		return;
	}

	const FMotionMatchingState& State = ResultNode->GetMotionMatchingState();
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
	Entry.AppliedInterruptMode = LastResolvedDatabaseChangeInterruptMode;
	Entry.bContinuingPoseSearch = SearchResult.bIsContinuingPoseSearch;
	Entry.bNewBlendThisFrame = bNewBlendThisFrame;

	for (const FBlendStackAnimPlayer& Player : ResultNode->AnimPlayers)
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

	// The AnimGraph can update more than once per rendered frame. Keep enough
	// transition samples to retain one complete Start -> Cycle -> Stop pass,
	// rather than only the last landing/air transition in a normal PIE run.
	constexpr int32 MaxPivotTraceEntries = 720;
	if (PivotDebugTrace.Num() > MaxPivotTraceEntries)
	{
		PivotDebugTrace.RemoveAt(0, PivotDebugTrace.Num() - MaxPivotTraceEntries, EAllowShrinking::No);
	}
	bWasPivotPhaseForDebug = bShouldCapturePhase;
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
	const EPoseSearchInterruptMode DatabaseChangeInterruptMode = ResolveDatabaseChangeInterruptMode();
	LastResolvedDatabaseChangeInterruptMode = DatabaseChangeInterruptMode;
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
			DatabaseChangeInterruptMode);
		AppliedDatabase = CurrentActiveDatabase;
	}

	// The visible AnimBP Motion Matching node may be the active graph in a linked
	// layer. Push the same C++-owned selection into every generated MM node so it
	// does not depend on an Anim Node Function mutating the database every update.
	const IAnimClassInterface* AnimClass = GetAnimClassInterface();
	if (!AnimClass)
	{
		CacheMotionMatchingPolicyState();
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
			DatabaseChangeInterruptMode);
		AppliedGeneratedDatabases.Add(NodeIndex, CurrentActiveDatabase);
	}
}
