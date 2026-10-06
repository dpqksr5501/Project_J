#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimationAsset.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JLocomotionDebugUtils.h"
#include "UObject/UnrealType.h"

#if !UE_BUILD_SHIPPING
namespace
{
TAutoConsoleVariable<int32> CVarContinuityTrace(TEXT("p.ProjectJ.LocomotionContinuityTrace"), 0,
	TEXT("Local primary mesh continuity diagnostics: 0=off, 1=state edges plus 5Hz, 2=edges plus 10Hz and graph branches. Read-only, post-evaluation."));

const TCHAR* GateName(EProject_JLocomotionSteeringGate Gate)
{
	using G = EProject_JLocomotionSteeringGate;
	switch (Gate)
	{
	case G::Enabled: return TEXT("Enabled");
	case G::MissingProfile: return TEXT("MissingProfile");
	case G::ProfileDisabled: return TEXT("ProfileDisabled");
	case G::ConsoleDisabled: return TEXT("ConsoleDisabled");
	case G::Ownership: return TEXT("Ownership");
	case G::NotGroundedOnFoot: return TEXT("NotGroundedOnFoot");
	case G::NoInput: return TEXT("NoInput");
	case G::PhaseOwner: return TEXT("PhaseOwner");
	case G::Action: return TEXT("Action");
	case G::Montage: return TEXT("Montage");
	case G::RootMotion: return TEXT("RootMotion");
	case G::MissingTrajectory: return TEXT("MissingTrajectory");
	case G::PredictionUnusable: return TEXT("PredictionUnusable");
	case G::StaleTrajectory: return TEXT("StaleTrajectory");
	case G::IncompletePrediction: return TEXT("IncompletePrediction");
	default: return TEXT("Unknown");
	}
}
}
#endif

