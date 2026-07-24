// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/TrajectoryTypes.h"
#include "Animation/Project_JAnimationBudgetTypes.h"
#include "Animation/Project_JCharacterAnimInstanceBase.h"
#include "Animation/Project_JAnimationLocomotionMode.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "BoneControllers/AnimNode_FootPlacement.h"
#include "Combat/Project_JCombatTypes.h"
#include "Project_JLocomotionAnimTypes.h"
#include "Project_JCharacterAnimInstance.generated.h"

class ACharacter;
class APawn;
class AProject_JPlayerCharacter;
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsStarting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bIsPivoting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bShouldTurnInPlace = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation|ThreadSafe")
	bool bShouldSpinTransition = false;
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
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

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
	EProject_JLocomotionGaitIntent GetThreadSafeGaitIntent() const;

	UFUNCTION(BlueprintPure, Category = "Animation|ThreadSafe", meta = (BlueprintThreadSafe))
	EProject_JLocomotionRotationMode GetThreadSafeRotationMode() const;

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

	UFUNCTION(BlueprintPure, Category = "Animation|Debug")
	FString GetAnimationDebugSummary() const;

protected:
	FProject_JAnimThreadSafeData BuildThreadSafeData(float DeltaSeconds) const;
	void FillMovementThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void FillLocomotionStateThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void ApplyGenericMovementFallback(FProject_JAnimThreadSafeData& Data) const;
	bool FillPlayerThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void FillMountThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void FinalizeThreadSafeData(FProject_JAnimThreadSafeData& Data, bool bHasAimData) const;
	void FillProceduralIKThreadSafeData(FProject_JAnimThreadSafeData& Data) const;
	void PublishThreadSafeDataToProxy(const FProject_JAnimThreadSafeData& Data);
	UPoseSearchDatabase* EvaluatePoseSearchDatabaseOnGameThread(const FProject_JAnimThreadSafeData& Data);
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
	EProject_JLocomotionPhaseFamily ChooserPhaseFamily = EProject_JLocomotionPhaseFamily::Idle;

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
	bool bHasEvaluatedMotionMatchingContext = false;
};
