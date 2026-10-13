// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JCharacterAnimInstance.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"

#include "ChooserFunctionLibrary.h"
#include "ChooserTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "IObjectChooser.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JLocomotionAnimStateComponentBase.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/Project_JMotionMatchingCVars.h"
#include "Animation/Project_JOneShotInputResponse.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "Animation/Project_JRiderAnimationProfile.h"
#include "Animation/AnimationAsset.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JBaseCharacter.h"
#include "Mount/Project_JMountComponent.h"
#include "Mount/Project_JMountCharacter.h"
#include "Mount/Project_JFlyingMountCharacter.h"
#include "Project_JLocomotionDebugUtils.h"
#include "StructUtils/InstancedStruct.h"

#include "Animation/Project_JAnimationPresentationHelpers.h"
using namespace ProjectJ::Animation::Presentation;

UProject_JCharacterAnimInstance::UProject_JCharacterAnimInstance()
{
	bUseMultiThreadedAnimationUpdate = true;

	FootPlacementPlantSettingsDefault.DistanceToGround = 10.0f;
	FootPlacementPlantSettingsDefault.MaxExtensionRatio = 0.95f;
	FootPlacementPlantSettingsDefault.MinExtensionRatio = 0.1f;
	FootPlacementPlantSettingsDefault.AnkleTwistReduction = 0.75f;

	FootPlacementPlantSettingsStops.DistanceToGround = 10.0f;
	FootPlacementPlantSettingsStops.MaxExtensionRatio = 0.95f;
	FootPlacementPlantSettingsStops.MinExtensionRatio = 0.1f;
	FootPlacementPlantSettingsStops.AnkleTwistReduction = 0.75f;
	FootPlacementPlantSettingsStops.SpeedThreshold = 80.0f;
	FootPlacementPlantSettingsStops.UnplantRadius = 25.0f;
	FootPlacementPlantSettingsStops.UnplantAngle = 35.0f;
	FootPlacementPlantSettingsStops.ReplantRadiusRatio = 0.5f;
	FootPlacementPlantSettingsStops.ReplantAngleRatio = 0.65f;

	FootPlacementInterpolationSettingsStops.UnplantLinearStiffness = 500.0f;
	FootPlacementInterpolationSettingsStops.UnplantAngularStiffness = 700.0f;
	FootPlacementInterpolationSettingsStops.FloorLinearStiffness = 1200.0f;
	FootPlacementInterpolationSettingsStops.FloorAngularStiffness = 650.0f;
}

void UProject_JCharacterAnimInstance::NativeInitializeAnimation()
{
	AnimationClock = FProject_JAnimationClock();
	AnimationFlowSampler = {};
	bAnimationFlowEvaluationPending = false;
	Super::NativeInitializeAnimation();
	GeneralTurnPolicy.Reset();
	BackwardRefacePolicy.Reset();
	CompletedTurnFeedback = {};
	MotionMatchingRuntime.Reset();
	ResetStateControllerOwnerPresentation();
	CurrentActivePoseSearchDatabase = nullptr;
}

void UProject_JCharacterAnimInstance::ResetStateControllerOwnerPresentation()
{
	bAuthoredEarlyTransitionPending = false;
	AuthoredEarlyTransitionAsset.Reset();
	OneShotEarlyTransitionWindowDepth = 0;
	StateControllerRuntime.Reset();
	ResetStateControllerChooserCache();
	CachedStateControllerPresentationState = EProject_JStateControllerPresentationState::Disabled;
	CachedStateControllerPivotRequestRevision = 0;
	++StateControllerChooserSelectionRevision;
	bWasMountedForStateController = false;
	bWasPlayingFullBodyMontageForStateController = false;
	bWasPlayingCombatPresentationTransitionForStateController = false;
	bSuppressPreTransitionLandingPresentationUntilLandingEnds = false;
	LastFullBodyMontageEndedAtSeconds = -100.0;
}

