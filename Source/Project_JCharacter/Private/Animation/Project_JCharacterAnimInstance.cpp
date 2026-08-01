// Fill out your copyright notice in the Description page of Project Settings.

#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"

#include "ChooserFunctionLibrary.h"
#include "ChooserTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "IObjectChooser.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JLocomotionAnimStateComponentBase.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/Project_JMotionMatchingCVars.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JWeaponAnimProfile.h"
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

// SyncLegacyFieldsFromStructuredData() removed.
// All code now uses sub-struct paths (e.g., Data.Movement.GroundSpeed) directly.

namespace
{
	EProject_JStateControllerStrafeDirection ResolveStateControllerStrafeDirection(const float DirectionDegrees)
	{
		const float Direction = FRotator::NormalizeAxis(DirectionDegrees);
		if (Direction >= -60.0f && Direction <= 60.0f)
		{
			return EProject_JStateControllerStrafeDirection::Forward;
		}

		if (Direction > 60.0f && Direction <= 120.0f)
		{
			return EProject_JStateControllerStrafeDirection::Right;
		}

		if (Direction < -60.0f && Direction >= -120.0f)
		{
			return EProject_JStateControllerStrafeDirection::Left;
		}

		return EProject_JStateControllerStrafeDirection::Backward;
	}

	EProject_JStateControllerPresentationState ResolveStateControllerPresentationState(
		const FProject_JAnimThreadSafeData& Data,
		const FProject_JAnimOneShotPresentationThreadSafeData& OneShot)
	{
		if (!OneShot.bEnabled)
		{
			return EProject_JStateControllerPresentationState::Disabled;
		}

		const bool bIsJumpingOrJumpStart = Data.Air.bIsJumping ||
			Data.Air.bIsFallOffStart ||
			OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::JumpStart ||
			OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::Fall;

		if (Data.Air.bIsInAir || bIsJumpingOrJumpStart)
		{
			return (Data.Air.bIsJumping || Data.Air.bIsFallOffStart || OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::JumpStart)
				? EProject_JStateControllerPresentationState::TransitionToInAir
				: EProject_JStateControllerPresentationState::InAirLoop;
		}

		if (Data.Landing.bIsLanding)
		{
			return EProject_JStateControllerPresentationState::TransitionToLand;
		}

		if (OneShot.bRequested)
		{
			switch (OneShot.PhaseFamily)
			{
			case EProject_JLocomotionPhaseFamily::Start:
			case EProject_JLocomotionPhaseFamily::Pivot:
				// A release/reversal can happen before the component's phase has
				// settled from Start/Pivot to Stop.  Let the trajectory-derived
				// intent preempt it so the State Controller can select a Stop now.
				return Data.LocomotionContext.bIsMotionMatchingMoving
					? EProject_JStateControllerPresentationState::TransitionToLocomotion
					: EProject_JStateControllerPresentationState::TransitionToIdle;
			case EProject_JLocomotionPhaseFamily::Stop:
				return EProject_JStateControllerPresentationState::TransitionToIdle;
			case EProject_JLocomotionPhaseFamily::Landing:
				return EProject_JStateControllerPresentationState::TransitionToLand;
			case EProject_JLocomotionPhaseFamily::JumpStart:
			case EProject_JLocomotionPhaseFamily::Fall:
				return EProject_JStateControllerPresentationState::TransitionToInAir;
			default:
				break;
			}
		}

		return Data.LocomotionContext.bIsMotionMatchingMoving
			? EProject_JStateControllerPresentationState::LocomotionLoop
			: EProject_JStateControllerPresentationState::IdleLoop;
	}

	auto IsTransitionState = [](const EProject_JStateControllerPresentationState State)
	{
		return State == EProject_JStateControllerPresentationState::TransitionToLocomotion ||
			State == EProject_JStateControllerPresentationState::TransitionToIdle ||
			State == EProject_JStateControllerPresentationState::TransitionToInAir ||
			State == EProject_JStateControllerPresentationState::TransitionToLand;
	};

	auto IsNaturalLoopContinuation = [](const EProject_JStateControllerPresentationState TransitionState,
		const EProject_JStateControllerPresentationState CandidateState)
	{
		return (TransitionState == EProject_JStateControllerPresentationState::TransitionToLocomotion &&
				CandidateState == EProject_JStateControllerPresentationState::LocomotionLoop) ||
			(TransitionState == EProject_JStateControllerPresentationState::TransitionToIdle &&
				CandidateState == EProject_JStateControllerPresentationState::IdleLoop) ||
			(TransitionState == EProject_JStateControllerPresentationState::TransitionToInAir &&
				CandidateState == EProject_JStateControllerPresentationState::InAirLoop) ||
			(TransitionState == EProject_JStateControllerPresentationState::TransitionToLand &&
				(CandidateState == EProject_JStateControllerPresentationState::LocomotionLoop ||
				 CandidateState == EProject_JStateControllerPresentationState::IdleLoop));
	};

	bool ShouldStateControllerPresentationLoop(const EProject_JStateControllerPresentationState PresentationState)
	{
		switch (PresentationState)
		{
		case EProject_JStateControllerPresentationState::IdleLoop:
		case EProject_JStateControllerPresentationState::LocomotionLoop:
		case EProject_JStateControllerPresentationState::InAirLoop:
			return true;
		default:
			return false;
		}
	}
}

UProject_JCharacterAnimInstance::UProject_JCharacterAnimInstance()
{
	bUseMultiThreadedAnimationUpdate = true;

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

void UProject_JCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (NeedsOwnerReferenceRefresh())
	{
		CacheOwnerReferences();
	}

	if (ShouldSkipNativeUpdate(DeltaSeconds))
	{
		return;
	}

	if (IsPrimaryMeshAnimInstance() &&
		OwningPlayerCharacter &&
		(!OwningPlayerCharacter->GetMountComponent() || !OwningPlayerCharacter->GetMountComponent()->IsMounted()))
	{
		if (UProject_JMotionMatchingTrajectoryComponent* TrajectoryComponent = OwningPlayerCharacter->GetMotionMatchingTrajectoryComponent())
		{
			TrajectoryComponent->UpdateTrajectoryState(DeltaSeconds);
		}
	}

	ThreadSafeData = BuildThreadSafeData(DeltaSeconds);
	// Chooser columns require a reflected property rather than a BlueprintPure
	// enum getter. Publish this game-thread mirror *before* evaluating the
	// chooser: its object-parameter columns read these reflected properties.
	// Publishing afterwards would make every selection use the previous frame's
	// presentation state (Idle -> Start -> Loop -> Stop one state late).
	StateControllerPresentationStateForChooser = ThreadSafeData.OneShotPresentation.PresentationState;
	RotationModeForChooser = ThreadSafeData.LocomotionContext.RotationMode;
	GaitIntentForChooser = ThreadSafeData.LocomotionContext.GaitIntent;
	StateControllerStanceForChooser = ThreadSafeData.OneShotPresentation.Stance;
	StateControllerStrafeDirectionForChooser = ThreadSafeData.OneShotPresentation.StrafeDirection;
	bCombatModeForChooser = ThreadSafeData.Combat.bIsCombatMode;
	EvaluateStateControllerAnimationChooserOnGameThread(ThreadSafeData);
	if (IsPrimaryMeshAnimInstance())
	{
		ResetTrajectoryHistoryOnAccelerationStop(ThreadSafeData);
	}
	PublishThreadSafeDataToProxy(ThreadSafeData);
}

FAnimInstanceProxy* UProject_JCharacterAnimInstance::CreateAnimInstanceProxy()
{
	return new FProject_JCharacterAnimInstanceProxy(this);
}

void UProject_JCharacterAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete InProxy;
}

FProject_JAnimThreadSafeData UProject_JCharacterAnimInstance::GetThreadSafeData() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
}

FTransformTrajectory UProject_JCharacterAnimInstance::GetThreadSafeTrajectory() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.Trajectory;
}

float UProject_JCharacterAnimInstance::GetThreadSafeAimYaw() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Aim.AimYaw;
}

float UProject_JCharacterAnimInstance::GetThreadSafeAimPitch() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Aim.AimPitch;
}

float UProject_JCharacterAnimInstance::GetThreadSafeAimOffsetAlpha() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Aim.AimOffsetAlpha;
}

