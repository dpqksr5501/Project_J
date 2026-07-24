// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Project_JLocomotionAnimTypes.h"
#include "Project_JMotionMatchingAssetSet.generated.h"

class UChooserTable;
class UPoseSearchDatabase;

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JMotionMatchingGaitDatabaseFamily
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Cycle")
	TObjectPtr<UPoseSearchDatabase> Cycle = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Start Stop")
	TObjectPtr<UPoseSearchDatabase> Start = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Start Stop")
	TObjectPtr<UPoseSearchDatabase> RemoteStart = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Start Stop")
	TObjectPtr<UPoseSearchDatabase> Stop = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Turn Redirect")
	TObjectPtr<UPoseSearchDatabase> TurnRedirect = nullptr;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JMotionMatchingLandDatabaseFamily
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Landing")
	TObjectPtr<UPoseSearchDatabase> Stand = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Landing")
	TObjectPtr<UPoseSearchDatabase> Run = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Landing")
	TObjectPtr<UPoseSearchDatabase> Sprint = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Landing|Heavy")
	TObjectPtr<UPoseSearchDatabase> StandHeavy = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Landing|Heavy")
	TObjectPtr<UPoseSearchDatabase> RunHeavy = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Landing|Heavy")
	TObjectPtr<UPoseSearchDatabase> SprintHeavy = nullptr;
};

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
		EProject_JLocomotionPhaseFamily PhaseFamily,
		bool bUseHeavyLand = false,
		bool bLandWasMoving = false,
		bool bLandWasSprinting = false,
		bool bUseFallOffStart = false,
		bool bUseRemoteStart = false,
		bool bUseGenericFamiliesForNonOrientToMovement = false) const;

	bool ValidateForProjectJLocomotion(const UObject* ValidationContext, TArray<FString>& OutWarnings) const;

	/** Validates the standard family slots required by camera-facing combat locomotion. */
	bool ValidateCombatStrafeForProjectJLocomotion(const UObject* ValidationContext, TArray<FString>& OutWarnings) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
	TObjectPtr<UPoseSearchDatabase> DefaultPoseSearchDatabase = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
	TObjectPtr<UPoseSearchDatabase> IdlePoseSearchDatabase = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Database Families")
	FProject_JMotionMatchingGaitDatabaseFamily RunDatabases;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Database Families")
	FProject_JMotionMatchingGaitDatabaseFamily SprintDatabases;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Database Families")
	TObjectPtr<UPoseSearchDatabase> JumpStartDatabase = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Database Families")
	TObjectPtr<UPoseSearchDatabase> FallOffStartDatabase = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Database Families")
	TObjectPtr<UPoseSearchDatabase> JumpAirDatabase = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Database Families")
	FProject_JMotionMatchingLandDatabaseFamily LandDatabases;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
	TObjectPtr<UChooserTable> MotionMatchingChooserTable = nullptr;

	/** Advanced context overrides. Standard locomotion and combat asset sets use the visible database families above. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Advanced Overrides", AdvancedDisplay, meta = (TitleProperty = "PoseSearchDatabase"))
	TArray<FProject_JMotionMatchingDatabaseEntry> DatabaseEntries;
};