void UProject_JCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_AnimNativeUpdate);
	Super::NativeUpdateAnimation(DeltaSeconds);
	++EarlyTransitionSnapshotRevision;
	AnimationClock.Advance(DeltaSeconds, GetWorld() && GetWorld()->IsPaused());

	if (NeedsOwnerReferenceRefresh())
	{
		CacheOwnerReferences();
		GeneralTurnPolicy.Reset();
		BackwardRefacePolicy.Reset();
		MotionMatchingRuntime.Reset();
		ResetStateControllerOwnerPresentation();
		CurrentActivePoseSearchDatabase = nullptr;
	}

	if (ShouldSkipNativeUpdate(DeltaSeconds))
	{
		return;
	}

	ThreadSafeData = BuildThreadSafeData(DeltaSeconds);
	UpdatePresentationFromSnapshot();
	if (IsPrimaryMeshAnimInstance())
	{
		ResetTrajectoryHistoryOnAccelerationStop(ThreadSafeData);
	}
	// Preserve the consumed snapshot; record cleanup separately rather than
	// overwriting history that this frame's Stop chooser has just consumed.
	if (CachedTrajectoryComponent)
	{
		ThreadSafeData.Movement.TrajectoryResetRevisionAfterConsumption = CachedTrajectoryComponent->GetResetRevision();
		ThreadSafeData.Movement.TrajectoryResetReasonAfterConsumption = uint8(CachedTrajectoryComponent->GetLastResetReason());
	}
	PublishThreadSafeDataToProxy(ThreadSafeData);
	if (IsPrimaryMeshAnimInstance() && IsLocallyControlledCharacter())
	{
		if (!Project_J::MotionMatchingCVars::ShouldTraceCombatStop())
		{
			bCombatStopTraceWasInputHeld = false;
			bCombatStopTraceWasTransitionActive = false;
			CombatStopTraceUntilSeconds = 0.0;
		}
		else
		{
			const double NowSeconds = FPlatformTime::Seconds();
			const bool bTransitionActive = ThreadSafeData.Combat.bIsPlayingCombatIntro ||
				ThreadSafeData.Combat.bIsPlayingCombatOutro;
			const bool bTransitionStarted = bTransitionActive && !bCombatStopTraceWasTransitionActive;
			const bool bTransitionEnded = !bTransitionActive && bCombatStopTraceWasTransitionActive;
			const bool bInputReleased = bCombatStopTraceWasInputHeld && !ThreadSafeData.Input.bHasMoveInput;
			if (bTransitionStarted)
			{
				CombatStopTraceUntilSeconds = NowSeconds + 8.0;
			}
			if (bInputReleased && (bTransitionActive || bCombatStopTraceWasTransitionActive ||
				NowSeconds < CombatStopTraceUntilSeconds))
			{
				CombatStopTraceUntilSeconds = FMath::Max(CombatStopTraceUntilSeconds, NowSeconds + 6.0);
			}
			if (bTransitionEnded)
			{
				CombatStopTraceUntilSeconds = FMath::Max(CombatStopTraceUntilSeconds, NowSeconds + 4.0);
			}
			bCombatStopTraceWasInputHeld = ThreadSafeData.Input.bHasMoveInput;
			bCombatStopTraceWasTransitionActive = bTransitionActive;
			if (bTransitionStarted || bTransitionEnded || bInputReleased ||
				(NowSeconds < CombatStopTraceUntilSeconds && NowSeconds >= CombatStopTraceNextSampleSeconds))
			{
				CombatStopTraceNextSampleSeconds = NowSeconds + 0.25;
				const TCHAR* Event = bInputReleased ? TEXT("InputReleased")
					: (bTransitionStarted ? TEXT("TransitionStarted")
						: (bTransitionEnded ? TEXT("TransitionEnded") : TEXT("Sample")));
				const UProject_JLocomotionAnimStateComponent* State = LocomotionAnimStateComponent.Get();
				const FProject_JAnimMotionMatchingPostSelectionData& Played = ThreadSafeData.MotionMatching.PostSelection;
				UE_LOG(LogProjectJPlayer, Display,
					TEXT("MMCombatStop State Event=%s Actor=%s Frame=%llu Intro=%d Outro=%d MontageWeight=%.2f Input=%d StateInput=%d AxisSize=%.2f Speed=%.1f FutureSpeed=%.1f Accel=%d GroundMode=%d RawPhase=%d PresentedPhase=%d MMMoving=%d OneShot=%d Held=%d Request=%d"),
					Event, *GetNameSafe(OwningCharacter), GFrameCounter,
					ThreadSafeData.Combat.bIsPlayingCombatIntro ? 1 : 0,
					ThreadSafeData.Combat.bIsPlayingCombatOutro ? 1 : 0,
					ThreadSafeData.ProceduralIK.FullBodyMontageWeight,
					ThreadSafeData.Input.bHasMoveInput ? 1 : 0,
					State && State->bHasMoveInput ? 1 : 0,
					State ? State->MoveInputSize : -1.0f,
					ThreadSafeData.Movement.GroundSpeed,
					ThreadSafeData.Movement.bHasFutureTrajectoryVelocity
						? ThreadSafeData.Movement.FutureTrajectoryVelocity.Size2D() : -1.0f,
					ThreadSafeData.Movement.bIsAccelerating ? 1 : 0,
					static_cast<int32>(ThreadSafeData.Ground.GroundMotionMode),
					State ? static_cast<int32>(State->DerivedLocomotionContext.PhaseFamily) : -1,
					static_cast<int32>(ThreadSafeData.LocomotionContext.PhaseFamily),
					ThreadSafeData.LocomotionContext.bIsMotionMatchingMoving ? 1 : 0,
					static_cast<int32>(ThreadSafeData.OneShotPresentation.PresentationState),
					static_cast<int32>(StateControllerRuntime.GetHeldState()),
					ThreadSafeData.OneShotPresentation.bRequested ? 1 : 0);
				UE_LOG(LogProjectJPlayer, Display,
					TEXT("MMCombatStop Pose Actor=%s Frame=%llu RequestedPSD=%s PlayedPSD=%s PlayedAnim=%s Time=%.2f/%.2f Rate=%.2f Continuing=%d DirectAnim=%s DirectOverride=%d DirectLoop=%d ForceSearch=%d SelectionRev=%d TrajectoryResetRev=%d"),
					*GetNameSafe(OwningCharacter), GFrameCounter,
					*GetNameSafe(CurrentActivePoseSearchDatabase.Get()),
					*Played.SelectedDatabase.ToString(), *Played.SelectedAnimation.ToString(),
					Played.SelectedAnimationTime, Played.SelectedAnimationLength, Played.WantedPlayRate,
					Played.bIsContinuingPoseSearch ? 1 : 0,
					*GetNameSafe(ThreadSafeData.OneShotPresentation.SelectedAnimation.Get()),
					ThreadSafeData.OneShotPresentation.bShouldOverrideMotionMatching ? 1 : 0,
					ThreadSafeData.OneShotPresentation.bSelectedAnimationShouldLoop ? 1 : 0,
					ThreadSafeData.MotionMatching.bForceReselect ? 1 : 0,
					ThreadSafeData.MotionMatching.SelectionRevision,
					ThreadSafeData.Movement.TrajectoryResetRevision);
			}
		}
	}
}