float UProject_JCharacterAnimInstance::GetThreadSafeGroundSpeed() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.GroundSpeed;
}

float UProject_JCharacterAnimInstance::GetThreadSafeCombatLocomotionSpeed() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.GroundSpeed;
}

float UProject_JCharacterAnimInstance::GetThreadSafeCombatLocomotionDirection() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.RelativeVelocityDirection;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeCombatLocomotionStartRequested() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Ground.bStartRequested;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeCombatLocomotionStopRequested() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Ground.bStopRequested;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeUsesFullBodyCombatLocomotion() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Combat.PresentationMode ==
		EProject_JCombatAnimationPresentationMode::FullBodyLocomotion;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeUsesCombatUpperBodyOverlay() const
{
	return !GetThreadSafeUsesFullBodyCombatLocomotion();
}

FVector UProject_JCharacterAnimInstance::GetThreadSafeVelocity() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.Velocity;
}

float UProject_JCharacterAnimInstance::GetThreadSafeVerticalSpeed() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.VerticalSpeed;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsAccelerating() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.bIsAccelerating;
}

FVector UProject_JCharacterAnimInstance::GetThreadSafeRelativeAccelerationAmount() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.RelativeAccelerationAmount;
}

FVector2D UProject_JCharacterAnimInstance::GetThreadSafeLeanAmount() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.LeanAmount;
}

float UProject_JCharacterAnimInstance::GetThreadSafePredictedStopDistance() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.PredictedStopDistance;
}

float UProject_JCharacterAnimInstance::GetThreadSafeVelocityToMoveInputAngle() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.VelocityToMoveInputAngle;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsDecelerating() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Movement.bIsDecelerating;
}

float UProject_JCharacterAnimInstance::GetThreadSafeMoveInputSize() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Input.MoveInputSize;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeHasMoveInput() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Input.bHasMoveInput;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsInAir() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Air.bIsInAir;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsJumping() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Air.bIsJumping;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsLanding() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Landing.bIsLanding;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsCombatMode() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Combat.bIsCombatMode;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsMoving() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().LocomotionContext.bIsMoving;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsMotionMatchingMoving() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().LocomotionContext.bIsMotionMatchingMoving;
}

EProject_JLocomotionGaitIntent UProject_JCharacterAnimInstance::GetThreadSafeGaitIntent() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().LocomotionContext.GaitIntent;
}

EProject_JLocomotionRotationMode UProject_JCharacterAnimInstance::GetThreadSafeRotationMode() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().LocomotionContext.RotationMode;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerWantsLocomotion() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return Data.OneShotPresentation.bEnabled &&
		!Data.Air.bIsInAir &&
		Data.LocomotionContext.bIsMotionMatchingMoving;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerWantsIdle() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return Data.OneShotPresentation.bEnabled &&
		!Data.Air.bIsInAir &&
		!Data.LocomotionContext.bIsMotionMatchingMoving;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerIsGrounded() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return Data.OneShotPresentation.bEnabled && !Data.Air.bIsInAir;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerIsInAir() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return Data.OneShotPresentation.bEnabled && Data.Air.bIsInAir;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeExperimentalOneShotEnabled() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bEnabled;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeOneShotRequested() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bRequested;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeOneShotUseMotionMatchOnEntry() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bUseMotionMatchOnEntry;
}

int32 UProject_JCharacterAnimInstance::GetThreadSafeOneShotRequestRevision() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.RequestRevision;
}

EProject_JLocomotionPhaseFamily UProject_JCharacterAnimInstance::GetThreadSafeOneShotPhase() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.PhaseFamily;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeOneShotEarlyTransitionWindowOpen() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bEarlyTransitionWindowOpen;
}

float UProject_JCharacterAnimInstance::GetThreadSafeOneShotFallbackLeadTime() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.FallbackLeadTime;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerLocomotionSemanticStateChanged() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bLocomotionSemanticStateChanged;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerIdleSemanticStateChanged() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bIdleSemanticStateChanged;
}

EProject_JStateControllerStrafeDirection UProject_JCharacterAnimInstance::GetThreadSafeStateControllerStrafeDirection() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.StrafeDirection;
}

EProject_JStateControllerStance UProject_JCharacterAnimInstance::GetThreadSafeStateControllerStance() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.Stance;
}

EProject_JStateControllerPresentationState UProject_JCharacterAnimInstance::GetThreadSafeStateControllerPresentationState() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.PresentationState;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerIdleBreakEnabled() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bIdleBreakEnabled;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerIdleBreakMinimumStateTime() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.IdleBreakMinimumStateTime;
}

UAnimationAsset* UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimation() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.SelectedAnimation;
}

FProject_JStateControllerChooserOutput UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimationOutput() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.SelectedAnimationOutput;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimationStartTime() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.SelectedAnimationOutput.StartTime;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimationBlendTime() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.SelectedAnimationOutput.BlendTime;
}

UBlendProfile* UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimationBlendProfile() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.SelectedAnimationOutput.BlendProfile;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimationShouldLoop() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bSelectedAnimationShouldLoop;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerHasSelectedAnimation() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bHasSelectedAnimation;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimationAlmostComplete() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bTransitionAnimationAlmostComplete;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimationTimeRemaining() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.TransitionTimeRemaining;
}

void UProject_JCharacterAnimInstance::BeginOneShotEarlyTransitionWindow()
{
	++OneShotEarlyTransitionWindowDepth;
}

void UProject_JCharacterAnimInstance::EndOneShotEarlyTransitionWindow()
{
	OneShotEarlyTransitionWindowDepth = FMath::Max(OneShotEarlyTransitionWindowDepth - 1, 0);
}

bool UProject_JCharacterAnimInstance::GetThreadSafeIsMounted() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.bIsMounted;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeMountedIsFlying() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.bIsFlying;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeMountedIsGliding() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.bIsGliding;
}

float UProject_JCharacterAnimInstance::GetThreadSafeMountedSpeed() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.Speed;
}

float UProject_JCharacterAnimInstance::GetThreadSafeMountedVerticalSpeed() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.VerticalSpeed;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeHasMountedHandIKTargets() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.bHasHandIKTargets;
}

FVector UProject_JCharacterAnimInstance::GetThreadSafeMountedLeftHandTargetComponentSpace() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.LeftHandTargetComponentSpace;
}

FVector UProject_JCharacterAnimInstance::GetThreadSafeMountedRightHandTargetComponentSpace() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.RightHandTargetComponentSpace;
}

EProject_JAnimationLocomotionMode UProject_JCharacterAnimInstance::GetThreadSafeLocomotionMode() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().LocomotionMode;
}

FFootPlacementPlantSettings UProject_JCharacterAnimInstance::Get_FootPlacementPlantSettings() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Data.Ground.bStopRequested ? Profile->FootPlacementPlantSettingsStops : Profile->FootPlacementPlantSettingsDefault;
	}

	return Data.Ground.bStopRequested ? FootPlacementPlantSettingsStops : FootPlacementPlantSettingsDefault;
}

FFootPlacementInterpolationSettings UProject_JCharacterAnimInstance::Get_FootPlacementInterpolationSettings() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		return Data.Ground.bStopRequested ? Profile->FootPlacementInterpolationSettingsStops : Profile->FootPlacementInterpolationSettingsDefault;
	}

	return Data.Ground.bStopRequested ? FootPlacementInterpolationSettingsStops : FootPlacementInterpolationSettingsDefault;
}

float UProject_JCharacterAnimInstance::GetThreadSafeFootPlacementAlpha() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().ProceduralIK.FootPlacementAlpha;
}

float UProject_JCharacterAnimInstance::GetThreadSafeLegIKAlpha() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().ProceduralIK.LegIKAlpha;
}

float UProject_JCharacterAnimInstance::GetThreadSafeFullBodyMontageWeight() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().ProceduralIK.FullBodyMontageWeight;
}

UPoseSearchDatabase* UProject_JCharacterAnimInstance::GetCurrentActivePoseSearchDatabaseThreadSafe() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetCurrentActiveDatabase();
}

