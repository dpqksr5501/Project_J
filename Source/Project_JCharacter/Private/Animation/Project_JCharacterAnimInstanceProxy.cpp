#include "Animation/Project_JCharacterAnimInstanceProxy.h"

#include "Animation/AnimClassInterface.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JMotionMatchingCVars.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "Project_JPlayerCharacter.h"
#include "UObject/UnrealType.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include <limits>

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

FMotionMatchingState* GetMutableMotionMatchingState(FAnimNode_MotionMatching& Node)
{
	// UE 5.8 exposes the state for reading but no search-completion counter. Use
	// its reflected transient struct, without depending on private C++ offsets.
	static FStructProperty* Property = FindFProperty<FStructProperty>(
		FAnimNode_MotionMatching::StaticStruct(), TEXT("MotionMatchingState"));
	return Property ? Property->ContainerPtrToValuePtr<FMotionMatchingState>(&Node) : nullptr;
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

void FProject_JCharacterAnimInstanceProxy::SetFlowTraceEnabled(bool bEnabled)
{
	if (bFlowTraceEnabled != bEnabled) { FlowTraceWork = {}; }
	bFlowTraceEnabled = bEnabled;
}

void FProject_JCharacterAnimInstanceProxy::QueueGameThreadData(
	const FProject_JAnimThreadSafeData& InData,
	UPoseSearchDatabase* InSelectedDatabase,
	bool bInMotionMatchingEnabled,
	bool bInUpdateMotionMatchingThisFrame,
	bool bInForceMotionMatchingReselect,
	bool bInFreshSnapshot,
	UPoseSearchDatabase* InTurnCycleCompanion)
{
	// A draw/sheathe montage can hide the moment input is released. If Cycle and
	// Idle resolve to the same PSD, changing its search context alone does not
	// retire the continuing walk pose. Interrupt it once at the Idle edge even
	// when the database pointer remains unchanged.
	const bool bEnteredGroundIdle =
		bMotionMatchingEnabled && bInMotionMatchingEnabled &&
		!InData.Air.bIsInAir &&
		!InData.LocomotionContext.bIsMotionMatchingMoving &&
		InData.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Idle &&
		(PendingGameThreadData.LocomotionContext.bIsMotionMatchingMoving ||
			PendingGameThreadData.LocomotionContext.PhaseFamily != EProject_JLocomotionPhaseFamily::Idle);
	const bool bReturnedFromOneShot = bMotionMatchingEnabled && bInMotionMatchingEnabled &&
		PendingGameThreadData.OneShotPresentation.bShouldOverrideMotionMatching &&
		!InData.OneShotPresentation.bShouldOverrideMotionMatching;
	const bool bNewForceRequest = bInFreshSnapshot && bInForceMotionMatchingReselect &&
		(!bLastPublishedForceReselect ||
			InData.MotionMatching.SelectionRevision != PendingGameThreadData.MotionMatching.SelectionRevision);
	const bool bCompletedTurnReturn = PendingGameThreadData.MotionMatching.SelectionContext.bMovingTurn180 &&
		InData.MotionMatching.SelectionContext.bAllowTurnContinuation &&
		CurrentTurnCycleCompanion && InSelectedDatabase == CurrentTurnCycleCompanion &&
		PendingGameThreadData.LocomotionContext.RotationMode == InData.LocomotionContext.RotationMode &&
		PendingGameThreadData.LocomotionContext.GaitIntent == InData.LocomotionContext.GaitIntent &&
		InData.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Cycle &&
		InData.LocomotionContext.bIsMotionMatchingMoving && !InData.Air.bIsInAir &&
		!InData.OneShotPresentation.bShouldOverrideMotionMatching;
	const bool bContinuationCancelled = PendingGameThreadData.MotionMatching.SelectionContext.bAllowTurnContinuation &&
		!InData.MotionMatching.SelectionContext.bAllowTurnContinuation;
	const bool bSelectedDatabaseChanged = bInUpdateMotionMatchingThisFrame && !bCompletedTurnReturn &&
		((InSelectedDatabase && InSelectedDatabase != CurrentActiveDatabase) ||
			(InTurnCycleCompanion && InTurnCycleCompanion != CurrentTurnCycleCompanion));
	if (!bInMotionMatchingEnabled || InData.LocomotionMode != EProject_JAnimationLocomotionMode::OnFoot)
	{
		ClearMotionMatchingReselects();
	}
	else if (bNewForceRequest || bEnteredGroundIdle || bReturnedFromOneShot || bSelectedDatabaseChanged || bContinuationCancelled)
	{
		if (!bForceMotionMatchingReselect) bRetireTurnContinuingPose = false;
		// Coalesce against the newest context. A later GT frame must not discard
		// an unconsumed return's pose-history query requirement.
		bReselectFromPoseHistory = bReturnedFromOneShot ||
			(bForceMotionMatchingReselect && bReselectFromPoseHistory);
		PendingReselectRevision = ++ReselectSerial;
		bForceMotionMatchingReselect = true;
		bRetireTurnContinuingPose |= bContinuationCancelled;
	}
	PendingGameThreadData = InData;
	++PublishedSnapshotRevision;
	if (bFlowTraceEnabled) { ++FlowTraceWork.QueuedSnapshot; }
	CurrentActiveDatabase = InSelectedDatabase;
	CurrentTurnCycleCompanion = InTurnCycleCompanion != InSelectedDatabase ? InTurnCycleCompanion : nullptr;
	bMotionMatchingEnabled = bInMotionMatchingEnabled;
	bUpdateMotionMatchingThisFrame = bInUpdateMotionMatchingThisFrame;
	// Skipped hidden samples are not new producer events. Retain the last pulse
	// identity so an old snapshot cannot re-arm the same request after a skip.
	if (bInFreshSnapshot)
	{
		bLastPublishedForceReselect = bInForceMotionMatchingReselect;
	}
}

void FProject_JCharacterAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	if (bHasNativeDefaultSearchThrottleTime)
	{
		SetMotionMatchingSearchThrottleTime(NativeMotionMatchingNode, NativeDefaultSearchThrottleTime);
	}
	FAnimInstanceProxy::Initialize(InAnimInstance);
	PendingGameThreadData = FProject_JAnimThreadSafeData();
	ThreadSafeData = FProject_JAnimThreadSafeData();
	PublishedSnapshotRevision = ConsumedSnapshotRevision = 0;
	ThreadSafeCandidateCount = 0;
	ClearMotionMatchingReselects();
	bLastPublishedForceReselect = false;
	CachedGeneratedMotionMatchingAnimClass = nullptr;
	CachedGeneratedMotionMatchingNodeIndices.Reset();
	AppliedGeneratedDatabases.Reset();
	AppliedGeneratedCompanions.Reset();
	DefaultSearchThrottleTimes.Reset();
	bHasNativeDefaultSearchThrottleTime = false;
	AppliedDatabase = nullptr;
	CurrentActiveDatabase = nullptr;
	CurrentTurnCycleCompanion = nullptr;
	AppliedTurnCycleCompanion = nullptr;
	bHasMotionMatchingPolicyState = false;
	bMotionMatchingEnabled = true;
	bUpdateMotionMatchingThisFrame = true;
	bLastPolicyWasInAir = false;
	bLastPolicyWasMoving = false;
	bLastPolicyWasCombat = false;
	bLastPolicyUsedSettledCycle = false;
	bWasPivotPhaseForDebug = false;
	PivotDebugTrace.Reset();
	LatestPostSelection = FProject_JAnimMotionMatchingPostSelectionData();
	FlowTraceWork = {};
	bFlowTraceEnabled = false;
	bUpdatingMotionMatchingGraph = false;
}

