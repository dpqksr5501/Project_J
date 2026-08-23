// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CharacterTrajectoryComponent.h"
#include "Project_JMotionMatchingTrajectoryComponent.generated.h"

class ACharacter;

UENUM(BlueprintType)
enum class EProject_JTrajectoryResetReason : uint8
{
	Initialization,
	Manual,
	RotationModeChanged,
	AccelerationStopped,
	PresentationWake
};

UCLASS(BlueprintType, Blueprintable, ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JMotionMatchingTrajectoryComponent : public UCharacterTrajectoryComponent
{
	GENERATED_BODY()

public:
	UProject_JMotionMatchingTrajectoryComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void InitializeComponent() override;
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category = "Motion Matching|Trajectory")
	void ResetTrajectoryHistory();

	UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
	const FTransformTrajectory& GetTrajectory() const { return Trajectory; }

	UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
	const FTrajectorySamplingData& GetSamplingData() const { return SamplingData; }

	UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
	const FCharacterTrajectoryData& GetCharacterTrajectoryData() const { return CharacterTrajectoryData; }

	UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
	int32 GetGenerationRevision() const { return GenerationRevision; }

	UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
	int32 GetResetRevision() const { return ResetRevision; }

	UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
	EProject_JTrajectoryResetReason GetLastResetReason() const { return LastResetReason; }

	UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
	bool IsTrajectoryGenerationEligible() const { return bWasTrajectoryGenerationEligible; }

	/** Age of the last generated snapshot, or -1 when this component has never generated one. */
	UFUNCTION(BlueprintPure, Category = "Motion Matching|Trajectory")
	float GetTrajectoryAgeSeconds() const;

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
	void ResetTrajectoryHistoryWithReason(EProject_JTrajectoryResetReason Reason);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Trajectory|Smoothing")
	bool bEnableTrajectorySmoothing = true;

	/** Interpolation speed coefficient (alpha = Clamp(DeltaTime * TrajectorySmoothingSpeed, 0, 1)) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Trajectory|Smoothing", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableTrajectorySmoothing"))
	float TrajectorySmoothingSpeed = 15.0f;

	/** Non-local trajectories are only generated while their mesh is relevant to presentation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion Matching|Trajectory|Budget", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "s"))
	float RemoteRecentlyRenderedTolerance = 0.25f;

private:
	void EnsureTrajectoryBuffers();
	bool ShouldGenerateTrajectory(const ACharacter& CharacterOwner) const;
	void GenerateTrajectory(ACharacter& CharacterOwner, float DeltaTime);
	void PostProcessTrajectory(ACharacter& CharacterOwner, float DeltaTime);
	void ApplyTrajectorySmoothing(float DeltaTime);
	void RepairRemoteTrajectoryFacing(const ACharacter& CharacterOwner);
	void InvalidateSamplingIndexCache();
	void RefreshSamplingIndexCache(float PredictionHorizon) const;

	UPROPERTY(Transient)
	FTransformTrajectory PreviousFilteredTrajectory;

	uint64 LastGenerationFrameCounter = TNumericLimits<uint64>::Max();
	uint64 LastPostProcessFrameCounter = TNumericLimits<uint64>::Max();
	bool bWasTrajectoryGenerationEligible = false;
	bool bHasGeneratedTrajectory = false;
	int32 GenerationRevision = 0;
	int32 ResetRevision = 0;
	double LastGeneratedWorldTimeSeconds = -1.0;
	EProject_JTrajectoryResetReason LastResetReason = EProject_JTrajectoryResetReason::Initialization;

	mutable int32 CachedPresentSampleIndex = INDEX_NONE;
	mutable int32 CachedFutureSampleIndex = INDEX_NONE;
	mutable int32 CachedTrajectorySampleCount = INDEX_NONE;
	mutable float CachedPredictionHorizon = -1.0f;
};
