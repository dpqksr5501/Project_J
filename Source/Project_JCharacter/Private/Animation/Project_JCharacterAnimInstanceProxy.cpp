#include "Animation/Project_JCharacterAnimInstanceProxy.h"

#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "GameFramework/Pawn.h"
#include "Animation/Project_JMotionMatchingCVars.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "Project_JLocomotionDebugUtils.h"
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
	bool bInUpdateMotionMatchingThisFrame)
{
	PendingGameThreadData = InData;
	CurrentActiveDatabase = InSelectedDatabase;
	bMotionMatchingEnabled = bInMotionMatchingEnabled;
	bUpdateMotionMatchingThisFrame = bInUpdateMotionMatchingThisFrame;
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
	const bool bIsJumpStart =
		ThreadSafeData.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::JumpStart;
	if (IsRemoteSimulatedProxy() &&
		PreviousPhaseFamily != EProject_JLocomotionPhaseFamily::JumpStart &&
		bIsJumpStart)
	{
		// The phase and the game-thread Pose Search database publication can arrive a few
		// animation frames apart. Keep the request pending until Motion Matching actually
		// creates the new JumpStart player instead of applying a one-frame-only override.
		bPendingRemoteJumpStartBlendCollapse = true;
	}
	else if (!bIsJumpStart)
	{
		bPendingRemoteJumpStartBlendCollapse = false;
	}

	if (bUpdateMotionMatchingThisFrame)
	{
		ApplySelectedDatabaseToNativeNode();
	}

	ApplyMotionMatchingSearchPolicy();
	FAnimInstanceProxy::UpdateAnimationNode_WithRoot(InContext, InRootNode, InLayerName);
	CollapsePendingRemoteJumpStartBlend();
	LogInAirAnimBlueprintBlendStacks();
	PreviousPhaseFamily = ThreadSafeData.LocomotionContext.PhaseFamily;
}

bool FProject_JCharacterAnimInstanceProxy::IsRemoteSimulatedProxy() const
{
	const UAnimInstance* AnimInstance = Cast<UAnimInstance>(GetAnimInstanceObject());
	const APawn* PawnOwner = AnimInstance ? AnimInstance->TryGetPawnOwner() : nullptr;
	return PawnOwner && PawnOwner->GetLocalRole() == ROLE_SimulatedProxy;
}

bool FProject_JCharacterAnimInstanceProxy::CollapsePendingRemoteJumpStartBlend()
{
	if (!bPendingRemoteJumpStartBlendCollapse)
	{
		return false;
	}

	bool bCollapsedAnyNode = false;
	auto CollapseNewJumpBlend = [this, &bCollapsedAnyNode](FAnimNode_MotionMatching& MotionMatchingNode)
	{
		if (MotionMatchingNode.AnimPlayers.IsEmpty())
		{
			return;
		}

		const FPoseSearchBlueprintResult& SearchResult =
			MotionMatchingNode.GetMotionMatchingState().SearchResult;
		if (!CurrentActiveDatabase ||
			SearchResult.SelectedDatabase != CurrentActiveDatabase ||
			SearchResult.SelectedAnim == nullptr ||
			MotionMatchingNode.AnimPlayers[0].GetAnimationAsset() != SearchResult.SelectedAnim)
		{
			return;
		}

		const int32 PreviousPlayerCount = MotionMatchingNode.AnimPlayers.Num();
		if (PreviousPlayerCount > 1)
		{
			// Update has already initialized the newest player at index zero. Evaluation has
			// not run yet, so discarding older ground players here makes the JumpStart pose
			// contribute at full weight in this same rendered frame.
			//
			// Refactoring guard: moving simulated proxies otherwise retain Run/Sprint at
			// roughly 75% on the first JumpStart update and fade it out over about 0.15 s.
			// This is deliberately restricted to the pending remote JumpStart edge; do not
			// generalize it to normal MM transitions or local/autonomous characters.
			MotionMatchingNode.AnimPlayers.SetNum(1, EAllowShrinking::No);
		}

		// AnyNewBlendToThisFrame() is tied to BlendTo's exact update counter and is no
		// longer reliable from this post-update hook. Matching the selected database and
		// newest player proves that the JumpStart player has actually been initialized.
		bCollapsedAnyNode = true;
		if (Project_J::MotionMatchingCVars::IsDebugJumpLatencyEnabled())
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("ProjectJ.JumpLatency Stage=RemoteJumpBlendCollapsed Frame=%llu AnimInstance=%s Anim=%s PreviousPlayers=%d"),
				GFrameCounter,
				*GetNameSafe(GetAnimInstanceObject()),
				*GetNameSafe(SearchResult.SelectedAnim),
				PreviousPlayerCount);
		}
	};

	CollapseNewJumpBlend(NativeMotionMatchingNode);
	if (const IAnimClassInterface* AnimClass = GetAnimClassInterface())
	{
		const TArray<FStructProperty*>& AnimNodeProperties = AnimClass->GetAnimNodeProperties();
		for (int32 NodeIndex = 0; NodeIndex < AnimNodeProperties.Num(); ++NodeIndex)
		{
			if (const FAnimNode_MotionMatching* MotionMatchingNode =
				GetNodeFromIndex<FAnimNode_MotionMatching>(NodeIndex))
			{
				CollapseNewJumpBlend(*const_cast<FAnimNode_MotionMatching*>(MotionMatchingNode));
			}
		}
	}

	if (bCollapsedAnyNode)
	{
		bPendingRemoteJumpStartBlendCollapse = false;
	}
	return bCollapsedAnyNode;
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