void FProject_JCharacterAnimInstanceProxy::ConsumeQueuedGameThreadData()
{
	if (ConsumedSnapshotRevision == PublishedSnapshotRevision) return;
	ThreadSafeData = PendingGameThreadData;
	ThreadSafeCandidateCount = bMotionMatchingEnabled && CurrentActiveDatabase ? (CurrentTurnCycleCompanion ? 2 : 1) : 0;
	ConsumedSnapshotRevision = PublishedSnapshotRevision;
	if (bFlowTraceEnabled) { FlowTraceWork.ConsumedSnapshot = FlowTraceWork.QueuedSnapshot; }
}

void FProject_JCharacterAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_AnimProxy_PreUpdate_GameThread);
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	ConsumeQueuedGameThreadData();
	ThreadSafeData.DeltaTime = DeltaSeconds;
}

void FProject_JCharacterAnimInstanceProxy::UpdateAnimationNode_WithRoot(
	const FAnimationUpdateContext& InContext,
	FAnimNode_Base* InRootNode,
	FName InLayerName)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(IsInGameThread() ? TEXT("ProjectJ_AnimProxy_Update_GameThread") : TEXT("ProjectJ_AnimProxy_Update_Worker"));
	if (bUpdatingMotionMatchingGraph)
	{
		// The outer traversal owns the temporary search timer/throttle. Completing
		// from a linked layer can disarm a MM node before its saved pose updates.
		FAnimInstanceProxy::UpdateAnimationNode_WithRoot(InContext, InRootNode, InLayerName);
		if (bFlowTraceEnabled) { ++FlowTraceWork.NestedTraversals; }
		return;
	}
	TGuardValue<bool> GraphScope(bUpdatingMotionMatchingGraph, true);
	// NativeUpdateAnimation can publish after PreUpdate. Consume that final GT
	// publication before traversing any graph, so candidate lists, ownership and
	// Steering inputs describe the same frame. Nested layers retain this snapshot.
	ConsumeQueuedGameThreadData();
	ThreadSafeData.DeltaTime = InContext.GetDeltaTime();
	NativePoseHistoryNode.TransformTrajectory = ThreadSafeData.Movement.Trajectory;

	if (bUpdateMotionMatchingThisFrame || !bMotionMatchingEnabled)
	{
		ApplySelectedDatabaseToNativeNode();
	}
	ApplyMotionMatchingSearchPolicy();
	if (PendingReselectRevision && bMotionMatchingEnabled && CurrentActiveDatabase &&
		!ThreadSafeData.OneShotPresentation.bShouldOverrideMotionMatching)
	{
		ForceReselectMotionMatchingNodes();
	}

	FAnimInstanceProxy::UpdateAnimationNode_WithRoot(InContext, InRootNode, InLayerName);
	if (bFlowTraceEnabled) { ++FlowTraceWork.Traversals; }
	CompleteMotionMatchingReselects();
	CapturePostSelection();
	CapturePivotDebugTrace();
}

