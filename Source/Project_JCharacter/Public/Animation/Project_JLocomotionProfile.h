// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/Project_JAnimationBudgetTypes.h"
#include "BoneControllers/AnimNode_FootPlacement.h"
#include "Engine/DataAsset.h"
#include "Project_JLocomotionAnimTypes.h"
#include "Project_JLocomotionProfile.generated.h"

class UProject_JMotionMatchingAssetSet;

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JMotionMatchingSearchPolicy
{
	GENERATED_BODY()

	/**
	 * Airborne databases normally contain short one-shot or looping clips where repeatedly
	 * selecting another pose from the same asset creates unnecessary BlendStack entries.
	 * Database changes still trigger a search when these options are disabled.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Motion Matching|Search")
	bool bSearchJumpStartEveryUpdate = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Motion Matching|Search")
	bool bSearchFallOffEveryUpdate = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Motion Matching|Search")
	bool bSearchAirborneLoopEveryUpdate = false;

	/**
	 * Landing PSDs are one-shot recovery clips. Keep the initial result until the
	 * locomotion component leaves Landing, otherwise Pose Search can restart the
	 * same landing clip while its database remains selected.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Motion Matching|Search")
	bool bSearchLandingEveryUpdate = false;

	/** Stop PSDs are one-shot deceleration clips; retain the initial result until Idle or a new movement phase takes over. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Motion Matching|Search")
	bool bSearchStopEveryUpdate = false;

	/**
	 * Duration for which a simulated proxy temporarily bypasses skeletal mesh URO after
	 * receiving a confirmed jump. Keep this long enough for the authored Motion Matching
	 * transition to begin, but short enough to preserve the normal MMO animation budget.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Motion Matching|Network", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float RemoteJumpUrgentAnimationUpdateDuration = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Motion Matching|Search", meta = (ClampMin = "0.0", Units = "s"))
	float SuppressedSearchThrottleTime = 3600.0f;

	bool ShouldSearchEveryUpdate(EProject_JLocomotionPhaseFamily PhaseFamily, bool bIsFallOffStart) const;
	float ResolveSearchThrottleTime(
		EProject_JLocomotionPhaseFamily PhaseFamily,
		bool bIsFallOffStart,
		float DefaultSearchThrottleTime,
		bool bDatabaseChanged) const;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JRemoteVisualLocomotionPolicy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Remote Visual")
	bool bUseForwardOnlyRemoteStart = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Remote Visual")
	bool bDisableStartStopChooserBeyondFarDistance = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Remote Visual", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RemoteStartTurnExitAngle = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Remote Visual", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RemoteStopStartSuppressDuration = 0.20f;
};

/**
 * Data-driven thresholds for the C++ ground locomotion state machine.  These
 * values intentionally live beside the PSD family rather than in an AnimBP,
 * so a new stance/profile can tune transitions without changing gameplay code.
 */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JLocomotionTransitionPolicy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Idle", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float IdleSpeedThreshold = 30.0f;

	/** GASP-style movement analysis uses a smaller non-zero threshold than Idle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Kinematic Analysis", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MovingSpeedThreshold = 10.0f;

	/** Minimum predicted speed gain before an input may be treated as a locomotion Start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Kinematic Analysis", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StartSpeedGainThreshold = 25.0f;

	/** Short prediction horizon used only for animation-state analysis, never for authoritative movement. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Kinematic Analysis", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float MovementPredictionTime = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Start", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float StartMinDuration = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Start", meta = (ClampMin = "0.05", UIMin = "0.05", Units = "s"))
	float StartMaxDuration = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Stop", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StopIntentSpeedThreshold = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Stop", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float StopMinDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Stop", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StopExitSpeedThreshold = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Stop", meta = (ClampMin = "0.05", UIMin = "0.05", Units = "s"))
	float StopFallbackDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SharpTurnAngleThreshold = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SharpTurnMinSpeed = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PivotAngleThreshold = 110.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float TurnRedirectAngleThreshold = 45.0f;

	/** Minimum ground speed for a moving TurnRedirect query. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float TurnRedirectMinSpeed = 180.0f;

	/** Minimum ground speed for a high-commitment Pivot query. Keep above TurnRedirectMinSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Turn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PivotMinSpeed = 350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Turn", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float TurnRedirectMinHoldTime = 0.18f;

	/** Coalesces same-database local turn/pivot re-searches during rapid direction changes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Turn", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float TurnRedirectReselectCooldown = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition|Sprint", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float SprintStopMemoryDuration = 0.25f;
};

/**
 * Runtime CMC policy equivalent to GASP's UpdateMovement_PreCMC helpers.
 * Directional strafe scaling is opt-in so existing Project_J combat movement
 * keeps its current feel until its authored PSD speeds have been validated.
 */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JLocomotionMovementPolicy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Movement|CMC", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RunMaxAcceleration = 2048.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Movement|CMC", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SprintMaxAcceleration = 2048.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Movement|CMC", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RunBrakingDeceleration = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Movement|CMC", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SprintBrakingDeceleration = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Movement|CMC", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RunGroundFriction = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Movement|CMC", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SprintGroundFriction = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Movement|Strafe")
	bool bEnableStrafeDirectionalSpeedScaling = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Movement|Strafe", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StrafeForwardSpeedMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Movement|Strafe", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StrafeSideSpeedMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Movement|Strafe", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float StrafeBackwardSpeedMultiplier = 1.0f;
};

