// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Project_JLocomotionAnimTypes.h"
#include "Project_JMotionMatchingAssetSet.generated.h"

class UChooserTable;
class UPoseSearchDatabase;

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JMotionMatchingDatabaseEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
	TObjectPtr<UPoseSearchDatabase> PoseSearchDatabase = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Filters")
	bool bMatchGaitIntent = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Filters", meta = (EditCondition = "bMatchGaitIntent"))
	EProject_JLocomotionGaitIntent GaitIntent = EProject_JLocomotionGaitIntent::Run;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Filters")
	bool bMatchRotationMode = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Filters", meta = (EditCondition = "bMatchRotationMode"))
	EProject_JLocomotionRotationMode RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Filters")
	bool bMatchPhaseFamily = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Filters", meta = (EditCondition = "bMatchPhaseFamily"))
	EProject_JLocomotionPhaseFamily PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;

	bool Matches(
		EProject_JLocomotionGaitIntent InGaitIntent,
		EProject_JLocomotionRotationMode InRotationMode,
		EProject_JLocomotionPhaseFamily InPhaseFamily) const;

	int32 GetSpecificity() const;
};

/**
 * Groups the motion matching assets used by a humanoid locomotion setup.
 *
 * Characters can reference one asset set instead of assigning default PSD,
 * idle PSD, and Chooser Table separately. Existing direct assignments remain
 * valid fallbacks for migration.
 */
UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JMotionMatchingAssetSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPoseSearchDatabase* FindDatabaseForContext(
		EProject_JLocomotionGaitIntent GaitIntent,
		EProject_JLocomotionRotationMode RotationMode,
		EProject_JLocomotionPhaseFamily PhaseFamily) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
	TObjectPtr<UPoseSearchDatabase> DefaultPoseSearchDatabase = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
	TObjectPtr<UPoseSearchDatabase> IdlePoseSearchDatabase = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
	TObjectPtr<UChooserTable> MotionMatchingChooserTable = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Database Families", meta = (TitleProperty = "PoseSearchDatabase"))
	TArray<FProject_JMotionMatchingDatabaseEntry> DatabaseEntries;
};