void FProject_JCharacterAnimInstanceProxy::CapturePostSelection()
{
	// The visible Motion Matching node can live in the generated AnimBP graph or a
	// linked layer. NativeMotionMatchingNode is only the fallback graph, so prefer
	// a generated node with a cached result. This is a representative result;
	// its presence alone does not establish this frame's final pose contribution.
	const FAnimNode_MotionMatching* ResultNode = nullptr;
	int32 ResultNodeIndex = INDEX_NONE;
	int32 CandidateCount = 0;
	bool bFoundRequestedDatabase = false;
	for (const int32 NodeIndex : GetGeneratedMotionMatchingNodeIndices())
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

		++CandidateCount;
		// Preserve the existing representative selection policy. A cached blend
		// weight is observable through UE's public API, but does not prove that
		// this node contributed to this frame's final blended pose.
		if (!bFoundRequestedDatabase)
		{
			ResultNode = Candidate;
			ResultNodeIndex = NodeIndex;
			bFoundRequestedDatabase = CandidateResult.SelectedDatabase == CurrentActiveDatabase;
		}
	}

	if (!ResultNode)
	{
		ResultNode = &NativeMotionMatchingNode;
	}

	const FMotionMatchingState& State = ResultNode->GetMotionMatchingState();
	const FPoseSearchBlueprintResult& Result = State.SearchResult;
	const FName DatabaseName = Result.SelectedDatabase ? Result.SelectedDatabase->GetFName() : NAME_None;
	const FName AnimationName = Result.SelectedAnim ? Result.SelectedAnim->GetFName() : NAME_None;
	LatestPostSelection.bResultChangedSincePreviousCapture = LatestPostSelection.ProducerNodeIndex != ResultNodeIndex
		|| LatestPostSelection.SelectedDatabase != DatabaseName || LatestPostSelection.SelectedAnimation != AnimationName
		|| LatestPostSelection.SelectedAnimationTime != Result.SelectedTime;
	if (LatestPostSelection.bResultChangedSincePreviousCapture) { LatestPostSelection.ResultLastChangedFrame = GFrameCounter; }
	LatestPostSelection.ProducerNodeIndex = ResultNodeIndex;
	LatestPostSelection.ResultCandidateCount = CandidateCount;
	LatestPostSelection.CaptureFrame = GFrameCounter;
	LatestPostSelection.CachedNodeWeight = ResultNode->GetCachedBlendWeight();
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

