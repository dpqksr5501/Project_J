#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/Project_JPresentationMeshResolver.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimationAsset.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JLocomotionDebugUtils.h"
#include "UObject/UnrealType.h"

#if !UE_BUILD_SHIPPING
namespace
{
TAutoConsoleVariable<int32> CVarAnimationFlow(TEXT("p.ProjectJ.AnimFlow"), 0,
	TEXT("Read-only animation flow trace: 0=off, 1=edges and 10Hz transition windows, 2=also 10Hz idle/cycle. Local primary mesh by default."));
TAutoConsoleVariable<FString> CVarAnimationFlowActor(TEXT("p.ProjectJ.AnimFlow.Actor"), TEXT(""),
	TEXT("Optional exact actor object name. An explicit name permits a remote actor; empty selects locally controlled pawns only."));
}
#endif

bool Project_J::AnimationFlowDebug::ShouldCapture(const AActor* Actor)
{
#if !UE_BUILD_SHIPPING
	if (!Actor || CVarAnimationFlow.GetValueOnGameThread() <= 0) { return false; }
	const FString Filter = CVarAnimationFlowActor.GetValueOnGameThread();
	if (!Filter.IsEmpty()) { return Actor->GetName().Equals(Filter, ESearchCase::IgnoreCase); }
	const APawn* Pawn = Cast<APawn>(Actor);
	return Pawn && Pawn->IsLocallyControlled();
#else
	return false;
#endif
}

void Project_J::AnimationFlowDebug::Decision(const AActor* Actor, const TCHAR* Layer, const TCHAR* Reason)
{
#if !UE_BUILD_SHIPPING
	if (!ShouldCapture(Actor)) { return; }
	UE_LOG(LogProjectJPlayer, Display, TEXT("AnimFlow Decision Frame=%llu T=%.3f Actor=%s Layer=%s Reason=%s"),
		GFrameCounter, Actor->GetWorld() ? Actor->GetWorld()->GetTimeSeconds() : 0.0, *Actor->GetPathName(), Layer, Reason);
#endif
}

