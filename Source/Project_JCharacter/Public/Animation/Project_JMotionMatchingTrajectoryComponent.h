// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CharacterTrajectoryComponent.h"
#include "Project_JMotionMatchingTrajectoryComponent.generated.h"

class ACharacter;

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

	/**
	 * Reconstructs planar velocity between the sample nearest the present and the
	 * positive sample nearest PredictionHorizon. Sampling indices are cached
	 * because the trajectory time layout is stable during ordinary updates.
	 */
	bool TryGetFuturePlanarVelocity(
		float PredictionHorizon,
		const FVector& CurrentPlanarVelocity,
		FVector& OutVelocity,
		float& OutTurnAngleDegrees) const;

	void UpdateTrajectoryState(float DeltaTime);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Trajectory|Smoothing")
	bool bEnableTrajectorySmoothing = true;

	/** Interpolation speed coefficient (alpha = Clamp(DeltaTime * TrajectorySmoothingSpeed, 0, 1)) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Trajectory|Smoothing", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableTrajectorySmoothing"))
	float TrajectorySmoothingSpeed = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Trajectory|Correction")
	bool bEnableSpeedChangeCorrection = true;

private:
	void EnsureTrajectoryBuffers();
	void ApplyTrajectorySmoothing(float DeltaTime);
	void RepairRemoteTrajectoryFacing(const ACharacter& CharacterOwner);
	void ScaleTrajectoryHistory(float ScaleRatio);
	void InvalidateSamplingIndexCache();
	void RefreshSamplingIndexCache(float PredictionHorizon) const;

	UPROPERTY(Transient)
	FTransformTrajectory PreviousFilteredTrajectory;

	UPROPERTY(Transient)
	float LastMaxWalkSpeed = 0.0f;

	mutable int32 CachedPresentSampleIndex = INDEX_NONE;
	mutable int32 CachedFutureSampleIndex = INDEX_NONE;
	mutable int32 CachedTrajectorySampleCount = INDEX_NONE;
	mutable float CachedPredictionHorizon = -1.0f;
};