void UProject_JCharacterAnimInstance::RecordLocomotionContinuityEvaluation()
{
#if !UE_BUILD_SHIPPING
	const int32 Mode = CVarContinuityTrace.GetValueOnGameThread();
	if (Mode <= 0 || !OwningPlayerCharacter || !OwningPlayerCharacter->IsPlayerControlled() ||
		!OwningPlayerCharacter->IsLocallyControlled() || !IsPrimaryMeshAnimInstance())
	{
		LocomotionContinuityTraceSampler = {};
		return;
	}
	auto& Proxy = GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
	const auto& Data = Proxy.GetThreadSafeData();
	const auto& Result = Proxy.GetLatestPostSelection();
	const auto& Shot = Data.OneShotPresentation;
	FProject_JLocomotionContinuityTraceKey Key;
	Key.Phase = int32(Data.LocomotionContext.PhaseFamily);
	Key.Presentation = int32(Shot.PresentationState);
	Key.Rotation = int32(Data.LocomotionContext.RotationMode);
	Key.Candidates = Proxy.GetThreadSafeCandidateCount();
	Key.SelectionRevision = Shot.SelectionRevision;
	Key.Gate = Data.LocomotionSteeringGate;
	Key.SelectedDatabase = Result.SelectedDatabase;
	Key.SelectedAnimation = Result.SelectedAnimation;
	Key.ExternalAnimation = Shot.SelectedAnimation ? Shot.SelectedAnimation->GetFName() : NAME_None;
	Key.bInput = Data.Input.bHasMoveInput;
	Key.bOverride = Shot.bShouldOverrideMotionMatching;
	Key.bContinuation = Data.MotionMatching.SelectionContext.bAllowTurnContinuation;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	bool bEdge = false;
	if (!LocomotionContinuityTraceSampler.ShouldRecord(Key, Now, Mode, bEdge)) return;

	const auto* Mesh = GetSkelMeshComponent();
	const bool bPending = Mesh && Mesh->IsRunningParallelEvaluation();
	const int32 RootIndex = Mesh ? Mesh->GetBoneIndex(TEXT("root")) : INDEX_NONE;
	// NativePostEvaluate precedes the engine's read-buffer flip. Socket access
	// here would combine the previous pose with this frame's component transform.
	// The evaluation buffer is complete on GT; read it without forcing any work.
	const bool bRootValid = Mesh && !bPending && RootIndex != INDEX_NONE &&
		Mesh->GetEditableComponentSpaceTransforms().IsValidIndex(RootIndex);
	const float ComponentYaw = Mesh ? Mesh->GetComponentRotation().Yaw : 0.0f;
	const float RootYaw = bRootValid ?
		(Mesh->GetEditableComponentSpaceTransforms()[RootIndex] * Mesh->GetComponentTransform()).Rotator().Yaw : 0.0f;
	const float RootOffset = bRootValid ? FMath::FindDeltaAngleDegrees(ComponentYaw, RootYaw) : 0.0f;
	const bool bNearLimit = bRootValid && Data.bLocomotionSteeringEnabled &&
		FMath::Abs(RootOffset) >= FMath::Max(0.0f, Data.LocomotionSteeringMaxYawError - 1.0f);
	UE_LOG(LogProjectJPlayer, Display,
		TEXT("LocomotionContinuityTrace Stage=Evaluated Frame=%llu T=%.3f Actor=%s Edge=%d SnapshotFrame=%llu ResultFrame=%llu Phase=%s Present=%d Rotation=%s Input=%d Speed=%.1f Air=%d Steering=%d Gate=%s TargetValid=%d TargetYaw=%.1f ActorYaw=%.1f ControlYaw=%.1f ComponentYaw=%.1f RootBuffer=EvaluatedPrePhysics RootValid=%d PendingBones=%d RootYaw=%.1f RootOffsetYaw=%.1f NearLimit=%d LookAhead=%.2f ProceduralTime=%.2f AnimatedTime=%.2f MaxYawError=%.1f OffsetMode=%d TrajectoryUsable=%d TrajectoryAge=%.3f Attack=%d Dodge=%d Hit=%d"),
		GFrameCounter, Now, *OwningCharacter->GetPathName(), bEdge, Data.Movement.SnapshotFrame, Result.CaptureFrame,
		Project_J::LocomotionDebug::ToDebugString(Data.LocomotionContext.PhaseFamily), Key.Presentation,
		Project_J::LocomotionDebug::ToDebugString(Data.LocomotionContext.RotationMode), Key.bInput, Data.Movement.GroundSpeed,
		Data.Air.bIsInAir, Data.bLocomotionSteeringEnabled, GateName(Key.Gate), Data.bLocomotionSteeringEnabled,
		Data.bLocomotionSteeringEnabled ? Data.LocomotionSteeringTarget.Rotator().Yaw : 0.0f,
		OwningCharacter->GetActorRotation().Yaw, OwningCharacter->GetControlRotation().Yaw, ComponentYaw,
		bRootValid, bPending, RootYaw, RootOffset, bNearLimit, Data.LocomotionSteeringLookAhead,
		Data.LocomotionSteeringProceduralTime, Data.LocomotionSteeringAnimatedTime, Data.LocomotionSteeringMaxYawError,
		int32(GetThreadSafeOffsetRootRotationMode()), Data.Movement.bTrajectoryPredictionUsable, Data.Movement.TrajectoryAgeSeconds,
		Data.Combat.bIsAttacking, Data.Combat.bIsDodging, Data.Combat.bIsHitReacting);
	UE_LOG(LogProjectJPlayer, Display,
		TEXT("LocomotionContinuityTrace Stage=Selection Frame=%llu Actor=%s Candidates=%d PrimaryPSD=%s CompanionPSD=%s MovingTurn=%d ContinueTurn=%d MMRepresentative=1 MMNode=%d MMWeight=%.3f SelectedPSD=%s SelectedClip=%s ClipTime=%.3f Cost=%.3f ContinuingPose=%d Override=%d ExternalClip=%s SelectionRev=%d ExternalElapsedApprox=%.3f ExternalRemainingApprox=%.3f ReselectPending=%d ReturnQuery=%d ContactValid=%d ContactL=%.3f ContactR=%.3f"),
		GFrameCounter, *OwningCharacter->GetPathName(), Key.Candidates, *GetNameSafe(Proxy.GetCurrentActiveDatabase()),
		*GetNameSafe(Proxy.GetTurnCycleCompanion()), Data.MotionMatching.SelectionContext.bMovingTurn180, Key.bContinuation,
		Result.ProducerNodeIndex, Result.CachedNodeWeight, *Result.SelectedDatabase.ToString(), *Result.SelectedAnimation.ToString(),
		Result.SelectedAnimationTime, Result.SearchCost, Result.bIsContinuingPoseSearch, Key.bOverride,
		*Key.ExternalAnimation.ToString(), Key.SelectionRevision, Shot.TransitionElapsedTime, Shot.TransitionTimeRemaining,
		Proxy.IsReselectPendingForTrace(), Proxy.IsReturnQueryForTrace(), bHasStateControllerFootContactCurves,
		CachedStateControllerLeftFootContact, CachedStateControllerRightFootContact);

	// Read active external players only when they own presentation. Never update
	// nodes, force a search, or wait for a follower's parallel bone evaluation.
	if (Key.bOverride)
	{
		if (const IAnimClassInterface* Interface = IAnimClassInterface::GetFromClass(GetClass()))
		{
			int32 Stacks = 0;
			for (const FStructProperty* Property : Interface->GetAnimNodeProperties())
			{
				if (!Property || !Property->Struct->IsChildOf(FAnimNode_BlendStack::StaticStruct()) ||
					Property->Struct->IsChildOf(FAnimNode_MotionMatching::StaticStruct())) continue;
				if (++Stacks > 2) break;
				const auto* Stack = Property->ContainerPtrToValuePtr<FAnimNode_BlendStack>(this);
				for (int32 Index = 0; Index < FMath::Min(3, Stack->AnimPlayers.Num()); ++Index)
				{
					const auto& Player = Stack->AnimPlayers[Index];
					UE_LOG(LogProjectJPlayer, Display,
						TEXT("LocomotionContinuityTrace Stage=ExternalPlayer Frame=%llu Actor=%s Node=%s Player=%d Clip=%s ActualTime=%.3f Weight=%.3f Active=%d"),
						GFrameCounter, *OwningCharacter->GetPathName(), *Property->GetName(), Index,
						*GetNameSafe(Player.GetAnimationAsset()), Player.GetCurrentAssetTime(), Player.GetBlendInWeight(), Player.IsActive());
				}
			}
		}
	}
	if (Mode >= 2)
	{
		FNodeDebugData Debug(this); Proxy.GatherDebugData(Debug);
		int32 Rows = 0;
		for (const auto& Row : Debug.GetFlattenedDebugData())
		{
			if (!Row.DebugLine.Contains(TEXT("Steering")) && !Row.DebugLine.Contains(TEXT("OffsetRoot")) &&
				!Row.DebugLine.Contains(TEXT("MotionMatching"))) continue;
			if (++Rows > 8) break;
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("LocomotionContinuityTrace Stage=Branch Frame=%llu Actor=%s Weight=%.3f Node=%s"),
				GFrameCounter, *OwningCharacter->GetPathName(), Row.AbsoluteWeight, *Row.DebugLine);
		}
	}
#endif
}
