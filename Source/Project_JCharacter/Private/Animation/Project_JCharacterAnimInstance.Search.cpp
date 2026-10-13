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

void UProject_JCharacterAnimInstance::PublishThreadSafeDataToProxy(FProject_JAnimThreadSafeData& Data)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_AnimPublishProxy);
	UpdateLocomotionSteeringData(Data);
	UpdateGeneralTurnData(Data);
	UpdateBackwardRefaceData(Data);
	const bool bMotionMatchingEnabled =
		IsPrimaryMeshAnimInstance() &&
		OwningCharacter &&
		!IsDedicatedServerAnimationContext() &&
		Data.LocomotionMode == EProject_JAnimationLocomotionMode::OnFoot;
	const bool bForceMotionMatchingRefresh = bMotionMatchingEnabled && ShouldForceMotionMatchingContextRefresh(Data);
	const bool bForceRemoteCombatStopReselect =
		bForceMotionMatchingRefresh &&
		OwningCharacter &&
		OwningCharacter->GetLocalRole() == ROLE_SimulatedProxy &&
		Data.Combat.bIsCombatMode &&
		Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe &&
		Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Stop;
	const bool bUpdateMotionMatchingThisFrame =
		bMotionMatchingEnabled &&
		ShouldEvaluateMotionMatchingThisFrame(Data.DeltaTime, bForceMotionMatchingRefresh);
	UPoseSearchDatabase* PreviousActiveDatabase = CurrentActivePoseSearchDatabase.Get();

	if (bUpdateMotionMatchingThisFrame)
	{
		// Chooser properties only need refreshing when we're actually re-evaluating the PSD this frame.
		// Skipping on throttled frames (mid/far distance, hidden remote) reduces Game Thread cost
		// proportionally to how aggressively the Motion Matching update interval is throttled.
		PublishChooserProperties(Data);
		CurrentActivePoseSearchDatabase = EvaluatePoseSearchDatabaseOnGameThread(Data);
		CurrentTurnCycleCompanion = EvaluateTurnCycleCompanionOnGameThread(Data);
		CacheEvaluatedMotionMatchingContext(Data);
	}
	if (!bMotionMatchingEnabled)
	{
		CurrentActivePoseSearchDatabase = nullptr;
		CurrentTurnCycleCompanion = nullptr;
		MotionMatchingRuntime.InvalidateContext();
	}
	const bool bForceMotionMatchingReselect = ShouldForceMotionMatchingReselect(Data) || bForceRemoteCombatStopReselect;
	const bool bDatabaseChanged = PreviousActiveDatabase != CurrentActivePoseSearchDatabase.Get();
	if (bUpdateMotionMatchingThisFrame && (bForceMotionMatchingRefresh || bDatabaseChanged || bForceMotionMatchingReselect))
	{
		RecordMotionMatchingTrace(Data, bDatabaseChanged, bForceMotionMatchingReselect);
		if (Project_J::MotionMatchingCVars::GetNetworkDebugMode() > 0)
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("MMNetSelection Actor=%s Rev=%d PSD=%s Changed=%s ForceReselect=%s Phase=%d Gait=%d Rotation=%d"),
				*GetNameSafe(OwningCharacter),
				Data.MotionMatching.SelectionRevision,
				*GetNameSafe(CurrentActivePoseSearchDatabase.Get()),
				bDatabaseChanged ? TEXT("true") : TEXT("false"),
				bForceMotionMatchingReselect ? TEXT("true") : TEXT("false"),
				static_cast<int32>(Data.MotionMatching.SelectionContext.PhaseFamily),
				static_cast<int32>(Data.MotionMatching.SelectionContext.GaitIntent),
				static_cast<int32>(Data.MotionMatching.SelectionContext.RotationMode));
		}
	}

	FProject_JCharacterAnimInstanceProxy& ProjectProxy = GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
	ProjectProxy.SetFlowTraceEnabled(Project_J::AnimationFlowDebug::ShouldCapture(OwningCharacter) && IsPrimaryMeshAnimInstance());
	const int32 TurnTraceMode = Project_J::MotionMatchingCVars::GetMovingTurnTraceMode();
	const bool bLocalTurnTrace = TurnTraceMode > 0 && OwningPlayerCharacter &&
		OwningPlayerCharacter->IsPlayerControlled() && OwningPlayerCharacter->IsLocallyControlled();
	ProjectProxy.SetMovingTurnTraceEnabled(bLocalTurnTrace);
