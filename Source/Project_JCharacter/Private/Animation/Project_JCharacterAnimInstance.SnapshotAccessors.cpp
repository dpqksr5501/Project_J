// AnimGraph read boundary. These functions may execute on animation workers.
// Resolve actor, component, profile and mutable gameplay state only before proxy publication.
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"

FProject_JAnimMotionMatchingThreadSafeData UProject_JCharacterAnimInstance::GetMotionMatchingDebugSnapshot() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().MotionMatching;
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

EProject_JStateControllerStrafeDirection UProject_JCharacterAnimInstance::GetThreadSafeStateControllerPreviousStrafeDirection() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.PreviousStrafeDirection;
}

EProject_JStateControllerStance UProject_JCharacterAnimInstance::GetThreadSafeStateControllerStance() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.Stance;
}

EProject_JStateControllerPresentationState UProject_JCharacterAnimInstance::GetThreadSafeStateControllerPresentationState() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.PresentationState;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerShouldTurnInPlace() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return Data.LocomotionContext.bShouldTurnInPlace;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerShouldAbortTurnInPlace() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	// Abort if presentation is disabled, or character is airborne, or has movement intent/motion.
	// Do not abort purely on !bShouldTurnInPlace: once entered, the authored one-shot completes
	// naturally unless interrupted by movement or air state.
	return !Data.OneShotPresentation.bEnabled ||
		Data.Air.bIsInAir ||
		Data.Input.bHasMoveInput ||
		Data.LocomotionContext.bIsMoving;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerTurnInPlaceSteeringAlpha() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return (Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::TurnInPlace ||
	        Data.OneShotPresentation.PresentationState == EProject_JStateControllerPresentationState::TurnInPlace)
		? 1.0f
		: 0.0f;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerTurnInPlaceIndex() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	if (Data.LocomotionContext.TurnInPlaceDirectionBucket >= 1 && Data.LocomotionContext.TurnInPlaceDirectionBucket <= 4)
	{
		return static_cast<float>(Data.LocomotionContext.TurnInPlaceDirectionBucket);
	}

	if (!Data.bIsLocallyControlled)
	{
		return 0.0f;
	}

	const float DeltaYaw = Data.LocomotionContext.DesiredFacingDeltaYaw;

	if (DeltaYaw >= -135.0f && DeltaYaw <= -30.0f)
	{
		return 1.0f; // Left 090
	}
	if (DeltaYaw < -135.0f || DeltaYaw <= -180.0f)
	{
		return 2.0f; // Left 180
	}
	if (DeltaYaw >= 30.0f && DeltaYaw < 135.0f)
	{
		return 3.0f; // Right 090
	}
	if (DeltaYaw >= 135.0f && DeltaYaw <= 180.0f)
	{
		return 4.0f; // Right 180
	}

	return 0.0f;
}

FRotator UProject_JCharacterAnimInstance::GetThreadSafeStateControllerDesiredFacingRotator() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return FRotator(0.0f, Data.LocomotionContext.DesiredFacingYaw, 0.0f);
}

EOffsetRootBoneMode UProject_JCharacterAnimInstance::GetThreadSafeOffsetRootRotationMode() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	if (!Data.OneShotPresentation.bEnabled || Data.Air.bIsInAir)
	{
		return EOffsetRootBoneMode::Release;
	}

	// Orient-to-Movement: Release continuously clears any root rotation offset to 0,
	// keeping the mesh perfectly centered on the capsule during OTM locomotion.
	if (Data.LocomotionContext.RotationMode == EProject_JLocomotionRotationMode::OrientToMovement)
	{
		return EOffsetRootBoneMode::Release;
	}

	// Combat Strafe TIP: allow Steering/OffsetRootBone to absorb procedural root rotation.
	if (Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::TurnInPlace)
	{
		return EOffsetRootBoneMode::Interpolate;
	}

	// Combat Strafe non-TIP: smoothly release any residual offset back to zero.
	return EOffsetRootBoneMode::Release;
}

