#pragma once

#include "CoreMinimal.h"
#include "Animation/TrajectoryTypes.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JTurnRequestSample.h"
#include "Animation/Project_JAnimationFlowTrace.h"
#include "Animation/Project_JAnimationLocomotionMode.h"
#include "Animation/Project_JStrafeUpperBodyFacing.h"
#include "Animation/Project_JStrafeLowerBodyFacing.h"
#include "Combat/Project_JCombatTypes.h"
#include "Project_JAnimationSnapshot.generated.h"

class UAnimationAsset;
class UPoseSearchDatabase;

// GT publishes a completed snapshot; traversal consumes it at UE's existing
// synchronization boundary. UObject references remain reflected/GC-retained.
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

	/** True if additive lean should be enabled (e.g. while actively sprinting or allowed in-air). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bShouldApplyLeanAdditive = false;

	/** True if sprint additive lean is suppressed due to cornering/curvature (Sprint Diamond). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bSprintCurvatureLeanSuppressed = false;

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

	/** Monotonic game-thread revisions used to diagnose stale or reset trajectory snapshots. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	int32 TrajectoryGenerationRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	int32 TrajectoryResetRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float TrajectoryAgeSeconds = -1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bTrajectoryGenerationEligible = false;

	/** Source snapshot consumed by this search, before any post-selection reset. */
	uint64 TrajectoryGenerationFrame = TNumericLimits<uint64>::Max();
	uint64 SnapshotFrame = 0;
	int32 TrajectoryResetRevisionAfterConsumption = 0;
	uint8 TrajectoryResetReason = 0;
	uint8 TrajectoryResetReasonAfterConsumption = 0;
	bool bTrajectoryPredictionUsable = false;

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

	/** Revision emitted when a remote Start must release its authored playback hold. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	int32 StartResponsiveExitRevision = 0;
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

	/** Monotonic physical landing boundary; changes once for every new landing. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	int32 PresentationRevision = 0;

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsPlayingCombatOutro = false;

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

	// Copied from the profile on the game thread, never resolved through the pawn in AnimGraph.
	UPROPERTY()
	FFootPlacementPlantSettings PlantSettings;

	UPROPERTY()
	FFootPlacementInterpolationSettings InterpolationSettings;


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

	/** Absolute world-space facing yaw. Keep this distinct from DesiredFacingDeltaYaw for Steering. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	float DesiredFacingYaw = 0.0f;

	FVector RequestedMoveWorldDirection = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsMoving = false;

	/** MM presentation movement state; use only for MM diagnostics/policy, not gameplay. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsMotionMatchingMoving = false;

	/** Current ground intent for returning from a consumed Land, independent of its physical landing lifetime. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bHasGroundMovementIntent = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsStarting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsPivoting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	int32 MoveIntentRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	int32 PivotRequestRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FVector PivotPreviousMovementDirection = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FVector PivotMoveIntentDirection = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bShouldTurnInPlace = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	uint8 TurnInPlaceDirectionBucket = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	int32 TurnInPlaceSequence = 0;

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

	/** Representative diagnostic result; source identity does not prove final blended pose contribution. */
	int32 ProducerNodeIndex = INDEX_NONE;
	int32 ResultCandidateCount = 0;
	uint64 CaptureFrame = 0;
	float CachedNodeWeight = 0.0f;
	uint64 ResultLastChangedFrame = 0;
	bool bResultChangedSincePreviousCapture = false;

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

	/** GT budget floor for worker PoseSearch; zero preserves the node/phase policy. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Motion Matching")
	float MinimumSearchInterval = 0.0f;

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

	/** One-frame command for the State Machine Blend Stack On Update binding. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|One Shot")
	bool bForceBlendNextUpdate = false;

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
	bool bUseSettledCycle = false;
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

	/** Profile-defined pose family/features; copied on the game thread. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	FGameplayTagContainer AnimationTags;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe|Mount")
	float TransitionBlendTime = 0.2f;

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

	UPROPERTY()
	bool bIsLocallyControlled = false;


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

	FProject_JStrafeUpperBodyFacingInput StrafeUpperBodyFacing;
	FProject_JStrafeLowerBodyFacingInput StrafeLowerBodyFacing;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JAnimMountThreadSafeData Mount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	EProject_JAnimationLocomotionMode LocomotionMode = EProject_JAnimationLocomotionMode::OnFoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	FProject_JMotionMatchingSearchPolicy MotionMatchingSearchPolicy;

	// Value-only visual rotation contract, produced on GT after one-shot ownership.
	FProject_JTurnRequestSample TurnRequest;
	float GeneralTurnRecentHeading = 0;
	float GeneralTurnWindowElapsed = 0;
	float GeneralTurnMoveYawRate = 0, GeneralTurnFacingYawRate = 0;
	float GeneralTurnPathError = 0, GeneralTurnFacingError = 0;
	bool bGeneralTurnDynamicCycle = false;
	bool bGeneralTurnCycleHandoff = false;
	FName GeneralTurnDemand = NAME_None;
	FName GeneralTurnReason = NAME_None;
	FName BackwardRefaceReason = NAME_None;
	float BackwardRefaceFacingError = 0, BackwardRefacePathError = 0;
	bool bLocomotionSteeringEnabled = false;
	EProject_JLocomotionSteeringGate LocomotionSteeringGate = EProject_JLocomotionSteeringGate::Unknown;
	float LocomotionSteeringLookAhead = 0.5f;
	FQuat LocomotionSteeringTarget = FQuat::Identity;
	float LocomotionSteeringProceduralTime = 0.4f;
	float LocomotionSteeringAnimatedTime = 2.0f;
	float LocomotionSteeringMaxYawError = 45.0f;

};