#if !UE_BUILD_SHIPPING
	if (bLocalTurnTrace && IsPrimaryMeshAnimInstance() &&
		(TurnTraceMode >= 2 || bDatabaseChanged || bForceMotionMatchingReselect || GFrameCounter % 12 == 0))
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("MovingTurnTrace Stage=Selection Frame=%llu Actor=%s Instance=%s Rev=%d Mode=%d Gait=%d Phase=%d Turn180=%d PSD=%s Enabled=%d Update=%d Refresh=%d Changed=%d Force=%d Override=%d Presentation=%d Budget=%.3f"),
			GFrameCounter, *GetNameSafe(OwningPlayerCharacter), *GetName(), Data.MotionMatching.SelectionRevision,
			static_cast<int32>(Data.MotionMatching.SelectionContext.RotationMode), static_cast<int32>(Data.MotionMatching.SelectionContext.GaitIntent),
			static_cast<int32>(Data.MotionMatching.SelectionContext.PhaseFamily), Data.MotionMatching.SelectionContext.bMovingTurn180 ? 1 : 0,
			*GetNameSafe(CurrentActivePoseSearchDatabase.Get()), bMotionMatchingEnabled ? 1 : 0, bUpdateMotionMatchingThisFrame ? 1 : 0,
			bForceMotionMatchingRefresh ? 1 : 0, bDatabaseChanged ? 1 : 0, bForceMotionMatchingReselect ? 1 : 0,
			Data.OneShotPresentation.bShouldOverrideMotionMatching ? 1 : 0,
			static_cast<int32>(Data.OneShotPresentation.PresentationState), Data.MotionMatching.MinimumSearchInterval);
	}
#endif
	UpdateStrafeFacingData(Data);
	ProjectProxy.QueueGameThreadData(
		Data,
		CurrentActivePoseSearchDatabase,
		bMotionMatchingEnabled,
		bUpdateMotionMatchingThisFrame,
		bForceMotionMatchingReselect, true, CurrentTurnCycleCompanion);
	RecordAnimationFlowPublication(Data, bUpdateMotionMatchingThisFrame, true);
}

bool UProject_JCharacterAnimInstance::IsPrimaryMeshAnimInstance() const
{
	return OwningCharacter &&
		OwningCharacter->GetMesh() &&
		OwningCharacter->GetMesh()->GetAnimInstance() == this;
}

void UProject_JCharacterAnimInstance::RecordMotionMatchingTrace(
	const FProject_JAnimThreadSafeData& Data,
	bool bDatabaseChanged,
	bool bForceReselect)
{
	FProject_JMotionMatchingTraceEntry& Entry = MotionMatchingTrace.AddDefaulted_GetRef();
	Entry.WorldTimeSeconds = OwningCharacter && OwningCharacter->GetWorld()
		? OwningCharacter->GetWorld()->GetTimeSeconds()
		: 0.0;
	Entry.SelectionRevision = Data.MotionMatching.SelectionRevision;
	Entry.DatabaseName = GetNameSafe(CurrentActivePoseSearchDatabase);
	Entry.SelectedAnimationName = Data.MotionMatching.PostSelection.SelectedAnimation.ToString();
	Entry.SelectedAnimationTime = Data.MotionMatching.PostSelection.SelectedAnimationTime;
	Entry.SelectedAnimationLength = Data.MotionMatching.PostSelection.SelectedAnimationLength;
	Entry.SelectedAnimationWantedPlayRate = Data.MotionMatching.PostSelection.WantedPlayRate;
	Entry.bSelectionIsContinuing = Data.MotionMatching.PostSelection.bIsContinuingPoseSearch;
	Entry.MoveDataSpeedCurve = GetCurveValue(TEXT("movedata_speed"));
	Entry.EnableWarpingCurve = GetCurveValue(TEXT("enable_warping"));
	Entry.PhaseCurve = GetCurveValue(TEXT("phase"));
	Entry.PhaseFamily = Data.LocomotionContext.PhaseFamily;
	Entry.GaitIntent = Data.LocomotionContext.GaitIntent;
	Entry.RotationMode = Data.LocomotionContext.RotationMode;
	Entry.bUseSettledCycle = Data.MotionMatching.SelectionContext.bUseSettledCycle;
	Entry.GroundMotionMode = Data.Ground.GroundMotionMode;
	Entry.GroundModeAgeSeconds = Data.Ground.GroundMotionModeElapsedTime;
	Entry.GroundSpeed = Data.Movement.GroundSpeed;
	Entry.PredictedSpeedGain = Data.Movement.PredictedSpeedGain;
	Entry.FutureTrajectorySpeed = Data.Movement.FutureTrajectorySpeed;
	Entry.FutureTrajectoryTurnAngle = Data.Movement.FutureTrajectoryTurnAngle;
	Entry.PredictedStopDistance = Data.Movement.PredictedStopDistance;
	Entry.InputTurnAngle = Data.Input.MoveInputTurnAngle;
	Entry.TrajectorySampleCount = Data.MotionMatching.TrajectorySampleCount;
	Entry.bHasMoveInput = Data.Input.bHasMoveInput;
	Entry.bIsAccelerating = Data.Movement.bIsAccelerating;
	Entry.bIsDecelerating = Data.Movement.bIsDecelerating;
	Entry.bHasFutureTrajectoryVelocity = Data.Movement.bHasFutureTrajectoryVelocity;
	Entry.bDatabaseChanged = bDatabaseChanged;
	Entry.bForceReselect = bForceReselect;

	const int32 MaxEntries = FMath::Max(MaxMotionMatchingTraceEntries, 1);
	if (MotionMatchingTrace.Num() > MaxEntries)
	{
		MotionMatchingTrace.RemoveAt(0, MotionMatchingTrace.Num() - MaxEntries, EAllowShrinking::No);
	}
}