FString UProject_JCharacterAnimInstance::GetAnimationDebugSummary() const
{
	using Project_J::LocomotionDebug::ToDebugString;

	const FProject_JAnimThreadSafeData& Data = ThreadSafeData;
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
			TEXT("t=%.3f Next=%.3f Rev=%d PSD=%s Anim=%s AnimTime=%.3f/%.3f PlayRate=%.2f Continuing=%s CurveSpeed=%.1f Warp=%.2f CurvePhase=%.2f Phase=%s Ground=%d Age=%.3f Gait=%s Rotation=%s Speed=%.1f FutureSpeed=%.1f Gain=%.1f FutureTurn=%.1f FutureValid=%s StopDist=%.1f Input=%s Accel=%s Decel=%s InputTurn=%.1f Trajectory=%d DBChanged=%s ForceReselect=%s\n"),
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

FProject_JAnimThreadSafeData UProject_JCharacterAnimInstance::BuildThreadSafeData(float DeltaSeconds) const
{
	FProject_JAnimThreadSafeData Data;
	Data.DeltaTime = DeltaSeconds;
	if (const UProject_JLocomotionProfile* Profile = GetLocomotionProfile())
	{
		Data.MotionMatchingSearchPolicy = Profile->MotionMatchingSearchPolicy;
	}

	if (!OwningCharacter)
	{
		return Data;
	}

	FillMovementThreadSafeData(Data);
	if (LocomotionAnimStateComponent)
	{
		FillLocomotionStateThreadSafeData(Data);
	}
	else
	{
		ApplyGenericMovementFallback(Data);
	}

	const bool bHasAimData = FillPlayerThreadSafeData(Data);
	FillMountThreadSafeData(Data);
	FinalizeThreadSafeData(Data, bHasAimData);
	FillProceduralIKThreadSafeData(Data);

	return Data;
}

void UProject_JCharacterAnimInstance::FillMovementThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
	const FVector CharacterVelocity = OwningCharacter->GetVelocity();
	Data.Movement.Velocity = CharacterVelocity;
	const FVector GroundVelocity(CharacterVelocity.X, CharacterVelocity.Y, 0.0f);
	Data.Movement.GroundSpeed = GroundVelocity.Size();
	if (Data.Movement.GroundSpeed > KINDA_SMALL_NUMBER)
	{
		const FVector LocalGroundVelocity = OwningCharacter->GetActorQuat().UnrotateVector(GroundVelocity);
		Data.Movement.RelativeVelocityDirection = FMath::RadiansToDegrees(FMath::Atan2(LocalGroundVelocity.Y, LocalGroundVelocity.X));
	}
	else
	{
		Data.Movement.RelativeVelocityDirection = 0.0f;
	}
	Data.Movement.VerticalSpeed = CharacterVelocity.Z;

	if (const UCharacterMovementComponent* MovementComponent = OwningCharacter->GetCharacterMovement())
	{
		Data.Movement.Acceleration = MovementComponent->GetCurrentAcceleration();
		Data.Movement.AccelerationDirection = Data.Movement.Acceleration.GetSafeNormal();
		Data.Movement.bIsAccelerating = Data.Movement.Acceleration.SizeSquared2D() > UE_KINDA_SMALL_NUMBER;
		Data.Air.bIsInAir = MovementComponent->IsFalling();

		const float MaxAcceleration = FMath::Max(MovementComponent->GetMaxAcceleration(), UE_KINDA_SMALL_NUMBER);
		Data.Movement.AccelerationRatio = FMath::Clamp(Data.Movement.Acceleration.Size2D() / MaxAcceleration, 0.0f, 1.0f);
	}

	Data.Movement.bWasAccelerating = ThreadSafeData.Movement.bIsAccelerating;
	Data.Movement.bStoppedAcceleratingThisFrame = Data.Movement.bWasAccelerating && !Data.Movement.bIsAccelerating;
}

