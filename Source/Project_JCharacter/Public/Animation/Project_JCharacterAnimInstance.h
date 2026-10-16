// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/AnimInstance.h"
#include "Animation/TrajectoryTypes.h"
#include "Animation/Project_JAnimationBudgetTypes.h"
#include "Animation/Project_JCharacterAnimInstanceBase.h"
#include "Animation/Project_JMotionMatchingRuntime.h"
#include "Animation/Project_JStateControllerRuntime.h"
#include "Animation/Project_JAnimationClock.h"
#include "Animation/Project_JGeneralTurnPolicy.h"
#include "Animation/Project_JBackwardRefacePolicy.h"
#include "Animation/Project_JOneShotCurvePolicy.h"
#include "Animation/Project_JTurnRequestSample.h"
#include "Animation/Project_JAnimationFlowTrace.h"
#include "Animation/Project_JAnimationLocomotionMode.h"
#include "Animation/Project_JStrafeUpperBodyFacing.h"
#include "Animation/Project_JStrafeLowerBodyFacing.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "BoneControllers/AnimNode_FootPlacement.h"
#include "Combat/Project_JCombatTypes.h"
#include "GameplayTagContainer.h"
#include "Project_JLocomotionAnimTypes.h"
#include "BoneControllers/AnimNode_OffsetRootBone.h"
#include "Animation/Project_JAnimationSnapshot.h"
#include "Project_JCharacterAnimInstance.generated.h"

class ACharacter;
class APawn;
class AProject_JPlayerCharacter;
class UAnimationAsset;
class UChooserTable;
class UPoseSearchDatabase;
class UProject_JLocomotionAnimStateComponent;
class UProject_JLocomotionProfile;
class UProject_JCombatAnimProfile;
class UProject_JMotionMatchingAssetSet;
struct FAnimNode_Base;
struct FAnimationUpdateContext;

UCLASS(Blueprintable, BlueprintType)
class PROJECT_JCHARACTER_API UProject_JCharacterAnimInstance : public UProject_JCharacterAnimInstanceBase
{
	GENERATED_BODY()

public:
	UProject_JCharacterAnimInstance();

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUninitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativePostEvaluateAnimation() override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

	FProject_JAnimOptimizationPolicy BuildOptimizationPolicy() const;
	void ApplyOptimizationPolicy(const FProject_JAnimOptimizationPolicy& NewPolicy);

private:
	friend class UProject_JAnimationUpdateCoordinatorComponent;
	friend class UProject_JCharacterAnimationBudgetSubsystem;
	/** GT-only fallback tick lease. The coordinator returns it before capturing authored controls. */
	void ReleaseFollowerTickOverride(bool bRestoreTick = true);
	TWeakObjectPtr<USkeletalMeshComponent> FallbackFollowerTickMesh;
	bool bSavedFollowerTick = true;
	bool bAppliedFollowerRetarget = true;

public:

	// Snapshot-only AnimGraph inputs. Written in UE's thread-safe update phase,
	// then read by generated property-copy handlers on the same animation task.
	// Do not use these as gameplay state or write them from Blueprint callbacks.
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Graph Snapshot")
	float GraphAimYaw = 0.0f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Graph Snapshot")
	float GraphAimPitch = 0.0f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Graph Snapshot")
	float GraphAimOffsetAlpha = 0.0f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Graph Snapshot")
	float GraphCombatSpeed = 0.0f;
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Graph Snapshot")
	float GraphCombatDirection = 0.0f;

