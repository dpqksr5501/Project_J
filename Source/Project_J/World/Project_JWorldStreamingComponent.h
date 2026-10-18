#pragma once
#include "CoreMinimal.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Project_JWorldStreamingComponent.generated.h"

/** Additional local-player source. Does not disable the controller's default source or block normal movement. */
UCLASS(ClassGroup=(World), meta=(BlueprintSpawnableComponent))
class PROJECT_J_API UProject_JWorldStreamingComponent : public UWorldPartitionStreamingSourceComponent
{
    GENERATED_BODY()
public:
    UProject_JWorldStreamingComponent();
    virtual bool GetStreamingSource(FWorldPartitionStreamingSource& Out) const override;
    static FVector PredictOffset(const FVector& Velocity, float Seconds, float Maximum);
protected:
    virtual void BeginPlay() override;
    bool bHasWorldDefinition = false;
public:
    UPROPERTY(EditAnywhere, Category="Preparation", meta=(ClampMin="0",ClampMax="10")) float PredictionSeconds = 3;
    UPROPERTY(EditAnywhere, Category="Preparation", meta=(ClampMin="1000",ClampMax="200000")) float PreparationRadius = 60000;
    UPROPERTY(EditAnywhere, Category="Preparation", meta=(ClampMin="0",ClampMax="200000")) float MaxPredictionDistance = 60000;
};