UPoseSearchDatabase* UProject_JCharacterAnimInstance::EvaluateTurnCycleCompanionOnGameThread(const FProject_JAnimThreadSafeData& Data) const
{
	// This broadens only qualified local running turn windows. One-shots, proxies,
	// mounted/full-body action owners and crowd tiers keep their existing search set.
	if (!Project_J::MotionMatchingCVars::ShouldUseTurnCycleCandidates() || !OwningPlayerCharacter || !OwningPlayerCharacter->IsPlayerControlled() ||
		!OwningPlayerCharacter->IsLocallyControlled() || Data.Air.bIsInAir ||
		Data.LocomotionMode != EProject_JAnimationLocomotionMode::OnFoot ||
		Data.OneShotPresentation.bShouldOverrideMotionMatching ||
		(!Data.MotionMatching.SelectionContext.bMovingTurn180 && !Data.MotionMatching.SelectionContext.bGeneralTurnCandidates &&
			!Data.MotionMatching.SelectionContext.bBackwardRefaceCandidates)) return nullptr;
	const bool bCombat = Data.Combat.bIsCombatMode &&
		Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe;
	const auto* Set = bCombat ? OwningPlayerCharacter->GetCombatStrafeMotionMatchingAssetSet() :
		OwningPlayerCharacter->GetMotionMatchingAssetSet();
	if (!Set) return nullptr;
	auto Context = Data.MotionMatching.SelectionContext;
	Context.bUseGenericFamiliesForNonOrientToMovement = bCombat;
	return Set->FindTurnCycleCompanion(Context, CurrentActivePoseSearchDatabase);
}