#if !UE_BUILD_SHIPPING
	const int32 TurnTraceMode = Project_J::MotionMatchingCVars::GetMovingTurnTraceMode();
	if (bMovingTurnTraceEnabled && TurnTraceMode > 0 &&
		(TurnTraceMode >= 2 || GFrameCounter % 12 == 0 || DatabaseName != LastMovingTurnTraceDatabase || AnimationName != LastMovingTurnTraceAnimation))
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("MovingTurnTrace Stage=Result Frame=%llu Instance=%s Node=%d Candidates=%d RequestedPSD=%s SelectedPSD=%s Anim=%s Time=%.3f Weight=%.3f Continuing=%d Interaction=%d SearchElapsed=%.3f Pending=%llu Force=%d Enabled=%d Update=%d Override=%d"),
			GFrameCounter, *GetNameSafe(GetAnimInstanceObject()), ResultNodeIndex, CandidateCount, *GetNameSafe(CurrentActiveDatabase.Get()),
			*DatabaseName.ToString(), *AnimationName.ToString(), Result.SelectedTime, ResultNode->GetCachedBlendWeight(),
			Result.bIsContinuingPoseSearch ? 1 : 0, Result.bIsInteraction ? 1 : 0, State.ElapsedPoseSearchTime, PendingReselectRevision,
			bForceMotionMatchingReselect ? 1 : 0, bMotionMatchingEnabled ? 1 : 0, bUpdateMotionMatchingThisFrame ? 1 : 0,
			ThreadSafeData.OneShotPresentation.bShouldOverrideMotionMatching ? 1 : 0);
		// Show inactive cached nodes separately; the representative result above
		// is not proof that it contributes to the final blended output.
		for (const int32 Index : GetGeneratedMotionMatchingNodeIndices())
		{
			if (const auto* Node = GetNodeFromIndex<FAnimNode_MotionMatching>(Index))
			{
				const auto& NodeState = Node->GetMotionMatchingState();
				const auto& NodeResult = NodeState.SearchResult;
				UE_LOG(LogProjectJPlayer, Display,
					TEXT("MovingTurnTrace Stage=Node Frame=%llu Instance=%s Index=%d AppliedPSD=%s SelectedPSD=%s Anim=%s Time=%.3f Weight=%.3f SearchElapsed=%.3f"),
					GFrameCounter, *GetNameSafe(GetAnimInstanceObject()), Index, *GetNameSafe(AppliedGeneratedDatabases.FindRef(Index).Get()),
					*GetNameSafe(NodeResult.SelectedDatabase.Get()), *GetNameSafe(NodeResult.SelectedAnim.Get()),
					NodeResult.SelectedTime, Node->GetCachedBlendWeight(), NodeState.ElapsedPoseSearchTime);
			}
		}
		LastMovingTurnTraceDatabase = DatabaseName; LastMovingTurnTraceAnimation = AnimationName;
	}
