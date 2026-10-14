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

#include "Animation/Project_JAnimationQualityPolicy.h"


FProject_JAnimOptimizationPolicy UProject_JCharacterAnimInstance::BuildOptimizationPolicy() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_AnimBuildOptimizationPolicy);
	ProjectJ::Animation::FQualityObservation Input;
	Input.bHasOwner = OwningCharacter != nullptr;
	FProject_JAnimationBudgetSettings Settings;
	if (OwningCharacter)
	{
		Input.bLocal = IsLocallyControlledCharacter();
		if (!Input.bLocal)
		{
			if (const auto* Mesh = Cast<UProject_JBudgetedSkeletalMeshComponent>(OwningCharacter->GetMesh()))
				Input.bGameplayPose = Mesh->IsCombatCritical() || Mesh->HasAnimationUpdateRequirement();
			if (!Input.bGameplayPose)
			{
				Input.bRecentlyRendered = WasOwnerRecentlyRendered(RecentlyRenderedTolerance);
				if (Input.bRecentlyRendered)
					if (const auto* Base = Cast<AProject_JBaseCharacter>(OwningCharacter))
					{ Input.bHasSignificance = true; Input.Significance = Base->GetSignificance(); }
				if (!Input.bRecentlyRendered || (Input.bHasSignificance && !(Input.Significance <= 0.f)))
					Settings = GetEffectiveAnimationBudgetSettings();
			}
		}
	}
	return ProjectJ::Animation::ResolveSourceQuality(Input, Settings);
}

void UProject_JCharacterAnimInstance::ApplyOptimizationPolicy(const FProject_JAnimOptimizationPolicy& NewPolicy)
{
	const bool bFollowerStateChanged = (CurrentOptimizationPolicy.bEnableFollowerRetarget != NewPolicy.bEnableFollowerRetarget);
	CurrentOptimizationPolicy = NewPolicy;

	const auto* Coordinator = OwningCharacter ? OwningCharacter->FindComponentByClass<UProject_JAnimationUpdateCoordinatorComponent>() : nullptr;
	const bool bVisualGroupOwned = Coordinator && Coordinator->IsVisualBudgetEnabled() &&
		Cast<UProject_JBudgetedSkeletalMeshComponent>(OwningCharacter->GetMesh());
	if (!bVisualGroupOwned && OwningCharacter && bFollowerStateChanged)
	{
		if (USkeletalMeshComponent* FollowerMesh = GetRuntimeRetargetFollowerMesh())
		{
			FollowerMesh->SetComponentTickEnabled(NewPolicy.bEnableFollowerRetarget);
		}
	}
}

bool UProject_JCharacterAnimInstance::NeedsFullRateVisualUpdate() const
{
	check(IsInGameThread());
	const auto& Selection = ThreadSafeData.MotionMatching.SelectionContext;
	return ThreadSafeData.OneShotPresentation.bShouldOverrideMotionMatching || ThreadSafeData.Air.bIsInAir ||
		Selection.bMovingTurn180 || Selection.bAllowTurnContinuation || Selection.bGeneralTurnCandidates ||
		Selection.bAllowGeneralTurnContinuation || Selection.bBackwardRefaceCandidates || Selection.bAllowBackwardRefaceContinuation;
}

void UProject_JCharacterAnimInstance::ResetTrajectoryHistoryOnAccelerationStop(const FProject_JAnimThreadSafeData& Data) const
{
	const bool bPreserveLocalCombatStrafeHistory =
		IsLocallyControlledCharacter() &&
		Data.Combat.bIsCombatMode &&
		Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe;
	if (!Data.Movement.bStoppedAcceleratingThisFrame || bPreserveLocalCombatStrafeHistory)
	{
		return;
	}

	// Combat Strafe Stop searches need the movement history that immediately precedes
	// input release. Resetting here only on the autonomous proxy made its Stop query
	// roughly one tenth as long as the simulated proxy query, causing wrong-direction
	// candidates to win. Normal locomotion retains its existing reset behavior.


	if (OwningCharacter &&
		OwningCharacter->GetLocalRole() == ROLE_SimulatedProxy &&
		Project_J::MotionMatchingCVars::ShouldDisableRemoteAccelerationReset())
	{
		// Simulated proxies do not own reliable input acceleration; replicated velocity can be valid while acceleration flickers.
		return;
	}

	if (CachedTrajectoryComponent)
	{
		CachedTrajectoryComponent->ResetTrajectoryHistoryWithReason(
			EProject_JTrajectoryResetReason::AccelerationStopped);
	}
}

float UProject_JCharacterAnimInstance::CalculateAimOffsetAlpha(const FProject_JAnimThreadSafeData& Data) const
{
	if (Data.Combat.bIsCombatMode)
	{
		return GetEffectiveCombatAimAlpha();
	}

	if (Data.Ground.bUseSprintLocomotion)
	{
		return SprintAimAlpha;
	}

	return Data.Movement.GroundSpeed > GetEffectiveGenericMoveInputSpeedThreshold() ? MovingAimAlpha : StandingAimAlpha;
}

