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
	double Timestamp = 0.0;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FQuat Rotation = FQuat::Identity;

	// World-space collision geometry at capture time, including crouch and scale.
	UPROPERTY() FVector CapsuleLocation = FVector::ZeroVector;
	UPROPERTY() FQuat CapsuleRotation = FQuat::Identity;
	UPROPERTY() float CapsuleRadius = 0.0f;
	UPROPERTY() float CapsuleHalfHeight = 0.0f;
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

	/** Call after a teleport/handover; interpolation must not bridge separate locations. */
	void ResetHistory();

	virtual void SetComponentTickEnabled(bool bEnabled) override;

protected:
	virtual void BeginPlay() override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	/** Called by UProject_JCombatSpatialHistorySubsystem in a single contiguous TG_PostPhysics batch pass. */
	void RecordBatchedSample(double Timestamp, const AActor* Owner, const class UCapsuleComponent* Capsule);
	// Maximum amount of time (in seconds) to store in the circular buffer
	UPROPERTY(EditDefaultsOnly, Category = "SSR")
	float MaxRecordTime = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "SSR", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float RecordRateHz = 30.0f;

	// Perform a rollback sweep/check at a specific time in the past
	UFUNCTION(BlueprintCallable, Category = "SSR")
	bool ServerVerifyHit(double ClientTimestamp, const FVector& TraceStart, const FVector& TraceEnd, float TraceRadius = 0.0f);

	/** Variant returning the exact rewound capsule center on confirmed intersection for consistent LOS checks. */
	bool ServerVerifyHitWithRewoundLocation(double ClientTimestamp, const FVector& TraceStart, const FVector& TraceEnd, float TraceRadius, FVector& OutRewoundLocation);

	/** GT-only O(1) retained timestamp observation; false leaves outputs unchanged. */
	bool TryGetHistoryTimeBounds(double& OutOldest, double& OutNewest) const;

private:
	friend class UProject_JCombatSpatialHistorySubsystem;
	void StartHistoryCapture();
	void StopHistoryCapture();
	bool bUsingBatchCapture = false;
	bool bCaptureRequested = true;
	friend class FProjectJRewindHistoryTest;
	friend class FProjectJRewindSchedulingTest;
	friend class FProjectJSpatialDiscontinuityTest;
	// Fixed-capacity circular buffer storing past transforms. It allocates once in
	// BeginPlay and never shifts elements when old records expire.
	TArray<FProject_JPoseHistoryBuffer> PoseHistory;
	int32 PoseHistoryStartIndex = 0;
	int32 PoseHistoryCount = 0;

	// Helper to find the closest poses to interpolate between
	bool GetPosesForTime(double Time, FProject_JPoseHistoryBuffer& OutPose1, FProject_JPoseHistoryBuffer& OutPose2, float& OutAlpha) const;
	const FProject_JPoseHistoryBuffer& GetPoseHistoryRecord(int32 LogicalIndex) const;
	void AppendPoseHistoryRecord(const FProject_JPoseHistoryBuffer& Record);
	void DiscardExpiredPoseHistoryRecords(double CurrentTime);
};