void UProject_JCharacterAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	// The engine owns update/evaluation ordering for both primary and linked
	// instances. Copy only immutable snapshot values here: state-controller values
	// can change later during graph traversal and must keep their existing getters.
	const auto& Snapshot = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	GraphAimYaw = Snapshot.Aim.AimYaw;
	GraphAimPitch = Snapshot.Aim.AimPitch;
	GraphAimOffsetAlpha = Snapshot.Aim.AimOffsetAlpha;
	GraphCombatSpeed = Snapshot.Movement.GroundSpeed;
	GraphCombatDirection = Snapshot.Movement.RelativeVelocityDirection;
	// This callback is part of UE's thread-safe update phase, but the Task Graph
	// may execute that phase as a foreground task. Record UE's runtime state so
	// Insights distinguishes actual parallel evaluation from foreground work.
	TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_AnimNativeThreadSafeUpdate);
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(IsInGameThread() ? TEXT("ProjectJ_AnimThreadSafeUpdate_GameThread") : TEXT("ProjectJ_AnimThreadSafeUpdate_Worker"));
	if (IsRunningParallelEvaluation())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_AnimNativeThreadSafeUpdate_ParallelEvaluation);
		Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);
	}
	else
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_AnimNativeThreadSafeUpdate_Foreground);
		Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);
	}
}