	/**
	 * Game-thread mirror used exclusively by Chooser property columns.
	 *
	 * Chooser property access cannot reliably resolve a BlueprintPure enum return
	 * value as an enum column (it appears as "Missing" in the asset editor).
	 * This value is copied from the immutable proxy snapshot once per native
	 * update. AnimGraph worker-thread logic must continue to use
	 * GetThreadSafeStateControllerPresentationState instead.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	EProject_JStateControllerPresentationState StateControllerPresentationStateForChooser = EProject_JStateControllerPresentationState::Disabled;

	/** Reflected game-thread mirrors for State Controller Chooser columns. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	EProject_JLocomotionRotationMode RotationModeForChooser = EProject_JLocomotionRotationMode::OrientToMovement;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	EProject_JLocomotionGaitIntent GaitIntentForChooser = EProject_JLocomotionGaitIntent::Walk;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	EProject_JStateControllerStance StateControllerStanceForChooser = EProject_JStateControllerStance::Stand;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	EProject_JStateControllerStrafeDirection StateControllerStrafeDirectionForChooser = EProject_JStateControllerStrafeDirection::Forward;

	/** Previous Strafe direction for Pivot rows. It is meaningful only when bChooserIsPivoting is true. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	EProject_JStateControllerStrafeDirection StateControllerPreviousStrafeDirectionForChooser = EProject_JStateControllerStrafeDirection::Forward;

	/** Local trajectory angle which resolved StateControllerStrafeDirectionForChooser. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	float StateControllerStrafeDirectionAngleForChooser = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	bool bStateControllerHasStrafeDirectionAngleForChooser = false;

	/**
	 * GASP's static Movement Direction Bias. It selects which authored foot is
	 * forward for Strafe's left/right sectors; F and B, and all OTM selection,
	 * remain unaffected. Left is the project default until a deliberate
	 * gameplay-driven policy replaces it.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|State Controller")
	EProject_JStateControllerMovementDirectionBias StateControllerMovementDirectionBias = EProject_JStateControllerMovementDirectionBias::LeftFootForward;

	/**
	 * Contact difference required to classify the lower-contact foot as the next
	 * swing foot. Values inside this band produce None so generic chooser rows
	 * can handle blended or curve-less poses.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|State Controller", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float StateControllerFootContactDifferenceThreshold = 0.20f;

	/**
	 * Stable authored fallback used only when neither live contact nor phase
	 * history can identify a foot (for example, a jump started from Idle).
	 * Foot-neutral chooser rows should use Any and be placed after L/R rows.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|State Controller")
	EProject_JStateControllerFoot StateControllerNoPhaseFootFallback = EProject_JStateControllerFoot::Left;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	bool bCombatModeForChooser = false;

	/**
	 * Signed yaw from the character's facing at State Controller selection time to
	 * the current movement-input heading.  Negative values are left and positive
	 * values are right.  This is a game-thread Chooser column only; AnimGraph
	 * code must use the immutable thread-safe snapshot instead.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	float StateControllerInputFacingDeltaYawForChooser = 0.0f;

	/**
	 * 1.0 (L090), 2.0 (L180), 3.0 (R090), 4.0 (R180) turn index mirror published
	 * on Game Thread before Chooser table evaluation.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	float StateControllerTurnInPlaceIndexForChooser = 0.0f;

	/**
	 * Signed yaw from character facing to the horizontal velocity sampled when a
	 * Stop presentation begins. Negative values are left and positive values are
	 * right. It remains latched through that Stop so deceleration cannot change
	 * the chosen Stop asset mid-playback.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	float StateControllerStopVelocityDeltaYawForChooser = 0.0f;

	/** Last evaluated final-pose contact curves, exposed for Chooser diagnostics. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	float StateControllerLeftFootContactForChooser = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	float StateControllerRightFootContactForChooser = 0.0f;

	/** True when the preceding final pose contributed each contact curve. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	bool bStateControllerHasLeftFootContactCurveForChooser = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	bool bStateControllerHasRightFootContactCurveForChooser = false;

	/**
	 * Latched curve-derived foot choice for the active Start/Stop/Jump/Land
	 * transition. None selects authored rows that have no L/R foot variant.
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	EProject_JStateControllerFoot StateControllerOneShotFootForChooser = EProject_JStateControllerFoot::None;

	/** Diagnostic explanation of the current one-shot foot result. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	EProject_JStateControllerFootSelectionReason StateControllerOneShotFootSelectionReasonForChooser = EProject_JStateControllerFootSelectionReason::MissingContactCurve;

	/** Last unambiguous moving-foot result retained through Stop/Fall/Land transitions. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	EProject_JStateControllerFoot StateControllerFootPhaseHistoryForChooser = EProject_JStateControllerFoot::None;

	/** Latched at TransitionToInAir entry so Jump Start and Fall Off rows cannot overlap. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	bool bStateControllerFallOffForChooser = false;

	/** Small native-only snapshot used by the MMO animation profiling command. */
	FProject_JAnimMotionMatchingThreadSafeData GetMotionMatchingDebugSnapshot() const;
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	/** Read-only query reconstruction for explicit automation diagnostics. */
	FString DescribeReconstructedSearchCostsForTesting();
#endif
	/** GT-only completed evaluation; never forces synchronization with parallel evaluation. */
	const FProject_JTurnSelectionFeedback& GetCompletedTurnFeedback() const { check(IsInGameThread()); return CompletedTurnFeedback; }
	bool HasMovingOneShotInputOwnerOnGameThread() const
	{
		check(IsInGameThread());
		return ThreadSafeData.OneShotPresentation.bShouldOverrideMotionMatching && IsMovingOneShotInputOwner(ThreadSafeData);
	}
	bool DidYieldOneShotForCurveOnGameThread() const { check(IsInGameThread()); return bOneShotCurveReleasedThisUpdate; }
	bool CanSteerOneShotInputOnGameThread() const
	{
		check(IsInGameThread());
		return ThreadSafeData.bLocomotionSteeringEnabled && GFrameCounter >= ThreadSafeData.Movement.SnapshotFrame &&
			GFrameCounter - ThreadSafeData.Movement.SnapshotFrame <= 2;
	}

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	FTransformTrajectory GetThreadSafeTrajectory() const;

	UFUNCTION(BlueprintPure, Category = "Animation|AimOffset", meta = (BlueprintThreadSafe))
	float GetThreadSafeAimYaw() const;

	UFUNCTION(BlueprintPure, Category = "Animation|AimOffset", meta = (BlueprintThreadSafe))
	float GetThreadSafeAimPitch() const;

	UFUNCTION(BlueprintPure, Category = "Animation|AimOffset", meta = (BlueprintThreadSafe))
	float GetThreadSafeAimOffsetAlpha() const;

	FProject_JStrafeUpperBodyFacingInput GetThreadSafeStrafeUpperBodyFacing() const;
	FProject_JStrafeLowerBodyFacingInput GetThreadSafeStrafeLowerBodyFacing() const;

	/** Actual XY speed for directional combat locomotion Blend Spaces. */
	UFUNCTION(BlueprintPure, Category = "Animation|Combat Locomotion", meta = (BlueprintThreadSafe))
	float GetThreadSafeCombatLocomotionSpeed() const;

	/**
	 * Actual velocity direction relative to actor facing for directional combat
	 * Blend Spaces. -90 is left, 0 is forward, +90 is right, +/-180 is backward.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation|Combat Locomotion", meta = (BlueprintThreadSafe))
	float GetThreadSafeCombatLocomotionDirection() const;

	/** True for the authoritative locomotion start phase; use this to enter a combat start state. */
	UFUNCTION(BlueprintPure, Category = "Animation|Combat Locomotion", meta = (BlueprintThreadSafe))
	bool GetThreadSafeCombatLocomotionStartRequested() const;

	/** True for the authoritative locomotion stop phase; use this to enter a combat stop state. */
	UFUNCTION(BlueprintPure, Category = "Animation|Combat Locomotion", meta = (BlueprintThreadSafe))
	bool GetThreadSafeCombatLocomotionStopRequested() const;