void UProject_JCharacterAnimInstance::FillLocomotionStateThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
	const UProject_JLocomotionAnimStateComponent* AnimState = LocomotionAnimStateComponent.Get();
	if (!AnimState)
	{
		return;
	}

	Data.Input.MoveInputSize = AnimState->MoveInputSize;
	Data.Input.MoveInputHeldTime = AnimState->MoveInputHeldTime;
	Data.Input.MoveInputTurnAngle = AnimState->MoveInputTurnAngle;
	Data.Input.MovementDirection = AnimState->MovementDirection;
	Data.Input.bHasMoveInput = AnimState->bHasMoveInput;
	Data.Input.bSharpTurnRequested = AnimState->bSharpTurnRequested;
	Data.Ground.bStartRequested = AnimState->bStartRequested || AnimState->bUseStartDatabase;
	Data.Ground.bStopRequested = AnimState->bStopRequested || AnimState->bUseStopDatabase;
	Data.Ground.bWantsSprint = AnimState->bWantsSprint;
	Data.Ground.bUseSprintLocomotion = AnimState->bUseSprintLocomotion;
	Data.Ground.bStartWasSprinting =
		AnimState->bStartWasSprinting ||
		(Data.Ground.bStartRequested && Data.Ground.bWantsSprint && Data.Input.bHasMoveInput);
	Data.Ground.bStopWasSprinting = AnimState->bStopWasSprinting;
	Data.Ground.GroundMotionMode = AnimState->GroundMotionMode;
	Data.Ground.GroundMotionModeElapsedTime = AnimState->GetGroundMotionModeElapsedTime();
	Data.Air.bIsJumping = AnimState->bIsJumping;
	Data.Air.bIsFallOffStart = AnimState->bIsFallOffStart;
	Data.Landing.bIsLanding = AnimState->bIsLanding || AnimState->bLandingRequested;
	Data.Landing.bUseHeavyLand = AnimState->bUseHeavyLand;
	Data.Landing.bLandWasSprinting = AnimState->bLandWasSprinting;
	Data.Landing.bLandWasMoving = AnimState->bLandWasMoving;
	Data.Landing.LastFallSpeed = AnimState->LastFallSpeed;
	Data.Landing.LandStartFallSpeed = AnimState->LandStartFallSpeed;
	Data.LocomotionContext.GaitIntent = AnimState->AuthoritativeContext.GaitIntent;
	Data.LocomotionContext.RotationMode = AnimState->AuthoritativeContext.RotationMode;
	Data.LocomotionContext.PhaseFamily = AnimState->DerivedLocomotionContext.PhaseFamily;
	Data.LocomotionContext.DesiredFacingDeltaYaw = AnimState->KinematicContext.DesiredFacingDeltaYaw;
	Data.LocomotionContext.bIsMoving = AnimState->DerivedLocomotionContext.bIsMoving;
	Data.LocomotionContext.bIsMotionMatchingMoving = AnimState->DerivedLocomotionContext.bIsMotionMatchingMoving;
	Data.LocomotionContext.bIsStarting = AnimState->DerivedLocomotionContext.bIsStarting;
	Data.LocomotionContext.bIsPivoting = AnimState->DerivedLocomotionContext.bIsPivoting;
	Data.LocomotionContext.bShouldTurnInPlace = AnimState->DerivedLocomotionContext.bShouldTurnInPlace;
	Data.LocomotionContext.bShouldSpinTransition = AnimState->DerivedLocomotionContext.bShouldSpinTransition;
	Data.Movement.RelativeAccelerationAmount = AnimState->KinematicContext.RelativeAccelerationAmount;
	Data.Movement.PredictedStopDistance = AnimState->KinematicContext.PredictedStopDistance;
	Data.Movement.PredictedSpeedGain = AnimState->KinematicContext.PredictedSpeedGain;
	Data.Movement.FutureTrajectoryVelocity = AnimState->KinematicContext.FutureTrajectoryVelocity;
	Data.Movement.FutureTrajectorySpeed = AnimState->KinematicContext.FutureTrajectorySpeed;
	Data.Movement.FutureTrajectoryTurnAngle = AnimState->KinematicContext.FutureTrajectoryTurnAngle;
	Data.Movement.bHasFutureTrajectoryVelocity = AnimState->KinematicContext.bHasFutureTrajectoryVelocity;
	Data.Movement.VelocityToMoveInputAngle = AnimState->KinematicContext.VelocityToMoveInputAngle;
	Data.Movement.bIsDecelerating = AnimState->KinematicContext.bIsDecelerating;
	Data.MotionMatching.SelectionRevision = AnimState->MotionMatchingSelectionRevision;
	Data.MotionMatching.bSelectionChanged = AnimState->bMotionMatchingSelectionChanged;
	Data.MotionMatching.bForceReselect = AnimState->bForceMotionMatchingReselect;
	Data.MotionMatching.SelectionContext = AnimState->GetMotionMatchingSelectionContext();
	Data.MotionMatching.PostSelection = GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>().GetLatestPostSelection();

	FProject_JAnimOneShotPresentationThreadSafeData& OneShot = Data.OneShotPresentation;
	OneShot.bEnabled = Data.MotionMatchingSearchPolicy.bEnableExperimentalOneShotPresentation;
	OneShot.bUseMotionMatchOnEntry =
		OneShot.bEnabled && Data.MotionMatchingSearchPolicy.bUseMotionMatchForExperimentalOneShotEntry;
	OneShot.bEarlyTransitionWindowOpen = OneShotEarlyTransitionWindowDepth > 0;
	OneShot.FallbackLeadTime = Data.MotionMatchingSearchPolicy.ExperimentalOneShotFallbackLeadTime;
	OneShot.bIdleBreakEnabled = OneShot.bEnabled && Data.MotionMatchingSearchPolicy.bEnableExperimentalIdleBreak;
	OneShot.IdleBreakMinimumStateTime = OneShot.bIdleBreakEnabled
		? Data.MotionMatchingSearchPolicy.ExperimentalIdleBreakMinimumStateTime
		: 0.0f;
	OneShot.RequestRevision = Data.MotionMatching.SelectionRevision;
	OneShot.PhaseFamily = Data.LocomotionContext.PhaseFamily;
	OneShot.RotationMode = Data.LocomotionContext.RotationMode;
	OneShot.StrafeDirection = OneShot.RotationMode == EProject_JLocomotionRotationMode::Strafe
		? ResolveStateControllerStrafeDirection(Data.Input.MovementDirection)
		: EProject_JStateControllerStrafeDirection::Forward;
	OneShot.Stance = OwningCharacter && OwningCharacter->bIsCrouched
		? EProject_JStateControllerStance::Crouch
		: EProject_JStateControllerStance::Stand;

	// State Controller re-entry is presentation-only. Compare two immutable
	// game-thread snapshots, then expose the result to the worker thread. This
	// intentionally does not use raw angle deltas: remote velocity can vary by a
	// few degrees every replication update and would otherwise restart a state.
	const FProject_JAnimThreadSafeData& PreviousData = ThreadSafeData;
	const bool bCanCompareStateControllerContext =
		OneShot.bEnabled &&
		PreviousData.OneShotPresentation.bEnabled &&
		!Data.Air.bIsInAir &&
		!PreviousData.Air.bIsInAir;
	const bool bRotationModeChanged =
		Data.LocomotionContext.RotationMode != PreviousData.LocomotionContext.RotationMode;
	const bool bGaitChanged =
		Data.LocomotionContext.GaitIntent != PreviousData.LocomotionContext.GaitIntent;
	const bool bLocomotionStanceChanged =
		OneShot.Stance != PreviousData.OneShotPresentation.Stance;
	const bool bStrafeDirectionChanged =
		Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe &&
		PreviousData.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe &&
		OneShot.StrafeDirection != PreviousData.OneShotPresentation.StrafeDirection;

	OneShot.bLocomotionSemanticStateChanged = bCanCompareStateControllerContext &&
		(bGaitChanged || bRotationModeChanged || bLocomotionStanceChanged || bStrafeDirectionChanged);
	OneShot.bIdleSemanticStateChanged = bCanCompareStateControllerContext &&
		bLocomotionStanceChanged;

	// Cycle stays in the regular MM node. These phase families are presentation
	// requests only; the future State Controller owns the one-shot lifetime.
	switch (OneShot.PhaseFamily)
	{
	case EProject_JLocomotionPhaseFamily::Start:
	case EProject_JLocomotionPhaseFamily::Stop:
	case EProject_JLocomotionPhaseFamily::Landing:
	case EProject_JLocomotionPhaseFamily::JumpStart:
	case EProject_JLocomotionPhaseFamily::Fall:
		OneShot.bRequested = OneShot.bEnabled;
		break;
	case EProject_JLocomotionPhaseFamily::Pivot:
		// Pivot assets are combat-Strafe content; never request them for OTM.
		OneShot.bRequested = OneShot.bEnabled &&
			OneShot.RotationMode == EProject_JLocomotionRotationMode::Strafe;
		break;
	default:
		OneShot.bRequested = false;
		break;
	}

	ResolveStateControllerPresentationStateWithPlaybackHold(Data, OneShot);
}

void UProject_JCharacterAnimInstance::ResolveStateControllerPresentationStateWithPlaybackHold(
	const FProject_JAnimThreadSafeData& Data,
	FProject_JAnimOneShotPresentationThreadSafeData& InOutOneShot) const
{
	EProject_JStateControllerPresentationState DesiredState =
		ResolveStateControllerPresentationState(Data, InOutOneShot);
	const double NowSeconds = FPlatformTime::Seconds();

	// GASP leaves Locomotion Loop as soon as its trajectory based IsMoving
	// predicate becomes false.  Do the same even while the physical Character
	// Movement component is still decelerating and has not yet entered its Stop
	// phase.  This produces an early authored Stop selection without changing
	// gameplay movement or the regular Motion Matching branch.
	const bool bStoppedFromPresentedLocomotion =
		!Data.Air.bIsInAir &&
		!Data.LocomotionContext.bIsMotionMatchingMoving &&
		DesiredState == EProject_JStateControllerPresentationState::IdleLoop &&
		(StateControllerPlaybackHoldState == EProject_JStateControllerPresentationState::TransitionToLocomotion ||
			StateControllerPlaybackHoldState == EProject_JStateControllerPresentationState::LocomotionLoop);
	if (bStoppedFromPresentedLocomotion)
	{
		DesiredState = EProject_JStateControllerPresentationState::TransitionToIdle;
	}

	const EProject_JStateControllerPresentationState RequestedState = DesiredState;
	const EProject_JStateControllerPresentationState PreviousHeldState = StateControllerPlaybackHoldState;
	const bool bHeldTransition = IsTransitionState(StateControllerPlaybackHoldState);
	const bool bNaturalContinuation = bHeldTransition &&
		IsNaturalLoopContinuation(StateControllerPlaybackHoldState, DesiredState);

	// Gameplay intent changes must replace a transition immediately.  In
	// particular, releasing movement during Start must enter Stop on that frame;
	// BP_NotifyState_EarlyTransition only controls Transition -> Loop/Re-enter,
	// never Start -> Stop or Stop -> Start intent replacement.
	bool bStartedNewPlaybackHold = !bHeldTransition ||
		(!bNaturalContinuation && DesiredState != StateControllerPlaybackHoldState);
	if (bStartedNewPlaybackHold)
	{
		StateControllerPlaybackHoldState = DesiredState;
		StateControllerPlaybackHoldStartedAtSeconds = NowSeconds;
		bStartedNewPlaybackHold = IsTransitionState(DesiredState);
	}

	InOutOneShot.TransitionElapsedTime = 0.0f;
	InOutOneShot.TransitionTimeRemaining = 0.0f;
	InOutOneShot.bTransitionAnimationAlmostComplete = false;

	if (!IsTransitionState(StateControllerPlaybackHoldState))
	{
		InOutOneShot.PresentationState = DesiredState;
		return;
	}

	// The chooser is evaluated after this snapshot is built.  On the frame that
	// enters Start/Stop/Air, CachedStateControllerSelectedAnimation still refers
	// to the old state (usually Idle or Cycle).  Do not inspect that stale asset
	// or clear the new clock; publish the transition once so the chooser can
	// select its authored one-shot on this frame.
	if (bStartedNewPlaybackHold)
	{
		InOutOneShot.PresentationState = StateControllerPlaybackHoldState;
		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
				TEXT("StateControllerTransition Requested=%d PreviousHeld=%d NewHeld=%d Reason=GameplayIntentReplacement"),
				static_cast<int32>(RequestedState),
				static_cast<int32>(PreviousHeldState),
				static_cast<int32>(StateControllerPlaybackHoldState));
		}
		return;
	}

	const UAnimationAsset* HeldAsset = CachedStateControllerSelectedAnimation;
	const float AssetLength = HeldAsset ? HeldAsset->GetPlayLength() : 0.0f;
	const float StartTime = FMath::Clamp(CachedStateControllerSelectedAnimationOutput.StartTime, 0.0f, AssetLength);
	const float AuthoredPlayableLength = FMath::Max(AssetLength - StartTime, 0.0f);
	const float Elapsed = FMath::Max(static_cast<float>(NowSeconds - StateControllerPlaybackHoldStartedAtSeconds), 0.0f);
	const float LeadTime = FMath::Max(InOutOneShot.FallbackLeadTime, 0.0f);
	const float Remaining = FMath::Max(AuthoredPlayableLength - Elapsed, 0.0f);
	const bool bHasPlayableTransitionAsset = HeldAsset &&
		AuthoredPlayableLength > KINDA_SMALL_NUMBER;
	const bool bReachedCompletion = bHasPlayableTransitionAsset && Remaining <= LeadTime;

	InOutOneShot.TransitionElapsedTime = Elapsed;
	InOutOneShot.TransitionTimeRemaining = Remaining;
	InOutOneShot.bTransitionAnimationAlmostComplete =
		InOutOneShot.bEarlyTransitionWindowOpen || bReachedCompletion;

	if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace() && bStartedNewPlaybackHold)
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("StateControllerHold: RequestedState=%d PreviousHeld=%d DesiredState=%d Elapsed=%.3f Remaining=%.3f PlayableLen=%.3f LeadTime=%.3f ReachedComp=%s EarlyOpen=%s AlmostComp=%s Asset=%s"),
			static_cast<int32>(RequestedState),
			static_cast<int32>(PreviousHeldState),
			static_cast<int32>(DesiredState),
			Elapsed,
			Remaining,
			AuthoredPlayableLength,
			LeadTime,
			bReachedCompletion ? TEXT("true") : TEXT("false"),
			InOutOneShot.bEarlyTransitionWindowOpen ? TEXT("true") : TEXT("false"),
			InOutOneShot.bTransitionAnimationAlmostComplete ? TEXT("true") : TEXT("false"),
			HeldAsset ? *HeldAsset->GetName() : TEXT("None"));
	}

	// A missing/looping entry must use the GASP "No Valid Anim" escape route.
	// Otherwise keep the actual Start/Stop/air asset until its authored end (or
	// an authored early-transition notify), never until a locomotion phase flips.
	const bool bStillRequestingHeldTransition = DesiredState == StateControllerPlaybackHoldState;
	if ((bNaturalContinuation || bStillRequestingHeldTransition) && bHasPlayableTransitionAsset &&
		!InOutOneShot.bTransitionAnimationAlmostComplete)
	{
		InOutOneShot.PresentationState = StateControllerPlaybackHoldState;
		return;
	}

	if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
	{
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("StateControllerExitHold: Exiting State=%d to State=%d (Elapsed=%.3f Remaining=%.3f AlmostComp=%s)"),
			static_cast<int32>(StateControllerPlaybackHoldState),
			static_cast<int32>(DesiredState),
			Elapsed,
			Remaining,
			InOutOneShot.bTransitionAnimationAlmostComplete ? TEXT("true") : TEXT("false"));
	}

	StateControllerPlaybackHoldState = DesiredState;
	StateControllerPlaybackHoldStartedAtSeconds = NowSeconds;
	InOutOneShot.PresentationState = DesiredState;
}

