// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Project_JMotionMatchingAssetSet.generated.h"

class UChooserTable;
class UPoseSearchDatabase;

/**
 * Groups the motion matching assets used by a humanoid locomotion setup.
 *
 * Characters can reference one asset set instead of assigning default PSD,
 * idle PSD, and Chooser Table separately. Existing direct assignments remain
 * valid fallbacks for migration.
 */
UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JMotionMatchingAssetSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
	TObjectPtr<UPoseSearchDatabase> DefaultPoseSearchDatabase = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
	TObjectPtr<UPoseSearchDatabase> IdlePoseSearchDatabase = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
	TObjectPtr<UChooserTable> MotionMatchingChooserTable = nullptr;
};