	/** True when the equipped weapon supplies a validated full-body combat locomotion set. */
	UFUNCTION(BlueprintPure, Category = "Animation|Combat Presentation", meta = (BlueprintThreadSafe))
	bool GetThreadSafeUsesFullBodyCombatLocomotion() const;

	/** True when combat should retain shared Motion Matching below the overlay root. */
	UFUNCTION(BlueprintPure, Category = "Animation|Combat Presentation", meta = (BlueprintThreadSafe))
	bool GetThreadSafeUsesCombatUpperBodyOverlay() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	FVector GetThreadSafeVelocity() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	float GetThreadSafeVerticalSpeed() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	bool GetThreadSafeIsAccelerating() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Locomotion", meta = (BlueprintThreadSafe))
	FVector GetThreadSafeRelativeAccelerationAmount() const;

	/** X=lateral, Y=forward/back. Use as the input of a Lean additive or Blend Space. */
	UFUNCTION(BlueprintPure, Category = "Animation|Locomotion", meta = (BlueprintThreadSafe))
	FVector2D GetThreadSafeLeanAmount() const;

	/** Returns true when additive lean should be enabled (actively sprinting or allowed in-air). */
	UFUNCTION(BlueprintPure, Category = "Animation|Locomotion", meta = (BlueprintThreadSafe))
	bool GetThreadSafeShouldApplyLeanAdditive() const;

	/** Returns true when sprint additive lean is suppressed due to cornering/curvature (Sprint Diamond active). */
	UFUNCTION(BlueprintPure, Category = "Animation|Locomotion", meta = (BlueprintThreadSafe))
	bool GetThreadSafeIsSprintCurvatureLeanSuppressed() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Locomotion", meta = (BlueprintThreadSafe))
	float GetThreadSafePredictedStopDistance() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Locomotion", meta = (BlueprintThreadSafe))
	float GetThreadSafeVelocityToMoveInputAngle() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Locomotion", meta = (BlueprintThreadSafe))
	bool GetThreadSafeIsDecelerating() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	float GetThreadSafeMoveInputSize() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	bool GetThreadSafeHasMoveInput() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	bool GetThreadSafeIsInAir() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	bool GetThreadSafeIsJumping() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	bool GetThreadSafeIsLanding() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	bool GetThreadSafeIsCombatMode() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	bool GetThreadSafeIsMoving() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	bool GetThreadSafeIsMotionMatchingMoving() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	EProject_JLocomotionGaitIntent GetThreadSafeGaitIntent() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	EProject_JLocomotionRotationMode GetThreadSafeRotationMode() const;

	/** GASP State Alias equivalent: Ground Idle-group -> TransitionToLocomotion. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeStateControllerWantsLocomotion() const;

	/** GASP State Alias equivalent: Ground Locomotion-group -> TransitionToIdle. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeStateControllerWantsIdle() const;

	/** Dedicated Combat-Strafe Idle TIP entry gate. Do not substitute OneShotRequested here. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeStateControllerShouldTurnInPlace() const;

	/** Immediate TIP cancellation for movement, falling, or a gameplay action. Normal completion is handled separately. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeStateControllerShouldAbortTurnInPlace() const;

	/** Alpha gate for the Blend Stack TIP Steering node (1.0 during TIP, 0.0 otherwise). */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	float GetThreadSafeStateControllerTurnInPlaceSteeringAlpha() const;

	/** Returns 1.0 (Left 90), 2.0 (Left 180), 3.0 (Right 90), 4.0 (Right 180) index for CHT_Player_Strafe_TurnInPlace Chooser table. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	float GetThreadSafeStateControllerTurnInPlaceIndex() const;

	/** Desired Facing Rotator consumed by Steering Target Orientation pin in the AnimGraph. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	FRotator GetThreadSafeStateControllerDesiredFacingRotator() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe|Steering", meta = (BlueprintThreadSafe))
	float GetThreadSafeLocomotionSteeringAlpha() const;
	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe|Steering", meta = (BlueprintThreadSafe))
	FQuat GetThreadSafeLocomotionSteeringTarget() const;
	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe|Steering", meta = (BlueprintThreadSafe))
	float GetThreadSafeLocomotionSteeringProceduralTime() const;
	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe|Steering", meta = (BlueprintThreadSafe))
	float GetThreadSafeLocomotionSteeringAnimatedTime() const;
	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe|Steering", meta = (BlueprintThreadSafe))
	float GetThreadSafeLocomotionSteeringMaxYawError() const;

	/** Offset Root Bone Rotation Mode consumed by AnimGraph Offset Root Bone node. */
	UFUNCTION(BlueprintPure, Category = "Animation|Offset Root", meta = (BlueprintThreadSafe))
	EOffsetRootBoneMode GetThreadSafeOffsetRootRotationMode() const;

	/** Offset Root Bone Translation Mode consumed by AnimGraph Offset Root Bone node. */
	UFUNCTION(BlueprintPure, Category = "Animation|Offset Root", meta = (BlueprintThreadSafe))
	EOffsetRootBoneMode GetThreadSafeOffsetRootTranslationMode() const;

	/** Offset Root Bone Translation HalfLife consumed by AnimGraph Offset Root Bone node. */
	UFUNCTION(BlueprintPure, Category = "Animation|Offset Root", meta = (BlueprintThreadSafe))
	float GetThreadSafeOffsetRootTranslationHalfLife() const;

	/** Offset Root Bone Translation Radius consumed by AnimGraph Offset Root Bone node. */
	UFUNCTION(BlueprintPure, Category = "Animation|Offset Root", meta = (BlueprintThreadSafe))
	float GetThreadSafeOffsetRootTranslationRadius() const;