void UProject_JCharacterAnimInstance::EvaluateStateControllerAnimationChooserOnGameThread(FProject_JAnimThreadSafeData& Data)
{
	FProject_JAnimOneShotPresentationThreadSafeData& OneShot = Data.OneShotPresentation;
	OneShot.SelectedAnimation = nullptr;
	OneShot.SelectedAnimationOutput = FProject_JStateControllerChooserOutput();
	OneShot.bHasSelectedAnimation = false;
	OneShot.bSelectedAnimationShouldLoop = ShouldStateControllerPresentationLoop(OneShot.PresentationState);
	OneShot.SelectionRevision = StateControllerChooserSelectionRevision;

	const UProject_JLocomotionProfile* Profile = GetLocomotionProfile();
	UChooserTable* ChooserTable = Profile
		? Profile->MotionMatchingSearchPolicy.StateControllerAnimationChooserTable.Get()
		: nullptr;
	if (!OneShot.bEnabled || !ChooserTable)
	{
		CachedStateControllerChooserTable.Reset();
		CachedStateControllerSelectedAnimation = nullptr;
		CachedStateControllerSelectedAnimationOutput = FProject_JStateControllerChooserOutput();
		bCachedStateControllerHasSelectedAnimation = false;
		return;
	}

	const bool bContextChanged =
		CachedStateControllerChooserTable.Get() != ChooserTable ||
		CachedStateControllerPresentationState != OneShot.PresentationState ||
		CachedStateControllerRotationMode != Data.LocomotionContext.RotationMode ||
		CachedStateControllerGaitIntent != Data.LocomotionContext.GaitIntent ||
		CachedStateControllerStance != OneShot.Stance ||
		CachedStateControllerStrafeDirection != OneShot.StrafeDirection ||
		bCachedStateControllerCombatMode != Data.Combat.bIsCombatMode;

	if (bContextChanged)
	{
		FChooserEvaluationContext ChooserContext;
		ChooserContext.AddObjectParam(this);
		FChooserPlayerSettings ChooserPlayerSettings;
		ChooserContext.AddStructParam(ChooserPlayerSettings);
		FProject_JStateControllerChooserOutput ChooserOutput;
		ChooserContext.AddStructParam(ChooserOutput);

		const FInstancedStruct ChooserObject = UChooserFunctionLibrary::MakeEvaluateChooser(ChooserTable);
		UObject* ResultObject = ChooserObject.IsValid()
			? UChooserFunctionLibrary::EvaluateObjectChooserBase(
				ChooserContext,
				ChooserObject,
				UObject::StaticClass())
			: nullptr;

		// If Parent Chooser returned a Sub-Chooser Table, evaluate the Sub-Chooser recursively
		while (UChooserTable* SubChooserTable = Cast<UChooserTable>(ResultObject))
		{
			const FInstancedStruct SubChooserObject = UChooserFunctionLibrary::MakeEvaluateChooser(SubChooserTable);
			if (!SubChooserObject.IsValid())
			{
				ResultObject = nullptr;
				break;
			}
			ResultObject = UChooserFunctionLibrary::EvaluateObjectChooserBase(
				ChooserContext,
				SubChooserObject,
				UObject::StaticClass());
		}

		UAnimationAsset* SelectedAsset = Cast<UAnimationAsset>(ResultObject);
		if (SelectedAsset && ChooserOutput.bUseMotionMatch)
		{
			TArray<UObject*> AssetsToSearch;
			AssetsToSearch.Add(SelectedAsset);
			FPoseSearchBlueprintResult MMResult;
			FPoseSearchContinuingProperties ContinuingProperties;
			FPoseSearchFutureProperties FutureProperties;

			FName PoseHistoryName = FName("PoseHistory");
			if (!UPoseSearchLibrary::FindPoseHistoryNode(PoseHistoryName, this))
			{
				PoseHistoryName = FName("PoseSearchHistoryCollector");
				if (!UPoseSearchLibrary::FindPoseHistoryNode(PoseHistoryName, this))
				{
					PoseHistoryName = NAME_None;
				}
			}

			UPoseSearchLibrary::MotionMatch(
				this,
				AssetsToSearch,
				PoseHistoryName,
				ContinuingProperties,
				FutureProperties,
				MMResult);

			if (MMResult.SelectedAnim &&
				(ChooserOutput.MotionMatchCostLimit <= 0.0f || MMResult.SearchCost <= ChooserOutput.MotionMatchCostLimit))
			{
				SelectedAsset = Cast<UAnimationAsset>(const_cast<UObject*>(MMResult.SelectedAnim.Get()));
				ChooserOutput.StartTime = MMResult.SelectedTime;
			}
		}

		CachedStateControllerChooserTable = ChooserTable;
		CachedStateControllerPresentationState = OneShot.PresentationState;
		CachedStateControllerRotationMode = Data.LocomotionContext.RotationMode;
		CachedStateControllerGaitIntent = Data.LocomotionContext.GaitIntent;
		CachedStateControllerStance = OneShot.Stance;
		CachedStateControllerStrafeDirection = OneShot.StrafeDirection;
		bCachedStateControllerCombatMode = Data.Combat.bIsCombatMode;
		CachedStateControllerSelectedAnimation = SelectedAsset;
		CachedStateControllerSelectedAnimationOutput = ChooserOutput;
		bCachedStateControllerHasSelectedAnimation = CachedStateControllerSelectedAnimation != nullptr;
		++StateControllerChooserSelectionRevision;

		if (Project_J::MotionMatchingCVars::ShouldCaptureTransitionDebugTrace())
		{
			UE_LOG(LogProjectJPlayer, Display,
			TEXT("StateControllerChooser Rev=%d State=%d Rotation=%d Gait=%d Stance=%d StrafeDir=%d Combat=%s MMMoving=%s Input=%s FutureSpeed=%.1f Accelerating=%s Asset=%s Length=%.3f Start=%.3f Loop=%s Blend=%.3f UseMM=%s Tags=%d HoldElapsed=%.3f HoldRemaining=%.3f AlmostComplete=%s"),
				StateControllerChooserSelectionRevision,
				static_cast<int32>(OneShot.PresentationState),
				static_cast<int32>(Data.LocomotionContext.RotationMode),
				static_cast<int32>(Data.LocomotionContext.GaitIntent),
				static_cast<int32>(OneShot.Stance),
				static_cast<int32>(OneShot.StrafeDirection),
				Data.Combat.bIsCombatMode ? TEXT("true") : TEXT("false"),
				Data.LocomotionContext.bIsMotionMatchingMoving ? TEXT("true") : TEXT("false"),
				Data.Input.bHasMoveInput ? TEXT("true") : TEXT("false"),
				Data.Movement.FutureTrajectorySpeed,
				Data.Movement.bIsAccelerating ? TEXT("true") : TEXT("false"),
				CachedStateControllerSelectedAnimation ? *CachedStateControllerSelectedAnimation->GetName() : TEXT("None"),
				CachedStateControllerSelectedAnimation ? CachedStateControllerSelectedAnimation->GetPlayLength() : 0.0f,
				ChooserOutput.StartTime,
				ShouldStateControllerPresentationLoop(OneShot.PresentationState) ? TEXT("true") : TEXT("false"),
				ChooserOutput.BlendTime,
				ChooserOutput.bUseMotionMatch ? TEXT("true") : TEXT("false"),
				ChooserOutput.Tags.Num(),
				OneShot.TransitionElapsedTime,
				OneShot.TransitionTimeRemaining,
				OneShot.bTransitionAnimationAlmostComplete ? TEXT("true") : TEXT("false"));
		}
	}

	OneShot.SelectedAnimation = CachedStateControllerSelectedAnimation;
	OneShot.SelectedAnimationOutput = CachedStateControllerSelectedAnimationOutput;
	OneShot.bHasSelectedAnimation = bCachedStateControllerHasSelectedAnimation;
	OneShot.SelectionRevision = StateControllerChooserSelectionRevision;
}