void FProject_JCharacterAnimInstanceProxy::LogInAirAnimBlueprintBlendStacks()
{
	const bool bDebugEnabled = Project_J::MotionMatchingCVars::IsDebugInAirBlendStackEnabled();
	const bool bShouldSearchEveryUpdate = ThreadSafeData.MotionMatchingSearchPolicy.ShouldSearchEveryUpdate(
		ThreadSafeData.LocomotionContext.PhaseFamily,
		ThreadSafeData.Air.bIsFallOffStart);
	const bool bIsInAirPhase =
		ThreadSafeData.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::JumpStart ||
		ThreadSafeData.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Fall;
	if (!bDebugEnabled || !bIsInAirPhase)
	{
		InAirBlendStackDebugStates.Reset();
		return;
	}

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

		const int32 PropertyIndex = AnimNodeProperties.Num() - 1 - NodeIndex;
		const FStructProperty* NodeProperty = AnimNodeProperties.IsValidIndex(PropertyIndex)
			? AnimNodeProperties[PropertyIndex]
			: nullptr;
		const FPoseSearchBlueprintResult& SearchResult =
			MotionMatchingNode->GetMotionMatchingState().SearchResult;
		const int32 StackCount = MotionMatchingNode->AnimPlayers.Num();
		const bool bNewBlend = MotionMatchingNode->AnyNewBlendToThisFrame();

		FString StackSignature;
		for (const FBlendStackAnimPlayer& Player : MotionMatchingNode->AnimPlayers)
		{
			StackSignature += Player.GetAnimationName();
			StackSignature.AppendChar(TEXT('|'));
		}

		FInAirBlendStackDebugState& DebugState = InAirBlendStackDebugStates.FindOrAdd(NodeIndex);
		const bool bStackChanged =
			DebugState.StackCount != StackCount ||
			DebugState.StackSignature != StackSignature;
		const bool bEnteredInAir = !DebugState.bWasInAir;
		if (!bNewBlend && !bStackChanged && !bEnteredInAir)
		{
			continue;
		}

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("ProjectJ.MM.InAirBlendStack Frame=%llu AnimInstance=%s Node=%s NodeIndex=%d Phase=%s FallOff=%s SearchEveryUpdate=%s SearchThrottle=%.3g InputPSD=%s SelectedPSD=%s SelectedAnim=%s SelectedTime=%.3f Continuing=%s NewBlend=%s Count=%d Max=%d"),
			GFrameCounter,
			*GetNameSafe(GetAnimInstanceObject()),
			NodeProperty ? *NodeProperty->GetName() : TEXT("Unknown"),
			NodeIndex,
			Project_J::LocomotionDebug::ToDebugString(ThreadSafeData.LocomotionContext.PhaseFamily),
			ThreadSafeData.Air.bIsFallOffStart ? TEXT("true") : TEXT("false"),
			bShouldSearchEveryUpdate ? TEXT("true") : TEXT("false"),
			GetMotionMatchingSearchThrottleTime(*MotionMatchingNode),
			*GetNameSafe(CurrentActiveDatabase),
			*GetNameSafe(SearchResult.SelectedDatabase),
			*GetNameSafe(SearchResult.SelectedAnim),
			SearchResult.SelectedTime,
			SearchResult.bIsContinuingPoseSearch ? TEXT("true") : TEXT("false"),
			bNewBlend ? TEXT("true") : TEXT("false"),
			StackCount,
			MotionMatchingNode->GetMaxActiveBlends());

		float RemainingWeight = 1.0f;
		for (int32 PlayerIndex = 0; PlayerIndex < StackCount; ++PlayerIndex)
		{
			const FBlendStackAnimPlayer& Player = MotionMatchingNode->AnimPlayers[PlayerIndex];
			const bool bIsOldestPlayer = PlayerIndex == StackCount - 1;
			const float BlendInWeight = bIsOldestPlayer ? 1.0f : Player.GetBlendInWeight();
			const float ContributionWeight = RemainingWeight * BlendInWeight;

			UE_LOG(
				LogTemp,
				Warning,
				TEXT("ProjectJ.MM.InAirBlendStackPlayer NodeIndex=%d Player=%d Anim=%s Time=%.3f AssetTime=%.3f Length=%.3f Contribution=%.3f BlendIn=%.3f BlendProgress=%.3f Active=%s Loop=%s StoredPose=%s"),
				NodeIndex,
				PlayerIndex,
				*Player.GetAnimationName(),
				Player.GetAccumulatedTime(),
				Player.GetCurrentAssetTime(),
				Player.GetCurrentAssetLength(),
				ContributionWeight,
				Player.GetBlendInWeight(),
				Player.GetBlendInPercentage(),
				Player.IsActive() ? TEXT("true") : TEXT("false"),
				Player.IsLooping() ? TEXT("true") : TEXT("false"),
				Player.GetAnimationAsset() ? TEXT("false") : TEXT("true"));

			RemainingWeight *= 1.0f - BlendInWeight;
		}

		DebugState.StackCount = StackCount;
		DebugState.StackSignature = MoveTemp(StackSignature);
		DebugState.bWasInAir = true;
	}
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
		return;
	}

	if (AppliedDatabase == CurrentActiveDatabase)
	{
		return;
	}

	NativeMotionMatchingNode.SetDatabaseToSearch(
		CurrentActiveDatabase.Get(),
		EPoseSearchInterruptMode::InterruptOnDatabaseChangeAndInvalidateContinuingPose);
	AppliedDatabase = CurrentActiveDatabase;
}
