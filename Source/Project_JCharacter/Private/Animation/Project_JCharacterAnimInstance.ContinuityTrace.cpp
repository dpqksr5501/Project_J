#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/AnimNode_ProjectJLocomotion.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimNode_ProjectJStrafeUpperBodyFacing.h"
#include "Animation/AnimNode_ProjectJStrafeLowerBodyFacing.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceHelpers.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "Project_JPlayerCharacter.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Project_JLocomotionDebugUtils.h"
#include "UObject/UnrealType.h"

#if !UE_BUILD_SHIPPING
namespace
{
TAutoConsoleVariable<int32> CVarContinuityTrace(TEXT("p.ProjectJ.LocomotionContinuityTrace"), 0,
	TEXT("Local primary mesh continuity diagnostics: 0=off, 1=state edges plus 5Hz, 2=edges plus 10Hz and graph branches/facing, 3=also every evaluated forward Strafe facing frame. Read-only, post-evaluation."));

struct FTraceBoneHeading
{
	float Yaw = 0.0f;
	bool bValid = false;
};

// Bones have different reference axes. Remove each component-space reference
// rotation before measuring the character's forward axis, rather than treating
// a pelvis/spine Rotator.Yaw as a body heading. Never synchronize/re-evaluate.
FTraceBoneHeading ReadEvaluatedHeading(const USkeletalMeshComponent* Mesh, const ACharacter* Character, FName Bone)
{
	FTraceBoneHeading Result;
	const auto* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset || !Character || Mesh->IsRunningParallelEvaluation()) return Result;
	const auto& Reference = Asset->GetRefSkeleton();
	const int32 Index = Mesh->GetBoneIndex(Bone);
	const auto& Pose = Mesh->GetEditableComponentSpaceTransforms();
	const auto& RefPose = Reference.GetRefBonePose();
	if (!Pose.IsValidIndex(Index) || !RefPose.IsValidIndex(Index)) return Result;
	FTransform RefCS = RefPose[Index];
	for (int32 Parent = Reference.GetParentIndex(Index); Parent != INDEX_NONE; Parent = Reference.GetParentIndex(Parent))
	{
		if (!RefPose.IsValidIndex(Parent)) return Result;
		RefCS = RefCS * RefPose[Parent];
	}
	const FQuat Evaluated = Pose[Index].GetRotation();
	const FQuat RefRotation = RefCS.GetRotation();
	const FQuat Component = Mesh->GetComponentQuat();
	if (Evaluated.ContainsNaN() || RefRotation.ContainsNaN() || Component.ContainsNaN() ||
		!Evaluated.IsNormalized() || !RefRotation.IsNormalized() || !Component.IsNormalized()) return Result;
	const FVector NeutralForward = Component.UnrotateVector(Character->GetActorForwardVector());
	FVector Forward = Component.RotateVector((Evaluated * RefRotation.Inverse()).RotateVector(NeutralForward));
	Forward.Z = 0;
	if (Forward.ContainsNaN() || Forward.IsNearlyZero()) return Result;
	Result.Yaw = Forward.Rotation().Yaw;
	Result.bValid = true;
	return Result;
}