UPoseSearchDatabase* UProject_JCharacterAnimInstance::EvaluatePoseSearchDatabaseOnGameThread(const FProject_JAnimThreadSafeData& Data)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_AnimPoseSearchDatabaseChooser);
	if (!OwningCharacter || IsDedicatedServerAnimationContext())
	{
		return nullptr;
	}

	const UProject_JMotionMatchingAssetSet* AssetSet = OwningPlayerCharacter
		? OwningPlayerCharacter->GetMotionMatchingAssetSet()
		: nullptr;
	const bool bUsesCombatStrafe =
		OwningPlayerCharacter && Data.Combat.bIsCombatMode &&
		Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe;
	const UProject_JMotionMatchingAssetSet* CombatStrafeAssetSet = bUsesCombatStrafe
			? OwningPlayerCharacter->GetCombatStrafeMotionMatchingAssetSet()
			: nullptr;
	UPoseSearchDatabase* IdleDatabase = AssetSet && AssetSet->IdlePoseSearchDatabase
		? AssetSet->IdlePoseSearchDatabase.Get()
		: DefaultIdlePoseSearchDatabase.Get();
	UPoseSearchDatabase* LocomotionDatabase = AssetSet && AssetSet->DefaultPoseSearchDatabase
		? AssetSet->DefaultPoseSearchDatabase.Get()
		: DefaultPoseSearchDatabase.Get();
	const FProject_JMotionMatchingSelectionContext& SelectionContext = Data.MotionMatching.SelectionContext;
	FProject_JMotionMatchingSelectionContext CombatSelectionContext = SelectionContext;
	// State Controller owns authored Start, Stop, Pivot, air and landing one-shots.
	// The Asset Set owns only continuous Cycle and optional moving TurnRedirect PSDs.
	switch (SelectionContext.PhaseFamily)
	{
	case EProject_JLocomotionPhaseFamily::Idle:
	case EProject_JLocomotionPhaseFamily::Cycle:
	case EProject_JLocomotionPhaseFamily::Turn:
		CombatSelectionContext.PhaseFamily = SelectionContext.PhaseFamily;
		break;
	case EProject_JLocomotionPhaseFamily::TurnInPlace:
		// A Turn In Place is a direct Blend Stack one-shot while the capsule is
		// stationary.  Keep Combat Idle evaluated underneath it so that releasing
		// the one-shot cannot reveal a Run Cycle pose for one blend-out frame.
		CombatSelectionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Idle;
		break;
	default:
		// Keep a stable locomotion pose beneath a direct State Controller one-shot.
		CombatSelectionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
		break;
	}
	CombatSelectionContext.bUseGenericFamiliesForNonOrientToMovement = true;
	if (CombatStrafeAssetSet && SelectionContext.bAllowTurnContinuation &&
		CombatStrafeAssetSet->RunDatabases.TurnRedirect &&
		Data.MotionMatching.PostSelection.SelectedDatabase == CombatStrafeAssetSet->RunDatabases.TurnRedirect->GetFName())
	{
		// A loop-only optimization must not retire an aligned Turn that is still
		// the actual continuing pose. Resume settled routing once Cycle wins.
		CombatSelectionContext.bUseSettledCycle = false;
	}
	if (CombatSelectionContext.bBackwardRefaceCandidates || CombatSelectionContext.bAllowBackwardRefaceContinuation ||
		CombatSelectionContext.bGeneralTurnCandidates ||
		(CombatSelectionContext.bAllowGeneralTurnContinuation && CombatStrafeAssetSet &&
			CombatStrafeAssetSet->RunDatabases.GeneralTurn && Data.MotionMatching.PostSelection.SelectedDatabase ==
			CombatStrafeAssetSet->RunDatabases.GeneralTurn->GetFName()) ||
		CombatSelectionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Turn)
	{
		// A missing or ineligible Turn must return through the Dynamic Cycle,
		// never a stale Loop-only settled selection from the previous snapshot.
		CombatSelectionContext.bUseSettledCycle = false;
	}
	UPoseSearchDatabase* SelectedDatabase = CombatStrafeAssetSet
		? CombatStrafeAssetSet->FindDatabaseForContext(CombatSelectionContext)
		: nullptr;
	if (!SelectedDatabase &&
		CombatStrafeAssetSet &&
		CombatSelectionContext.GaitIntent == EProject_JLocomotionGaitIntent::Sprint &&
		(CombatSelectionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Cycle ||
			CombatSelectionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Turn))
	{
		// Combat sprint is intentionally limited to forward / forward-diagonal input.
		// Until dedicated sprint Cycle or TurnRedirect PSDs are assigned, retain
		// a coherent combat result by falling back to the Run family, never OTM.
		FProject_JMotionMatchingSelectionContext RunFallbackContext = CombatSelectionContext;
		RunFallbackContext.GaitIntent = EProject_JLocomotionGaitIntent::Run;
		SelectedDatabase = CombatStrafeAssetSet->FindDatabaseForContext(RunFallbackContext);
	}
	bool bSelectedCombatStrafeDatabase = SelectedDatabase != nullptr;
	if (!SelectedDatabase && !bUsesCombatStrafe && AssetSet)
	{
		SelectedDatabase = AssetSet->FindDatabaseForContext(SelectionContext);
	}

	// An upper-body overlay may use the normal lower-body loops only when no
	// combat loop set is assigned. It must never borrow an OTM one-shot PSD.
	if (!SelectedDatabase &&
		!CombatStrafeAssetSet &&
		Data.Combat.bIsCombatMode &&
		Data.Combat.PresentationMode == EProject_JCombatAnimationPresentationMode::UpperBodyOverlay &&
		Data.LocomotionContext.RotationMode != EProject_JLocomotionRotationMode::OrientToMovement &&
		AssetSet)
	{
		FProject_JMotionMatchingSelectionContext OverlayFallbackContext = SelectionContext;
		OverlayFallbackContext.RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
		SelectedDatabase = AssetSet->FindDatabaseForContext(OverlayFallbackContext);
	}
	if (!SelectedDatabase)
	{
		if (bUsesCombatStrafe && CombatStrafeAssetSet)
		{
			// The combat DA intentionally uses DefaultPoseSearchDatabase for Idle
			// (see DA_Player_Combat_Strafe). A missing moving slot must therefore
			// retry the appropriate Cycle family before using that default; otherwise
			// an incomplete Sprint/Turn setup would visibly fall back to idle.
			if (CombatSelectionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Idle)
			{
				SelectedDatabase = CombatStrafeAssetSet->IdlePoseSearchDatabase.Get();
			}
			else
			{
				FProject_JMotionMatchingSelectionContext CycleFallbackContext = CombatSelectionContext;
				CycleFallbackContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
				SelectedDatabase = CombatStrafeAssetSet->FindDatabaseForContext(CycleFallbackContext);
				if (!SelectedDatabase && CycleFallbackContext.GaitIntent == EProject_JLocomotionGaitIntent::Sprint)
				{
					CycleFallbackContext.GaitIntent = EProject_JLocomotionGaitIntent::Run;
					SelectedDatabase = CombatStrafeAssetSet->FindDatabaseForContext(CycleFallbackContext);
				}
			}

			// Keep this last-resort fallback inside the combat DA. It must never
			// borrow an Orient-to-Movement database while combat owns the body.
			if (!SelectedDatabase)
			{
				SelectedDatabase = CombatStrafeAssetSet->DefaultPoseSearchDatabase.Get();
			}
			bSelectedCombatStrafeDatabase = SelectedDatabase != nullptr;
		}
	}
	if (!SelectedDatabase)
	{
		SelectedDatabase = Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Idle && IdleDatabase
			? IdleDatabase
			: LocomotionDatabase;
	}
	const UChooserTable* ChooserTable = bSelectedCombatStrafeDatabase || SelectionContext.bMovingTurn180 || SelectionContext.bGeneralTurnCandidates || SelectionContext.bAllowGeneralTurnContinuation || SelectionContext.bBackwardRefaceCandidates || SelectionContext.bAllowBackwardRefaceContinuation
		? nullptr
		: MotionMatchingChooserTable.Get();

	bool bIsFarDistance = false;
	if (AProject_JBaseCharacter* BaseChar = Cast<AProject_JBaseCharacter>(OwningCharacter))
	{
		bIsFarDistance = BaseChar->GetSignificance() >= 2.0f;
	}

	if (ShouldDisableMotionMatchingBeyondFarDistance() && bIsFarDistance)
	{
		return nullptr;
	}

	if (!ChooserTable)
	{
		return SelectedDatabase;
	}

	FChooserEvaluationContext ChooserContext;
	ChooserContext.AddObjectParam(const_cast<UProject_JCharacterAnimInstance*>(this));
	FChooserPlayerSettings ChooserPlayerSettings;
	ChooserContext.AddStructParam(ChooserPlayerSettings);

	const FInstancedStruct ChooserObject = UChooserFunctionLibrary::MakeEvaluateChooser(const_cast<UChooserTable*>(ChooserTable));
	if (!ChooserObject.IsValid())
	{
		return SelectedDatabase;
	}

	UObject* ResultObject = UChooserFunctionLibrary::EvaluateObjectChooserBase(
		ChooserContext,
		ChooserObject,
		UPoseSearchDatabase::StaticClass());

	if (UPoseSearchDatabase* ResultDatabase = Cast<UPoseSearchDatabase>(ResultObject))
	{
		// An upper-body combat overlay deliberately keeps the shared Motion
		// Matching lower body.  A combat-specific Chooser row must therefore
		// never replace a valid moving/start/stop database with the idle
		// database; doing so freezes the legs while the CharacterMovement
		// component continues moving.  Full-body combat locomotion remains free
		// to provide its own database selection through its presentation layer.
		const bool bUsesSharedCombatLowerBody =
			Data.Combat.bIsCombatMode &&
			Data.Combat.PresentationMode == EProject_JCombatAnimationPresentationMode::UpperBodyOverlay;
		const bool bHasNonIdleLocomotionContext =
			Data.Ground.GroundMotionMode != EProject_JGroundMotionMode::Idle ||
			Data.LocomotionContext.PhaseFamily != EProject_JLocomotionPhaseFamily::Idle;
		const bool bChooserWouldReplaceMovingPoseWithIdle =
			bUsesSharedCombatLowerBody &&
			bHasNonIdleLocomotionContext &&
			SelectedDatabase &&
			ResultDatabase == IdleDatabase;

		if (!bChooserWouldReplaceMovingPoseWithIdle)
		{
			SelectedDatabase = ResultDatabase;
		}
	}

	return SelectedDatabase;
}

