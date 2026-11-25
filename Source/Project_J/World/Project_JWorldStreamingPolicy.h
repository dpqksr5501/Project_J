#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Project_JWorldStreamingPolicy.generated.h"

/**
 * Velocity-driven 2-stage streaming tier for 8x8km high-speed travel (BP02).
 * Stage 1 (Loaded) warms disk/Pak/PSO ahead of Stage 2 (Activated) actor spawn.
 */
USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JStreamingVelocityProfile
{
	GENERATED_BODY()

	/** Minimum 2D speed (cm/s) required to activate 2-stage high-speed prediction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming", meta = (ClampMin = "0.0"))
	float HighSpeedThresholdCmPerSec = 600.0f;

	/** Speed (cm/s) at which full flying-mount prediction parameters are reached. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming", meta = (ClampMin = "600.0"))
	float FlyingSpeedCmPerSec = 1800.0f;

	/** Stage 1 (Loaded) look-ahead seconds at ground gallop vs full flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Stage1Loaded", meta = (ClampMin = "1.0", ClampMax = "15.0"))
	float LoadedLeadSecondsMin = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Stage1Loaded", meta = (ClampMin = "1.0", ClampMax = "15.0"))
	float LoadedLeadSecondsMax = 7.0f;

	/** Stage 1 (Loaded) maximum distance (cm): 900m ground -> 1800m flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Stage1Loaded", meta = (ClampMin = "10000.0", ClampMax = "250000.0"))
	float LoadedMaxDistanceCm = 180000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Stage1Loaded", meta = (ClampMin = "5000.0", ClampMax = "150000.0"))
	float LoadedShapeRadiusCm = 45000.0f;

	/** Stage 2 (Activated) look-ahead seconds and cap (cm): up to 1000m in full flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Stage2Activated", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float ActivatedLeadSecondsMin = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Stage2Activated", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float ActivatedLeadSecondsMax = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Stage2Activated", meta = (ClampMin = "10000.0", ClampMax = "150000.0"))
	float ActivatedMaxDistanceCm = 100000.0f;

	/** Yaw rate (deg/s) above which forward prediction fans out +/-45 deg to prevent turn pop-in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|TurnWidening", meta = (ClampMin = "10.0"))
	float TurnWideningYawRateDegPerSec = 60.0f;

	/** Trailing hysteresis buffer (cm) and hold duration (s) to stop cell boundary thrashing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Hysteresis", meta = (ClampMin = "0.0"))
	float TrailingBufferCm = 20000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Hysteresis", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float UnloadHysteresisSeconds = 6.0f;

	/** Altitude above ground (cm) at which Grid_Detail loading is suppressed during flight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Altitude", meta = (ClampMin = "1000.0"))
	float SuppressDetailAltitudeCm = 15000.0f;

	/** Minimum soft speed clamp scale when forward cells are unready on slow storage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Streaming|Safety", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float UnreadyForwardSpeedClampMin = 0.35f;
};

/**
 * 3-Tier World Partition & Foliage Impostor HLOD authoring specification (BP02 / BP06).
 * Validated by editor cross-asset validators to prevent 91GB Embree OOM and VSM cache thrashing.
 */
USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JFoliageHLODPolicy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage|HLOD")
	FName RequiredFoliageHLODLayerName = TEXT("HLODLayer_Foliage_Impostor");

	/** Mandatory WPO wind cut distance (cm) so mid/far foliage stays in VSM static cache. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage|VSM", meta = (ClampMin = "1000.0", ClampMax = "15000.0"))
	float MaxAllowedWPODisableDistanceCm = 10000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage|Impostor", meta = (ClampMin = "10000.0"))
	float ImpostorTransitionDistanceCm = 35000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage|Impostor", meta = (ClampMin = "50000.0"))
	float ImpostorMaxDrawDistanceCm = 220000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage|Grid")
	FName MacroGridName = TEXT("Grid_Macro");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage|Grid")
	FName MainGridName = TEXT("Grid_Main");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage|Grid")
	FName DetailGridName = TEXT("Grid_Detail");
};

UCLASS(BlueprintType)
class PROJECT_J_API UProject_JWorldStreamingPolicyAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming")
	FProject_JStreamingVelocityProfile VelocityProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foliage")
	FProject_JFoliageHLODPolicy FoliagePolicy;
};