const UMirrorDataTable* ReadPlayerMirrorTable(const FBlendStackAnimPlayer& Player)
{
	// UE 5.8 does not export the player's convenience getter. Read its
	// reflected mirror node and use the exported const getter instead.
	const auto* Property = FindFProperty<FStructProperty>(FBlendStackAnimPlayer::StaticStruct(), TEXT("MirrorNode"));
	if (!Property || Property->Struct != FAnimNode_Mirror_Standalone::StaticStruct()) return nullptr;
	const auto* Mirror = Property->ContainerPtrToValuePtr<FAnimNode_Mirror_Standalone>(&Player);
	return Mirror->GetMirrorDataTable();
}

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
	Key.bContinuation = Data.MotionMatching.SelectionContext.bAllowTurnContinuation || Data.MotionMatching.SelectionContext.bAllowGeneralTurnContinuation || Data.MotionMatching.SelectionContext.bAllowBackwardRefaceContinuation;
	Key.bAcuteApproach = Data.TurnRequest.bAcuteApproach;
	Key.AcuteReason = FName(Data.TurnRequest.AcuteReason);
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	bool bEdge = false;
	const bool bRecord = LocomotionContinuityTraceSampler.ShouldRecord(Key, Now, FMath::Min(Mode, 2), bEdge);
	const auto* Mesh = GetSkelMeshComponent();
	const float CameraYaw = OwningCharacter->GetControlRotation().Yaw;
	const FVector MoveDirection = Data.LocomotionContext.RequestedMoveWorldDirection;
	const float MoveToCamera = FMath::FindDeltaAngleDegrees(CameraYaw, MoveDirection.Rotation().Yaw);
	const bool bForwardStrafe = Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe &&
		Data.LocomotionMode == EProject_JAnimationLocomotionMode::OnFoot && !Data.Air.bIsInAir &&
		(Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Cycle ||
			Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Turn) &&
		!Key.bOverride && Key.bInput && Data.Movement.GroundSpeed > 100 && !MoveDirection.IsNearlyZero() &&
		FMath::Abs(MoveToCamera) <= 30;
	if (Mode >= 2 && (bRecord || Mode >= 3) && bForwardStrafe)
	{
		const auto Root = ReadEvaluatedHeading(Mesh, OwningCharacter, TEXT("root"));
		const auto Pelvis = ReadEvaluatedHeading(Mesh, OwningCharacter, TEXT("pelvis"));
		const auto Spine = ReadEvaluatedHeading(Mesh, OwningCharacter, TEXT("spine_01"));
		const FName ChestBone = Mesh && Mesh->GetBoneIndex(TEXT("spine_05")) != INDEX_NONE ? TEXT("spine_05") : TEXT("spine_03");
		const auto Chest = ReadEvaluatedHeading(Mesh, OwningCharacter, ChestBone);
		if (const auto* Interface = IAnimClassInterface::GetFromClass(GetClass()))
		{
			for (const FStructProperty* Property : Interface->GetAnimNodeProperties())
			{
				if (Property && Property->Struct == FAnimNode_ProjectJStrafeLowerBodyFacing::StaticStruct())
				{
					const auto* Facing = Property->ContainerPtrToValuePtr<FAnimNode_ProjectJStrafeLowerBodyFacing>(this);
					UE_LOG(LogProjectJPlayer, Display, TEXT("LocomotionContinuityTrace Stage=LowerBodyFacing Frame=%llu Enabled=%d InputPelvisYaw=%.2f Desired=%.2f Applied=%.2f ReachScale=%.3f"),
						GFrameCounter, Facing->WasEnabled(), Facing->GetInputPelvisYaw(), Facing->GetDesiredYaw(), Facing->GetAppliedYaw(), Facing->GetReachScale());
				}
				if (!Property || Property->Struct != FAnimNode_ProjectJStrafeUpperBodyFacing::StaticStruct()) continue;
				const auto* Facing = Property->ContainerPtrToValuePtr<FAnimNode_ProjectJStrafeUpperBodyFacing>(this);
				UE_LOG(LogProjectJPlayer, Display, TEXT("LocomotionContinuityTrace Stage=UpperBodyFacing Frame=%llu Enabled=%d InputChestYaw=%.2f Error=%.2f Desired=%.2f Applied=%.2f RootWeight=%.3f"),
					GFrameCounter, Facing->WasEnabled(), Facing->GetInputChestYaw(), Facing->GetFacingError(),
					Facing->GetDesiredYaw(), Facing->GetAppliedYaw(), Facing->GetRootWeight());
			}
		}
		const float VelocityYaw = Data.Movement.Velocity.Rotation().Yaw;
		const auto* Trajectory = OwningPlayerCharacter->GetMotionMatchingTrajectoryComponent();
		const bool bTargetValid = Mesh && Data.bLocomotionSteeringEnabled;
		const float TargetBodyYaw = bTargetValid ?
			(Data.LocomotionSteeringTarget * Mesh->GetRelativeRotation().Quaternion().Inverse()).Rotator().Yaw : 0.0f;
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("LocomotionContinuityTrace Stage=Facing Frame=%llu T=%.3f Actor=%s SnapshotFrame=%llu ResultFrame=%llu Phase=%s PSD=%s Clip=%s ClipTime=%.3f Arc=%d Speed=%.1f ActorYaw=%.2f CameraYaw=%.2f MoveYaw=%.2f MoveToCamera=%.2f VelocityYaw=%.2f VelocityToActor=%.2f Steering=%d TargetValid=%d TargetBodyYaw=%.2f CameraYawRate=%.2f ClampedCameraYawRate=%.2f AimYaw=%.2f AimPitch=%.2f AimAlpha=%.3f BoneBasis=ReferenceCalibrated Buffer=EvaluatedPrePhysics PendingBones=%d RootValid=%d RootBodyYaw=%.2f PelvisValid=%d PelvisBodyYaw=%.2f SpineValid=%d SpineBodyYaw=%.2f ChestBone=%s ChestValid=%d ChestBodyYaw=%.2f ChestToRoot=%.2f ChestToCamera=%.2f ChestToVelocity=%.2f"),
			GFrameCounter, Now, *OwningCharacter->GetPathName(), Data.Movement.SnapshotFrame, Result.CaptureFrame,
			Project_J::LocomotionDebug::ToDebugString(Data.LocomotionContext.PhaseFamily),
			*Result.SelectedDatabase.ToString(), *Result.SelectedAnimation.ToString(), Result.SelectedAnimationTime,
			Result.SelectedAnimation.ToString().Contains(TEXT("_Arc_")), Data.Movement.GroundSpeed,
			OwningCharacter->GetActorRotation().Yaw, CameraYaw, MoveDirection.Rotation().Yaw, MoveToCamera, VelocityYaw,
			Data.Movement.RelativeVelocityDirection, Data.bLocomotionSteeringEnabled, bTargetValid, TargetBodyYaw,
			Trajectory ? Trajectory->GetCharacterTrajectoryData().ControllerYawRate : 0.0f,
			Trajectory ? Trajectory->GetCharacterTrajectoryData().ControllerYawRateClamped : 0.0f,
			Data.Aim.AimYaw, Data.Aim.AimPitch, Data.Aim.AimOffsetAlpha, Mesh && Mesh->IsRunningParallelEvaluation(),
			Root.bValid, Root.Yaw, Pelvis.bValid, Pelvis.Yaw, Spine.bValid, Spine.Yaw, *ChestBone.ToString(), Chest.bValid, Chest.Yaw,
			Root.bValid && Chest.bValid ? FMath::FindDeltaAngleDegrees(Root.Yaw, Chest.Yaw) : 0.0f,
			Chest.bValid ? FMath::FindDeltaAngleDegrees(CameraYaw, Chest.Yaw) : 0.0f,
			Chest.bValid ? FMath::FindDeltaAngleDegrees(VelocityYaw, Chest.Yaw) : 0.0f);
		// Inspect each contributing player's authored root horizon only at the
		// regular sample rate. Mode 3's extra rows are cheap final-pose reads.
		if (const auto* Node = bRecord ? Proxy.GetCapturedMotionMatchingNode() : nullptr)
		{
			for (int32 Index = 0; Index < FMath::Min(4, Node->AnimPlayers.Num()); ++Index)
			{
				const auto& Player = Node->AnimPlayers[Index];
				const auto* Sequence = Cast<UAnimSequence>(Player.GetAnimationAsset());
				const auto* MirrorTable = ReadPlayerMirrorTable(Player);
				const bool bAuthoredValid = Sequence && Sequence->GetPlayLength() > UE_SMALL_NUMBER &&
					(!Player.GetMirror() || MirrorTable);
				const auto* Mirror = Player.GetMirror() ? MirrorTable : nullptr;
				const FTransform ShortRoot = bAuthoredValid ? UE::Anim::ExtractRootMotionFromAnimationAsset(
					Sequence, Mirror, Player.GetCurrentAssetTime(), .1f, Sequence->bLoop) : FTransform::Identity;
				const float Horizon = FMath::Clamp(Data.LocomotionSteeringAnimatedTime, .01f, 5.0f);
				const FTransform FutureRoot = bAuthoredValid ? UE::Anim::ExtractRootMotionFromAnimationAsset(
					Sequence, Mirror, Player.GetCurrentAssetTime(), Horizon, Sequence->bLoop) : FTransform::Identity;
				UE_LOG(LogProjectJPlayer, Display,
					TEXT("LocomotionContinuityTrace Stage=FacingPlayer Frame=%llu Actor=%s Player=%d Clip=%s ActualTime=%.3f RelativeWeight=%.3f Active=%d Mirrored=%d MirrorTable=%s PlayerLoop=%d AssetLoop=%d AuthoredValid=%d TravelValid=%d LocalTravelYaw=%.2f ShortRootYaw=%.2f AnimatedHorizon=%.2f FutureRootYaw=%.2f"),
					GFrameCounter, *OwningCharacter->GetPathName(), Index, *GetNameSafe(Sequence), Player.GetCurrentAssetTime(),
					Player.GetBlendInWeight(), Player.IsActive(), Player.GetMirror(), *GetNameSafe(MirrorTable),
					Player.IsLooping(), Sequence && Sequence->bLoop, bAuthoredValid, bAuthoredValid && !ShortRoot.GetTranslation().IsNearlyZero(),
					ShortRoot.GetTranslation().Rotation().Yaw, ShortRoot.Rotator().Yaw, Horizon, FutureRoot.Rotator().Yaw);
			}
		}
	}
	if (!bRecord) return;

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
		TEXT("LocomotionContinuityTrace Stage=Selection Frame=%llu Actor=%s Candidates=%d PrimaryPSD=%s CompanionPSD=%s MovingTurn=%d ContinueTurn=%d GeneralTurn=%d GeneralContinue=%d GeneralReason=%s RecentHeading=%.1f WindowElapsed=%.3f MoveYawRate=%.1f FacingYawRate=%.1f PathError=%.1f FacingError=%.1f DynamicCycle=%d SettledCycle=%d MMRepresentative=1 MMNode=%d MMWeight=%.3f SelectedPSD=%s SelectedClip=%s ClipTime=%.3f Cost=%.3f ContinuingPose=%d Override=%d ExternalClip=%s SelectionRev=%d ExternalElapsedApprox=%.3f ExternalRemainingApprox=%.3f ReselectPending=%d ReturnQuery=%d ContactValid=%d ContactL=%.3f ContactR=%.3f"),
		GFrameCounter, *OwningCharacter->GetPathName(), Key.Candidates, *GetNameSafe(Proxy.GetCurrentActiveDatabase()),
		*GetNameSafe(Proxy.GetTurnCycleCompanion()), Data.MotionMatching.SelectionContext.bMovingTurn180, Key.bContinuation,
		Data.MotionMatching.SelectionContext.bGeneralTurnCandidates, Data.MotionMatching.SelectionContext.bAllowGeneralTurnContinuation,
		*Data.GeneralTurnReason.ToString(), Data.GeneralTurnRecentHeading, Data.GeneralTurnWindowElapsed,
		Data.GeneralTurnMoveYawRate, Data.GeneralTurnFacingYawRate, Data.GeneralTurnPathError, Data.GeneralTurnFacingError,
		Data.bGeneralTurnDynamicCycle, Data.MotionMatching.SelectionContext.bUseSettledCycle,
		Result.ProducerNodeIndex, Result.CachedNodeWeight, *Result.SelectedDatabase.ToString(), *Result.SelectedAnimation.ToString(),
		Result.SelectedAnimationTime, Result.SearchCost, Result.bIsContinuingPoseSearch, Key.bOverride,
		*Key.ExternalAnimation.ToString(), Key.SelectionRevision, Shot.TransitionElapsedTime, Shot.TransitionTimeRemaining,
		Proxy.IsReselectPendingForTrace(), Proxy.IsReturnQueryForTrace(), bHasStateControllerFootContactCurves,
		CachedStateControllerLeftFootContact, CachedStateControllerRightFootContact);
	if (Mode >= 2)
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("LocomotionContinuityTrace Stage=BackwardReface Frame=%llu T=%.3f Actor=%s Open=%d Continue=%d Reason=%s FacingError=%.1f PathError=%.1f"),
			GFrameCounter, Now, *OwningCharacter->GetPathName(), Data.MotionMatching.SelectionContext.bBackwardRefaceCandidates,
			Data.MotionMatching.SelectionContext.bAllowBackwardRefaceContinuation, *Data.BackwardRefaceReason.ToString(),
			Data.BackwardRefaceFacingError, Data.BackwardRefacePathError);
		const auto& Request = Data.TurnRequest;
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("LocomotionContinuityTrace Stage=TurnRequest Frame=%llu Actor=%s SampleFrame=%llu Age=%.3f Valid=%d Usable=%d Acute=%d Approach=%d AcuteReason=%s Guard=%s ActorYaw=%.1f FacingYaw=%.1f MoveYaw=%.1f VelocityYaw=%.1f Speed=%.1f Preparing=%d Demand=%d Sweep=%.1f RemainingFacing=%.1f VisualValid=%d VisualYaw=%.1f"),
			GFrameCounter, *OwningCharacter->GetPathName(), Request.Frame, Now - Request.Seconds,
			Request.bValid, Request.IsUsable(GFrameCounter, Now, Data.LocomotionContext.RotationMode),
			Request.bAcuteActive, Request.bAcuteApproach, Request.AcuteReason, Request.EligibilityGuard,
			Request.ActorYaw, Request.FacingYaw, Request.MoveYaw, Request.VelocityYaw, Request.Speed,
			Request.bPreparing, Request.Demand, Request.RequestedSweep, Request.RemainingFacing,
			Request.bVisualFacingValid, Request.VisualFacingYaw);
		const auto* Movement = OwningCharacter->GetCharacterMovement();
		const auto* Trajectory = OwningPlayerCharacter->GetMotionMatchingTrajectoryComponent();
		const FVector Move = Data.LocomotionContext.RequestedMoveWorldDirection;
		const float ControlYaw = OwningCharacter->GetControlRotation().Yaw;
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("LocomotionContinuityTrace Stage=Direction Frame=%llu Actor=%s MoveValid=%d MoveYaw=%.1f MoveToCamera=%.1f VelocityYaw=%.1f VelocityToActor=%.1f AccelX=%.1f AccelY=%.1f CMCOrient=%d CMCControllerDesired=%d CMCRotationRate=%.1f CameraYawRate=%.1f ClampedCameraYawRate=%.1f"),
			GFrameCounter, *OwningCharacter->GetPathName(), !Move.IsNearlyZero(), Move.Rotation().Yaw,
			FMath::FindDeltaAngleDegrees(ControlYaw, Move.Rotation().Yaw), Data.Movement.Velocity.Rotation().Yaw,
			Data.Movement.RelativeVelocityDirection, Data.Movement.Acceleration.X, Data.Movement.Acceleration.Y,
			Movement && Movement->bOrientRotationToMovement, Movement && Movement->bUseControllerDesiredRotation,
			Movement ? Movement->RotationRate.Yaw : 0.0f,
			Trajectory ? Trajectory->GetCharacterTrajectoryData().ControllerYawRate : 0.0f,
			Trajectory ? Trajectory->GetCharacterTrajectoryData().ControllerYawRateClamped : 0.0f);
		int32 FutureRows = 0;
		for (int32 Index = 0; Mesh && Index + 1 < Data.Movement.Trajectory.Samples.Num() && FutureRows < 3; ++Index)
		{
			const auto& Sample = Data.Movement.Trajectory.Samples[Index];
			const auto& Next = Data.Movement.Trajectory.Samples[Index + 1];
			if (Sample.TimeInSeconds < .35f || Sample.TimeInSeconds > .81f) continue;
			const FVector Travel = Next.Position - Sample.Position;
			const float BodyFacing = (Sample.Facing * Mesh->GetRelativeRotation().Quaternion().Inverse()).Rotator().Yaw;
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("LocomotionContinuityTrace Stage=Prediction Frame=%llu Actor=%s SampleTime=%.3f TravelValid=%d TravelYaw=%.1f BodyFacingYaw=%.1f TravelToFacing=%.1f"),
				GFrameCounter, *OwningCharacter->GetPathName(), Sample.TimeInSeconds, !Travel.IsNearlyZero(),
				Travel.Rotation().Yaw, BodyFacing, FMath::FindDeltaAngleDegrees(BodyFacing, Travel.Rotation().Yaw));
			++FutureRows;
		}
		if (const auto* Node = Proxy.GetCapturedMotionMatchingNode())
		{
			const auto& Search = Node->GetMotionMatchingState().SearchResult;
			if (const auto* Sequence = Cast<UAnimSequence>(Search.SelectedAnim.Get()))
			{
				const float End = FMath::Min(Search.SelectedTime + .1f, Sequence->GetPlayLength());
				const FVector RootDelta = Sequence->ExtractRootMotionFromRange(Search.SelectedTime, End, FAnimExtractContext()).GetTranslation();
				UE_LOG(LogProjectJPlayer, Display,
					TEXT("LocomotionContinuityTrace Stage=CandidateDirection Frame=%llu Actor=%s AuthoredDeltaValid=%d AuthoredLocalTravelYaw=%.1f Mirrored=%d"),
					GFrameCounter, *OwningCharacter->GetPathName(), !RootDelta.IsNearlyZero(), RootDelta.Rotation().Yaw, Search.bIsMirrored);
			}
			const auto ReadFloat = [Node](const TCHAR* Name)
			{
				const auto* Property = FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(), Name);
				return Property ? Property->GetPropertyValue_InContainer(Node) : -1.0f;
			};
			const auto ReadBool = [Node](const TCHAR* Name)
			{
				const auto* Property = FindFProperty<FBoolProperty>(FAnimNode_MotionMatching::StaticStruct(), Name);
				return Property ? int32(Property->GetPropertyValue_InContainer(Node)) : -1;
			};
			const auto ReadRange = [Node](const TCHAR* Name)
			{
				const auto* Property = FindFProperty<FStructProperty>(FAnimNode_MotionMatching::StaticStruct(), Name);
				return Property ?
					*Property->ContainerPtrToValuePtr<FFloatInterval>(Node) : FFloatInterval(-1, -1);
			};
			const auto PlayRate = ReadRange(TEXT("PlayRate"));
			const auto Jump = ReadRange(TEXT("PoseJumpThresholdTime"));
			const auto* MaxBlends = FindFProperty<FIntProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("MaxActiveBlends"));
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("LocomotionContinuityTrace Stage=NodeSettings Frame=%llu Actor=%s MMNode=%d ResultFrame=%llu BlendTime=%.3f SearchThrottleRestored=%.3f SearchElapsed=%.3f ReselectHistory=%.3f JumpMin=%.3f JumpMax=%.3f PlayRateMin=%.3f PlayRateMax=%.3f WantedPlayRate=%.3f Inertial=%d MaxActiveBlends=%d PlayerCount=%d NotifyRecency=%.3f OverrideBlendTime=%.3f DepthBlendMultiplier=%.3f Demand=%s CycleHandoff=%d ProfileDefaultBlend=%.3f ProfileMaxBlends=%d ProfilePlayMin=%.3f ProfilePlayMax=%.3f"),
				GFrameCounter, *OwningCharacter->GetPathName(), Result.ProducerNodeIndex, Result.CaptureFrame,
				ReadFloat(TEXT("BlendTime")), ReadFloat(TEXT("SearchThrottleTime")), Node->GetMotionMatchingState().ElapsedPoseSearchTime,
				ReadFloat(TEXT("PoseReselectHistory")), Jump.Min, Jump.Max, PlayRate.Min, PlayRate.Max, Result.WantedPlayRate,
				ReadBool(TEXT("bUseInertialBlend")), MaxBlends ? MaxBlends->GetPropertyValue_InContainer(Node) : -1, Node->AnimPlayers.Num(),
				ReadFloat(TEXT("NotifyRecencyTimeOut")), ReadFloat(TEXT("MaxBlendInTimeToOverrideAnimation")), ReadFloat(TEXT("PlayerDepthBlendInTimeMultiplier")),
				*Data.GeneralTurnDemand.ToString(), Data.bGeneralTurnCycleHandoff, Data.MotionMatchingSearchPolicy.DefaultBlendTime,
				Data.MotionMatchingSearchPolicy.MaxActiveBlends, Data.MotionMatchingSearchPolicy.MinPlayRate, Data.MotionMatchingSearchPolicy.MaxPlayRate);
			for (int32 Index = 0; Index < FMath::Min(5, Node->AnimPlayers.Num()); ++Index)
			{
				const auto& Player = Node->AnimPlayers[Index];
				UE_LOG(LogProjectJPlayer, Display,
					TEXT("LocomotionContinuityTrace Stage=MMPlayer Frame=%llu Actor=%s MMNode=%d Player=%d Clip=%s ActualTime=%.3f RelativeBlendWeight=%.3f BlendTime=%.3f BlendElapsed=%.3f Active=%d"),
					GFrameCounter, *OwningCharacter->GetPathName(), Result.ProducerNodeIndex, Index, *GetNameSafe(Player.GetAnimationAsset()),
					Player.GetCurrentAssetTime(), Player.GetBlendInWeight(), Player.GetTotalBlendInTime(), Player.GetCurrentBlendInTime(), Player.IsActive());
			}
		}
		if (!bPending)
		{
			if (const IAnimClassInterface* Interface = IAnimClassInterface::GetFromClass(GetClass()))
			{
				int32 Rows = 0;
				for (const FStructProperty* Property : Interface->GetAnimNodeProperties())
				{
					if (!Property || !Property->Struct->IsChildOf(FAnimNode_PoseSearchHistoryCollector_Base::StaticStruct())) continue;
					const auto* Collector = Property->ContainerPtrToValuePtr<FAnimNode_PoseSearchHistoryCollector_Base>(this);
					const auto* History = Collector->GetPoseHistoryPtr();
					if (!History || History->IsEmpty() || ++Rows > 2) continue;
					FTransform CurrentRoot, FutureRoot, NextRoot;
					const bool bValid = History->GetTransformAtTime(0, CurrentRoot, CurrentSkeleton, UE::PoseSearch::RootBoneIndexType, UE::PoseSearch::WorldSpaceIndexType) &&
						History->GetTransformAtTime(.4f, FutureRoot, CurrentSkeleton, UE::PoseSearch::RootBoneIndexType, UE::PoseSearch::WorldSpaceIndexType) &&
						History->GetTransformAtTime(.5f, NextRoot, CurrentSkeleton, UE::PoseSearch::RootBoneIndexType, UE::PoseSearch::WorldSpaceIndexType);
					const FVector Travel = NextRoot.GetTranslation() - FutureRoot.GetTranslation();
					UE_LOG(LogProjectJPlayer, Display,
						TEXT("LocomotionContinuityTrace Stage=QueryHistory Frame=%llu Actor=%s Node=%s Generated=%d SpeedMultiplier=%.2f Recovery=%.2f Valid=%d RootYaw=%.1f FutureRootYaw=%.1f FutureTravelYaw=%.1f FutureTravelToRoot=%.1f"),
						GFrameCounter, *OwningCharacter->GetPathName(), *Property->GetName(), Collector->bGenerateTrajectory,
						Collector->TrajectorySpeedMultiplier, Collector->RootBoneRecoveryTime, bValid, CurrentRoot.Rotator().Yaw,
						FutureRoot.Rotator().Yaw, Travel.Rotation().Yaw, FMath::FindDeltaAngleDegrees(FutureRoot.Rotator().Yaw, Travel.Rotation().Yaw));
				}
			}
		}
	}

	// Read active external players only when they own presentation. Never update
	// nodes, force a search, or wait for a follower's parallel bone evaluation.
	if (Key.bOverride)
	{
		if (const IAnimClassInterface* Interface = IAnimClassInterface::GetFromClass(GetClass()))
		{
			int32 Stacks = 0;
			for (const FStructProperty* Property : Interface->GetAnimNodeProperties())
			{
				if (!Property) continue;
				const bool bUnified = Property->Struct->IsChildOf(FAnimNode_ProjectJLocomotion::StaticStruct());
				if (!bUnified && (!Property->Struct->IsChildOf(FAnimNode_BlendStack::StaticStruct()) ||
					Property->Struct->IsChildOf(FAnimNode_MotionMatching::StaticStruct()))) continue;
				if (++Stacks > 2) break;
				const auto* Stack = Property->ContainerPtrToValuePtr<FAnimNode_BlendStack_Standalone>(this);
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