void UProject_JCharacterAnimInstance::PublishChooserProperties(const FProject_JAnimThreadSafeData& Data)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_AnimPublishChooserProperties);
	const FProject_JAnimOptimizationPolicy OptimizationPolicy = BuildOptimizationPolicy();
	ApplyOptimizationPolicy(OptimizationPolicy);

	PublishChooserMovementProperties(Data);
	PublishChooserGroundProperties(Data);
	PublishChooserAirProperties(Data);
	PublishChooserLandingProperties(Data);
	PublishChooserCombatProperties(Data);

	if (OptimizationPolicy.bUseFarChooserRowsOnly)
	{
		ApplyFarChooserOverrides(Data);
	}
}

void UProject_JCharacterAnimInstance::PublishChooserMovementProperties(const FProject_JAnimThreadSafeData& Data)
{
	ChooserVerticalSpeed = Data.Movement.VerticalSpeed;
	ChooserAccelerationRatio = Data.Movement.AccelerationRatio;
	ChooserMoveInputSize = Data.Input.MoveInputSize;
	ChooserMoveInputHeldTime = Data.Input.MoveInputHeldTime;
	ChooserMoveInputTurnAngle = Data.Input.MoveInputTurnAngle;
	bChooserHasMoveInput = Data.Input.bHasMoveInput;
	bChooserSharpTurnRequested = Data.Input.bSharpTurnRequested;
	bChooserIsRemoteProxy = OwningPlayerCharacter && !IsLocallyControlledCharacter();
}

