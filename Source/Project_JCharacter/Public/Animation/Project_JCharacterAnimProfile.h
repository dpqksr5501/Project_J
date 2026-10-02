// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Animation/Project_JHandGripProfile.h"
#include "Project_JCharacterAnimProfile.generated.h"

class UProject_JLocomotionProfile;
class UProject_JCombatAnimProfile;

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

	/** Optional shared body profile. Existing inline calibration remains the fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Hand IK")
	TObjectPtr<UProject_JHandGripProfile> HandGripProfile = nullptr;
};
