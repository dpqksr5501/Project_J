// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Project_JLocomotionAnimTypes.h"
#include "Project_JMotionMatchingAssetSet.generated.h"

class UPoseSearchDatabase;

/**
 * Complete, value-only input to a Motion Matching database family lookup.
 *
 * Keeping this as one context prevents call sites from silently swapping the
 * landing, remote-start, and rotation fallback booleans.  It is deliberately
 * free of Actor, Component, and asset references so a game-thread locomotion
 * snapshot can safely carry the same semantic decision into animation.
 */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JMotionMatchingSelectionContext
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Motion Matching|Context")
	EProject_JLocomotionGaitIntent GaitIntent = EProject_JLocomotionGaitIntent::Run;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Motion Matching|Context")
	EProject_JLocomotionRotationMode RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Motion Matching|Context")
	EProject_JLocomotionPhaseFamily PhaseFamily = EProject_JLocomotionPhaseFamily::Idle;

	/** Allow family slots on a non-OTM asset set when no specific override exists. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Motion Matching|Context")
	bool bUseGenericFamiliesForNonOrientToMovement = false;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JMotionMatchingGaitDatabaseFamily
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Cycle")
	TObjectPtr<UPoseSearchDatabase> Cycle = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching|Turn Redirect")
	TObjectPtr<UPoseSearchDatabase> TurnRedirect = nullptr;
};

/**
 * Groups the motion matching assets used by a humanoid locomotion setup.
 *
	 * Contains only the continuous locomotion PSDs. One-shot animation selection
	 * (start, stop, pivot, air and landing) is owned by the State Controller Chooser.
 */
UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JMotionMatchingAssetSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPoseSearchDatabase* FindDatabaseForContext(const FProject_JMotionMatchingSelectionContext& Context) const;

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

};