void UProject_JCharacterAnimInstance::PublishChooserGroundProperties(const FProject_JAnimThreadSafeData& Data)
{
	const bool bDerivedStart = Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Start;
	const bool bDerivedStop = Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Stop;
	const bool bDerivedCycle = Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Cycle;
	const bool bDerivedIdle = Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Idle;

	bChooserStartRequested = Data.Ground.bStartRequested || bDerivedStart;
	bChooserStopRequested = Data.Ground.bStopRequested || bDerivedStop;
	bChooserWantsSprint = Data.Ground.bWantsSprint;
	bChooserUseSprintLocomotion =
		(Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion || bDerivedCycle) &&
		Data.Ground.bUseSprintLocomotion;
	bChooserUseRunStart = bChooserStartRequested && !Data.Ground.bStartWasSprinting && !bChooserIsRemoteProxy;
	bChooserUseRemoteRunStart = bChooserStartRequested && !Data.Ground.bStartWasSprinting && bChooserIsRemoteProxy;
	bChooserUseSprintStart = bChooserStartRequested && Data.Ground.bStartWasSprinting;
	bChooserUseRunStop = bChooserStopRequested && !Data.Ground.bStopWasSprinting;
	bChooserUseSprintStop = bChooserStopRequested && Data.Ground.bStopWasSprinting;
	bChooserUseRunLocomotion =
		(Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion || bDerivedCycle) &&
		!Data.Ground.bUseSprintLocomotion &&
		!bChooserIsRemoteProxy;
	bChooserUseRemoteRunLocomotion =
		(Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion || bDerivedCycle) &&
		!Data.Ground.bUseSprintLocomotion &&
		bChooserIsRemoteProxy;
	bChooserUseSprintLocomotionRow =
		(Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion || bDerivedCycle) &&
		Data.Ground.bUseSprintLocomotion;
	bChooserStartWasSprinting = Data.Ground.bStartWasSprinting;
	bChooserStopWasSprinting = Data.Ground.bStopWasSprinting;
	bChooserIsIdle = Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Idle || bDerivedIdle;
	ChooserGroundMotionMode = Data.Ground.GroundMotionMode;
	ChooserGaitIntent = Data.LocomotionContext.GaitIntent;
	ChooserRotationMode = Data.LocomotionContext.RotationMode;
	ChooserDesiredFacingDeltaYaw = Data.LocomotionContext.DesiredFacingDeltaYaw;
	bChooserIsStartingDerived = Data.LocomotionContext.bIsStarting;
	bChooserIsPivoting = Data.LocomotionContext.bIsPivoting;
	bChooserShouldTurnInPlace = Data.LocomotionContext.bShouldTurnInPlace;
	bChooserShouldSpinTransition = Data.LocomotionContext.bShouldSpinTransition;
}