void UProject_JCharacterAnimInstance::ApplyGenericMovementFallback(FProject_JAnimThreadSafeData& Data) const
{
	const float MoveInputSpeedThreshold = GetEffectiveGenericMoveInputSpeedThreshold();
	Data.Input.bHasMoveInput = Data.Movement.bIsAccelerating || Data.Movement.GroundSpeed > MoveInputSpeedThreshold;
	Data.Ground.bUseSprintLocomotion = Data.Movement.GroundSpeed >= GetEffectiveSprintLocomotionSpeedThreshold();
	Data.Ground.GroundMotionMode = Data.Movement.GroundSpeed > MoveInputSpeedThreshold
		? EProject_JGroundMotionMode::Locomotion
		: EProject_JGroundMotionMode::Idle;
	Data.LocomotionContext.GaitIntent = Data.Ground.bUseSprintLocomotion
		? EProject_JLocomotionGaitIntent::Sprint
		: EProject_JLocomotionGaitIntent::Run;
	Data.LocomotionContext.PhaseFamily = Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion
		? EProject_JLocomotionPhaseFamily::Cycle
		: EProject_JLocomotionPhaseFamily::Idle;
	Data.LocomotionContext.bIsMoving = Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion;
	Data.LocomotionContext.bIsMotionMatchingMoving = Data.LocomotionContext.bIsMoving;
	Data.MotionMatching.SelectionContext.GaitIntent = Data.LocomotionContext.GaitIntent;
	Data.MotionMatching.SelectionContext.RotationMode = Data.LocomotionContext.RotationMode;
	Data.MotionMatching.SelectionContext.PhaseFamily = Data.LocomotionContext.PhaseFamily;
	Data.MotionMatching.SelectionContext.bUseHeavyLand = false;
	Data.MotionMatching.SelectionContext.bLandWasMoving = false;
	Data.MotionMatching.SelectionContext.bLandWasSprinting = false;
	Data.MotionMatching.SelectionContext.bUseFallOffStart = false;
	Data.MotionMatching.SelectionContext.bUseRemoteStart = false;
	Data.MotionMatching.SelectionContext.bUseGenericFamiliesForNonOrientToMovement = false;
}

bool UProject_JCharacterAnimInstance::FillPlayerThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
	if (!OwningPlayerCharacter)
	{
		return false;
	}

	Data.Ground.bWantsSprint =
		(Data.Ground.bWantsSprint || OwningPlayerCharacter->IsSprintLocomotionAllowed()) &&
		OwningPlayerCharacter->IsSprintLocomotionAllowed();
	Data.Ground.bStartWasSprinting =
		Data.Ground.bStartWasSprinting ||
		(Data.Ground.bStartRequested && Data.Ground.bWantsSprint && Data.Input.bHasMoveInput);
	Data.Ground.bUseSprintLocomotion =
		Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		Data.Ground.bWantsSprint &&
		(Data.Input.bHasMoveInput || Data.Movement.GroundSpeed > GetEffectiveGenericMoveInputSpeedThreshold());
	Data.Combat.bIsCombatMode = OwningPlayerCharacter->IsCombatModeActive();
	Data.Combat.bIsAttacking = OwningPlayerCharacter->IsAttacking();
	Data.Combat.bIsDodging = OwningPlayerCharacter->IsDodging();
	Data.Combat.bIsHitReacting = OwningPlayerCharacter->IsHitReacting();
	Data.Combat.bIsPlayingCombatIntro = OwningPlayerCharacter->IsCombatIntroPlaying();
	if (const UProject_JWeaponAnimProfile* WeaponAnimProfile = OwningPlayerCharacter->GetWeaponAnimProfile())
	{
		Data.Combat.PresentationMode = WeaponAnimProfile->CombatPresentationMode;
	}

	if (const UProject_JMotionMatchingTrajectoryComponent* TrajectoryComponent = OwningPlayerCharacter->GetMotionMatchingTrajectoryComponent())
	{
		Data.Movement.Trajectory = TrajectoryComponent->GetTrajectory();
		Data.Movement.bHasTrajectory = !Data.Movement.Trajectory.Samples.IsEmpty();
		Data.MotionMatching.TrajectorySampleCount = Data.Movement.Trajectory.Samples.Num();
	}

	// GetBaseAimRotation uses the owning controller for the autonomous proxy and
	// the engine-replicated remote view pitch for simulated proxies. Reading the
	// controller directly left remote combat overlays at zero aim whenever that
	// client did not own the pawn.
	const FRotator AimRotation = OwningCharacter->GetBaseAimRotation();
	const FRotator ActorRotation = OwningCharacter->GetActorRotation();
	Data.Aim.AimYaw = FMath::Clamp(FMath::FindDeltaAngleDegrees(ActorRotation.Yaw, AimRotation.Yaw), -MaxAimYaw, MaxAimYaw);
	Data.Aim.AimPitch = FMath::Clamp(FRotator::NormalizeAxis(AimRotation.Pitch), -MaxAimPitch, MaxAimPitch);
	return true;
}