void UProject_JCharacterAnimInstance::RecordAnimationFlowPublication(const FProject_JAnimThreadSafeData& Data,
	bool bChooserUpdate, bool bFreshSnapshot)
{
#if !UE_BUILD_SHIPPING
	const bool bCapture = Project_J::AnimationFlowDebug::ShouldCapture(OwningCharacter) && IsPrimaryMeshAnimInstance();
	if (!bCapture) { AnimationFlowSampler = {}; bAnimationFlowEvaluationPending = false; return; }
	FProject_JCharacterAnimInstanceProxy& Proxy = GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
	const auto& OneShot = Data.OneShotPresentation;
	FProject_JAnimationFlowKey Key;
	Key.Phase = static_cast<int32>(Data.LocomotionContext.PhaseFamily);
	Key.Presentation = static_cast<int32>(OneShot.PresentationState);
	Key.Rotation = static_cast<int32>(Data.LocomotionContext.RotationMode);
	Key.MoveRevision = Data.LocomotionContext.MoveIntentRevision;
	Key.LandingRevision = Data.Landing.PresentationRevision;
	Key.SelectionRevision = OneShot.SelectionRevision;
	Key.Asset = OneShot.SelectedAnimation ? OneShot.SelectedAnimation->GetFName() : NAME_None;
	Key.bOverride = OneShot.bShouldOverrideMotionMatching;
	Key.bHasInput = Data.Input.bHasMoveInput;
	Key.bLanding = Data.Landing.bIsLanding;
	Key.bForce = Data.MotionMatching.bForceReselect;
	const float ControlYaw = OwningCharacter->GetControlRotation().Yaw;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	bool bEdge = false;
	if (!AnimationFlowSampler.ShouldRecord(Key, ControlYaw, Now, CVarAnimationFlow.GetValueOnGameThread(),
		Key.bOverride || Data.Landing.bIsLanding || Data.LocomotionContext.bIsStarting, bEdge)) { return; }
	bAnimationFlowEvaluationPending = true;
	const auto& Work = Proxy.GetFlowTraceWork();
	const FVector LastInput = OwningCharacter->GetLastMovementInputVector();
	const auto* Profile = GetLocomotionProfile();
	UE_LOG(LogProjectJPlayer, Display,
		TEXT("AnimFlow Published Frame=%llu T=%.3f Actor=%s Local=%d Role=%d Tier=%d Edge=%d Fresh=%d ChooserUpdate=%d Snapshot=%llu WorkerSnapshot=%llu Traversals=%llu ReselectSearches=%llu Phase=%s Present=%d Rotation=%s MoveRev=%d Input=%d LastInputWorld=(%.2f,%.2f) InputTurn=%.1f ActorYaw=%.1f ControlYaw=%.1f DesiredYaw=%.1f Speed=%.1f FutureSpeed=%.1f FutureTurn=%.1f TrajectoryUsable=%d TrajectoryFrame=%llu TrajectoryAge=%.3f ResetRev=%d Land=%d LandRev=%d"),
		GFrameCounter, Now, *OwningCharacter->GetPathName(), Data.bIsLocallyControlled, static_cast<int32>(OwningCharacter->GetLocalRole()),
		static_cast<int32>(CurrentOptimizationPolicy.Tier), bEdge, bFreshSnapshot, bChooserUpdate, Work.QueuedSnapshot,
		Work.ConsumedSnapshot, Work.Traversals, Work.ReselectSearches, Project_J::LocomotionDebug::ToDebugString(Data.LocomotionContext.PhaseFamily),
		Key.Presentation, Project_J::LocomotionDebug::ToDebugString(Data.LocomotionContext.RotationMode), Key.MoveRevision, Key.bHasInput,
		LastInput.X, LastInput.Y, Data.Input.MoveInputTurnAngle, OwningCharacter->GetActorRotation().Yaw, ControlYaw,
		Data.LocomotionContext.DesiredFacingYaw, Data.Movement.GroundSpeed, Data.Movement.FutureTrajectorySpeed,
		Data.Movement.FutureTrajectoryTurnAngle, Data.Movement.bTrajectoryPredictionUsable, Data.Movement.TrajectoryGenerationFrame,
		Data.Movement.TrajectoryAgeSeconds, Data.Movement.TrajectoryResetRevision, Key.bLanding, Key.LandingRevision);
	UE_LOG(LogProjectJPlayer, Display,
		TEXT("AnimFlow Policy Frame=%llu Actor=%s Request=%d SelectRev=%d Override=%d Asset=%s EntryTime=%.3f ApproxElapsed=%.3f Remaining=%.3f EarlyWindow=%d AlmostComplete=%d ForceBlend=%d OrientationGate=%d TIPSteeringGate=%d MouseDeltaValid=%d MouseDelta=%.1f MouseLimit=%.1f MoveLimit=%.1f MMForce=%d SearchFloor=%.3f RequestedPSD=%s ReselectRequest=%llu ReselectPending=%d ReturnQuery=%d GroundIntent=%d"),
		GFrameCounter, *OwningCharacter->GetPathName(), OneShot.RequestRevision, Key.SelectionRevision, Key.bOverride,
		*Key.Asset.ToString(), OneShot.SelectedAnimationOutput.StartTime, OneShot.TransitionElapsedTime, OneShot.TransitionTimeRemaining,
		OneShot.bEarlyTransitionWindowOpen, OneShot.bTransitionAnimationAlmostComplete, OneShot.bForceBlendNextUpdate,
		OneShot.bShouldEnableCombatStrafeOrientationWarping,
		(Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::TurnInPlace ||
			OneShot.PresentationState == EProject_JStateControllerPresentationState::TurnInPlace),
		bHasStateControllerOneShotControlYaw, bHasStateControllerOneShotControlYaw ?
			FMath::FindDeltaAngleDegrees(StateControllerOneShotControlYaw, ControlYaw) : 0.0f,
		Profile ? (OneShot.PresentationState == EProject_JStateControllerPresentationState::TransitionToLand ?
			Profile->TransitionPolicy.LandMouseTurnCancelAngle : Profile->TransitionPolicy.StartMouseTurnCancelAngle) :
			(OneShot.PresentationState == EProject_JStateControllerPresentationState::TransitionToLand ? 25.0f : 15.0f),
		Profile ? Profile->TransitionPolicy.StartMoveInputCancelAngle : 30.0f, Data.MotionMatching.bForceReselect,
		Data.MotionMatching.MinimumSearchInterval, *GetNameSafe(CurrentActivePoseSearchDatabase.Get()),
		Proxy.GetReselectRequestForTrace(), Proxy.IsReselectPendingForTrace(), Proxy.IsReturnQueryForTrace(),
		Data.LocomotionContext.bHasGroundMovementIntent);
#endif
}

