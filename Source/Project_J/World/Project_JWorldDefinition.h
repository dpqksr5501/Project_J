#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Project_JWorldDefinition.generated.h"

USTRUCT(BlueprintType)
struct FProject_JWorldPlace
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName PlaceId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector Location = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) float Radius = 10000;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName EnvironmentId;
    /** Authoring classification, independent of server zones and WP cell IDs. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName PlaceType;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0")) int32 RecommendedMinLevel = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0")) int32 RecommendedMaxLevel = 0;
    /** Optional world-space boundary. Spawn legality still requires server/Nav validation. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FVector> Boundary;
};

USTRUCT(BlueprintType)
struct FProject_JWorldConnection
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName ConnectionId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName BoundarySide;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName ConnectionType;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector Location = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector OutwardDirection = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float WidthCm = 0;
};

/** Authoring identity and tuning only. Never stores mutable weather, kills or rewards. Units: cm/seconds. */
UCLASS(BlueprintType)
class PROJECT_J_API UProject_JWorldDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName RegionId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FProject_JWorldPlace> Places;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FProject_JWorldConnection> Connections;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FBox AuthoringBounds = FBox(ForceInit);
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 GenerationSeed = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 AuthoringVersion = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FString SourcePlanHash;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="60")) float DayLengthSeconds = 2400;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0",ClampMax="24")) float InitialHour = 10;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0",ClampMax="10")) float PredictionSeconds = 3;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1000",ClampMax="200000")) float PreparationRadius = 60000;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0",ClampMax="200000")) float MaxPredictionDistance = 60000;
};