void UProject_JCharacterAnimInstance::NativePostEvaluateAnimation()
{
	// Post-evaluate is intentionally measured separately from NativeUpdate and
	// NativeThreadSafeUpdate. Its thread lane in Insights distinguishes the
	// completion-side contact-curve work from worker-side graph evaluation.
	TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_AnimNativePostEvaluate);
	Super::NativePostEvaluateAnimation();

	if (!IsPrimaryMeshAnimInstance())
	{
		return;
	}
	CompletedTurnFeedback = {};
	if (OwningCharacter && OwningCharacter->IsLocallyControlled())
	{
		const auto& Proxy = GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
		const auto& Data = Proxy.GetThreadSafeData();
		const auto& Result = Proxy.GetLatestPostSelection();
		CompletedTurnFeedback.Frame = Result.CaptureFrame;
		CompletedTurnFeedback.Seconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0;
		CompletedTurnFeedback.Mode = Data.LocomotionContext.RotationMode;
		CompletedTurnFeedback.Gait = Data.LocomotionContext.GaitIntent;
		CompletedTurnFeedback.SelectedDatabase = Result.SelectedDatabase;
		CompletedTurnFeedback.bRelevant = Result.CachedNodeWeight > UE_SMALL_NUMBER &&
			!Data.OneShotPresentation.bShouldOverrideMotionMatching;
		if (const auto* Node = Proxy.GetCapturedMotionMatchingNode())
			CompletedTurnFeedback.bRelevant &= !Node->GetMotionMatchingState().SearchResult.bIsInteraction;
		const auto* Mesh = GetSkelMeshComponent();
		const int32 Root = Mesh ? Mesh->GetBoneIndex(TEXT("root")) : INDEX_NONE;
		if (Mesh && Root != INDEX_NONE && Mesh->GetEditableComponentSpaceTransforms().IsValidIndex(Root))
		{
			const float RootYaw = (Mesh->GetEditableComponentSpaceTransforms()[Root] * Mesh->GetComponentTransform()).Rotator().Yaw;
			// Remove the mesh's reference-facing axis, not just the retarget/root
			// world yaw. Selection reads this pose; gameplay rotation never does.
			CompletedTurnFeedback.VisualFacingYaw = FRotator::NormalizeAxis(OwningCharacter->GetActorRotation().Yaw +
				FMath::FindDeltaAngleDegrees(Mesh->GetComponentRotation().Yaw, RootYaw));
			CompletedTurnFeedback.VisualFrame = GFrameCounter;
			CompletedTurnFeedback.bVisualFacingValid = FMath::IsFinite(CompletedTurnFeedback.VisualFacingYaw);
		}
	}

	float LeftContact = 0.0f;
	float RightContact = 0.0f;
	const bool bHasLeftContact = GetCurveValue(TEXT("contact_l"), LeftContact);
	const bool bHasRightContact = GetCurveValue(TEXT("contact_r"), RightContact);
	bHasStateControllerLeftFootContactCurve = bHasLeftContact;
	bHasStateControllerRightFootContactCurve = bHasRightContact;
	bHasStateControllerFootContactCurves = bHasLeftContact && bHasRightContact;
	if (bHasStateControllerFootContactCurves)
	{
		CachedStateControllerLeftFootContact = FMath::Clamp(LeftContact, 0.0f, 1.0f);
		CachedStateControllerRightFootContact = FMath::Clamp(RightContact, 0.0f, 1.0f);
		const float ContactDelta = CachedStateControllerLeftFootContact - CachedStateControllerRightFootContact;
		if (FMath::Abs(ContactDelta) >= StateControllerFootContactDifferenceThreshold)
		{
			StateControllerFootPhaseHistory = ContactDelta < 0.0f
				? EProject_JStateControllerFoot::Left
				: EProject_JStateControllerFoot::Right;
			bHasStateControllerFootPhaseHistory = true;
		}
	}
	else
	{
		CachedStateControllerLeftFootContact = 0.0f;
		CachedStateControllerRightFootContact = 0.0f;
	}
	RecordAnimationFlowEvaluation();
	RecordLocomotionContinuityEvaluation();
}