#endif

	// The summary above picks one result. When Idle is requested but a run pose
	// persists, show every node so a retained result in an inactive graph cannot
	// be mistaken for the pose actually selected by the active graph.
	if (Project_J::MotionMatchingCVars::ShouldTraceCombatStop() &&
		CurrentActiveDatabase && Result.SelectedDatabase != CurrentActiveDatabase &&
		!ThreadSafeData.LocomotionContext.bIsMotionMatchingMoving &&
		ThreadSafeData.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Idle &&
		GFrameCounter % 30 == 0)
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("MMCombatStop Nodes AnimInstance=%s Frame=%llu RequestedPSD=%s GeneratedCount=%d UpdateThisFrame=%d ForceReselect=%d NativePSD=%s NativeAnim=%s"),
			*GetNameSafe(GetAnimInstanceObject()), GFrameCounter,
			*GetNameSafe(CurrentActiveDatabase.Get()), GetGeneratedMotionMatchingNodeIndices().Num(),
			bUpdateMotionMatchingThisFrame ? 1 : 0, bForceMotionMatchingReselect ? 1 : 0,
			*GetNameSafe(NativeMotionMatchingNode.GetMotionMatchingState().SearchResult.SelectedDatabase.Get()),
			*GetNameSafe(NativeMotionMatchingNode.GetMotionMatchingState().SearchResult.SelectedAnim.Get()));
		for (const int32 NodeIndex : GetGeneratedMotionMatchingNodeIndices())
		{
			const FAnimNode_MotionMatching* Candidate = GetNodeFromIndex<FAnimNode_MotionMatching>(NodeIndex);
			if (!Candidate)
			{
				continue;
			}
			const FPoseSearchBlueprintResult& CandidateResult = Candidate->GetMotionMatchingState().SearchResult;
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("MMCombatStop Node AnimInstance=%s Frame=%llu Index=%d AppliedPSD=%s SelectedPSD=%s SelectedAnim=%s Time=%.2f Continuing=%d"),
				*GetNameSafe(GetAnimInstanceObject()), GFrameCounter, NodeIndex,
				*GetNameSafe(AppliedGeneratedDatabases.FindRef(NodeIndex).Get()),
				*GetNameSafe(CandidateResult.SelectedDatabase.Get()),
				*GetNameSafe(CandidateResult.SelectedAnim.Get()), CandidateResult.SelectedTime,
				CandidateResult.bIsContinuingPoseSearch ? 1 : 0);
		}
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
	// Stop and Idle both have false movement intent. A database change at that
	// boundary must nevertheless retire the Stop/Cycle continuing pose. Keep this
	// based on the destination snapshot, not a one-frame edge: a throttled chooser
	// or a newly relevant graph may apply the Idle database on a later update.
	// SetDatabaseToSearch is called only when the database changes, so settled
	// Idle does not trigger repeated searches or restart its animation every frame.
	if (!ThreadSafeData.Air.bIsInAir &&
		!ThreadSafeData.LocomotionContext.bIsMotionMatchingMoving &&
		ThreadSafeData.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Idle)
	{
		return EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose;
	}

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
	// Dynamic and Settled are deliberately different Combat-Strafe Cycle PSDs.
	// Treat their boundary as an interrupt even though gait, movement, and phase
	// are unchanged; otherwise a continuing Full-PSD Arc/Diamond can survive
	// after the Loop-only PSD has been selected (or vice versa).
	const bool bCombatStrafeCycleFamilyChanged =
		ThreadSafeData.Combat.bIsCombatMode &&
		ThreadSafeData.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe &&
		ThreadSafeData.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Cycle &&
		ThreadSafeData.MotionMatching.SelectionContext.bUseSettledCycle != bLastPolicyUsedSettledCycle;
	const bool bMovingTurnFamilyChanged =
		bIsMoving && ThreadSafeData.LocomotionContext.PhaseFamily != LastPolicyPhaseFamily &&
		(ThreadSafeData.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Turn ||
			LastPolicyPhaseFamily == EProject_JLocomotionPhaseFamily::Turn) &&
		!ThreadSafeData.MotionMatching.SelectionContext.bAllowTurnContinuation;

	// GASP Get_MMInterruptMode: default to DoNotInterrupt and only interrupt on
	// a core locomotion change. Project_J has no separate stance enum yet, so
	// combat stance and rotation family are its safe equivalent.
	const bool bInterrupt = bMovementModeChanged ||
		(!bIsInAir && (bMovementStateChanged || (!bIsMoving && bGaitChanged) ||
			bLocomotionStanceChanged || bCombatStrafeCycleFamilyChanged || bMovingTurnFamilyChanged));
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
	bLastPolicyUsedSettledCycle = ThreadSafeData.MotionMatching.SelectionContext.bUseSettledCycle;
	LastPolicyGaitIntent = ThreadSafeData.LocomotionContext.GaitIntent;
	LastPolicyRotationMode = ThreadSafeData.LocomotionContext.RotationMode;
	LastPolicyPhaseFamily = ThreadSafeData.LocomotionContext.PhaseFamily;
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
		NativeMotionMatchingNode.GetMotionMatchingState().SearchResult.SelectedDatabase != CurrentActiveDatabase &&
		(!CurrentTurnCycleCompanion || NativeMotionMatchingNode.GetMotionMatchingState().SearchResult.SelectedDatabase != CurrentTurnCycleCompanion);
	SetMotionMatchingSearchThrottleTime(
		NativeMotionMatchingNode,
		ThreadSafeData.MotionMatchingSearchPolicy.ResolveSearchThrottleTime(
			ThreadSafeData.LocomotionContext.PhaseFamily,
			ThreadSafeData.Air.bIsFallOffStart,
			NativeDefaultSearchThrottleTime,
			bNativeDatabaseChanged,
			ThreadSafeData.MotionMatching.MinimumSearchInterval));

	for (const int32 NodeIndex : GetGeneratedMotionMatchingNodeIndices())
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
			MotionMatchingNode->GetMotionMatchingState().SearchResult.SelectedDatabase != CurrentActiveDatabase &&
			(!CurrentTurnCycleCompanion || MotionMatchingNode->GetMotionMatchingState().SearchResult.SelectedDatabase != CurrentTurnCycleCompanion);
		const bool bRecoverGroundIdle =
			bMotionMatchingEnabled && CurrentActiveDatabase && bDatabaseChanged &&
			!ThreadSafeData.OneShotPresentation.bShouldOverrideMotionMatching &&
			!ThreadSafeData.Air.bIsInAir &&
			!ThreadSafeData.LocomotionContext.bIsMotionMatchingMoving &&
			ThreadSafeData.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Idle;
		SetMotionMatchingSearchThrottleTime(
			*const_cast<FAnimNode_MotionMatching*>(MotionMatchingNode),
			bRecoverGroundIdle ? 0.0f : ThreadSafeData.MotionMatchingSearchPolicy.ResolveSearchThrottleTime(
				ThreadSafeData.LocomotionContext.PhaseFamily,
				ThreadSafeData.Air.bIsFallOffStart,
				*DefaultSearchThrottleTime,
				bDatabaseChanged,
				ThreadSafeData.MotionMatching.MinimumSearchInterval));
		if (bRecoverGroundIdle)
		{
			// An Idle edge can be hidden by a draw/sheathe montage. If the node
			// still owns a run pose after the selected PSD changed, a one-frame
			// interrupt was not enough: bypass search throttling and retire that
			// continuing pose until the requested Idle PSD actually wins.
			FAnimNode_MotionMatching* MutableNode = const_cast<FAnimNode_MotionMatching*>(MotionMatchingNode);
			MutableNode->SetDatabaseToSearch(
				CurrentActiveDatabase.Get(),
				EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose);
		}
	}

	CacheMotionMatchingPolicyState();
}

