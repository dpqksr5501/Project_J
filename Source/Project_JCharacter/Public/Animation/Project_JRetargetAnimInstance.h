// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Project_JRetargetAnimInstance.generated.h"

class USceneComponent;
class ACharacter;

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
	UProject_JRetargetAnimInstance();

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;

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
	/**
	 * Target location for Two-Bone IK effector in Component Space.
	 * Connect directly to Two-Bone IK 'Effector Location' (Space: Component Space).
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	FVector RightGripLocation = FVector::ZeroVector;

	/**
	 * Dynamic blending weight for Hand IK.
	 * Automatically interpolates between 1.0 (sheathed on back) and 0.0 (drawn/combat).
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|Grip")
	float GripIKAlpha = 1.0f;

	/** Socket name on the weapon to align the hand with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project_J|IK|Config")
	FName PrimaryGripSocketName = TEXT("WeaponGrip_R");

	/** Speed of alpha interpolation during draw/sheathe transitions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project_J|IK|Config", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float GripInterpSpeed = 12.0f;

	/** True if the character is currently in drawn/combat stance. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Project_J|IK|State")
	bool bIsCombatMode = false;

	/** Fallback: automatically queries weapon component on owner actor if not explicitly set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Project_J|IK|Config")
	bool bAutoDetectWeaponIfNull = true;

protected:
	/** Weak reference to the currently tracked weapon visual component. */
	UPROPERTY(Transient)
	TWeakObjectPtr<USceneComponent> CachedWeaponComponent = nullptr;

	/** Cached game-thread snapshot passed to worker thread evaluation. */
	FTransform SnapshotWeaponSocketWorldTransform = FTransform::Identity;
	FTransform SnapshotOwningCompWorldTransform = FTransform::Identity;
	bool bHasValidSocketSnapshot = false;
	float TargetAlphaSnapshot = 1.0f;
};