void UProject_JCharacterAnimInstance::FillMountThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
	if (!OwningPlayerCharacter)
	{
		return;
	}

	Data.LocomotionMode = OwningPlayerCharacter->GetAnimationLocomotionMode();

	const UProject_JMountComponent* MountComponent = OwningPlayerCharacter->GetMountComponent();
	const AProject_JMountCharacter* MountedMount = MountComponent ? MountComponent->GetMountedMount() : nullptr;
	if (!MountedMount)
	{
		return;
	}

	Data.Mount.bIsMounted = Data.LocomotionMode == EProject_JAnimationLocomotionMode::Mounted;
	const FVector MountVelocity = MountedMount->GetVelocity();
	Data.Mount.Speed = MountVelocity.Size2D();
	Data.Mount.VerticalSpeed = MountVelocity.Z;

	if (const AProject_JFlyingMountCharacter* FlyingMount = Cast<AProject_JFlyingMountCharacter>(MountedMount))
	{
		Data.Mount.bIsFlying = FlyingMount->IsFlyingMount();
		Data.Mount.bIsGliding = FlyingMount->IsGliding();
	}

	USkeletalMeshComponent* RiderMesh = OwningPlayerCharacter->GetMesh();
	FVector LeftHandTargetWorld;
	FVector RightHandTargetWorld;
	if (RiderMesh && MountedMount->GetRiderHandIKTargetsWorld(LeftHandTargetWorld, RightHandTargetWorld))
	{
		const FTransform RiderMeshTransform = RiderMesh->GetComponentTransform();
		Data.Mount.LeftHandTargetComponentSpace = RiderMeshTransform.InverseTransformPosition(LeftHandTargetWorld);
		Data.Mount.RightHandTargetComponentSpace = RiderMeshTransform.InverseTransformPosition(RightHandTargetWorld);
		Data.Mount.bHasHandIKTargets = true;
	}
}

void UProject_JCharacterAnimInstance::FinalizeThreadSafeData(FProject_JAnimThreadSafeData& Data, bool bHasAimData) const
{
	if (bHasAimData)
	{
		Data.Aim.AimOffsetAlpha = CalculateAimOffsetAlpha(Data);
	}

	const UProject_JLocomotionProfile* Profile = GetLocomotionProfile();
	const FProject_JLocomotionPresentationPolicy* PresentationPolicy = Profile
		? &Profile->PresentationPolicy
		: nullptr;
	if (!PresentationPolicy || !PresentationPolicy->bEnableLean || Data.Air.bIsInAir)
	{
		Data.Movement.LeanAmount = FVector2D::ZeroVector;
		return;
	}

	const float LeanMultiplier = Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe
		? PresentationPolicy->CombatStrafeLeanMultiplier
		: PresentationPolicy->OrientToMovementLeanMultiplier;
	const float LeanClamp = FMath::Max(0.0f, PresentationPolicy->LeanAxisClamp);
	// The state component stores local X as forward/braking and local Y as lateral.
	// Lean consumers conventionally expose X=lateral and Y=forward/back.
	Data.Movement.LeanAmount = FVector2D(
		FMath::Clamp(Data.Movement.RelativeAccelerationAmount.Y * LeanMultiplier, -LeanClamp, LeanClamp),
		FMath::Clamp(Data.Movement.RelativeAccelerationAmount.X * LeanMultiplier, -LeanClamp, LeanClamp));
}

void UProject_JCharacterAnimInstance::FillProceduralIKThreadSafeData(FProject_JAnimThreadSafeData& Data) const
{
	// Slot state belongs to UAnimInstance and is therefore sampled on the game
	// thread here. The result is copied to the proxy before AnimGraph worker
	// thread evaluation, avoiding an unsafe montage query from Blueprint.
	const float FullBodyMontageWeight = FMath::Clamp(
		GetSlotMontageGlobalWeight(FullBodyMontageIKPolicy.FullBodyMontageSlotName),
		0.0f,
		1.0f);

	Data.ProceduralIK.FullBodyMontageWeight = FullBodyMontageWeight;
	Data.ProceduralIK.FootPlacementAlpha = FMath::Lerp(
		1.0f,
		FullBodyMontageIKPolicy.FootPlacementAlphaDuringFullBodyMontage,
		FullBodyMontageWeight);
	const float MontageLegIKAlpha = FMath::Lerp(
		1.0f,
		FullBodyMontageIKPolicy.LegIKAlphaDuringFullBodyMontage,
		FullBodyMontageWeight);

	// Only a weapon profile that explicitly supplies full-body locomotion owns
	// the lower-body pose. Upper-body overlays deliberately retain the shared
	// Motion Matching lower body and therefore retain normal procedural Leg IK.
	// Full-body draw/attack montages are handled independently by their slot
	// weight above, so combat intro blending cannot produce an IK 0->1->0 pulse.
	const bool bUsesAuthoredCombatLowerBody =
		Data.Combat.PresentationMode == EProject_JCombatAnimationPresentationMode::FullBodyLocomotion &&
		(Data.Combat.bIsCombatMode || Data.Combat.bIsPlayingCombatIntro);
	Data.ProceduralIK.LegIKAlpha = bUsesAuthoredCombatLowerBody
		? FMath::Min(MontageLegIKAlpha, FullBodyMontageIKPolicy.LegIKAlphaDuringFullBodyCombat)
		: MontageLegIKAlpha;
}