bool UProject_JCharacterAnimInstance::ShouldSkipNativeUpdate(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Project_J_AnimShouldSkipNativeUpdate);
	if (!OwningCharacter)
	{
		ThreadSafeData = FProject_JAnimThreadSafeData();
		PublishThreadSafeDataToProxy(ThreadSafeData);
		return true;
	}

	if (bSkipDedicatedServerAnimationDataUpdate && IsDedicatedServerAnimationContext())
	{
		ThreadSafeData = BuildThreadSafeData(DeltaSeconds);
		PublishThreadSafeDataToProxy(ThreadSafeData);
		return true;
	}

	ApplyOptimizationPolicy(BuildOptimizationPolicy());
	if (CurrentOptimizationPolicy.bUpdateAnimationData)
	{
		HiddenRemoteUpdateAccumulator = 0.0f;
		return false;
	}

	const float UpdateInterval = CurrentOptimizationPolicy.MotionMatchingUpdateInterval;
	if (UpdateInterval > 0.0f)
	{
		HiddenRemoteUpdateAccumulator += DeltaSeconds;
		if (HiddenRemoteUpdateAccumulator >= UpdateInterval)
		{
			HiddenRemoteUpdateAccumulator = 0.0f;
			return false;
		}
	}

	FProject_JCharacterAnimInstanceProxy& ProjectProxy = GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
	// Movement sampling remains throttled while hidden, but a changed budget must
	// reach the worker immediately. Do not replay the last sampled force pulse or
	// request an urgent mesh update merely to deliver a policy/reselect request.
	ThreadSafeData.MotionMatching.MinimumSearchInterval = CurrentOptimizationPolicy.MotionMatchingUpdateInterval;
	const bool bMotionMatchingEnabled = IsPrimaryMeshAnimInstance() &&
		ThreadSafeData.LocomotionMode == EProject_JAnimationLocomotionMode::OnFoot;
	ProjectProxy.SetFlowTraceEnabled(Project_J::AnimationFlowDebug::ShouldCapture(OwningCharacter) && IsPrimaryMeshAnimInstance());
	ProjectProxy.QueueGameThreadData(ThreadSafeData, CurrentActivePoseSearchDatabase, bMotionMatchingEnabled, false, false, false, CurrentTurnCycleCompanion);
	RecordAnimationFlowPublication(ThreadSafeData, false, false);
	return true;
}

const UProject_JLocomotionProfile* UProject_JCharacterAnimInstance::GetLocomotionProfile() const
{
	return OwningPlayerCharacter ? OwningPlayerCharacter->GetLocomotionProfile() : nullptr;
}

FProject_JAnimationBudgetSettings UProject_JCharacterAnimInstance::GetEffectiveAnimationBudgetSettings() const
{
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Profile->GetResolvedAnimationBudgetSettings();
	}

	FProject_JAnimationBudgetSettings Settings;
	Settings.NearDistance = NearMotionMatchingDistance;
	Settings.MidDistance = MidMotionMatchingDistance;
	Settings.FarDistance = FarMotionMatchingDistance;
	Settings.MidUpdateInterval = MidMotionMatchingUpdateInterval;
	Settings.FarUpdateInterval = FarMotionMatchingUpdateInterval;
	Settings.HiddenUpdateInterval = HiddenRemoteUpdateInterval;
	Settings.bDisableMotionMatchingBeyondFarDistance = bDisableMotionMatchingBeyondFarDistance;
	return Settings;
}

float UProject_JCharacterAnimInstance::GetEffectiveGenericMoveInputSpeedThreshold() const
{
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Profile->GenericMoveInputSpeedThreshold;
	}

	return GenericMoveInputSpeedThreshold;
}

float UProject_JCharacterAnimInstance::GetEffectiveSprintLocomotionSpeedThreshold() const
{
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Profile->SprintLocomotionSpeedThreshold;
	}

	return SprintLocomotionSpeedThreshold;
}

FProject_JRemoteVisualLocomotionPolicy UProject_JCharacterAnimInstance::GetEffectiveRemoteVisualPolicy() const
{
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Profile->RemoteVisualPolicy;
	}

	return FProject_JRemoteVisualLocomotionPolicy();
}

bool UProject_JCharacterAnimInstance::ShouldDisableMotionMatchingBeyondFarDistance() const
{
	return GetEffectiveAnimationBudgetSettings().bDisableMotionMatchingBeyondFarDistance;
}

const UProject_JCombatAnimProfile* UProject_JCharacterAnimInstance::GetCombatAnimProfile() const
{
	return OwningPlayerCharacter ? OwningPlayerCharacter->GetCombatAnimProfile() : nullptr;
}

float UProject_JCharacterAnimInstance::GetEffectiveCombatAimAlpha() const
{
	if (const UProject_JCombatAnimProfile* Profile = GetCombatAnimProfile())
	{
		return Profile->CombatAimAlpha;
	}

	return CombatAimAlpha;
}
