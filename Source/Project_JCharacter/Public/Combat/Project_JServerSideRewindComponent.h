#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JServerSideRewindComponent.generated.h"

/**
 * Data structure to hold raw capsule position and rotation over time for Server-Side Rewinding (SSR).
 * Pillar 4: Prediction & Lag Compensation.
 */
USTRUCT(BlueprintType)
struct FProject_JPoseHistoryBuffer
{
	GENERATED_BODY()

	UPROPERTY()
	float Timestamp = 0.0f;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FQuat Rotation = FQuat::Identity;
};

/**
 * Component attached to characters to record their transform history on the server,
 * enabling collision rollback when verifying lag-compensated RPCs from clients.
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JServerSideRewindComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JServerSideRewindComponent();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	// Maximum amount of time (in seconds) to store in the circular buffer
	UPROPERTY(EditDefaultsOnly, Category = "SSR")
	float MaxRecordTime = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "SSR", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float RecordRateHz = 30.0f;

	// Perform a rollback sweep/check at a specific time in the past
	UFUNCTION(BlueprintCallable, Category = "SSR")
	bool ServerVerifyHit(float ClientTimestamp, const FVector& TraceStart, const FVector& TraceEnd);

private:
	// The circular buffer storing the past transforms
	TArray<FProject_JPoseHistoryBuffer> PoseHistory;

	float TimeSinceLastRecord = 0.0f;
	
	// Helper to find the closest poses to interpolate between
	bool GetPosesForTime(float Time, FProject_JPoseHistoryBuffer& OutPose1, FProject_JPoseHistoryBuffer& OutPose2, float& OutAlpha) const;
};