void UProject_JCharacterAnimInstance::PublishThreadSafeDataToProxy(const FProject_JAnimThreadSafeData& Data)
{
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
		(bForceMotionMatchingRefresh || ShouldEvaluateMotionMatchingThisFrame(Data.DeltaTime));
	UPoseSearchDatabase* PreviousActiveDatabase = CurrentActivePoseSearchDatabase.Get();

	if (bUpdateMotionMatchingThisFrame)
	{
		// Chooser properties only need refreshing when we're actually re-evaluating the PSD this frame.
		// Skipping on throttled frames (mid/far distance, hidden remote) reduces Game Thread cost
		// proportionally to how aggressively the Motion Matching update interval is throttled.
		PublishChooserProperties(Data);
		CurrentActivePoseSearchDatabase = EvaluatePoseSearchDatabaseOnGameThread(Data);
		CacheEvaluatedMotionMatchingContext(Data);
	}
	if (!bMotionMatchingEnabled)
	{
		CurrentActivePoseSearchDatabase = nullptr;
		bHasEvaluatedMotionMatchingContext = false;
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
	ProjectProxy.QueueGameThreadData(
		Data,
		CurrentActivePoseSearchDatabase,
		bMotionMatchingEnabled,
		bUpdateMotionMatchingThisFrame,
		bForceMotionMatchingReselect);
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

UPoseSearchDatabase* UProject_JCharacterAnimInstance::EvaluatePoseSearchDatabaseOnGameThread(const FProject_JAnimThreadSafeData& Data)
{
	if (!OwningCharacter || IsDedicatedServerAnimationContext())
	{
		return nullptr;
	}

	const UProject_JMotionMatchingAssetSet* AssetSet = OwningPlayerCharacter
		? OwningPlayerCharacter->GetMotionMatchingAssetSet()
		: nullptr;
	const UProject_JMotionMatchingAssetSet* CombatStrafeAssetSet =
		OwningPlayerCharacter && Data.Combat.bIsCombatMode &&
		Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe
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
	CombatSelectionContext.bUseGenericFamiliesForNonOrientToMovement = true;
	UPoseSearchDatabase* SelectedDatabase = CombatStrafeAssetSet
		? CombatStrafeAssetSet->FindDatabaseForContext(CombatSelectionContext)
		: nullptr;
	const bool bSelectedCombatStrafeDatabase = SelectedDatabase != nullptr;
	if (!SelectedDatabase && AssetSet)
	{
		SelectedDatabase = AssetSet->FindDatabaseForContext(SelectionContext);
	}

	// A combat asset set uses the same complete family layout as normal
	// locomotion. The base set remains only as a migration fallback when the
	// combat set itself has an intentionally unassigned slot.
	if (!SelectedDatabase &&
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
		SelectedDatabase = Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Idle && IdleDatabase
			? IdleDatabase
			: LocomotionDatabase;
	}
	const UChooserTable* ChooserTable = bSelectedCombatStrafeDatabase
		? nullptr
		: AssetSet && AssetSet->MotionMatchingChooserTable
			? AssetSet->MotionMatchingChooserTable.Get()
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
	// All accesses now use sub-struct paths; no legacy flat fields.
	const FProject_JAnimOptimizationPolicy OptimizationPolicy = BuildOptimizationPolicy();
	CurrentOptimizationPolicy = OptimizationPolicy;

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
	ChooserGroundSpeed = Data.Movement.GroundSpeed;
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
	ChooserPhaseFamily = Data.LocomotionContext.PhaseFamily;
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
	if (!GetEffectiveRemoteVisualPolicy().bDisableStartStopChooserBeyondFarDistance)
	{
		return;
	}

	const bool bUseFarLocomotion =
		Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Locomotion &&
		!Data.Air.bIsInAir &&
		!Data.Air.bIsJumping;
	const bool bUseFarRunLocomotion = bUseFarLocomotion && !Data.Ground.bUseSprintLocomotion;
	const bool bUseFarSprintLocomotion = bUseFarLocomotion && Data.Ground.bUseSprintLocomotion;

	bChooserStartRequested = false;
	bChooserStopRequested = false;
	bChooserSharpTurnRequested = false;
	bChooserUseRunStart = false;
	bChooserUseRemoteRunStart = false;
	bChooserUseSprintStart = false;
	bChooserUseRunStop = false;
	bChooserUseSprintStop = false;
	bChooserUseFallOff = false;
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
	bChooserUseSprintLocomotion = bUseFarSprintLocomotion;
	bChooserUseRunLocomotion = false;
	bChooserUseRemoteRunLocomotion = bUseFarRunLocomotion;
	bChooserUseSprintLocomotionRow = bUseFarSprintLocomotion;
	bChooserUseJumpStart = Data.Air.bIsJumping;
	bChooserUseFallLoop = Data.Air.bIsInAir && !Data.Air.bIsJumping;
	bChooserIsIdle = !Data.Air.bIsInAir && Data.Ground.GroundMotionMode == EProject_JGroundMotionMode::Idle;
}

bool UProject_JCharacterAnimInstance::ShouldEvaluateMotionMatchingThisFrame(float DeltaSeconds)
{
	if (!OwningCharacter || IsDedicatedServerAnimationContext())
	{
		MotionMatchingUpdateAccumulator = 0.0f;
		return false;
	}

	CurrentOptimizationPolicy = BuildOptimizationPolicy();
	const float UpdateInterval = CurrentOptimizationPolicy.MotionMatchingUpdateInterval;
	if (UpdateInterval <= 0.0f)
	{
		MotionMatchingUpdateAccumulator = 0.0f;
		return true;
	}

	MotionMatchingUpdateAccumulator += DeltaSeconds;
	if (MotionMatchingUpdateAccumulator < UpdateInterval)
	{
		return false;
	}

	MotionMatchingUpdateAccumulator = 0.0f;
	return true;
}

bool UProject_JCharacterAnimInstance::ShouldForceMotionMatchingContextRefresh(const FProject_JAnimThreadSafeData& Data) const
{
	if (!bHasEvaluatedMotionMatchingContext)
	{
		return true;
	}

	return
		LastEvaluatedMotionMatchingSelectionRevision != Data.MotionMatching.SelectionRevision ||
		LastEvaluatedGroundMotionMode != Data.Ground.GroundMotionMode ||
		LastEvaluatedGaitIntent != Data.LocomotionContext.GaitIntent ||
		LastEvaluatedRotationMode != Data.LocomotionContext.RotationMode ||
		LastEvaluatedPhaseFamily != Data.LocomotionContext.PhaseFamily ||
		bLastEvaluatedStartRequested != Data.Ground.bStartRequested ||
		bLastEvaluatedStartWasSprinting != Data.Ground.bStartWasSprinting;
}

bool UProject_JCharacterAnimInstance::ShouldForceMotionMatchingReselect(const FProject_JAnimThreadSafeData& Data) const
{
	return Data.MotionMatching.bForceReselect;
}

void UProject_JCharacterAnimInstance::CacheEvaluatedMotionMatchingContext(const FProject_JAnimThreadSafeData& Data)
{
	LastEvaluatedGroundMotionMode = Data.Ground.GroundMotionMode;
	LastEvaluatedGaitIntent = Data.LocomotionContext.GaitIntent;
	LastEvaluatedRotationMode = Data.LocomotionContext.RotationMode;
	LastEvaluatedPhaseFamily = Data.LocomotionContext.PhaseFamily;
	bLastEvaluatedStartRequested = Data.Ground.bStartRequested;
	bLastEvaluatedStartWasSprinting = Data.Ground.bStartWasSprinting;
	LastEvaluatedMotionMatchingSelectionRevision = Data.MotionMatching.SelectionRevision;
	bHasEvaluatedMotionMatchingContext = true;
	MotionMatchingUpdateAccumulator = 0.0f;
}

FProject_JAnimOptimizationPolicy UProject_JCharacterAnimInstance::BuildOptimizationPolicy() const
{
	FProject_JAnimOptimizationPolicy Policy;
	if (!OwningCharacter)
	{
		Policy.Tier = EProject_JAnimBudgetTier::Hidden;
		Policy.bUpdateAnimationData = false;
		Policy.bUseFullChooserRows = false;
		Policy.bUseFarChooserRowsOnly = false;
		return Policy;
	}

	if (IsLocallyControlledCharacter())
	{
		return Policy;
	}

	const bool bRecentlyRendered = WasOwnerRecentlyRendered(RecentlyRenderedTolerance);
	if (!bRecentlyRendered)
	{
		Policy.Tier = EProject_JAnimBudgetTier::Hidden;
		Policy.bUpdateAnimationData = false;
		Policy.bUseFullChooserRows = false;
		Policy.bUseFarChooserRowsOnly = false;
		Policy.MotionMatchingUpdateInterval = GetEffectiveHiddenRemoteUpdateInterval();
		return Policy;
	}

	if (const AProject_JBaseCharacter* BaseChar = Cast<AProject_JBaseCharacter>(OwningCharacter))
	{
		const float Significance = BaseChar->GetSignificance();
		if (Significance <= 0.0f)
		{
			Policy.Tier = EProject_JAnimBudgetTier::Near;
			return Policy;
		}

		if (Significance <= 1.0f)
		{
			Policy.Tier = EProject_JAnimBudgetTier::Mid;
			Policy.MotionMatchingUpdateInterval = GetEffectiveMidMotionMatchingUpdateInterval();
			return Policy;
		}

		Policy.Tier = EProject_JAnimBudgetTier::Far;
		Policy.bUseFullChooserRows = false;
		Policy.bUseFarChooserRowsOnly = true;
		Policy.MotionMatchingUpdateInterval = GetEffectiveFarMotionMatchingUpdateInterval();
		return Policy;
	}

	Policy.Tier = EProject_JAnimBudgetTier::Near;
	return Policy;
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
		CachedTrajectoryComponent->ResetTrajectoryHistory();
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

	CurrentOptimizationPolicy = BuildOptimizationPolicy();
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
	ProjectProxy.QueueGameThreadData(ThreadSafeData, CurrentActivePoseSearchDatabase, true, false, false);
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

float UProject_JCharacterAnimInstance::GetEffectiveHiddenRemoteUpdateInterval() const
{
	return GetEffectiveAnimationBudgetSettings().HiddenUpdateInterval;
}

float UProject_JCharacterAnimInstance::GetEffectiveNearMotionMatchingDistance() const
{
	return GetEffectiveAnimationBudgetSettings().NearDistance;
}

float UProject_JCharacterAnimInstance::GetEffectiveMidMotionMatchingDistance() const
{
	return GetEffectiveAnimationBudgetSettings().MidDistance;
}

float UProject_JCharacterAnimInstance::GetEffectiveMidMotionMatchingUpdateInterval() const
{
	return GetEffectiveAnimationBudgetSettings().MidUpdateInterval;
}

float UProject_JCharacterAnimInstance::GetEffectiveFarMotionMatchingUpdateInterval() const
{
	return GetEffectiveAnimationBudgetSettings().FarUpdateInterval;
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
