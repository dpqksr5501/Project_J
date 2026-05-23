// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_FootPlacement.h"
#include "Engine/DataAsset.h"
#include "Project_JLocomotionProfile.generated.h"

class UProject_JMotionMatchingAssetSet;

/**
 * Data-driven locomotion defaults shared by the player character and its native anim instance.
 *
 * Keep this focused on generic biped locomotion policy. Job-specific combat animation can layer
 * on top through montages or separate combat assets without changing the base movement profile.
 */
UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JLocomotionProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UProject_JLocomotionProfile();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Motion Matching")
	TObjectPtr<UProject_JMotionMatchingAssetSet> MotionMatchingAssetSet = nullptr;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Optimization|Anim State", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AnimStateHiddenRemoteUpdateInterval = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion|Optimization|Anim Instance", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AnimInstanceHiddenRemoteUpdateInterval = 0.10f;

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
