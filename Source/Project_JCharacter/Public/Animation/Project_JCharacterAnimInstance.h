// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/AnimInstance.h"
#include "Animation/TrajectoryTypes.h"
#include "Animation/Project_JAnimationBudgetTypes.h"
#include "Animation/Project_JCharacterAnimInstanceBase.h"
#include "Animation/Project_JAnimationLocomotionMode.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "BoneControllers/AnimNode_FootPlacement.h"
#include "Combat/Project_JCombatTypes.h"
#include "Project_JLocomotionAnimTypes.h"
#include "BoneControllers/AnimNode_OffsetRootBone.h"
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

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimMovementThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FVector Acceleration = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FVector AccelerationDirection = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FTransformTrajectory Trajectory;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float AccelerationRatio = 0.0f;

	/** Signed actor-local acceleration normalized by CMC acceleration/braking limits. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FVector RelativeAccelerationAmount = FVector::ZeroVector;

	/** X is lateral lean, Y is forward/back lean; calculated from RelativeAccelerationAmount. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FVector2D LeanAmount = FVector2D::ZeroVector;

	/** Animation-only predicted braking distance, in centimetres. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float PredictedStopDistance = 0.0f;

	/** Animation-only expected speed gain over the locomotion prediction horizon. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float PredictedSpeedGain = 0.0f;
	/** Game-thread reconstructed velocity from the predicted trajectory sample. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FVector FutureTrajectoryVelocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float FutureTrajectorySpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float FutureTrajectoryTurnAngle = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bHasFutureTrajectoryVelocity = false;

	/** Angle from current velocity to requested movement input. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float VelocityToMoveInputAngle = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float GroundSpeed = 0.0f;

	/**
	 * Direction of actual horizontal velocity relative to the character's actor
	 * facing. -90 is left, 0 is forward, +90 is right, and +/-180 is backward.
	 * This intentionally uses velocity rather than local input, so simulated
	 * proxies and movement correction use the same Blend Space coordinates.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float RelativeVelocityDirection = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float VerticalSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsAccelerating = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bWasAccelerating = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bStoppedAcceleratingThisFrame = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsDecelerating = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bHasTrajectory = false;

};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimInputThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float MoveInputSize = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float MoveInputHeldTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float MoveInputTurnAngle = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float MovementDirection = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bHasMoveInput = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bSharpTurnRequested = false;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimGroundThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bStartRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bStopRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bWantsSprint = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bUseSprintLocomotion = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bStartWasSprinting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bStopWasSprinting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	EProject_JGroundMotionMode GroundMotionMode = EProject_JGroundMotionMode::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float GroundMotionModeElapsedTime = 0.0f;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimAirThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsInAir = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsJumping = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsFallOffStart = false;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimLandingThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float LastFallSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float LandStartFallSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsLanding = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bUseHeavyLand = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bLandWasSprinting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bLandWasMoving = false;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimCombatThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsCombatMode = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsAttacking = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsDodging = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsHitReacting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsPlayingCombatIntro = false;

	/** Continuous animation composition selected by the equipped weapon profile. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	EProject_JCombatAnimationPresentationMode PresentationMode = EProject_JCombatAnimationPresentationMode::UpperBodyOverlay;
};

/**
 * Per-frame procedural leg correction snapshot.
 *
 * Full-body montages author their own lower-body pose.  Their slot weight is
 * sampled on the game thread and copied through the animation proxy so the
 * AnimGraph never needs to query montage state from a worker thread.
 */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimProceduralIKThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Procedural IK")
	float FullBodyMontageWeight = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Procedural IK")
	float FootPlacementAlpha = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Procedural IK")
	float LegIKAlpha = 1.0f;
};