void FProject_JCharacterAnimInstanceProxy::ForceReselectMotionMatchingNodes()
{
	const TArray<int32>& GeneratedIndices = GetGeneratedMotionMatchingNodeIndices();
	if (GeneratedIndices.IsEmpty())
	{
		ArmMotionMatchingReselect(NativeMotionMatchingNode, NativeReselectState);
	}

	for (const int32 NodeIndex : GeneratedIndices)
	{
		if (FAnimNode_MotionMatching* MotionMatchingNode =
			const_cast<FAnimNode_MotionMatching*>(GetNodeFromIndex<FAnimNode_MotionMatching>(NodeIndex)))
		{
			ArmMotionMatchingReselect(*MotionMatchingNode, GeneratedReselectStates.FindOrAdd(NodeIndex));
		}
	}
}

EPoseSearchInterruptMode FProject_JCharacterAnimInstanceProxy::ResolveReselectInterruptMode() const
{
	return bReselectFromPoseHistory || bRetireTurnContinuingPose ||
		ResolveDatabaseChangeInterruptMode() == EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose
		? EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose
		: EPoseSearchInterruptMode::ForceInterrupt;
}

void FProject_JCharacterAnimInstanceProxy::ArmMotionMatchingReselect(
	FAnimNode_MotionMatching& Node, FNodeReselectState& State)
{
	if (!PendingReselectRevision || State.HandledRevision == PendingReselectRevision || State.ArmedRevision)
	{
		return;
	}
	if (FMotionMatchingState* MotionState = GetMutableMotionMatchingState(Node))
	{
		// Interaction owns this node's selection until it releases the result.
		if (MotionState->SearchResult.bIsInteraction) { return; }
		State.ArmedRevision = PendingReselectRevision;
		State.SavedElapsedSearchTime = MotionState->ElapsedPoseSearchTime;
		State.SavedSearchThrottleTime = GetMotionMatchingSearchThrottleTime(Node);
		// Mark this search due using the engine's own reset value. UE 5.8 resets
		// this timer to exactly zero only on the regular MotionMatch search path.
		// Missing history, an inactive branch, and an interaction do not acknowledge
		// the request. This also works when the search selects the same pose.
		MotionState->ElapsedPoseSearchTime = std::numeric_limits<float>::infinity();
		SetMotionMatchingSearchThrottleTime(Node, 0.0f);
		Node.SetInterruptMode(ResolveReselectInterruptMode());
	}
}