void UProject_JCharacterAnimInstance::RecordAnimationFlowEvaluation()
{
#if !UE_BUILD_SHIPPING
	if (!bAnimationFlowEvaluationPending || !Project_J::AnimationFlowDebug::ShouldCapture(OwningCharacter)) { return; }
	bAnimationFlowEvaluationPending = false;
	auto& Proxy = GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
	const auto& Work = Proxy.GetFlowTraceWork();
	const auto& Data = Proxy.GetThreadSafeData();
	const auto& Result = Proxy.GetLatestPostSelection();
	UE_LOG(LogProjectJPlayer, Display,
		TEXT("AnimFlow Evaluated Frame=%llu Actor=%s Snapshot=%llu Traversals=%llu ReselectSearches=%llu LastSearchRequest=%llu SearchSnapshot=%llu FromHistory=%d ConsumedSnapshotFrame=%llu ResultCaptureFrame=%llu RepresentativeNode=%d CachedWeight=%.3f ResultCandidates=%d CachedPSD=%s CachedAnim=%s CachedTime=%.3f Cost=%.3f Continuing=%d ContactL=%.3f ContactR=%.3f ContactsValid=%d EnableWarpingCurve=%.3f NestedTraversals=%llu"),
		GFrameCounter, *OwningCharacter->GetPathName(), Work.ConsumedSnapshot, Work.Traversals, Work.ReselectSearches,
		Work.LastSearchRequest, Work.LastSearchSnapshot, Work.bLastSearchFromHistory, Data.Movement.SnapshotFrame, Result.CaptureFrame,
		Result.ProducerNodeIndex, Result.CachedNodeWeight, Result.ResultCandidateCount, *Result.SelectedDatabase.ToString(),
		*Result.SelectedAnimation.ToString(), Result.SelectedAnimationTime, Result.SearchCost, Result.bIsContinuingPoseSearch,
		CachedStateControllerLeftFootContact, CachedStateControllerRightFootContact, bHasStateControllerFootContactCurves,
		GetCurveValue(TEXT("enable_warping")), Work.NestedTraversals);
	// Read live external stack clocks after evaluation. This does not execute callbacks or force a graph update.
	if (const IAnimClassInterface* Interface = IAnimClassInterface::GetFromClass(GetClass()))
	{
		int32 StackCount = 0;
		for (const FStructProperty* Property : Interface->GetAnimNodeProperties())
		{
			if (!Property || !Property->Struct->IsChildOf(FAnimNode_BlendStack::StaticStruct())) { continue; }
			if (++StackCount > 8) { break; }
			const auto* Stack = Property->ContainerPtrToValuePtr<FAnimNode_BlendStack>(this);
			for (int32 Index = 0; Index < FMath::Min(4, Stack->AnimPlayers.Num()); ++Index)
			{
				const auto& Player = Stack->AnimPlayers[Index];
				UE_LOG(LogProjectJPlayer, Display,
					TEXT("AnimFlow ExternalStack Frame=%llu Actor=%s Node=%s Player=%d Count=%d Asset=%s ActualTime=%.3f Active=%d BlendWeight=%.3f NewBlend=%d"),
					GFrameCounter, *OwningCharacter->GetPathName(), *Property->GetName(), Index, Stack->AnimPlayers.Num(),
					*GetNameSafe(Player.GetAnimationAsset()), Player.GetCurrentAssetTime(), Player.IsActive(),
					Player.GetBlendInWeight(), Stack->AnyNewBlendToThisFrame());
			}
		}
	}
	// A cached MM result is not proof that the MM branch owns the evaluated
	// pose. Read the actual debug traversal only in the existing sampled trace.
	FNodeDebugData Debug(this);
	Proxy.GatherDebugData(Debug);
	int32 Branches = 0;
	for (const auto& Row : Debug.GetFlattenedDebugData())
	{
		if (!Row.bPoseSource && !Row.DebugLine.Contains(TEXT("Blend")) &&
			!Row.DebugLine.Contains(TEXT("MotionMatching"))) continue;
		if (++Branches > 32) break;
		UE_LOG(LogProjectJPlayer, Display, TEXT("AnimFlow OutputBranch Frame=%llu Actor=%s Weight=%.3f PoseSource=%d Node=%s"),
			GFrameCounter, *OwningCharacter->GetPathName(), Row.AbsoluteWeight, Row.bPoseSource, *Row.DebugLine);
	}
	// Followers can finalize after their source. Never wait for/wake one for a
	// diagnostic; mark pending evaluation rather than mixing its bone times.
	for (USkeletalMeshComponent* Mesh : {OwningCharacter->GetMesh(), Project_J::Animation::FindVisualFollower(*OwningCharacter)})
	{
		if (!Mesh) continue;
		const bool bPending = Mesh->IsRunningParallelEvaluation();
		// The authored Greatsword follower uses Bip01 feet, unlike Quinn.
		// Report the actual bone chosen; an absent bone must not become a root sample.
		const FName LeftFoot = Mesh->GetBoneIndex(TEXT("foot_l")) != INDEX_NONE ? FName(TEXT("foot_l")) : FName(TEXT("Bip01-L-Foot"));
		const FName RightFoot = Mesh->GetBoneIndex(TEXT("foot_r")) != INDEX_NONE ? FName(TEXT("foot_r")) : FName(TEXT("Bip01-R-Foot"));
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("AnimFlow OutputMesh Frame=%llu Actor=%s Mesh=%s AnimClass=%s Visible=%d PendingEvaluation=%d ComponentYaw=%.3f ComponentWorld=%s Root=%s FootBoneL=%s FootL=%s FootBoneR=%s FootR=%s"),
			GFrameCounter, *OwningCharacter->GetPathName(), *Mesh->GetPathName(), *GetPathNameSafe(Mesh->GetAnimClass()),
			Mesh->IsVisible(), bPending, Mesh->GetComponentRotation().Yaw, *Mesh->GetComponentLocation().ToString(),
			bPending ? TEXT("Pending") : Mesh->GetBoneIndex(TEXT("root")) == INDEX_NONE ? TEXT("MissingBone") : *Mesh->GetSocketLocation(TEXT("root")).ToString(),
			*LeftFoot.ToString(), bPending ? TEXT("Pending") : Mesh->GetBoneIndex(LeftFoot) == INDEX_NONE ? TEXT("MissingBone") : *Mesh->GetSocketLocation(LeftFoot).ToString(),
			*RightFoot.ToString(), bPending ? TEXT("Pending") : Mesh->GetBoneIndex(RightFoot) == INDEX_NONE ? TEXT("MissingBone") : *Mesh->GetSocketLocation(RightFoot).ToString());
	}
#endif
}