/** Shared policy for protecting authored combat poses from procedural leg correction. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JFullBodyMontageIKPolicy
{
	GENERATED_BODY()

	/** Slot reserved for full-body montages such as attacks, dodge, draw/sheath, hit react, and death. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Procedural IK")
	FName FullBodyMontageSlotName = TEXT("DefaultSlot");

	/** Alpha used by Foot Placement at a full weight full-body montage. Keep at 1.0 when only Leg IK needs suppression. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Procedural IK", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float FootPlacementAlphaDuringFullBodyMontage = 1.0f;

	/** Alpha used by Leg IK at a full weight full-body montage. Full-body combat actions normally use 0.0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Procedural IK", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float LegIKAlphaDuringFullBodyMontage = 0.0f;

	/** Alpha used by Leg IK only while a full-body combat locomotion set is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Procedural IK", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float LegIKAlphaDuringFullBodyCombat = 0.0f;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimLocomotionContextThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	EProject_JLocomotionGaitIntent GaitIntent = EProject_JLocomotionGaitIntent::Run;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	EProject_JLocomotionRotationMode RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	EProject_JLocomotionPhaseFamily PhaseFamily = EProject_JLocomotionPhaseFamily::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float DesiredFacingDeltaYaw = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsMoving = false;

	/** MM presentation movement state; use only for MM diagnostics/policy, not gameplay. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsMotionMatchingMoving = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsStarting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsPivoting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bShouldTurnInPlace = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bShouldSpinTransition = false;
};

/**
 * Motion-Matching control data authored by the locomotion component on the
 * game thread and consumed read-only through the proxy.  This deliberately
 * contains no Actor, Controller, Component, or GameplayTag references.
 */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimMotionMatchingPostSelectionData
{
	GENERATED_BODY()

	/** Previous-frame native result copied on the game thread; no live asset references are exposed to worker threads. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Motion Matching")
	FName SelectedDatabase = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Motion Matching")
	FName SelectedAnimation = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Motion Matching")
	float SelectedAnimationTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Motion Matching")
	float SelectedAnimationLength = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Motion Matching")
	float WantedPlayRate = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Motion Matching")
	float SearchCost = 0.0f;


	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Motion Matching")
	bool bIsContinuingPoseSearch = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Motion Matching")
	TArray<FName> DatabaseTags;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimMotionMatchingThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Motion Matching")
	int32 SelectionRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Motion Matching")
	bool bSelectionChanged = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Motion Matching")
	bool bForceReselect = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Motion Matching")
	int32 TrajectorySampleCount = 0;

	/** Component-authored selection contract. It contains no live UObject references. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Motion Matching")
	FProject_JMotionMatchingSelectionContext SelectionContext;

	/** Native selection result from the preceding animation update, used for read-only presentation/debug policy. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Motion Matching")
	FProject_JAnimMotionMatchingPostSelectionData PostSelection;
};

/**
 * Read-only request from C++ locomotion state to an optional GASP-style
 * logical State Machine / Blend Stack presentation layer. Asset selection,
 * start time, loop and completion remain ABP/Chooser responsibilities.
 */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimOneShotPresentationThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	bool bEnabled = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	bool bRequested = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	bool bUseMotionMatchOnEntry = false;

	/** Authored NotifyState window which may permit an early transition to a loop. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	bool bEarlyTransitionWindowOpen = false;

	/** Optional data-driven fallback. Prefer a Chooser output value per animation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	float FallbackLeadTime = 0.0f;

	/** Incremented by the locomotion component when its semantic MM context changes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	int32 RequestRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	EProject_JLocomotionPhaseFamily PhaseFamily = EProject_JLocomotionPhaseFamily::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	EProject_JLocomotionRotationMode RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;

	/** Eight-direction combat-Strafe sector. It is never used for OTM. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	EProject_JStateControllerStrafeDirection StrafeDirection = EProject_JStateControllerStrafeDirection::Forward;

	/** Previous valid eight-direction combat-Strafe sector, used only by Strafe Pivot chooser rows. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	EProject_JStateControllerStrafeDirection PreviousStrafeDirection = EProject_JStateControllerStrafeDirection::Forward;

	/** Signed local angle (actor facing -> trajectory velocity) used to resolve StrafeDirection. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	float StrafeDirectionAngle = 0.0f;

	/** True when StrafeDirectionAngle came from a usable future/current trajectory velocity. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	bool bHasStrafeDirectionAngle = false;

	/**
	 * Foot selected from the prior evaluated contact curves when a direct
	 * one-shot starts. None means the curves were unavailable or ambiguous.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	EProject_JStateControllerFoot Foot = EProject_JStateControllerFoot::None;

	/** GASP's locomotion stance (Stand/Crouch), not Project_J combat or weapon stance. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	EProject_JStateControllerStance Stance = EProject_JStateControllerStance::Stand;

	/** Resolved State Controller state. It is intentionally inert while bEnabled is false. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	EProject_JStateControllerPresentationState PresentationState = EProject_JStateControllerPresentationState::Disabled;

	/** Enables the authored Idle Loop -> Idle Break rule; it does not start a break by itself. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	bool bIdleBreakEnabled = false;

	/** Data Asset threshold consumed by an ABP logical-state-time transition rule. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	float IdleBreakMinimumStateTime = 0.0f;

	/**
	 * Immutable asset reference selected on the game thread by the optional
	 * State Controller chooser. AnimGraph only consumes this cached reference;
	 * it must not inspect the asset or query the world on a worker thread.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	TObjectPtr<UAnimationAsset> SelectedAnimation = nullptr;

	/** Metadata authored beside SelectedAnimation in the State Controller chooser. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	FProject_JStateControllerChooserOutput SelectedAnimationOutput;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	bool bHasSelectedAnimation = false;

	/** True only while a direct non-loop transition asset should replace regular Motion Matching. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	bool bShouldOverrideMotionMatching = false;

	/**
	 * Combat-Strafe only gate for the State Controller Blend Stack's Orientation
	 * Warping node. This never changes capsule/controller rotation; it merely
	 * permits the selected direct one-shot to be visually warped when its own
	 * authored enable_warping curve is non-zero.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	bool bShouldEnableCombatStrafeOrientationWarping = false;

	/** Loop intent is derived from the logical presentation state, never guessed from asset duration. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	bool bSelectedAnimationShouldLoop = false;

	/** Increments only when the cached chooser result/context changes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	int32 SelectionRevision = 0;

	/**
	 * Asset-progress approximation for the logical State Controller. It is derived
	 * from the selected Blend Stack asset, never from a fixed Start/Stop duration.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	float TransitionElapsedTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	float TransitionTimeRemaining = 0.0f;

	/** True at the asset end, or during an authored EarlyTransition NotifyState window. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	bool bTransitionAnimationAlmostComplete = false;

	/** GASP Loco - State Changed equivalent; ABP still gates it with state time > 0. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	bool bLocomotionSemanticStateChanged = false;

	/** GASP Idle - State Changed equivalent. Only locomotion stance may restart Idle. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	bool bIdleSemanticStateChanged = false;
};

/** One game-thread Motion Matching decision retained for post-movement debugging. */
struct FProject_JMotionMatchingTraceEntry
{
	double WorldTimeSeconds = 0.0;
	int32 SelectionRevision = 0;
	FString DatabaseName;
	/** Native Pose Search result from the preceding animation update. This is trace-only; it never drives locomotion state. */
	FString SelectedAnimationName;
	float SelectedAnimationTime = 0.0f;
	float SelectedAnimationLength = 0.0f;
	float SelectedAnimationWantedPlayRate = 1.0f;
	bool bSelectionIsContinuing = false;
	/** Final blended animation-curve values sampled on the game thread for asset-contract diagnostics. */
	float MoveDataSpeedCurve = 0.0f;
	float EnableWarpingCurve = 0.0f;
	float PhaseCurve = 0.0f;
	EProject_JLocomotionPhaseFamily PhaseFamily = EProject_JLocomotionPhaseFamily::Idle;
	EProject_JLocomotionGaitIntent GaitIntent = EProject_JLocomotionGaitIntent::Run;
	EProject_JLocomotionRotationMode RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
	EProject_JGroundMotionMode GroundMotionMode = EProject_JGroundMotionMode::Idle;
	float GroundModeAgeSeconds = 0.0f;
	float GroundSpeed = 0.0f;
	float PredictedSpeedGain = 0.0f;
	float FutureTrajectorySpeed = 0.0f;
	float FutureTrajectoryTurnAngle = 0.0f;
	float PredictedStopDistance = 0.0f;
	float InputTurnAngle = 0.0f;
	int32 TrajectorySampleCount = 0;
	bool bHasMoveInput = false;
	bool bIsAccelerating = false;
	bool bIsDecelerating = false;
	bool bHasFutureTrajectoryVelocity = false;
	bool bDatabaseChanged = false;
	bool bForceReselect = false;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimAimThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float AimYaw = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float AimPitch = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float AimOffsetAlpha = 0.0f;
};

/** Snapshot used by the player ABP while the player character is attached to a mount. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimMountThreadSafeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	float Speed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	float VerticalSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	FVector LeftHandTargetComponentSpace = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	FVector RightHandTargetComponentSpace = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	bool bIsMounted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	bool bIsFlying = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	bool bIsGliding = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	bool bHasHandIKTargets = false;
};

/**
 * All animation thread-safe data used by the Proxy and AnimGraph.
 *
 * Flat legacy fields (Velocity, GroundSpeed, bIsInAir, etc.) have been removed.
 * All data is now accessed exclusively through the sub-structs below.
 * Use dedicated thread-safe getter functions in AnimGraph instead of splitting this struct.
 */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JAnimThreadSafeData
{
	GENERATED_BODY()

	// DeltaTime is injected by Proxy.PreUpdate, kept here as metadata for worker thread logic.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float DeltaTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimMovementThreadSafeData Movement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimInputThreadSafeData Input;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimGroundThreadSafeData Ground;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimAirThreadSafeData Air;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimLandingThreadSafeData Landing;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimCombatThreadSafeData Combat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimProceduralIKThreadSafeData ProceduralIK;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimLocomotionContextThreadSafeData LocomotionContext;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimMotionMatchingThreadSafeData MotionMatching;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimOneShotPresentationThreadSafeData OneShotPresentation;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimAimThreadSafeData Aim;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimMountThreadSafeData Mount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	EProject_JAnimationLocomotionMode LocomotionMode = EProject_JAnimationLocomotionMode::OnFoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JMotionMatchingSearchPolicy MotionMatchingSearchPolicy;

};

UCLASS(Blueprintable, BlueprintType)
class PROJECT_JCHARACTER_API UProject_JCharacterAnimInstance : public UProject_JCharacterAnimInstanceBase
{
	GENERATED_BODY()

public:
	UProject_JCharacterAnimInstance();

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativePostEvaluateAnimation() override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

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

	/** Backward-compatible Stop-only mirror of StateControllerOneShotFootForChooser. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	EProject_JStateControllerFoot StateControllerStopFootForChooser = EProject_JStateControllerFoot::None;

	/** Latched at TransitionToInAir entry so Jump Start and Fall Off rows cannot overlap. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Animation|Chooser Context")
	bool bStateControllerFallOffForChooser = false;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe, DeprecatedFunction, DeprecationMessage = "Use dedicated thread-safe getters such as GetThreadSafeTrajectory, GetThreadSafeAimYaw, GetThreadSafeAimPitch, and GetThreadSafeAimOffsetAlpha in AnimGraph."))
	FProject_JAnimThreadSafeData GetThreadSafeData() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	FTransformTrajectory GetThreadSafeTrajectory() const;

	UFUNCTION(BlueprintPure, Category = "Animation|AimOffset", meta = (BlueprintThreadSafe))
	float GetThreadSafeAimYaw() const;

	UFUNCTION(BlueprintPure, Category = "Animation|AimOffset", meta = (BlueprintThreadSafe))
	float GetThreadSafeAimPitch() const;

	UFUNCTION(BlueprintPure, Category = "Animation|AimOffset", meta = (BlueprintThreadSafe))
	float GetThreadSafeAimOffsetAlpha() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	float GetThreadSafeGroundSpeed() const;

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

	UFUNCTION(BlueprintPure, Category = "Animation|One Shot", meta = (BlueprintThreadSafe))
	float GetThreadSafeOneShotFallbackLeadTime() const;

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

	/** Game-thread only bridge for UAnimNotifyState_Project_JLocomotionEarlyTransition. */
	void BeginOneShotEarlyTransitionWindow();
	void EndOneShotEarlyTransitionWindow();

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

	UFUNCTION(BlueprintPure, Category = "Animation|Locomotion", meta = (BlueprintThreadSafe))
	EProject_JAnimationLocomotionMode GetThreadSafeLocomotionMode() const;

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

protected:
	FProject_JAnimThreadSafeData BuildThreadSafeData(float DeltaSeconds) const;
	void ResolveStateControllerPresentationStateWithPlaybackHold(
		const FProject_JAnimThreadSafeData& Data,
		FProject_JAnimOneShotPresentationThreadSafeData& InOutOneShot) const;
	void FillMovementThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void FillLocomotionStateThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void ApplyGenericMovementFallback(FProject_JAnimThreadSafeData& Data) const;
	bool FillPlayerThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void FillMountThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void FinalizeThreadSafeData(FProject_JAnimThreadSafeData& Data, bool bHasAimData) const;
	void FillProceduralIKThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void PublishThreadSafeDataToProxy(const FProject_JAnimThreadSafeData& Data);
	EProject_JStateControllerFoot ResolveStateControllerFootFromContactCurves(
		bool bAllowPhaseHistoryFallback,
		EProject_JStateControllerFootSelectionReason& OutReason) const;
	UPoseSearchDatabase* EvaluatePoseSearchDatabaseOnGameThread(const FProject_JAnimThreadSafeData& Data);
	void EvaluateStateControllerAnimationChooserOnGameThread(FProject_JAnimThreadSafeData& Data);
	void PublishChooserProperties(const FProject_JAnimThreadSafeData& Data);
	void PublishChooserMovementProperties(const FProject_JAnimThreadSafeData& Data);
	void PublishChooserGroundProperties(const FProject_JAnimThreadSafeData& Data);
	void PublishChooserAirProperties(const FProject_JAnimThreadSafeData& Data);
	void PublishChooserLandingProperties(const FProject_JAnimThreadSafeData& Data);
	void PublishChooserCombatProperties(const FProject_JAnimThreadSafeData& Data);
	void ApplyFarChooserOverrides(const FProject_JAnimThreadSafeData& Data);
	bool ShouldEvaluateMotionMatchingThisFrame(float DeltaSeconds);
	bool ShouldForceMotionMatchingContextRefresh(const FProject_JAnimThreadSafeData& Data) const;
	bool ShouldForceMotionMatchingReselect(const FProject_JAnimThreadSafeData& Data) const;
	void CacheEvaluatedMotionMatchingContext(const FProject_JAnimThreadSafeData& Data);
	void RecordMotionMatchingTrace(const FProject_JAnimThreadSafeData& Data, bool bDatabaseChanged, bool bForceReselect);
	/** Linked layers may consume the snapshot, but only the mesh's primary instance may mutate MM/trajectory state. */
	bool IsPrimaryMeshAnimInstance() const;
	FProject_JAnimOptimizationPolicy BuildOptimizationPolicy() const;
	void ResetTrajectoryHistoryOnAccelerationStop(const FProject_JAnimThreadSafeData& Data) const;
	float CalculateAimOffsetAlpha(const FProject_JAnimThreadSafeData& Data) const;
	bool ShouldSkipNativeUpdate(float DeltaSeconds);
	const UProject_JLocomotionProfile* GetLocomotionProfile() const;
	FProject_JAnimationBudgetSettings GetEffectiveAnimationBudgetSettings() const;
	float GetEffectiveGenericMoveInputSpeedThreshold() const;
	float GetEffectiveSprintLocomotionSpeedThreshold() const;
	FProject_JRemoteVisualLocomotionPolicy GetEffectiveRemoteVisualPolicy() const;
	float GetEffectiveHiddenRemoteUpdateInterval() const;
	float GetEffectiveNearMotionMatchingDistance() const;
	float GetEffectiveMidMotionMatchingDistance() const;
	float GetEffectiveMidMotionMatchingUpdateInterval() const;
	float GetEffectiveFarMotionMatchingUpdateInterval() const;
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

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimThreadSafeData ThreadSafeData;

	/** Game-thread NotifyState depth. A depth avoids prematurely closing overlapping blend windows. */
	int32 OneShotEarlyTransitionWindowDepth = 0;

	/** Game-thread cache backing the immutable proxy selection snapshot. */
	TWeakObjectPtr<UChooserTable> CachedStateControllerChooserTable;
	EProject_JStateControllerPresentationState CachedStateControllerPresentationState = EProject_JStateControllerPresentationState::Disabled;
	EProject_JLocomotionRotationMode CachedStateControllerRotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
	EProject_JLocomotionGaitIntent CachedStateControllerGaitIntent = EProject_JLocomotionGaitIntent::Walk;
	EProject_JStateControllerStance CachedStateControllerStance = EProject_JStateControllerStance::Stand;
	EProject_JStateControllerStrafeDirection CachedStateControllerStrafeDirection = EProject_JStateControllerStrafeDirection::Forward;
	EProject_JStateControllerStrafeDirection CachedStateControllerPreviousStrafeDirection = EProject_JStateControllerStrafeDirection::Forward;
	EProject_JStateControllerFoot CachedStateControllerOneShotFoot = EProject_JStateControllerFoot::None;
	bool bCachedStateControllerCombatMode = false;
	bool bCachedStateControllerFallOff = false;
	/** Captured at a Start/Land one-shot entry to detect a local-player mouse turn. */
	bool bHasStateControllerOneShotControlYaw = false;
	float StateControllerOneShotControlYaw = 0.0f;
	bool bHasStateControllerLeftFootContactCurve = false;
	bool bHasStateControllerRightFootContactCurve = false;
	bool bHasStateControllerFootContactCurves = false;
	float CachedStateControllerLeftFootContact = 0.0f;
	float CachedStateControllerRightFootContact = 0.0f;
	EProject_JStateControllerFoot StateControllerFootPhaseHistory = EProject_JStateControllerFoot::None;
	bool bHasStateControllerFootPhaseHistory = false;
	/** Start gait is briefly provisional, then held until the authored Start exits. */
	EProject_JLocomotionGaitIntent StateControllerStartGaitForChooser = EProject_JLocomotionGaitIntent::Run;
	double StateControllerStartGaitStartedAtSeconds = 0.0;
	bool bHasStateControllerStartGaitForChooser = false;
	bool bStateControllerStartGaitCommitted = false;
	EProject_JLocomotionGaitIntent StateControllerStopGaitForChooser = EProject_JLocomotionGaitIntent::Run;
	bool bHasStateControllerStopGaitForChooser = false;
	bool bHasStateControllerFallOffForChooser = false;
	TObjectPtr<UAnimationAsset> CachedStateControllerSelectedAnimation = nullptr;
	FProject_JStateControllerChooserOutput CachedStateControllerSelectedAnimationOutput;
	bool bCachedStateControllerHasSelectedAnimation = false;
	int32 StateControllerChooserSelectionRevision = 0;

	/** Game-thread presentation clock; it never drives CharacterMovement or replication. */
	mutable EProject_JStateControllerPresentationState StateControllerPlaybackHoldState = EProject_JStateControllerPresentationState::Disabled;
	mutable double StateControllerPlaybackHoldStartedAtSeconds = 0.0;

	// --- Chooser Variables (read by Chooser Table rows on Game Thread) ---

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Animation|Motion Matching|Chooser")
	float ChooserGroundSpeed = 0.0f;

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
	float HiddenRemoteUpdateAccumulator = 0.0f;
	float MotionMatchingUpdateAccumulator = 0.0f;

	EProject_JGroundMotionMode LastEvaluatedGroundMotionMode = EProject_JGroundMotionMode::Idle;
	EProject_JLocomotionGaitIntent LastEvaluatedGaitIntent = EProject_JLocomotionGaitIntent::Run;
	EProject_JLocomotionRotationMode LastEvaluatedRotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
	EProject_JLocomotionPhaseFamily LastEvaluatedPhaseFamily = EProject_JLocomotionPhaseFamily::Idle;
	bool bLastEvaluatedStartRequested = false;
	bool bLastEvaluatedStartWasSprinting = false;
	int32 LastEvaluatedMotionMatchingSelectionRevision = 0;
	TArray<FProject_JMotionMatchingTraceEntry> MotionMatchingTrace;
	int32 MaxMotionMatchingTraceEntries = 96;

	bool bHasEvaluatedMotionMatchingContext = false;
};