void UProject_JCharacterAnimInstance::PublishChooserAirProperties(const FProject_JAnimThreadSafeData& Data)
{
	bChooserUseJumpStart =
		Data.Air.bIsJumping &&
		!Data.Landing.bIsLanding;
	bChooserUseFallOff =
		Data.Air.bIsFallOffStart &&
		!Data.Air.bIsJumping &&
		!Data.Landing.bIsLanding;
	bChooserUseFallLoop =
		Data.Air.bIsInAir &&
		!Data.Air.bIsJumping &&
		!Data.Air.bIsFallOffStart &&
		!Data.Landing.bIsLanding;
	bChooserIsInAir = Data.Air.bIsInAir;
	bChooserIsJumping = Data.Air.bIsJumping;
}

void UProject_JCharacterAnimInstance::PublishChooserLandingProperties(const FProject_JAnimThreadSafeData& Data)
{
	ChooserLastFallSpeed = Data.Landing.LastFallSpeed;
	ChooserLandStartFallSpeed = Data.Landing.LandStartFallSpeed;
	bChooserUseLightLand =
		Data.Landing.bIsLanding &&
		!Data.Landing.bUseHeavyLand;
	bChooserUseHeavyLandRow =
		Data.Landing.bIsLanding &&
		Data.Landing.bUseHeavyLand;
	bChooserUseStandLightLand =
		Data.Landing.bIsLanding &&
		!Data.Landing.bUseHeavyLand &&
		!Data.Landing.bLandWasMoving;
	bChooserUseStandHeavyLand =
		Data.Landing.bIsLanding &&
		Data.Landing.bUseHeavyLand &&
		!Data.Landing.bLandWasMoving;
	bChooserUseRunLightLand =
		Data.Landing.bIsLanding &&
		!Data.Landing.bUseHeavyLand &&
		Data.Landing.bLandWasMoving &&
		!Data.Landing.bLandWasSprinting;
	bChooserUseSprintLightLand =
		Data.Landing.bIsLanding &&
		!Data.Landing.bUseHeavyLand &&
		Data.Landing.bLandWasMoving &&
		Data.Landing.bLandWasSprinting;
	bChooserUseRunHeavyLand =
		Data.Landing.bIsLanding &&
		Data.Landing.bUseHeavyLand &&
		Data.Landing.bLandWasMoving &&
		!Data.Landing.bLandWasSprinting;
	bChooserUseSprintHeavyLand =
		Data.Landing.bIsLanding &&
		Data.Landing.bUseHeavyLand &&
		Data.Landing.bLandWasMoving &&
		Data.Landing.bLandWasSprinting;
	bChooserLandWasSprinting = Data.Landing.bLandWasSprinting;
	bChooserLandWasMoving = Data.Landing.bLandWasMoving;
	bChooserIsLanding = Data.Landing.bIsLanding;
	bChooserUseHeavyLand = Data.Landing.bUseHeavyLand;
}

void UProject_JCharacterAnimInstance::PublishChooserCombatProperties(const FProject_JAnimThreadSafeData& Data)
{
	bChooserIsCombatMode = Data.Combat.bIsCombatMode;
}