EProject_JStateControllerFoot UProject_JCharacterAnimInstance::ResolveStateControllerFootFromContactCurves(
	const bool bAllowPhaseHistoryFallback,
	EProject_JStateControllerFootSelectionReason& OutReason) const
{
	auto UsePhaseHistoryOrDefault = [this, bAllowPhaseHistoryFallback, &OutReason]()
	{
		if (bAllowPhaseHistoryFallback && bHasStateControllerFootPhaseHistory)
		{
			OutReason = EProject_JStateControllerFootSelectionReason::PhaseHistoryFallback;
			return StateControllerFootPhaseHistory;
		}

		OutReason = EProject_JStateControllerFootSelectionReason::DefaultFootFallback;
		return StateControllerNoPhaseFootFallback;
	};

	if (!bHasStateControllerFootContactCurves)
	{
		OutReason = EProject_JStateControllerFootSelectionReason::MissingContactCurve;
		return UsePhaseHistoryOrDefault();
	}

	const bool bLeftUnplanted = CachedStateControllerLeftFootContact < StateControllerFootContactDifferenceThreshold;
	const bool bRightUnplanted = CachedStateControllerRightFootContact < StateControllerFootContactDifferenceThreshold;
	if (bLeftUnplanted && bRightUnplanted)
	{
		OutReason = EProject_JStateControllerFootSelectionReason::BothFeetUnplanted;
		return UsePhaseHistoryOrDefault();
	}

	const float ContactDelta = CachedStateControllerLeftFootContact - CachedStateControllerRightFootContact;
	if (FMath::Abs(ContactDelta) < StateControllerFootContactDifferenceThreshold)
	{
		OutReason = EProject_JStateControllerFootSelectionReason::ContactsTooSimilar;
		return UsePhaseHistoryOrDefault();
	}

	// A _Lfoot/_Rfoot one-shot begins by moving that foot. Pick the foot with
	// lower contact (the current swing/airborne foot) to continue the stride.
	if (ContactDelta < 0.0f)
	{
		OutReason = EProject_JStateControllerFootSelectionReason::LeftFootLowerContact;
		return EProject_JStateControllerFoot::Left;
	}

	OutReason = EProject_JStateControllerFootSelectionReason::RightFootLowerContact;
	return EProject_JStateControllerFoot::Right;
}

FAnimInstanceProxy* UProject_JCharacterAnimInstance::CreateAnimInstanceProxy()
{
	return new FProject_JCharacterAnimInstanceProxy(this);
}

void UProject_JCharacterAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}

void UProject_JCharacterAnimInstance::OnStateEntry_TurnInPlace(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	// Empty stub for Blueprint State Machine On Entry Binding
}

void UProject_JCharacterAnimInstance::BeginOneShotEarlyTransitionWindow()
{
	++OneShotEarlyTransitionWindowDepth;
}

void UProject_JCharacterAnimInstance::RequestAuthoredEarlyTransition(UAnimSequenceBase* Animation, bool bRequireGaitChange, EProject_JLocomotionGaitIntent ExcludedGait)
{
	check(IsInGameThread());
	// Notify events from the outgoing Blend Stack or a Motion Matching clip may
	// coexist with the held one-shot. They cannot authorize this state's exit.
	if (!Animation || Animation != CachedStateControllerSelectedAnimation || !ThreadSafeData.OneShotPresentation.bEnabled) return;
	AuthoredEarlyTransitionAsset = Animation; AuthoredEarlyTransitionSnapshotRevision = EarlyTransitionSnapshotRevision;
	bAuthoredEarlyTransitionPending = true;
	bAuthoredEarlyTransitionRequiresGaitChange = bRequireGaitChange;
	AuthoredEarlyTransitionExcludedGait = ExcludedGait;
}
bool UProject_JCharacterAnimInstance::IsAuthoredEarlyTransitionAllowed(EProject_JLocomotionGaitIntent CurrentGait) const
{
	return bAuthoredEarlyTransitionPending && AuthoredEarlyTransitionAsset.IsValid() &&
		AuthoredEarlyTransitionAsset.Get() == CachedStateControllerSelectedAnimation &&
		EarlyTransitionSnapshotRevision <= AuthoredEarlyTransitionSnapshotRevision + 1 &&
		(!bAuthoredEarlyTransitionRequiresGaitChange || CurrentGait != AuthoredEarlyTransitionExcludedGait);
}

void UProject_JCharacterAnimInstance::EndOneShotEarlyTransitionWindow()
{
	OneShotEarlyTransitionWindowDepth = FMath::Max(OneShotEarlyTransitionWindowDepth - 1, 0);
}

