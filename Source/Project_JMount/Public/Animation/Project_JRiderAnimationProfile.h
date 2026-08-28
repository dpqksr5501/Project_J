// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Project_JRiderAnimationProfile.generated.h"

/**
 * Shared, immutable presentation policy for the humanoid riding a mount.
 *
 * Concrete mounts reference this small asset directly so mounts with the same
 * rider pose family can share it. Heavy animation blueprint content remains a
 * soft reference and is streamed only on rendering clients.
 */
UCLASS(BlueprintType, Const)
class PROJECT_JMOUNT_API UProject_JRiderAnimationProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Linked AnimBP that implements the master's MountedLocomotion layer.
	 * Several profiles may intentionally reference the same generic rider layer.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rider Animation")
	TSoftClassPtr<UAnimInstance> AnimationLayerClass;

	/**
	 * Data-driven classification consumed by the rider AnimBP. Prefer hierarchical
	 * tags such as Animation.Mount.Pose.Horse over concrete mount class checks.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rider Animation")
	FGameplayTagContainer AnimationTags;

	/** Enables the mount socket targets in the rider's thread-safe animation snapshot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rider Animation|IK")
	bool bUseHandIK = true;

	/** Profile-selected blend duration exposed to the rider AnimBP. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rider Animation", meta = (ClampMin = "0.0"))
	float TransitionBlendTime = 0.2f;
};