/**
 * Presentation-only values calculated from the authoritative locomotion snapshot.
 * They never alter CharacterMovement and are deliberately separated from the CMC
 * policy so non-combat OTM and combat Strafe can be tuned independently.
 */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JLocomotionPresentationPolicy
{
	GENERATED_BODY()

	/** Enables the thread-safe Relative Acceleration -> Lean snapshot for AnimGraph consumers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Presentation|Lean")
	bool bEnableLean = true;

	/** Scale applied to the OTM (non-combat) lean snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Presentation|Lean", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float OrientToMovementLeanMultiplier = 1.0f;

	/** Scale applied only to camera-facing combat Strafe. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Presentation|Lean", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CombatStrafeLeanMultiplier = 1.0f;

	/** Maximum absolute value of either lean axis exposed to the AnimGraph. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Presentation|Lean", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LeanAxisClamp = 1.0f;
};

/**
 * Data-driven locomotion defaults shared by the player character and its native anim instance.
 *
 * Keep this focused on generic biped locomotion policy. Job-specific combat animation can layer
 * on top through montages or separate combat assets without changing the base movement profile.
 */
UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JLocomotionProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UProject_JLocomotionProfile();

	FProject_JAnimationBudgetSettings GetResolvedAnimationBudgetSettings() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Motion Matching")
	TObjectPtr<UProject_JMotionMatchingAssetSet> MotionMatchingAssetSet = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Motion Matching")
	FProject_JMotionMatchingSearchPolicy MotionMatchingSearchPolicy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Movement", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float WalkSpeed = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Movement", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SprintSpeed = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Movement", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float WalkRotationRateYaw = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Movement", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SprintRotationRateYaw = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Animation", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float GenericMoveInputSpeedThreshold = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Animation", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SprintLocomotionSpeedThreshold = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Transition")
	FProject_JLocomotionTransitionPolicy TransitionPolicy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Movement")
	FProject_JLocomotionMovementPolicy MovementPolicy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Presentation")
	FProject_JLocomotionPresentationPolicy PresentationPolicy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Remote Visual")
	FProject_JRemoteVisualLocomotionPolicy RemoteVisualPolicy;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Optimization|Anim State", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AnimStateHiddenRemoteUpdateInterval = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Optimization|Anim Instance", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AnimInstanceHiddenRemoteUpdateInterval = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Optimization")
	FProject_JAnimationBudgetSettings AnimationBudget;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Optimization|Motion Matching", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float NearMotionMatchingDistance = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Optimization|Motion Matching", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MidMotionMatchingDistance = 6000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Optimization|Motion Matching", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FarMotionMatchingDistance = 12000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Optimization|Motion Matching", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MidMotionMatchingUpdateInterval = 0.033f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Optimization|Motion Matching", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FarMotionMatchingUpdateInterval = 0.083f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Optimization|Motion Matching")
	bool bDisableMotionMatchingBeyondFarDistance = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Foot Placement|Plant")
	FFootPlacementPlantSettings FootPlacementPlantSettingsDefault;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Foot Placement|Plant")
	FFootPlacementPlantSettings FootPlacementPlantSettingsStops;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Foot Placement|Interpolation")
	FFootPlacementInterpolationSettings FootPlacementInterpolationSettingsDefault;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Foot Placement|Interpolation")
	FFootPlacementInterpolationSettings FootPlacementInterpolationSettingsStops;
};
