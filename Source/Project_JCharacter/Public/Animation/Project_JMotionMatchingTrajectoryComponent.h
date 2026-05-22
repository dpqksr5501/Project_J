// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CharacterTrajectoryComponent.h"
#include "Project_JMotionMatchingTrajectoryComponent.generated.h"

UCLASS(BlueprintType, Blueprintable, ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JMotionMatchingTrajectoryComponent : public UCharacterTrajectoryComponent
{
	GENERATED_BODY()

public:
	UProject_JMotionMatchingTrajectoryComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void InitializeComponent() override;

	UFUNCTION(BlueprintCallable, Category = "Motion Matching|Trajectory")
	void ResetTrajectoryHistory();

	UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
	const FTransformTrajectory& GetTrajectory() const { return Trajectory; }

	UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
	const FTrajectorySamplingData& GetSamplingData() const { return SamplingData; }

	UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
	const FCharacterTrajectoryData& GetCharacterTrajectoryData() const { return CharacterTrajectoryData; }

private:
	void EnsureTrajectoryBuffers();
};