void UProject_JCharacterAnimInstance::ApplyFarChooserOverrides(const FProject_JAnimThreadSafeData& Data)
{
	const FProject_JRemoteVisualLocomotionPolicy RemotePolicy = GetEffectiveRemoteVisualPolicy();
	if (!RemotePolicy.bDisableStartStopChooserBeyondFarDistance &&
		!RemotePolicy.bDisableLandChooserBeyondFarDistance)
	{
		return;
	}

	const bool bUseFarLocomotion =
		Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		!Data.Air.bIsInAir &&
		!Data.Air.bIsJumping;
	const bool bUseFarRunLocomotion = bUseFarLocomotion && !Data.Ground.bUseSprintLocomotion;
	const bool bUseFarSprintLocomotion = bUseFarLocomotion && Data.Ground.bUseSprintLocomotion;

	if (RemotePolicy.bDisableStartStopChooserBeyondFarDistance)
	{
		bChooserStartRequested = false;
		bChooserStopRequested = false;
		bChooserSharpTurnRequested = false;
		bChooserUseRunStart = false;
		bChooserUseRemoteRunStart = false;
		bChooserUseSprintStart = false;
		bChooserUseRunStop = false;
		bChooserUseSprintStop = false;
	}
	if (RemotePolicy.bDisableLandChooserBeyondFarDistance)
	{
		bChooserUseLightLand = false;
		bChooserUseHeavyLandRow = false;
		bChooserUseStandLightLand = false;
		bChooserUseStandHeavyLand = false;
		bChooserUseRunLightLand = false;
		bChooserUseSprintLightLand = false;
		bChooserUseRunHeavyLand = false;
		bChooserUseSprintHeavyLand = false;
		bChooserIsLanding = false;
		bChooserUseHeavyLand = false;
		bChooserLandWasMoving = false;
		bChooserLandWasSprinting = false;
	}
	bChooserUseSprintLocomotion = bUseFarSprintLocomotion;
	bChooserUseRunLocomotion = false;
	bChooserUseRemoteRunLocomotion = bUseFarRunLocomotion;
	bChooserUseSprintLocomotionRow = bUseFarSprintLocomotion;
	bChooserUseJumpStart = Data.Air.bIsJumping;
	bChooserUseFallLoop = Data.Air.bIsInAir && !Data.Air.bIsJumping;
	bChooserIsIdle = !Data.Air.bIsInAir && Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Idle;
}

bool UProject_JCharacterAnimInstance::ShouldEvaluateMotionMatchingThisFrame(float DeltaSeconds, bool bForceRefresh)
{
	if (!OwningCharacter || IsDedicatedServerAnimationContext())
	{
		MotionMatchingRuntime.Reset();
		return false;
	}

	ApplyOptimizationPolicy(BuildOptimizationPolicy());
	const float UpdateInterval = CurrentOptimizationPolicy.MotionMatchingUpdateInterval;
	// Stagger periodic GT Chooser/database selection. State changes and full-rate policies stay immediate.
	// The same budget interval is also published as a worker search floor. Phase
	// suppression and sparse transition searches remain owned by the proxy.
	return MotionMatchingRuntime.ShouldEvaluate(DeltaSeconds, UpdateInterval, OwningCharacter->GetUniqueID(), bForceRefresh);
}

static FProject_JMotionMatchingRuntime::FContext MakeMotionMatchingRuntimeContext(const FProject_JAnimThreadSafeData& Data)
{
	FProject_JMotionMatchingRuntime::FContext Context;
	Context.GroundMotionMode = Data.Ground.GroundMotionMode;
	Context.GaitIntent = Data.LocomotionContext.GaitIntent;
	Context.RotationMode = Data.LocomotionContext.RotationMode;
	Context.PhaseFamily = Data.LocomotionContext.PhaseFamily;
	Context.bStartRequested = Data.Ground.bStartRequested;
	Context.bStartWasSprinting = Data.Ground.bStartWasSprinting;
	Context.SelectionRevision = Data.MotionMatching.SelectionRevision;
	return Context;
}

bool UProject_JCharacterAnimInstance::ShouldForceMotionMatchingContextRefresh(const FProject_JAnimThreadSafeData& Data) const
{
	auto Context = MakeMotionMatchingRuntimeContext(Data);
	Context.bGeneralTurnCandidates = Data.MotionMatching.SelectionContext.bGeneralTurnCandidates;
	Context.bGeneralTurnContinuation = Data.MotionMatching.SelectionContext.bAllowGeneralTurnContinuation;
	return MotionMatchingRuntime.HasContextChanged(Context);
}

bool UProject_JCharacterAnimInstance::ShouldForceMotionMatchingReselect(const FProject_JAnimThreadSafeData& Data) const
{
	return Data.MotionMatching.bForceReselect;
}

void UProject_JCharacterAnimInstance::CacheEvaluatedMotionMatchingContext(const FProject_JAnimThreadSafeData& Data)
{
	auto Context = MakeMotionMatchingRuntimeContext(Data);
	Context.bGeneralTurnCandidates = Data.MotionMatching.SelectionContext.bGeneralTurnCandidates;
	Context.bGeneralTurnContinuation = Data.MotionMatching.SelectionContext.bAllowGeneralTurnContinuation;
	MotionMatchingRuntime.CacheEvaluatedContext(Context);
}