bool FProject_JCharacterAnimInstanceProxy::CompleteMotionMatchingReselect(
	FAnimNode_MotionMatching& Node, FNodeReselectState& State)
{
	if (!State.ArmedRevision) { return false; }
	FMotionMatchingState* MotionState = GetMutableMotionMatchingState(Node);
	const bool bSearched = MotionState && !MotionState->SearchResult.bIsInteraction &&
		MotionState->ElapsedPoseSearchTime == 0.0f;
#if !UE_BUILD_SHIPPING
	if (bMovingTurnTraceEnabled && Project_J::MotionMatchingCVars::GetMovingTurnTraceMode() > 0)
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("MovingTurnTrace Stage=Reselect Frame=%llu Instance=%s Kind=%s Request=%llu Executed=%d RequestedPSD=%s SelectedPSD=%s Anim=%s Weight=%.3f PoseHistory=%d"),
			GFrameCounter, *GetNameSafe(GetAnimInstanceObject()), &Node == &NativeMotionMatchingNode ? TEXT("Native") : TEXT("Generated"),
			State.ArmedRevision, bSearched ? 1 : 0, *GetNameSafe(CurrentActiveDatabase.Get()),
			*GetNameSafe(MotionState ? MotionState->SearchResult.SelectedDatabase.Get() : nullptr),
			*GetNameSafe(MotionState ? MotionState->SearchResult.SelectedAnim.Get() : nullptr), Node.GetCachedBlendWeight(), bReselectFromPoseHistory ? 1 : 0);
	}
#endif
	if (bSearched)
	{
		State.HandledRevision = State.ArmedRevision;
	}
	else if (MotionState && MotionState->ElapsedPoseSearchTime == std::numeric_limits<float>::infinity())
	{
		// Do not leave a due-time marker in an inactive node's playback state.
		MotionState->ElapsedPoseSearchTime = State.SavedElapsedSearchTime;
	}
	if (!bSearched)
	{
		// Do not leave a force interrupt waiting inside an inactive node after a
		// later mount/disable cancels the request. The proxy re-arms when needed.
		Node.SetInterruptMode(EPoseSearchInterruptMode::DoNotInterrupt);
	}
	SetMotionMatchingSearchThrottleTime(Node, State.SavedSearchThrottleTime);
	State.ArmedRevision = 0;
	return bSearched;
}

void FProject_JCharacterAnimInstanceProxy::CompleteMotionMatchingReselects()
{
	bool bAnySearchExecuted = CompleteMotionMatchingReselect(NativeMotionMatchingNode, NativeReselectState);
	for (const int32 NodeIndex : GetGeneratedMotionMatchingNodeIndices())
	{
		if (FNodeReselectState* State = GeneratedReselectStates.Find(NodeIndex))
		{
			if (auto* Node = const_cast<FAnimNode_MotionMatching*>(GetNodeFromIndex<FAnimNode_MotionMatching>(NodeIndex)))
			{
				bAnySearchExecuted |= CompleteMotionMatchingReselect(*Node, *State);
			}
		}
	}
	if (bAnySearchExecuted)
	{
		if (bFlowTraceEnabled)
		{
			++FlowTraceWork.ReselectSearches;
			FlowTraceWork.LastSearchRequest = PendingReselectRevision;
			FlowTraceWork.LastSearchSnapshot = FlowTraceWork.ConsumedSnapshot;
			FlowTraceWork.bLastSearchFromHistory = bReselectFromPoseHistory;
		}
		bForceMotionMatchingReselect = false;
		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("MMReselectSearch AnimInstance=%s Frame=%llu Request=%llu PoseHistory=%d BudgetInterval=%.3f"),
				*GetNameSafe(GetAnimInstanceObject()), GFrameCounter, PendingReselectRevision,
				bReselectFromPoseHistory ? 1 : 0, ThreadSafeData.MotionMatching.MinimumSearchInterval);
		}
		// Keep the revision: other inactive nodes consume it once when they next
		// update. Already handled nodes never repeat a forced search for it.
	}
}

void FProject_JCharacterAnimInstanceProxy::ClearMotionMatchingReselects()
{
	PendingReselectRevision = 0;
	bForceMotionMatchingReselect = false;
	bReselectFromPoseHistory = false;
	bRetireTurnContinuingPose = false;
	NativeReselectState = FNodeReselectState();
	GeneratedReselectStates.Reset();
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
	for (const int32 NodeIndex : GetGeneratedMotionMatchingNodeIndices())
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
#if WITH_EDITORONLY_DATA
	LinkNativeGraph();
	return &NativePoseHistoryNode;
#else
	// Plain proxy-owned MotionMatching nodes have no compiler-generated folded
	// NodeData in cooked builds. A mesh can initialize before its AnimBP is set.
	// Use the engine's reference-pose fallback for that interval; the generated
	// AnimBP root takes precedence once installed. Never tick an uncompiled node.
	return nullptr;
#endif
}

void FProject_JCharacterAnimInstanceProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
#if WITH_EDITORONLY_DATA
	LinkNativeGraph();
	OutNodes.Add(&NativePoseHistoryNode);
	OutNodes.Add(&NativeMotionMatchingNode);
#endif
}

void FProject_JCharacterAnimInstanceProxy::LinkNativeGraph()
{
	NativePoseHistoryNode.Source.SetLinkNode(&NativeMotionMatchingNode);
	NativePoseHistoryNode.bGenerateTrajectory = false;
	NativePoseHistoryNode.PoseCount = 10;
	NativePoseHistoryNode.SamplingInterval = 0.04f;
	NativePoseHistoryNode.TrajectorySpeedMultiplier = 1.0f;
}

const TArray<int32>& FProject_JCharacterAnimInstanceProxy::GetGeneratedMotionMatchingNodeIndices()
{
	const IAnimClassInterface* AnimClass = GetAnimClassInterface();
	if (CachedGeneratedMotionMatchingAnimClass == AnimClass)
	{
		return CachedGeneratedMotionMatchingNodeIndices;
	}

	if (CachedGeneratedMotionMatchingAnimClass && !bForceMotionMatchingReselect)
	{
		// A fulfilled request retained only for dormant old-class nodes must not
		// become a new return event when the AnimBP is replaced.
		ClearMotionMatchingReselects();
	}
	CachedGeneratedMotionMatchingAnimClass = AnimClass;
	CachedGeneratedMotionMatchingNodeIndices.Reset();
	AppliedGeneratedDatabases.Reset();
	AppliedGeneratedCompanions.Reset();
	DefaultSearchThrottleTimes.Reset();
	// Reinstanced nodes must not inherit acknowledgements belonging to old
	// node memory. A still-pending request is applied to the replacement nodes.
	GeneratedReselectStates.Reset();

	if (!AnimClass)
	{
		return CachedGeneratedMotionMatchingNodeIndices;
	}

	const TArray<FStructProperty*>& AnimNodeProperties = AnimClass->GetAnimNodeProperties();
	for (int32 NodeIndex = 0; NodeIndex < AnimNodeProperties.Num(); ++NodeIndex)
	{
		if (GetNodeFromIndex<FAnimNode_MotionMatching>(NodeIndex))
		{
			CachedGeneratedMotionMatchingNodeIndices.Add(NodeIndex);
		}
	}

	return CachedGeneratedMotionMatchingNodeIndices;
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
			AppliedTurnCycleCompanion = nullptr;
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
						AppliedGeneratedCompanions.Remove(NodeIndex);
					}
				}
			}
		}
		return;
	}

	UPoseSearchDatabase* Candidates[] = {CurrentActiveDatabase.Get(), CurrentTurnCycleCompanion.Get()};
	const TConstArrayView<UPoseSearchDatabase*> CandidateView(Candidates, CurrentTurnCycleCompanion ? 2 : 1);
	if (AppliedDatabase != CurrentActiveDatabase || AppliedTurnCycleCompanion != CurrentTurnCycleCompanion)
	{
		NativeMotionMatchingNode.SetDatabasesToSearch(
			CandidateView,
			DatabaseChangeInterruptMode);
		AppliedDatabase = CurrentActiveDatabase;
		AppliedTurnCycleCompanion = CurrentTurnCycleCompanion;
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
		if (!MotionMatchingNode || (AppliedGeneratedDatabases.FindRef(NodeIndex) == CurrentActiveDatabase &&
			AppliedGeneratedCompanions.FindRef(NodeIndex) == CurrentTurnCycleCompanion))
		{
			continue;
		}

		MotionMatchingNode->SetDatabasesToSearch(
			CandidateView,
			DatabaseChangeInterruptMode);
		AppliedGeneratedDatabases.Add(NodeIndex, CurrentActiveDatabase);
		AppliedGeneratedCompanions.Add(NodeIndex, CurrentTurnCycleCompanion);
	}
}