FString UProject_JCharacterAnimInstance::GetAnimationDebugSummary() const
{
	using Project_J::LocomotionDebug::ToDebugString;

	const FProject_JAnimThreadSafeData& Data = ThreadSafeData;
	const FProject_JAnimMotionMatchingPostSelectionData& PostSelection =
		GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().MotionMatching.PostSelection;
	const bool bSprintAllowed = OwningPlayerCharacter && OwningPlayerCharacter->IsSprintLocomotionAllowed();
	const bool bJumpAllowed = OwningPlayerCharacter && OwningPlayerCharacter->IsJumpLocomotionAllowed();
	const UProject_JWeaponAnimProfile* WeaponAnimProfile = OwningPlayerCharacter ? OwningPlayerCharacter->GetWeaponAnimProfile() : nullptr;
	return FString::Printf(
		TEXT("Optimization Tier=%d UpdateData=%s FullChooser=%s FarOnly=%s MMInterval=%.3f ActivePSD=%s\n")
		TEXT("Weapon Profile=%s Stance=%s Presentation=%d\n")
		TEXT("Movement GroundSpeed=%.1f VerticalSpeed=%.1f AccelRatio=%.2f RelAccel=(%.2f,%.2f) Lean=(%.2f,%.2f) Decel=%s StopDist=%.1f VelocityToInput=%.1f HasTrajectory=%s StoppedAccel=%s\n")
		TEXT("Input Has=%s Size=%.2f Held=%.2f Turn=%.1f SharpTurn=%s MoveDir=%.1f\n")
		TEXT("Context Gait=%s Rotation=%s Phase=%s Starting=%s Pivoting=%s TurnInPlace=%s Spin=%s DesiredYaw=%.1f\n")
		TEXT("Policy SprintAllowed=%s JumpAllowed=%s Combat=%s Attack=%s Dodge=%s HitReact=%s\n")
		TEXT("Ground Mode=%d Start=%s Stop=%s WantsSprint=%s UseSprint=%s StartSprint=%s StopSprint=%s\n")
		TEXT("Air InAir=%s Jumping=%s FallOff=%s Landing=%s HeavyLand=%s LandMoving=%s LandSprint=%s\n")
		TEXT("MM Revision=%d Changed=%s ForceReselect=%s TrajectorySamples=%d NativePoseHistory=true\n")
		TEXT("Trajectory GenerationFrame=%llu SnapshotFrame=%llu Age=%.3f Usable=%s Reset=%d/%d AfterConsumption=%d/%d\n")
		TEXT("MM RepresentativeNode=%d Candidates=%d CaptureFrame=%llu CachedWeight=%.3f ResultChangedFrame=%llu FinalContributionUnverified=true\n")
		TEXT("Chooser Start=%s Stop=%s RunLoc=%s RemoteRunLoc=%s SprintLoc=%s Jump=%s Fall=%s Land=%s Combat=%s Remote=%s"),
		static_cast<int32>(CurrentOptimizationPolicy.Tier),
		CurrentOptimizationPolicy.bUpdateAnimationData ? TEXT("true") : TEXT("false"),
		CurrentOptimizationPolicy.bUseFullChooserRows ? TEXT("true") : TEXT("false"),
		CurrentOptimizationPolicy.bUseFarChooserRowsOnly ? TEXT("true") : TEXT("false"),
		CurrentOptimizationPolicy.MotionMatchingUpdateInterval,
		*GetNameSafe(CurrentActivePoseSearchDatabase),
		*GetNameSafe(WeaponAnimProfile),
		WeaponAnimProfile ? ToDebugString(WeaponAnimProfile->WeaponStance) : TEXT("None"),
		static_cast<int32>(Data.Combat.PresentationMode),
		Data.Movement.GroundSpeed,
		Data.Movement.VerticalSpeed,
		Data.Movement.AccelerationRatio,
		Data.Movement.RelativeAccelerationAmount.X,
		Data.Movement.RelativeAccelerationAmount.Y,
		Data.Movement.LeanAmount.X,
		Data.Movement.LeanAmount.Y,
		Data.Movement.bIsDecelerating ? TEXT("true") : TEXT("false"),
		Data.Movement.PredictedStopDistance,
		Data.Movement.VelocityToMoveInputAngle,
		Data.Movement.bHasTrajectory ? TEXT("true") : TEXT("false"),
		Data.Movement.bStoppedAcceleratingThisFrame ? TEXT("true") : TEXT("false"),
		Data.Input.bHasMoveInput ? TEXT("true") : TEXT("false"),
		Data.Input.MoveInputSize,
		Data.Input.MoveInputHeldTime,
		Data.Input.MoveInputTurnAngle,
		Data.Input.bSharpTurnRequested ? TEXT("true") : TEXT("false"),
		Data.Input.MovementDirection,
		ToDebugString(Data.LocomotionContext.GaitIntent),
		ToDebugString(Data.LocomotionContext.RotationMode),
		ToDebugString(Data.LocomotionContext.PhaseFamily),
		Data.LocomotionContext.bIsStarting ? TEXT("true") : TEXT("false"),
		Data.LocomotionContext.bIsPivoting ? TEXT("true") : TEXT("false"),
		Data.LocomotionContext.bShouldTurnInPlace ? TEXT("true") : TEXT("false"),
		Data.LocomotionContext.bShouldSpinTransition ? TEXT("true") : TEXT("false"),
		Data.LocomotionContext.DesiredFacingDeltaYaw,
		bSprintAllowed ? TEXT("true") : TEXT("false"),
		bJumpAllowed ? TEXT("true") : TEXT("false"),
		Data.Combat.bIsCombatMode ? TEXT("true") : TEXT("false"),
		Data.Combat.bIsAttacking ? TEXT("true") : TEXT("false"),
		Data.Combat.bIsDodging ? TEXT("true") : TEXT("false"),
		Data.Combat.bIsHitReacting ? TEXT("true") : TEXT("false"),
		static_cast<int32>(Data.Ground.GroundMotionMode),
		Data.Ground.bStartRequested ? TEXT("true") : TEXT("false"),
		Data.Ground.bStopRequested ? TEXT("true") : TEXT("false"),
		Data.Ground.bWantsSprint ? TEXT("true") : TEXT("false"),
		Data.Ground.bUseSprintLocomotion ? TEXT("true") : TEXT("false"),
		Data.Ground.bStartWasSprinting ? TEXT("true") : TEXT("false"),
		Data.Ground.bStopWasSprinting ? TEXT("true") : TEXT("false"),
		Data.Air.bIsInAir ? TEXT("true") : TEXT("false"),
		Data.Air.bIsJumping ? TEXT("true") : TEXT("false"),
		Data.Air.bIsFallOffStart ? TEXT("true") : TEXT("false"),
		Data.Landing.bIsLanding ? TEXT("true") : TEXT("false"),
		Data.Landing.bUseHeavyLand ? TEXT("true") : TEXT("false"),
		Data.Landing.bLandWasMoving ? TEXT("true") : TEXT("false"),
		Data.Landing.bLandWasSprinting ? TEXT("true") : TEXT("false"),
		Data.MotionMatching.SelectionRevision,
		Data.MotionMatching.bSelectionChanged ? TEXT("true") : TEXT("false"),
		Data.MotionMatching.bForceReselect ? TEXT("true") : TEXT("false"),
		Data.MotionMatching.TrajectorySampleCount,
		Data.Movement.TrajectoryGenerationFrame,
		Data.Movement.SnapshotFrame,
		Data.Movement.TrajectoryAgeSeconds,
		Data.Movement.bTrajectoryPredictionUsable ? TEXT("true") : TEXT("false"),
		Data.Movement.TrajectoryResetRevision,
		static_cast<int32>(Data.Movement.TrajectoryResetReason),
		Data.Movement.TrajectoryResetRevisionAfterConsumption,
		static_cast<int32>(Data.Movement.TrajectoryResetReasonAfterConsumption),
		PostSelection.ProducerNodeIndex,
		PostSelection.ResultCandidateCount,
		PostSelection.CaptureFrame,
		PostSelection.CachedNodeWeight,
		PostSelection.ResultLastChangedFrame,
		bChooserStartRequested ? TEXT("true") : TEXT("false"),
		bChooserStopRequested ? TEXT("true") : TEXT("false"),
		bChooserUseRunLocomotion ? TEXT("true") : TEXT("false"),
		bChooserUseRemoteRunLocomotion ? TEXT("true") : TEXT("false"),
		bChooserUseSprintLocomotionRow ? TEXT("true") : TEXT("false"),
		bChooserUseJumpStart ? TEXT("true") : TEXT("false"),
		(bChooserUseFallOff || bChooserUseFallLoop) ? TEXT("true") : TEXT("false"),
		bChooserIsLanding ? TEXT("true") : TEXT("false"),
		bChooserIsCombatMode ? TEXT("true") : TEXT("false"),
		bChooserIsRemoteProxy ? TEXT("true") : TEXT("false"));
}

