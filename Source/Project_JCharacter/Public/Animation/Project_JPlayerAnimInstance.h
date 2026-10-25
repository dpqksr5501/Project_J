#pragma once

#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JPlayerLocomotionTypes.h"
#include "Animation/AnimExecutionContext.h"
#include "Animation/AnimNodeReference.h"
#include "MoverSimulationTypes.h"
#include "AnimationWarpingTypes.h"
#include "BoneControllers/AnimNode_OrientationWarping.h"
#include "Project_JPlayerAnimInstance.generated.h"

/** Completed movement snapshot consumed by the migrated GASP 5.8 animation graph. */
UCLASS(Blueprintable, BlueprintType)
class PROJECT_JCHARACTER_API UProject_JPlayerAnimInstance : public UProject_JCharacterAnimInstance
{
	GENERATED_BODY()
	bool bNeedsLocomotionHistorySeed = true;
	void SeedLocomotionHistory();
public:
 UFUNCTION(BlueprintCallable, Category="Player Locomotion", meta=(BlueprintThreadSafe))
 void SetNativeInteractionTransform(FTransform Value) { PlayerInteractionTransform=Value; }
 UFUNCTION(BlueprintCallable, Category="Player Locomotion", meta=(BlueprintThreadSafe))
 void SetNativeReTransition(bool Value) { PlayerNotifyTransitionReTransition=Value; }
 UFUNCTION(BlueprintCallable, Category="Player Locomotion", meta=(BlueprintThreadSafe))
 void SetNativeTransitionToLoop(bool Value) { PlayerNotifyTransitionToLoop=Value; }
 /** Ordinal-only adapter for the retained sample authoring interface; gameplay uses the typed PlayerGait. */
 UFUNCTION(BlueprintPure, Category="Player Locomotion", meta=(BlueprintThreadSafe))
 uint8 GetNativeGaitOrdinal() const { return static_cast<uint8>(PlayerGait); }
 UFUNCTION(BlueprintPure, Category="Player Locomotion", meta=(BlueprintThreadSafe))
 bool EvaluateNativeTransition(EProject_JPlayerTransitionRule Rule, float StateElapsedTime);
 UFUNCTION(BlueprintPure, Category="Player Locomotion", meta=(BlueprintThreadSafe))
 double EvaluateTurnInPlaceSteeringAlpha(const FAnimNodeReference& Node);
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUninitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativePostEvaluateAnimation() override;
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) void UpdateNativeLocomotionLogic();
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion", meta=(DevelopmentOnly))
	FString InspectFoundationPlayback() const;
 FName GetFoundationStateName() const;
 int32 GetTrajectoryPredictionRate() const;
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe))
	FTransformTrajectory GetFoundationTrajectory() const;
 UPROPERTY(EditDefaultsOnly, Category="Project J|Player Locomotion") TObjectPtr<UChooserTable> AirAnimationChooser;
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) void BeginProjectAir(bool bLoop);
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) void UpdateProjectAir(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);
 UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) bool IsProjectAirTransitionComplete() const;
 UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) bool UsesProjectJumpAir() const;
 /** Authored movement notify request. State selection consumes and clears it. */
 UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion")
 void RequestLocomotionTransition(bool bToLoop, bool bRequireGaitChange, EProject_JLocomotionGaitIntent ExcludedGait);
	// These functions own calculations. Authored graphs retain pose nodes and data bindings only.
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) void UpdateNativeTrajectory();
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) void UpdateNativeEssentialValues();
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) void UpdateNativeStates();
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) void UpdateNativeAimOffset();
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) void UpdateNativeAdditiveLean();
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) void UpdateNativeLocomotionTransition();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) bool EvaluateIsMoving();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) bool EvaluateIsStarting();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) bool EvaluateIsPivoting();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) bool EvaluateTurnInPlace();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) bool EvaluateSpinTransition();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) bool EvaluateLightLanding();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) bool EvaluateHeavyLanding();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) bool EvaluateTeleport();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) bool EvaluateTraversed();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) double EvaluateTrajectoryTurnAngle();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) double EvaluateTotalFacingDelta(const TArray<float>& Times);
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) double EvaluateLandVelocity();
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) void UpdateNativeRootOffset(const FAnimNodeReference& Node);
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) void ResetNativeFootPlacement();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) FVector EvaluateBlendSpaceInputs();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) FVector EvaluateStrafeWarpDirection();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) bool EvaluateFootPinning();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) bool EvaluateSlopeWarping();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) EOffsetRootBoneMode EvaluateRootRotationMode();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) EOffsetRootBoneMode EvaluateRootTranslationMode();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) double EvaluateRootTranslationHalfLife();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) double EvaluateRootTranslationRadius();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) EOrientationWarpingSpace EvaluateOrientationWarpingSpace();
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) double EvaluateDynamicPlayRate(const FAnimNodeReference& Node);
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) double EvaluateWarpAlpha(const FAnimNodeReference& Node);
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) double EvaluateProceduralTargetTime(const FAnimNodeReference& Node);
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) bool EvaluateSteering(const FAnimNodeReference& Node);
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) FQuat EvaluateDesiredFacing(const FAnimNodeReference& Node);
	UFUNCTION(BlueprintPure, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) bool EvaluateAnimationAlmostComplete();
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) void SelectNativeStateAnimation(FName StateName, uint8 StateMachineState, bool ForceBlend);
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) void EnterNativeLocomotionTransition();
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) void EnterNativeAirTransition();
	UFUNCTION(BlueprintCallable, Category="Project J|Player Locomotion", meta=(BlueprintThreadSafe)) void EnterNativeAirLoop();
	UPROPERTY(EditDefaultsOnly, Category="Project J|Player Locomotion") TObjectPtr<UChooserTable> GroundAnimationChooser;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JPlayerMovementMode PlayerMovementMode = EProject_JPlayerMovementMode::OnGround;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JPlayerMovementMode PlayerMovementModeLastFrame = EProject_JPlayerMovementMode::OnGround;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JPlayerMovementMode PlayerMovementModeRecent = EProject_JPlayerMovementMode::OnGround;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|상태", meta=(AllowPrivateAccess="true"))
 double PlayerMovementModeTime = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|상태", meta=(AllowPrivateAccess="true"))
 double PlayerMovementModeLastStateTime = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JPlayerRotationMode PlayerRotationMode = EProject_JPlayerRotationMode::OrientToMovement;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JPlayerRotationMode PlayerRotationModeLastFrame = EProject_JPlayerRotationMode::OrientToMovement;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JPlayerRotationMode PlayerRotationModeRecent = EProject_JPlayerRotationMode::OrientToMovement;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|상태", meta=(AllowPrivateAccess="true"))
 double PlayerRotationModeTime = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|상태", meta=(AllowPrivateAccess="true"))
 double PlayerRotationModeLastStateTime = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JPlayerMovementState PlayerMovementState = EProject_JPlayerMovementState::Idle;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JPlayerMovementState PlayerMovementStateLastFrame = EProject_JPlayerMovementState::Moving;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JPlayerMovementState PlayerMovementStateRecent = EProject_JPlayerMovementState::Moving;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|상태", meta=(AllowPrivateAccess="true"))
 double PlayerMovementStateTime = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|상태", meta=(AllowPrivateAccess="true"))
 double PlayerMovementStateLastStateTime = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JLocomotionGaitIntent PlayerGait = EProject_JLocomotionGaitIntent::Walk;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JLocomotionGaitIntent PlayerGaitLastFrame = EProject_JLocomotionGaitIntent::Walk;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JLocomotionGaitIntent PlayerGaitRecent = EProject_JLocomotionGaitIntent::Walk;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|상태", meta=(AllowPrivateAccess="true"))
 double PlayerGaitTime = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|상태", meta=(AllowPrivateAccess="true"))
 double PlayerGaitLastStateTime = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JPlayerStance PlayerStance = EProject_JPlayerStance::Stand;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JPlayerStance PlayerStanceLastFrame = EProject_JPlayerStance::Stand;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 FProject_JPlayerCharacterPropertiesForAnimation PlayerCharacterProperties = {};
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 FProject_JPlayerRagdollProperties PlayerRagdollProperties = {};
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 FTransform PlayerCharacterTransform = FTransform::Identity;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 FTransform PlayerCharacterTransformLastFrame = FTransform::Identity;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 FTransform PlayerRootTransform = FTransform::Identity;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 FVector PlayerAcceleration = FVector::ZeroVector;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 FVector PlayerAccelerationLastFrame = FVector::ZeroVector;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 bool PlayerHasVelocity = false;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 FVector PlayerVelocity = FVector::ZeroVector;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 FVector PlayerVelocityLastFrame = FVector::ZeroVector;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 FVector PlayerRelativeAcceleration = FVector::ZeroVector;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 FVector PlayerVelocityAcceleration = FVector::ZeroVector;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 FVector PlayerLastNonZeroVelocity = FVector::ZeroVector;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 double PlayerSpeed2D = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Trajectory", meta=(AllowPrivateAccess="true"))
 FTransformTrajectory PlayerTrajectory = {};
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Trajectory", meta=(AllowPrivateAccess="true"))
 FPoseSearchTrajectory_WorldCollisionResults PlayerTrajectoryCollision = {};
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Aim Offset", meta=(AllowPrivateAccess="true"))
 FRotator PlayerSmoothedAimTarget = FRotator::ZeroRotator;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 double PlayerHeavyLandSpeedThreshold = -500.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 FTransform PlayerInteractionTransform = FTransform::Identity;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Trajectory", meta=(AllowPrivateAccess="true"))
 FVector PlayerTrajectoryNearFutureVelocity = FVector::ZeroVector;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Trajectory", meta=(AllowPrivateAccess="true"))
 FVector PlayerTrajectoryFutureVelocity = FVector::ZeroVector;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Root Offset", meta=(AllowPrivateAccess="true"))
 bool PlayerOffsetRootBoneEnabled = false;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|State Machine (Experimental)", meta=(AllowPrivateAccess="true"))
 FProject_JPlayerBlendStackInputs PlayerBlendStackInputs = {};
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|State Machine (Experimental)", meta=(AllowPrivateAccess="true"))
 EProject_JPlayerPlaybackState PlayerStateMachineState = EProject_JPlayerPlaybackState::IdleLoop;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|State Machine (Experimental)", meta=(AllowPrivateAccess="true"))
 bool PlayerNoValidAnim = true;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|State Machine (Experimental)", meta=(AllowPrivateAccess="true"))
 bool PlayerNotifyTransitionReTransition = false;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|State Machine (Experimental)", meta=(AllowPrivateAccess="true"))
 bool PlayerNotifyTransitionToLoop = false;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Player Locomotion|Foot Placement", meta=(AllowPrivateAccess="true"))
 FFootPlacementPlantSettings PlayerPlantSettingsDefault = {};
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Player Locomotion|Foot Placement", meta=(AllowPrivateAccess="true"))
 FFootPlacementPlantSettings PlayerPlantSettingsStops = {};
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Player Locomotion|Foot Placement", meta=(AllowPrivateAccess="true"))
 FFootPlacementInterpolationSettings PlayerInterpolationSettingsDefault = {};
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Player Locomotion|Foot Placement", meta=(AllowPrivateAccess="true"))
 FFootPlacementInterpolationSettings PlayerInterpolationSettingsStops = {};
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JPlayerStance PlayerStanceRecent = EProject_JPlayerStance::Stand;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|상태", meta=(AllowPrivateAccess="true"))
 double PlayerStanceTime = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|상태", meta=(AllowPrivateAccess="true"))
 double PlayerStanceLastStateTime = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JPlayerMovementDirection PlayerMovementDirection = EProject_JPlayerMovementDirection::Forward;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JPlayerMovementDirection PlayerMovementDirectionLastFrame = EProject_JPlayerMovementDirection::Forward;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Root Offset", meta=(AllowPrivateAccess="true"))
 double PlayerOffsetRootTranslationRadius = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|디폴트", meta=(AllowPrivateAccess="true"))
 bool PlayerHasOwningActor = false;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|디폴트", meta=(AllowPrivateAccess="true"))
 bool PlayerHasLocomotionSource = false;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|디폴트", meta=(AllowPrivateAccess="true"))
 bool PlayerUseThreadSafeUpdateAnimation = false;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|궤적", meta=(AllowPrivateAccess="true"))
 FRotator PlayerTrajectoryFutureFacing = FRotator::ZeroRotator;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|궤적", meta=(AllowPrivateAccess="true"))
 double PlayerTrajectoryTurnAngle = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|궤적", meta=(AllowPrivateAccess="true"))
 FVector PlayerTrajectoryCurrentAngularVelocity = FVector::ZeroVector;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|궤적", meta=(AllowPrivateAccess="true"))
 bool PlayerTrajectoryIsCircling = false;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|궤적", meta=(AllowPrivateAccess="true"))
 double PlayerTrajectoryCirclingTime = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Trajectory", meta=(AllowPrivateAccess="true"))
 double PlayerFutureFacingDelta = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Foot Placement", meta=(AllowPrivateAccess="true"))
 bool PlayerFootPlacementEnable = false;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Foot Placement", meta=(AllowPrivateAccess="true"))
 bool PlayerFootPlacementDebug = false;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Foot Placement", meta=(AllowPrivateAccess="true"))
 int32 PlayerFootPlacementMode = 0;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|State Machine (Experimental)", meta=(AllowPrivateAccess="true"))
 FRotator PlayerFutureFacingOnTransitionStart = FRotator::ZeroRotator;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Aim Offset", meta=(AllowPrivateAccess="true"))
 FVector2D PlayerAO = FVector2D::ZeroVector;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|States", meta=(AllowPrivateAccess="true"))
 EProject_JPlayerMovementDirection PlayerMovementDirectionRecent = EProject_JPlayerMovementDirection::Forward;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|상태", meta=(AllowPrivateAccess="true"))
 double PlayerMovementDirectionTime = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Aim Offset", meta=(AllowPrivateAccess="true"))
 bool PlayerEnableAO = false;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Aim Offset", meta=(AllowPrivateAccess="true"))
 FVector PlayerAimAngularVelocity = FVector::ZeroVector;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Player Locomotion|Foot Placement", meta=(AllowPrivateAccess="true"))
 double PlayerTeleportThreshold = 50.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Trajectory", meta=(AllowPrivateAccess="true"))
 double PlayerFutureFacingDeltaLastFrame = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 FVector PlayerSmoothedGroundNormal = FVector::ZeroVector;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Essential Values", meta=(AllowPrivateAccess="true"))
 FVector2D PlayerSlopeAngle = FVector2D::ZeroVector;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Procedural", meta=(AllowPrivateAccess="true"))
 bool PlayerForceFootPlacementReset = false;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|Additive Lean", meta=(AllowPrivateAccess="true"))
 FVector2D PlayerLeanAmount = FVector2D::ZeroVector;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|상태", meta=(AllowPrivateAccess="true"))
 double PlayerMovementDirectionLastStateTime = 0.000000;
 UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Player Locomotion|State Machine (Experimental)", meta=(AllowPrivateAccess="true"))
 FName PlayerStateName = NAME_None;