	/** Returns true when Foot IK / Leg IK should be disabled (e.g. during TurnInPlace, falling, or airborne transitions). */
	UFUNCTION(BlueprintPure, Category = "Animation|Leg IK", meta = (BlueprintThreadSafe))
	bool GetThreadSafeStateControllerDisableLegIK() const;

	/** Returns 0.0 when Leg IK should be disabled (e.g. TurnInPlace), and 1.0 when Leg IK is active. */
	UFUNCTION(BlueprintPure, Category = "Animation|Leg IK", meta = (BlueprintThreadSafe))
	float GetThreadSafeStateControllerLegIKAlpha() const;

	UFUNCTION(BlueprintCallable, Category = "Animation|State Controller")
	void OnStateEntry_TurnInPlace(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** GASP Grounded Conduit condition. Kept separate from idle/locomotion aliases. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeStateControllerIsGrounded() const;

	/** GASP -> In Air alias condition. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeStateControllerIsInAir() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeExperimentalOneShotEnabled() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeOneShotRequested() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeOneShotUseMotionMatchOnEntry() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	int32 GetThreadSafeOneShotRequestRevision() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	EProject_JLocomotionPhaseFamily GetThreadSafeOneShotPhase() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeOneShotEarlyTransitionWindowOpen() const;

	/** GASP Loco - State Changed condition; apply only from a locomotion-loop re-entry rule. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeStateControllerLocomotionSemanticStateChanged() const;

	/** GASP Idle - State Changed condition; apply only from an idle-loop re-entry rule. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeStateControllerIdleSemanticStateChanged() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	EProject_JStateControllerStrafeDirection GetThreadSafeStateControllerStrafeDirection() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	EProject_JStateControllerStrafeDirection GetThreadSafeStateControllerPreviousStrafeDirection() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	EProject_JStateControllerStance GetThreadSafeStateControllerStance() const;

	/** State Controller / Chooser input; does not replace the current regular-MM AnimGraph. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	EProject_JStateControllerPresentationState GetThreadSafeStateControllerPresentationState() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeStateControllerIdleBreakEnabled() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	float GetThreadSafeStateControllerIdleBreakMinimumStateTime() const;

	/** Cached State Controller chooser result. Safe only as a Blend Stack asset input. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	UAnimationAsset* GetThreadSafeStateControllerSelectedAnimation() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	FProject_JStateControllerChooserOutput GetThreadSafeStateControllerSelectedAnimationOutput() const;

	/** Convenience pins for the Blend Stack; equivalent to breaking SelectedAnimationOutput. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	float GetThreadSafeStateControllerSelectedAnimationStartTime() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	float GetThreadSafeStateControllerPlaybackHoldElapsedTime() const;

	/** Monotonic chooser result revision. A changing value means C++ requested a new Blend Stack entry. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	int32 GetThreadSafeStateControllerSelectionRevision() const;

	/** True only on the update immediately following a new State Controller chooser selection. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeStateControllerShouldForceBlend() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	float GetThreadSafeStateControllerSelectedAnimationBlendTime() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	UBlendProfile* GetThreadSafeStateControllerSelectedAnimationBlendProfile() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeStateControllerSelectedAnimationShouldLoop() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeStateControllerHasSelectedAnimation() const;

	/** Keeps loop/idle chooser rows from masking the regular Motion Matching pose. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeStateControllerShouldOverrideMotionMatching() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	float GetThreadSafeOneShotReturnBlendTime() const;

	/** Only the authored player receives its entry warp while a shared stack blends. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	float GetThreadSafePlaybackOrientationWarpingAlpha(const FAnimNodeReference& SampleNode) const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafePlaybackUsesAuthoredCorrections(const FAnimNodeReference& SampleNode) const;

	/**
	 * Returns 1 only for a selected Combat-Strafe direct one-shot with a stable
	 * movement direction. Multiply this by the selected animation's
	 * enable_warping curve inside the State Controller Blend Stack.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation|Combat Strafe", meta = (BlueprintThreadSafe))
	float GetThreadSafeStateControllerCombatStrafeOrientationWarpingAlpha() const;

	/** Signed actor-local angle input for the Combat-Strafe direct one-shot Orientation Warping node. */
	UFUNCTION(BlueprintPure, Category = "Animation|Combat Strafe", meta = (BlueprintThreadSafe))
	float GetThreadSafeStateControllerCombatStrafeOrientationWarpingAngle() const;

	/** GASP-equivalent rule for a non-looping State Controller Blend Stack asset. */
	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	bool GetThreadSafeStateControllerSelectedAnimationAlmostComplete() const;

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	float GetThreadSafeStateControllerSelectedAnimationTimeRemaining() const;

	/** Current held clip and snapshot revision own authored early-transition permission. GT only. */
	void RequestAuthoredEarlyTransition(UAnimSequenceBase* Animation, bool bRequireGaitChange, EProject_JLocomotionGaitIntent ExcludedGait);

	UFUNCTION(BlueprintPure, Category = "Animation|Mount", meta = (BlueprintThreadSafe))
	bool GetThreadSafeIsMounted() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Mount", meta = (BlueprintThreadSafe))
	bool GetThreadSafeMountedIsFlying() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Mount", meta = (BlueprintThreadSafe))
	bool GetThreadSafeMountedIsGliding() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Mount", meta = (BlueprintThreadSafe))
	float GetThreadSafeMountedSpeed() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Mount", meta = (BlueprintThreadSafe))
	float GetThreadSafeMountedVerticalSpeed() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Mount", meta = (BlueprintThreadSafe))
	bool GetThreadSafeHasMountedHandIKTargets() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Mount", meta = (BlueprintThreadSafe))
	FVector GetThreadSafeMountedLeftHandTargetComponentSpace() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Mount", meta = (BlueprintThreadSafe))
	FVector GetThreadSafeMountedRightHandTargetComponentSpace() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Mount", meta = (BlueprintThreadSafe))
	FGameplayTagContainer GetThreadSafeMountedAnimationTags() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Mount", meta = (BlueprintThreadSafe))
	bool GetThreadSafeMountedHasAnimationTag(FGameplayTag Tag) const;

	UFUNCTION(BlueprintPure, Category = "Animation|Mount", meta = (BlueprintThreadSafe))
	float GetThreadSafeMountedTransitionBlendTime() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Locomotion", meta = (BlueprintThreadSafe))
	EProject_JAnimationLocomotionMode GetThreadSafeLocomotionMode() const;

	/** True only while the shared on-foot Motion Matching and foot presentation are valid. */
	UFUNCTION(BlueprintPure, Category = "Animation|Locomotion", meta = (BlueprintThreadSafe))
	bool GetThreadSafeUsesOnFootLocomotion() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Foot Placement", meta = (BlueprintThreadSafe))
	FFootPlacementPlantSettings Get_FootPlacementPlantSettings() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Foot Placement", meta = (BlueprintThreadSafe))
	FFootPlacementInterpolationSettings Get_FootPlacementInterpolationSettings() const;

	/** Alpha for the Foot Placement node. Full-body montage weight is sampled on the game thread. */
	UFUNCTION(BlueprintPure, Category = "Animation|Procedural IK", meta = (BlueprintThreadSafe))
	float GetThreadSafeFootPlacementAlpha() const;

	/** Alpha for the Leg IK node. Montage and combat-layer state are sampled on the game thread. */
	UFUNCTION(BlueprintPure, Category = "Animation|Procedural IK", meta = (BlueprintThreadSafe))
	float GetThreadSafeLegIKAlpha() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Procedural IK", meta = (BlueprintThreadSafe))
	float GetThreadSafeFullBodyMontageWeight() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Motion Matching", meta = (BlueprintThreadSafe))
	UPoseSearchDatabase* GetCurrentActivePoseSearchDatabaseThreadSafe() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe|Motion Matching", meta = (BlueprintThreadSafe))
	int32 GetThreadSafeMotionMatchingCandidateCount() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Motion Matching", meta = (BlueprintThreadSafe))
	FName GetThreadSafeMotionMatchingSelectedAnimation() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Motion Matching", meta = (BlueprintThreadSafe))
	float GetThreadSafeMotionMatchingSelectedAnimationProgress() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Motion Matching", meta = (BlueprintThreadSafe))
	bool GetThreadSafeMotionMatchingSelectionIsContinuing() const;

	UFUNCTION(BlueprintPure, Category = "Animation|Debug")
	FString GetAnimationDebugSummary() const;

	/** Returns recent native Motion Matching decisions after the character has stopped moving. */
	FString GetMotionMatchingTraceSummary() const;

	/** Returns native Motion Matching and BlendStack frames captured during a Pivot session. */
	FString GetMotionMatchingPivotTraceSummary() const;

	/** Returns actual Motion Matching Cycle/Turn selection and BlendStack frames in Combat Strafe. */
protected:
	void ResetStateControllerChooserCache();
	void UpdatePresentationFromSnapshot();
	void ClearStateControllerInputReferences();
	void PublishStateControllerGroundLoop(EProject_JStateControllerPresentationState LoopState);
	FProject_JAnimThreadSafeData BuildThreadSafeData(float DeltaSeconds) const;
	void ResolveStateControllerPresentationStateWithPlaybackHold(
		const FProject_JAnimThreadSafeData& Data,
		FProject_JAnimOneShotPresentationThreadSafeData& InOutOneShot) const;
	void FillMovementThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void FillLocomotionStateThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void UpdateReleasedLandingMotionContext(FProject_JAnimThreadSafeData& Data, bool bReleasingNow = false) const;
	bool ShouldCancelLocalOneShotForInput(bool bCommittedPivot, float MouseCancelAngle, float MoveCancelAngle);
	void ApplyGenericMovementFallback(FProject_JAnimThreadSafeData& Data) const;
	bool FillPlayerThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void FillMountThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void FinalizeThreadSafeData(FProject_JAnimThreadSafeData& Data, bool bHasAimData) const;
	void FillProceduralIKThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void PublishThreadSafeDataToProxy(FProject_JAnimThreadSafeData& Data);
	void UpdateLocomotionSteeringData(FProject_JAnimThreadSafeData& Data) const;
	void UpdateGeneralTurnData(FProject_JAnimThreadSafeData& Data);
	void UpdateBackwardRefaceData(FProject_JAnimThreadSafeData& Data);
	bool IsMovingOneShotInputOwner(const FProject_JAnimThreadSafeData& Data) const;
	FProject_JGeneralTurnPolicy GeneralTurnPolicy;
	FProject_JBackwardRefacePolicy BackwardRefacePolicy;
	FProject_JOneShotCurvePolicy OneShotCurvePolicy;
	bool bOneShotCurveReleasedThisUpdate = false;
	FProject_JTurnSelectionFeedback CompletedTurnFeedback;
	void RecordAnimationFlowPublication(const FProject_JAnimThreadSafeData& Data, bool bChooserUpdate, bool bFreshSnapshot);
	void RecordAnimationFlowEvaluation();
	void RecordLocomotionContinuityEvaluation();
	EProject_JStateControllerFoot ResolveStateControllerFootFromContactCurves(
		bool bAllowPhaseHistoryFallback,
		EProject_JStateControllerFootSelectionReason& OutReason) const;
	UPoseSearchDatabase* EvaluatePoseSearchDatabaseOnGameThread(const FProject_JAnimThreadSafeData& Data);
	UPoseSearchDatabase* EvaluateTurnCycleCompanionOnGameThread(const FProject_JAnimThreadSafeData& Data) const;
	void EvaluateStateControllerAnimationChooserOnGameThread(FProject_JAnimThreadSafeData& Data);
	void PublishChooserProperties(const FProject_JAnimThreadSafeData& Data);
	void PublishChooserMovementProperties(const FProject_JAnimThreadSafeData& Data);
	void PublishChooserGroundProperties(const FProject_JAnimThreadSafeData& Data);
	void PublishChooserAirProperties(const FProject_JAnimThreadSafeData& Data);
	void PublishChooserLandingProperties(const FProject_JAnimThreadSafeData& Data);
	void PublishChooserCombatProperties(const FProject_JAnimThreadSafeData& Data);
	void ApplyFarChooserOverrides(const FProject_JAnimThreadSafeData& Data);
	bool ShouldEvaluateMotionMatchingThisFrame(float DeltaSeconds, bool bForceRefresh);
	bool ShouldForceMotionMatchingContextRefresh(const FProject_JAnimThreadSafeData& Data) const;
	bool ShouldForceMotionMatchingReselect(const FProject_JAnimThreadSafeData& Data) const;
	void CacheEvaluatedMotionMatchingContext(const FProject_JAnimThreadSafeData& Data);
	void RecordMotionMatchingTrace(const FProject_JAnimThreadSafeData& Data, bool bDatabaseChanged, bool bForceReselect);
	/** Linked layers may consume the snapshot, but only the mesh's primary instance may mutate MM/trajectory state. */
	bool IsPrimaryMeshAnimInstance() const;
	void ResetTrajectoryHistoryOnAccelerationStop(const FProject_JAnimThreadSafeData& Data) const;
	float CalculateAimOffsetAlpha(const FProject_JAnimThreadSafeData& Data) const;
	void UpdateStrafeFacingData(FProject_JAnimThreadSafeData& Data) const;
	bool ShouldSkipNativeUpdate(float DeltaSeconds);
	const UProject_JLocomotionProfile* GetLocomotionProfile() const;
	FProject_JAnimationBudgetSettings GetEffectiveAnimationBudgetSettings() const;
	float GetEffectiveGenericMoveInputSpeedThreshold() const;
	float GetEffectiveSprintLocomotionSpeedThreshold() const;
	FProject_JRemoteVisualLocomotionPolicy GetEffectiveRemoteVisualPolicy() const;
	bool ShouldDisableMotionMatchingBeyondFarDistance() const;
	const UProject_JCombatAnimProfile* GetCombatAnimProfile() const;
	float GetEffectiveCombatAimAlpha() const;

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Motion Matching", AdvancedDisplay, meta = (ToolTip = "Final fallback Chooser Table. Prefer CharacterAnimProfile -> LocomotionProfile -> MotionMatchingAssetSet on the character."))
	TObjectPtr<UChooserTable> MotionMatchingChooserTable = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Motion Matching", AdvancedDisplay, meta = (ToolTip = "Final fallback locomotion PSD. Prefer CharacterAnimProfile -> LocomotionProfile -> MotionMatchingAssetSet on the character."))
	TObjectPtr<UPoseSearchDatabase> DefaultPoseSearchDatabase = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Motion Matching", AdvancedDisplay, meta = (ToolTip = "Final fallback idle PSD. Prefer CharacterAnimProfile -> LocomotionProfile -> MotionMatchingAssetSet on the character."))
	TObjectPtr<UPoseSearchDatabase> DefaultIdlePoseSearchDatabase = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|Motion Matching")
	TObjectPtr<UPoseSearchDatabase> CurrentActivePoseSearchDatabase = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UPoseSearchDatabase> CurrentTurnCycleCompanion = nullptr;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimThreadSafeData ThreadSafeData;
	FProject_JAnimationFlowSampler AnimationFlowSampler;
	FProject_JLocomotionContinuityTraceSampler LocomotionContinuityTraceSampler;
	bool bAnimationFlowEvaluationPending = false;

	friend class FProjectJAuthoredEarlyTransitionTest;
	bool IsAuthoredEarlyTransitionAllowed(EProject_JLocomotionGaitIntent CurrentGait) const;
	TWeakObjectPtr<UAnimSequenceBase> AuthoredEarlyTransitionAsset;
	uint64 EarlyTransitionSnapshotRevision = 0;
	uint64 AuthoredEarlyTransitionSnapshotRevision = 0;
	bool bAuthoredEarlyTransitionPending = false;
	bool bAuthoredEarlyTransitionRequiresGaitChange = true;
	EProject_JLocomotionGaitIntent AuthoredEarlyTransitionExcludedGait = EProject_JLocomotionGaitIntent::Run;

	/** Game-thread cache backing the immutable proxy selection snapshot. */
	TWeakObjectPtr<UChooserTable> CachedStateControllerChooserTable;
	EProject_JStateControllerPresentationState CachedStateControllerPresentationState = EProject_JStateControllerPresentationState::Disabled;
	EProject_JLocomotionRotationMode CachedStateControllerRotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
	EProject_JLocomotionGaitIntent CachedStateControllerGaitIntent = EProject_JLocomotionGaitIntent::Walk;
	EProject_JStateControllerStance CachedStateControllerStance = EProject_JStateControllerStance::Stand;
	EProject_JStateControllerStrafeDirection CachedStateControllerStrafeDirection = EProject_JStateControllerStrafeDirection::Forward;
	EProject_JStateControllerStrafeDirection CachedStateControllerPreviousStrafeDirection = EProject_JStateControllerStrafeDirection::Forward;
	int32 CachedStateControllerPivotRequestRevision = 0;
	EProject_JStateControllerFoot CachedStateControllerOneShotFoot = EProject_JStateControllerFoot::None;
	/** TIP chooser row (L90/L180/R90/R180) last used by the cached direct asset. */
	float CachedStateControllerTurnInPlaceIndex = 0.0f;
	bool bCachedStateControllerCombatMode = false;
	bool bCachedStateControllerFallOff = false;
	bool bCachedStateControllerLandWasMoving = false;
	bool bCachedStateControllerLandWasSprinting = false;
	bool bCachedStateControllerUseHeavyLand = false;
	int32 CachedStateControllerLandingPresentationRevision = INDEX_NONE;
	int32 LastHandledStartResponsiveExitRevision = 0;
	/** Captured at a Start/Land one-shot entry to detect a local-player mouse turn or movement input change. */
	bool bHasStateControllerOneShotControlYaw = false;
	float StateControllerOneShotControlYaw = 0.0f;
	bool bHasStateControllerOneShotMoveInputYaw = false;
	float StateControllerOneShotMoveInputYaw = 0.0f;
	/** Fixed Start-entry reference used while a simulated proxy's direct Start asset remains held. */
	bool bHasStateControllerRemoteStartReference = false;
	float StateControllerRemoteStartActorYaw = 0.0f;
	bool bHasStateControllerRemoteStartMoveYaw = false;
	float StateControllerRemoteStartMoveYaw = 0.0f;
	bool bHasStateControllerLeftFootContactCurve = false;
	bool bHasStateControllerRightFootContactCurve = false;
	bool bHasStateControllerFootContactCurves = false;
	float CachedStateControllerLeftFootContact = 0.0f;
	float CachedStateControllerRightFootContact = 0.0f;
	EProject_JStateControllerFoot StateControllerFootPhaseHistory = EProject_JStateControllerFoot::None;
	bool bHasStateControllerFootPhaseHistory = false;
	/** Last Pivot semantic request that latched a direct one-shot foot. */
	int32 StateControllerPivotFootLatchRequestRevision = 0;
	/** Start gait is briefly provisional, then held until the authored Start exits. */
	EProject_JLocomotionGaitIntent StateControllerStartGaitForChooser = EProject_JLocomotionGaitIntent::Run;
	double StateControllerStartGaitStartedAtSeconds = 0.0;
	bool bHasStateControllerStartGaitForChooser = false;
	bool bStateControllerStartGaitCommitted = false;
	EProject_JLocomotionGaitIntent StateControllerStopGaitForChooser = EProject_JLocomotionGaitIntent::Run;
	bool bHasStateControllerStopGaitForChooser = false;
	EProject_JLocomotionGaitIntent StateControllerLandGaitForChooser = EProject_JLocomotionGaitIntent::Run;
	bool bHasStateControllerLandGaitForChooser = false;
	TObjectPtr<UAnimationAsset> CachedStateControllerSelectedAnimation = nullptr;
	FProject_JStateControllerChooserOutput CachedStateControllerSelectedAnimationOutput;
	bool bCachedStateControllerHasSelectedAnimation = false;
	int32 StateControllerChooserSelectionRevision = 0;
	/** Debug-only edge guard for a linked instance that observes a Pivot request. */
	int32 LastLoggedNonPrimaryPivotRequestRevision = 0;

	/** 게임 스레드에서 일회성 동작의 수명을 관리한다. 이동 의미 상태는 컴포넌트에 남는다. */
	mutable FProject_JStateControllerRuntime StateControllerRuntime;
	void ResetStateControllerOwnerPresentation();
	/** Bounded game-thread diagnostic window around draw/sheathe and input release. */
	bool bCombatStopTraceWasInputHeld = false;
	bool bCombatStopTraceWasTransitionActive = false;
	double CombatStopTraceUntilSeconds = 0.0;
	double CombatStopTraceNextSampleSeconds = 0.0;

	/** 공중 점프 재선택 상태 변수 */
	mutable float LastJumpAirReselectElapsed = 0.0f;
	mutable float LastJumpAirBlendDuration = 0.15f;
	mutable bool bIsJumpAirReselecting = false;
	mutable float SavedJumpAirElapsed = 0.0f;
	mutable EProject_JStateControllerStrafeDirection PendingJumpAirDirection = EProject_JStateControllerStrafeDirection::Forward;

	/** 스프린트 곡선 주행(Diamond) 시 린 차단 히스테리시스 상태 플래그 */
	mutable bool bSprintCurvatureLeanSuppressed = false;
	/**
	 * A combat draw/sheathe montage is a presentation boundary.  A direct Land asset
	 * selected before that boundary must never resume after the montage blends
	 * out.  This flag only suppresses that already-active landing presentation
	 * until both its semantic phase and physical landing event have ended; it
	 * does not change gameplay landing state.
	 */
	bool bSuppressPreTransitionLandingPresentationUntilLandingEnds = false;

	/** Rising-edge detector for local combat draw/sheathe presentation boundaries. */
	bool bWasPlayingCombatPresentationTransitionForStateController = false;

	/** Rising/falling-edge detector and hold timestamp for full-body action montages (attacks, skills, dodges). */
	bool bWasPlayingFullBodyMontageForStateController = false;
	double LastFullBodyMontageEndedAtSeconds = -100.0;
	/** 탑승 경계에서 이전 지상 일회성 애니메이션이 다시 선택되지 않도록 한다. */
	bool bWasMountedForStateController = false;



	// --- Chooser Variables (read by Chooser Table rows on Game Thread) ---

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	float ChooserVerticalSpeed = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	float ChooserAccelerationRatio = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	float ChooserMoveInputSize = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	float ChooserMoveInputHeldTime = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	float ChooserMoveInputTurnAngle = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	float ChooserLastFallSpeed = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	float ChooserLandStartFallSpeed = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserHasMoveInput = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserStartRequested = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserStopRequested = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserSharpTurnRequested = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserWantsSprint = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseSprintLocomotion = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseRunStart = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseRemoteRunStart = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseSprintStart = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseRunStop = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseSprintStop = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseRunLocomotion = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseRemoteRunLocomotion = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseSprintLocomotionRow = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseJumpStart = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseFallOff = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseFallLoop = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseLightLand = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseHeavyLandRow = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseStandLightLand = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseStandHeavyLand = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseRunLightLand = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseSprintLightLand = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseRunHeavyLand = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseSprintHeavyLand = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserLandWasSprinting = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserLandWasMoving = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserStartWasSprinting = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserStopWasSprinting = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserIsInAir = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserIsJumping = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserIsLanding = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserUseHeavyLand = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserIsCombatMode = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserIsIdle = true;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserIsRemoteProxy = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	EProject_JGroundMotionMode ChooserGroundMotionMode = EProject_JGroundMotionMode::Idle;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	EProject_JLocomotionGaitIntent ChooserGaitIntent = EProject_JLocomotionGaitIntent::Run;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	EProject_JLocomotionRotationMode ChooserRotationMode = EProject_JLocomotionRotationMode::OrientToMovement;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	float ChooserDesiredFacingDeltaYaw = 0.0f;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserIsStartingDerived = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserIsPivoting = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserShouldTurnInPlace = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	bool bChooserShouldSpinTransition = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Optimization")
	FProject_JAnimOptimizationPolicy CurrentOptimizationPolicy;

	const FProject_JAnimOptimizationPolicy& GetCurrentOptimizationPolicy() const { return CurrentOptimizationPolicy; }
	/** GT presentation demand; does not wait for a worker proxy or read its mutable state. */
	bool NeedsFullRateVisualUpdate() const;

	// --- Optimization Settings ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Advanced|Optimization", AdvancedDisplay, meta = (ToolTip = "Skips animation-only data work on dedicated servers. Event replication still runs."))
	bool bSkipDedicatedServerAnimationDataUpdate = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Optimization", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback optimization setting used when no effective LocomotionProfile is assigned."))
	float HiddenRemoteUpdateInterval = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Advanced|Optimization", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Render visibility tolerance used before hidden remote update throttling is allowed."))
	float RecentlyRenderedTolerance = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Optimization", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback distance setting used when no effective LocomotionProfile is assigned."))
	float NearMotionMatchingDistance = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Optimization", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback distance setting used when no effective LocomotionProfile is assigned."))
	float MidMotionMatchingDistance = 6000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Optimization", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback distance setting used when no effective LocomotionProfile is assigned."))
	float FarMotionMatchingDistance = 12000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Optimization", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback update interval used when no effective LocomotionProfile is assigned."))
	float MidMotionMatchingUpdateInterval = 0.033f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Optimization", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback update interval used when no effective LocomotionProfile is assigned."))
	float FarMotionMatchingUpdateInterval = 0.083f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Optimization", AdvancedDisplay, meta = (ToolTip = "Fallback optimization setting used when no effective LocomotionProfile is assigned."))
	bool bDisableMotionMatchingBeyondFarDistance = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Movement", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback movement threshold used when no effective LocomotionProfile is assigned."))
	float GenericMoveInputSpeedThreshold = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Movement", AdvancedDisplay, meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Fallback sprint threshold used when no effective LocomotionProfile is assigned."))
	float SprintLocomotionSpeedThreshold = 600.0f;

	// --- AimOffset Settings ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxAimYaw = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxAimPitch = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float StandingAimAlpha = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MovingAimAlpha = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float SprintAimAlpha = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|AimOffset", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float CombatAimAlpha = 1.0f;

	// --- Procedural IK ---

	/** Shared full-body montage policy. The same master AnimBP rule works for every job. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Procedural IK")
	FProject_JFullBodyMontageIKPolicy FullBodyMontageIKPolicy;

	// --- Foot Placement Fallbacks ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Foot Placement", AdvancedDisplay, meta = (ToolTip = "Fallback used only when no effective LocomotionProfile provides foot placement plant settings."))
	FFootPlacementPlantSettings FootPlacementPlantSettingsDefault;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Foot Placement", AdvancedDisplay, meta = (ToolTip = "Fallback used only when no effective LocomotionProfile provides stop foot placement plant settings."))
	FFootPlacementPlantSettings FootPlacementPlantSettingsStops;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Foot Placement", AdvancedDisplay, meta = (ToolTip = "Fallback used only when no effective LocomotionProfile provides foot placement interpolation settings."))
	FFootPlacementInterpolationSettings FootPlacementInterpolationSettingsDefault;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Migration Fallbacks|Foot Placement", AdvancedDisplay, meta = (ToolTip = "Fallback used only when no effective LocomotionProfile provides stop foot placement interpolation settings."))
	FFootPlacementInterpolationSettings FootPlacementInterpolationSettingsStops;

private:
	friend class FProjectJAnimationSnapshotBoundaryTest;
	friend class FProjectJGeneratedMMDatabaseLifecycleTest;
	friend class FProjectJGroundStateOwnershipTest;
	friend class FProjectJAnimationProxyLifetimeTest;
	friend class FProjectJLandingReturnContextTest;
	friend class FProjectJStrafeFacingSelectionTest;
	friend class FProjectJStrafePivotCardinalTest;
	friend class FProjectJStrafePivotRedirectBasisTest;
	friend class FProjectJStrafePivotConsecutiveTest;
	friend class FProjectJOneShotModeContinuityTest;
	friend class FProjectJAirLoopOutputTest;
	friend class FProjectJLocomotionSteeringOwnershipTest;
	friend class FProjectJAnimationClockTest;
	FProject_JAnimationClock AnimationClock;
	friend class FProjectJTurnInPlaceAndCombatStopTest;
	float HiddenRemoteUpdateAccumulator = 0.0f;
	FProject_JMotionMatchingRuntime MotionMatchingRuntime;
	TArray<FProject_JMotionMatchingTraceEntry> MotionMatchingTrace;
	int32 MaxMotionMatchingTraceEntries = 96;
};