FName UProject_JCharacterAnimInstance::GetThreadSafeMotionMatchingSelectedAnimation() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().MotionMatching.PostSelection.SelectedAnimation;
}

float UProject_JCharacterAnimInstance::GetThreadSafeMotionMatchingSelectedAnimationProgress() const
{
	const FProject_JAnimMotionMatchingPostSelectionData& PostSelection =
		GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().MotionMatching.PostSelection;
	return PostSelection.SelectedAnimationLength > UE_KINDA_SMALL_NUMBER
		? FMath::Clamp(PostSelection.SelectedAnimationTime / PostSelection.SelectedAnimationLength, 0.0f, 1.0f)
		: 0.0f;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeMotionMatchingSelectionIsContinuing() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().MotionMatching.PostSelection.bIsContinuingPoseSearch;
}

FString UProject_JCharacterAnimInstance::GetMotionMatchingTraceSummary() const
{
	using Project_J::LocomotionDebug::ToDebugString;

	FString Summary = FString::Printf(TEXT("==== Motion Matching Trace (%d entries) ====\n"), MotionMatchingTrace.Num());
	for (int32 EntryIndex = 0; EntryIndex < MotionMatchingTrace.Num(); ++EntryIndex)
	{
		const FProject_JMotionMatchingTraceEntry& Entry = MotionMatchingTrace[EntryIndex];
		const double UntilNextSeconds = EntryIndex + 1 < MotionMatchingTrace.Num()
			? MotionMatchingTrace[EntryIndex + 1].WorldTimeSeconds - Entry.WorldTimeSeconds
			: -1.0;
		Summary += FString::Printf(
			TEXT("t=%.3f Next=%.3f Rev=%d PSD=%s Anim=%s AnimTime=%.3f/%.3f PlayRate=%.2f Continuing=%s CurveSpeed=%.1f Warp=%.2f CurvePhase=%.2f Phase=%s Ground=%d Age=%.3f Gait=%s Rotation=%s SettledCycle=%s Speed=%.1f FutureSpeed=%.1f Gain=%.1f FutureTurn=%.1f FutureValid=%s StopDist=%.1f Input=%s Accel=%s Decel=%s InputTurn=%.1f Trajectory=%d DBChanged=%s ForceReselect=%s\n"),
			Entry.WorldTimeSeconds,
			UntilNextSeconds,
			Entry.SelectionRevision,
			*Entry.DatabaseName,
			*Entry.SelectedAnimationName,
			Entry.SelectedAnimationTime,
			Entry.SelectedAnimationLength,
			Entry.SelectedAnimationWantedPlayRate,
			Entry.bSelectionIsContinuing ? TEXT("true") : TEXT("false"),
			Entry.MoveDataSpeedCurve,
			Entry.EnableWarpingCurve,
			Entry.PhaseCurve,
			ToDebugString(Entry.PhaseFamily),
			static_cast<int32>(Entry.GroundMotionMode),
			Entry.GroundModeAgeSeconds,
			ToDebugString(Entry.GaitIntent),
			ToDebugString(Entry.RotationMode),
			Entry.bUseSettledCycle ? TEXT("true") : TEXT("false"),
			Entry.GroundSpeed,
			Entry.FutureTrajectorySpeed,
			Entry.PredictedSpeedGain,
			Entry.FutureTrajectoryTurnAngle,
			Entry.bHasFutureTrajectoryVelocity ? TEXT("true") : TEXT("false"),
			Entry.PredictedStopDistance,
			Entry.bHasMoveInput ? TEXT("true") : TEXT("false"),
			Entry.bIsAccelerating ? TEXT("true") : TEXT("false"),
			Entry.bIsDecelerating ? TEXT("true") : TEXT("false"),
			Entry.InputTurnAngle,
			Entry.TrajectorySampleCount,
			Entry.bDatabaseChanged ? TEXT("true") : TEXT("false"),
			Entry.bForceReselect ? TEXT("true") : TEXT("false"));
	}
	return Summary;
}

FString UProject_JCharacterAnimInstance::GetMotionMatchingPivotTraceSummary() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetPivotTraceSummary();
}
