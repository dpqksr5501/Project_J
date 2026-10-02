// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/Project_JAnimationBudgetTypes.h"
#include "Animation/Project_JHandGripProfile.h"
#include "Project_JRetargetAnimInstance.generated.h"

class USceneComponent;
class ACharacter;
class UProject_JWeaponPresentationComponent;

/**
 * Lightweight native AnimInstance optimized for large-scale MMORPG runtime retargeting.
 *
 * Sits on visual mesh components driven by 'Retarget Pose From Mesh'.
 * Computes deterministic weapon hand-grip IK targets in Component Space with
 * smooth combat-state blending and zero network replication overhead.
 */
UCLASS(Blueprintable, BlueprintType)
class PROJECT_JCHARACTER_API UProject_JRetargetAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Optional body profile on the visual ABP, shared across jobs using this body. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Project_J|IK|Config")
	TObjectPtr<UProject_JHandGripProfile> HandGripProfile = nullptr;

	/** Game-thread only: visual body override, character shared profile, then legacy inline calibration. */
	FProject_JHandGripCalibration GetHandGripCalibration() const;

	UProject_JRetargetAnimInstance();

	virtual void BeginDestroy() override;
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativePostEvaluateAnimation() override;

	uint64 GetGripTargetSnapshotFrame() const { return GripTargetSnapshotFrame; }
	uint64 GetGripPoseEvaluationFrame() const { return GripPoseEvaluationFrame; }

	/**
	 * Explicitly registers or clears the weapon visual component to track.
	 * Event-driven: called on equipment equip/unequip, avoiding per-frame component searches.
	 */
	UFUNCTION(BlueprintCallable, Category = "Project_J|IK|Weapon")
	void UpdateWeaponTarget(USceneComponent* InWeaponComponent, FName InSocketName = NAME_None);

	/** Force updates combat mode for immediate IK blending response. */
	UFUNCTION(BlueprintCallable, Category = "Project_J|IK|State")
	void SetCombatMode(bool bInCombatMode);

public:
	/** Target location for Right Hand Two-Bone IK effector in Component Space. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	FVector RightGripLocation = FVector::ZeroVector;

	/** Target location for Left Hand Two-Bone IK effector in Component Space (two-handed grip). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	FVector LeftGripLocation = FVector::ZeroVector;

	/** Calibrated component-space WRIST orientations. Palm conversion has already been applied. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	FRotator RightGripRotation = FRotator::ZeroRotator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	FRotator LeftGripRotation = FRotator::ZeroRotator;

	/** Solver-independent wrist targets. Existing location/rotation pins remain compatible. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	FTransform RightWristTarget = FTransform::Identity;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	FTransform LeftWristTarget = FTransform::Identity;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	bool bRightContactTargetValid = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	bool bLeftContactTargetValid = false;

	/** Same-frame target for a weapon driven by the visible primary hand. The final rig evaluates it in PrimaryHandBoneName space. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	FTransform LeftGripInPrimaryHandSpace = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	FName PrimaryHandBoneName = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	bool bUsePrimaryHandSpaceGrip = false;

	/** Optional component-space joint targets. AnimGraphs may opt into these when authored for the follower skeleton. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	FVector RightElbowTarget = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	FVector LeftElbowTarget = FVector::ZeroVector;

	/**
	 * Dynamic blending weight for Right Hand IK.
	 * Automatically interpolates between 1.0 (sheathed on back) and target combat grip alpha.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	float RightGripAlpha = 1.0f;

	/** Dynamic blending weight for Left Hand IK (secondary grip). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	float LeftGripAlpha = 0.0f;

	/** Alias for RightGripAlpha for backward compatibility with existing Two-Bone IK blueprints. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	float GripIKAlpha = 1.0f;

	/** Primary socket name on the weapon to align the right hand with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project_J|IK|Config")
	FName PrimaryGripSocketName = TEXT("WeaponGrip_R");

	/** Secondary socket name on the weapon to align the left hand with (two-handed weapons). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project_J|IK|Config")
	FName SecondaryGripSocketName = TEXT("WeaponGrip_L");

	/** Optional curve name on Leader playing animation to dynamically drive Left Hand IK alpha. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project_J|IK|Config")
	FName LeftHandIKCurveName = TEXT("LeftHandIK");

	/** Optional curve name on Leader playing animation to dynamically drive Right Hand IK alpha. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project_J|IK|Config")
	FName RightHandIKCurveName = TEXT("RightHandIK");

	/** Speed of alpha interpolation during draw/sheathe transitions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project_J|IK|Config", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float GripInterpSpeed = 12.0f;

	/** True if the character is currently in drawn/combat stance. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|State")
	bool bIsCombatMode = false;

	/** Whether combat-mode drawn grip IK is enabled (e.g. aligning hands to weapon handle in combat). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project_J|IK|Config")
	bool bEnableCombatGripIK = true;

	/** Fallback: automatically queries weapon component on owner actor if not explicitly set (default false for production). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project_J|IK|Config")
	bool bAutoDetectWeaponIfNull = false;

	/** Current animation quality tier inherited from the Leader character. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Quality")
	EProject_JAnimBudgetTier CurrentQualityTier = EProject_JAnimBudgetTier::Local;

	/** Whether Follower Retarget evaluation is enabled under the current quality tier. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Quality")
	bool bEnableFollowerRetarget = true;

	/** Whether Hand IK is enabled under the current quality tier. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Quality")
	bool bTierAllowsHandIK = true;

	/** Whether Foot IK is enabled under the current quality tier. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Quality")
	bool bTierAllowsFootIK = true;

	/** Whether Retarget IK is enabled under the current quality tier (can drive Retarget Pose From Mesh node). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Quality")
	bool bTierAllowsRetargetIK = true;

	/** IK LOD threshold for Retarget Pose From Mesh node (LODThresholdForIK). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Quality")
	int32 RetargetIKLODThreshold = 0;

	/** Max LOD threshold for Retarget Pose From Mesh node (LODThreshold). -1 runs at all LODs. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Quality")
	int32 RetargetLODThreshold = -1;

protected:
	/** Weak reference to the currently tracked weapon visual component. */
	UPROPERTY(Transient)
	TWeakObjectPtr<USceneComponent> CachedWeaponComponent = nullptr;

	/** Weak reference to weapon presentation component on owner if present. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UProject_JWeaponPresentationComponent> CachedPresentationComp = nullptr;

	/** Cached game-thread snapshot passed to worker thread evaluation. */
	FTransform SnapshotRightGripWorldTransform = FTransform::Identity;
	FTransform SnapshotLeftGripWorldTransform = FTransform::Identity;
	FTransform SnapshotOwningCompWorldTransform = FTransform::Identity;
	FTransform SnapshotLeftGripInPrimaryHandSpace = FTransform::Identity;
	FName SnapshotPrimaryHandBoneName = NAME_None;
	bool bHasPrimaryHandSpaceGripSnapshot = false;
	bool bHasValidRightSnapshot = false;
	/** A hand-driven weapon cannot be an IK target for that same hand, even for one fading frame. */
	bool bPrimaryIKSuppressedSnapshot = false;
	bool bContactRecoverySnapshot = false;
	uint64 GripTargetSnapshotFrame = MAX_uint64;
	uint64 GripPoseEvaluationFrame = MAX_uint64;
	bool bHasValidLeftSnapshot = false;
	float TargetRightAlphaSnapshot = 1.0f;
	float TargetLeftAlphaSnapshot = 0.0f;
	FVector SnapshotRightElbowTarget = FVector::ZeroVector;
	FVector SnapshotLeftElbowTarget = FVector::ZeroVector;
};
