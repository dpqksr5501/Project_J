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

	if (bUpdateMotionMatchingThisFrame)
	{
		ApplySelectedDatabaseToNativeNode();
	}
	if (bForceMotionMatchingReselect && bMotionMatchingEnabled && CurrentActiveDatabase)
	{
		ForceReselectMotionMatchingNodes();
	}

	ApplyMotionMatchingSearchPolicy();
	FAnimInstanceProxy::UpdateAnimationNode_WithRoot(InContext, InRootNode, InLayerName);
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

void FProject_JCharacterAnimInstanceProxy::LogMotionMatchingDiagnostics()
{
#if 0 // Temporary Motion Matching diagnostics removed after landing investigation.
	const int32 DebugLevel = Project_J::MotionMatchingCVars::GetDebugLevel();
	if (DebugLevel <= 0)
	{
		MotionMatchingDebugStates.Reset();
		return;
	}

	const bool bShouldSearchEveryUpdate = ThreadSafeData.MotionMatchingSearchPolicy.ShouldSearchEveryUpdate(
		ThreadSafeData.LocomotionContext.PhaseFamily,
		ThreadSafeData.Air.bIsFallOffStart);
	const IAnimClassInterface* AnimClass = GetAnimClassInterface();
	if (!AnimClass)
	{
		return;
	}

	const TArray<FStructProperty*>& AnimNodeProperties = AnimClass->GetAnimNodeProperties();
	for (int32 NodeIndex = 0; NodeIndex < AnimNodeProperties.Num(); ++NodeIndex)
	{
		const FAnimNode_MotionMatching* MotionMatchingNode = GetNodeFromIndex<FAnimNode_MotionMatching>(NodeIndex);
		if (!MotionMatchingNode)
		{
			continue;
		}

		const int32 PropertyIndex = AnimNodeProperties.Num() - 1 - NodeIndex;
		const FStructProperty* NodeProperty = AnimNodeProperties.IsValidIndex(PropertyIndex)
			? AnimNodeProperties[PropertyIndex]
			: nullptr;
		const FPoseSearchBlueprintResult& SearchResult = MotionMatchingNode->GetMotionMatchingState().SearchResult;
		const bool bMissingInputDatabase = bMotionMatchingEnabled && !CurrentActiveDatabase;
		const bool bMissingTrajectory =
			ThreadSafeData.LocomotionContext.bIsMoving && !ThreadSafeData.Movement.bHasTrajectory;
		const bool bDatabaseMismatch =
			CurrentActiveDatabase && SearchResult.SelectedDatabase &&
			SearchResult.SelectedDatabase != CurrentActiveDatabase;
		const bool bMissingSelectedAnimation =
			CurrentActiveDatabase && !SearchResult.SelectedAnim;
		const bool bForcedReselectStillContinuing =
			bForceMotionMatchingReselect && SearchResult.bIsContinuingPoseSearch;
		const bool bHasIssue = bMissingInputDatabase || bMissingTrajectory || bDatabaseMismatch ||
			bMissingSelectedAnimation || bForcedReselectStillContinuing;

		const FString Signature = FString::Printf(
			TEXT("%s|%s|%s|%s|%s|%s|%d|%d|%d|%d|%d|%d"),
			*GetNameSafe(CurrentActiveDatabase),
			*GetNameSafe(SearchResult.SelectedDatabase),
			*GetNameSafe(SearchResult.SelectedAnim),
			Project_J::LocomotionDebug::ToDebugString(ThreadSafeData.LocomotionContext.GaitIntent),
			Project_J::LocomotionDebug::ToDebugString(ThreadSafeData.LocomotionContext.RotationMode),
			Project_J::LocomotionDebug::ToDebugString(ThreadSafeData.LocomotionContext.PhaseFamily),
			bForceMotionMatchingReselect ? 1 : 0,
			SearchResult.bIsContinuingPoseSearch ? 1 : 0,
			bHasIssue ? 1 : 0,
			ThreadSafeData.Landing.bIsLanding ? 1 : 0,
			ThreadSafeData.Landing.bLandWasMoving ? 1 : 0,
			ThreadSafeData.Landing.bUseHeavyLand ? 1 : 0);
		FMotionMatchingDebugState& DebugState = MotionMatchingDebugStates.FindOrAdd(NodeIndex);
		const bool bShouldLog = Signature != DebugState.Signature ||
			bHasIssue != DebugState.bHadIssue ||
			(DebugLevel >= 2 && bForceMotionMatchingReselect) ||
			MotionMatchingNode->AnyNewBlendToThisFrame();
		if (!bShouldLog)
		{
			continue;
		}

		UE_LOG(
			LogTemp,
			Log,
			TEXT("ProjectJ.MM Frame=%llu AnimInstance=%s Node=%s InputPSD=%s SelectedPSD=%s SelectedAnim=%s Time=%.3f Continuing=%s NewBlend=%s ForceReselect=%s Context[Combat=%s Gait=%s Rotation=%s Phase=%s GroundMode=%d] Input[Has=%s Turn=%.1f] Movement[Speed=%.1f Accel=%.2f Trajectory=%s Samples=%d] Landing[Active=%s Moving=%s Sprint=%s Heavy=%s FallStart=%.1f LastFall=%.1f] Search[EveryUpdate=%s Throttle=%.3f] Issues[NoInputPSD=%s NoTrajectory=%s PSDMismatch=%s NoAnim=%s ForcedContinue=%s]"),
		GFrameCounter,
		*GetNameSafe(GetAnimInstanceObject()),
		NodeProperty ? *NodeProperty->GetName() : TEXT("Unknown"),
		*GetNameSafe(CurrentActiveDatabase),
		*GetNameSafe(SearchResult.SelectedDatabase),
		*GetNameSafe(SearchResult.SelectedAnim),
		SearchResult.SelectedTime,
		SearchResult.bIsContinuingPoseSearch ? TEXT("true") : TEXT("false"),
		MotionMatchingNode->AnyNewBlendToThisFrame() ? TEXT("true") : TEXT("false"),
		bForceMotionMatchingReselect ? TEXT("true") : TEXT("false"),
		ThreadSafeData.Combat.bIsCombatMode ? TEXT("true") : TEXT("false"),
		Project_J::LocomotionDebug::ToDebugString(ThreadSafeData.LocomotionContext.GaitIntent),
		Project_J::LocomotionDebug::ToDebugString(ThreadSafeData.LocomotionContext.RotationMode),
		Project_J::LocomotionDebug::ToDebugString(ThreadSafeData.LocomotionContext.PhaseFamily),
		static_cast<int32>(ThreadSafeData.Ground.GroundMotionMode),
		ThreadSafeData.Input.bHasMoveInput ? TEXT("true") : TEXT("false"),
		ThreadSafeData.Input.MoveInputTurnAngle,
		ThreadSafeData.Movement.GroundSpeed,
		ThreadSafeData.Movement.AccelerationRatio,
		ThreadSafeData.Movement.bHasTrajectory ? TEXT("true") : TEXT("false"),
		ThreadSafeData.Movement.Trajectory.Samples.Num(),
		ThreadSafeData.Landing.bIsLanding ? TEXT("true") : TEXT("false"),
		ThreadSafeData.Landing.bLandWasMoving ? TEXT("true") : TEXT("false"),
		ThreadSafeData.Landing.bLandWasSprinting ? TEXT("true") : TEXT("false"),
		ThreadSafeData.Landing.bUseHeavyLand ? TEXT("true") : TEXT("false"),
		ThreadSafeData.Landing.LandStartFallSpeed,
		ThreadSafeData.Landing.LastFallSpeed,
		bShouldSearchEveryUpdate ? TEXT("true") : TEXT("false"),
		GetMotionMatchingSearchThrottleTime(*MotionMatchingNode),
		bMissingInputDatabase ? TEXT("true") : TEXT("false"),
		bMissingTrajectory ? TEXT("true") : TEXT("false"),
		bDatabaseMismatch ? TEXT("true") : TEXT("false"),
		bMissingSelectedAnimation ? TEXT("true") : TEXT("false"),
		bForcedReselectStillContinuing ? TEXT("true") : TEXT("false"));

		DebugState.Signature = Signature;
		DebugState.bHadIssue = bHasIssue;

		const bool bIsLandingPhase =
			ThreadSafeData.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Landing;
		if (DebugLevel >= 2 && bIsLandingPhase)
		{
			const int32 StackCount = MotionMatchingNode->AnimPlayers.Num();
			UE_LOG(
				LogTemp,
				Log,
				TEXT("ProjectJ.MM.LandingBlendStack Frame=%llu Node=%s InputPSD=%s SelectedPSD=%s SelectedAnim=%s NewBlend=%s StackCount=%d MaxBlends=%d Landing[Active=%s Moving=%s Sprint=%s Heavy=%s FallStart=%.1f LastFall=%.1f]"),
				GFrameCounter,
				NodeProperty ? *NodeProperty->GetName() : TEXT("Unknown"),
				*GetNameSafe(CurrentActiveDatabase),
				*GetNameSafe(SearchResult.SelectedDatabase),
				*GetNameSafe(SearchResult.SelectedAnim),
				MotionMatchingNode->AnyNewBlendToThisFrame() ? TEXT("true") : TEXT("false"),
				StackCount,
				MotionMatchingNode->GetMaxActiveBlends(),
				ThreadSafeData.Landing.bIsLanding ? TEXT("true") : TEXT("false"),
				ThreadSafeData.Landing.bLandWasMoving ? TEXT("true") : TEXT("false"),
				ThreadSafeData.Landing.bLandWasSprinting ? TEXT("true") : TEXT("false"),
				ThreadSafeData.Landing.bUseHeavyLand ? TEXT("true") : TEXT("false"),
				ThreadSafeData.Landing.LandStartFallSpeed,
				ThreadSafeData.Landing.LastFallSpeed);

			float RemainingWeight = 1.0f;
			for (int32 PlayerIndex = 0; PlayerIndex < StackCount; ++PlayerIndex)
			{
				const FBlendStackAnimPlayer& Player = MotionMatchingNode->AnimPlayers[PlayerIndex];
				const bool bIsOldestPlayer = PlayerIndex == StackCount - 1;
				const float BlendInWeight = bIsOldestPlayer ? 1.0f : Player.GetBlendInWeight();
				const float ContributionWeight = RemainingWeight * BlendInWeight;
				UE_LOG(
					LogTemp,
					Log,
					TEXT("ProjectJ.MM.LandingBlendPlayer Frame=%llu Node=%s Index=%d Anim=%s Time=%.3f AssetTime=%.3f Length=%.3f Contribution=%.3f BlendIn=%.3f BlendProgress=%.3f Active=%s Loop=%s"),
					GFrameCounter,
					NodeProperty ? *NodeProperty->GetName() : TEXT("Unknown"),
					PlayerIndex,
					*Player.GetAnimationName(),
					Player.GetAccumulatedTime(),
					Player.GetCurrentAssetTime(),
					Player.GetCurrentAssetLength(),
					ContributionWeight,
					Player.GetBlendInWeight(),
					Player.GetBlendInPercentage(),
					Player.IsActive() ? TEXT("true") : TEXT("false"),
					Player.IsLooping() ? TEXT("true") : TEXT("false"));
				RemainingWeight *= 1.0f - BlendInWeight;
			}
		}
	}
#endif
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