EOffsetRootBoneMode UProject_JCharacterAnimInstance::GetThreadSafeOffsetRootTranslationMode() const
{
	// Always return Release so the visual mesh root position stays 100% centered
	// on the physical capsule cylinder without off-center translation drifting.
	return EOffsetRootBoneMode::Release;
}

float UProject_JCharacterAnimInstance::GetThreadSafeOffsetRootTranslationHalfLife() const
{
	return 0.1f;
}

float UProject_JCharacterAnimInstance::GetThreadSafeOffsetRootTranslationRadius() const
{
	return 30.0f;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerDisableLegIK() const
{
	const FProject_JAnimThreadSafeData& Data = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData();
	return Data.LocomotionMode != EProject_JAnimationLocomotionMode::OnFoot || Data.Air.bIsInAir;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerLegIKAlpha() const
{
	return GetThreadSafeStateControllerDisableLegIK() ? 0.0f : 1.0f;
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

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerPlaybackHoldElapsedTime() const
{
	const double NowSeconds = FPlatformTime::Seconds();
	return FMath::Max(static_cast<float>(NowSeconds - StateControllerPlaybackHoldStartedAtSeconds), 0.0f);
}

int32 UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectionRevision() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.SelectionRevision;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerShouldForceBlend() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bForceBlendNextUpdate;
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

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerShouldOverrideMotionMatching() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bShouldOverrideMotionMatching;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerCombatStrafeOrientationWarpingAlpha() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bShouldEnableCombatStrafeOrientationWarping
		? 1.0f
		: 0.0f;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerCombatStrafeOrientationWarpingAngle() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.StrafeDirectionAngle;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimationAlmostComplete() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.bTransitionAnimationAlmostComplete;
}

float UProject_JCharacterAnimInstance::GetThreadSafeStateControllerSelectedAnimationTimeRemaining() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().OneShotPresentation.TransitionTimeRemaining;
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

FGameplayTagContainer UProject_JCharacterAnimInstance::GetThreadSafeMountedAnimationTags() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.AnimationTags;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeMountedHasAnimationTag(FGameplayTag Tag) const
{
	return Tag.IsValid() &&
		GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.AnimationTags.HasTag(Tag);
}

float UProject_JCharacterAnimInstance::GetThreadSafeMountedTransitionBlendTime() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().Mount.TransitionBlendTime;
}

EProject_JAnimationLocomotionMode UProject_JCharacterAnimInstance::GetThreadSafeLocomotionMode() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().LocomotionMode;
}

bool UProject_JCharacterAnimInstance::GetThreadSafeUsesOnFootLocomotion() const
{
	return GetThreadSafeLocomotionMode() == EProject_JAnimationLocomotionMode::OnFoot;
}

FFootPlacementPlantSettings UProject_JCharacterAnimInstance::Get_FootPlacementPlantSettings() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().ProceduralIK.PlantSettings;
}

FFootPlacementInterpolationSettings UProject_JCharacterAnimInstance::Get_FootPlacementInterpolationSettings() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().ProceduralIK.InterpolationSettings;
}

float UProject_JCharacterAnimInstance::GetThreadSafeFootPlacementAlpha() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().ProceduralIK.FootPlacementAlpha;
}

float UProject_JCharacterAnimInstance::GetThreadSafeLegIKAlpha() const
{
	const float BaseLegIKAlpha = GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().ProceduralIK.LegIKAlpha;
	const float StateControllerAlpha = GetThreadSafeStateControllerLegIKAlpha();
	return FMath::Min(BaseLegIKAlpha, StateControllerAlpha);
}

float UProject_JCharacterAnimInstance::GetThreadSafeFullBodyMontageWeight() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetThreadSafeData().ProceduralIK.FullBodyMontageWeight;
}

UPoseSearchDatabase* UProject_JCharacterAnimInstance::GetCurrentActivePoseSearchDatabaseThreadSafe() const
{
	return GetProxyOnAnyThread<FProject_JCharacterAnimInstanceProxy>().GetCurrentActiveDatabase();
}