protected:
	void PublishPlayerLocomotionSourceFrame(const FProject_JAnimThreadSafeData& Data);
private:
 // A ground recovery belongs to uninterrupted locomotion or a physical fall,
 // never to displacement/history hidden by a full-body action.
 bool bGroundRecoveryEligible=true;
 // A covered base-state clock cannot mature a fresh idle break. This is
 // continuous presentation ownership, not a skill cooldown or movement clock.
 double GroundPresentationSeconds=TNumericLimits<double>::Max();
 bool IsActionControllingLocomotion() const;
 void UpdateNativeConsoleVariables();
 bool HasBlendStackTag(FName Tag) const;
 FTransform ReadOffsetRootTransform();
 FAnimNodeReference FindFoundationNode(FName Tag);
 UFUNCTION() void AccumulateBasedMovement(const FTransform& Delta, const FMoverTimeStep& Step);
 void BindLocomotionSource(class UMoverComponent* Source);
 TWeakObjectPtr<class UMoverComponent> LocomotionSource;
 FTransform PendingBasedMovementDelta=FTransform::Identity;
 void BuildProjectAirFrame(FProject_JAnimThreadSafeData& Data);
 bool bAirEntryPending=false;
 bool bAirLoop=false;
 uint64 ActiveAirEpisode=0;
 FProject_JPlayerAirQuery CommittedAirQuery;
 float AirEntryStartTime=0.f;
 double LastAirReselection=-1.;
 float LastAirBlendTime=.15f;
 UPROPERTY(Transient) TObjectPtr<UAnimationAsset> AirSelectedAsset;
 UPROPERTY(VisibleAnywhere, Transient, Category="Project J|Player Locomotion") int32 AirReselectionCount=0;
 UPROPERTY(VisibleAnywhere, Transient, Category="Project J|Player Locomotion") int32 AirTransitionEntryCount=0;
 UPROPERTY(VisibleAnywhere, Transient, Category="Project J|Player Locomotion") int32 AirLoopEntryCount=0;
 UPROPERTY(VisibleAnywhere, Transient, Category="Project J|Player Locomotion") float LastAirReselectionFromTime=0.f;
 UPROPERTY(VisibleAnywhere, Transient, Category="Project J|Player Locomotion") float LastAirReselectionToTime=0.f;
	bool bWasPhysicallyInAir = false;
	FVector LastLandVelocity;
	UPROPERTY(Transient) TObjectPtr<class UMoverTrajectoryPredictor> MoverPredictor;
	FTransformTrajectory MoverTrajectory;
	float PredictorYaw = 0.f;
};
