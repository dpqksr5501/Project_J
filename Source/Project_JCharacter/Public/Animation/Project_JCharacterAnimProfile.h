// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Project_JCharacterAnimProfile.generated.h"

class UProject_JLocomotionProfile;
class UProject_JCombatAnimProfile;

/** Character-specific alignment after a weapon defines its grip sockets. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JHandGripCalibration
{
	GENERATED_BODY()

	/** Local to the primary weapon grip socket; identity preserves existing IK. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Hand IK")
	FTransform PrimaryHandOffset = FTransform::Identity;

	/** Local to the secondary weapon grip socket; identity preserves existing IK. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Hand IK")
	FTransform SecondaryHandOffset = FTransform::Identity;

	/** Optional component-space joint targets for a follower AnimGraph Two Bone IK node. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Hand IK")
	FVector PrimaryElbowTarget = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Hand IK")
	FVector SecondaryElbowTarget = FVector::ZeroVector;
};

/**
 * Top-level animation profile for a playable character archetype.
 *
 * Owns the archetype's locomotion and combat profiles plus body-specific visual
 * calibration. Weapon socket placement remains on the weapon presentation asset.
 */
UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JCharacterAnimProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Locomotion")
	TObjectPtr<UProject_JLocomotionProfile> LocomotionProfile = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Combat")
	TObjectPtr<UProject_JCombatAnimProfile> CombatAnimProfile = nullptr;

	/** Per-body proportions and palm orientation; weapon assets only define grip sockets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Hand IK")
	FProject_JHandGripCalibration HandGripCalibration;
};
