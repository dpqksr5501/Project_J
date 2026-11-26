#pragma once

#include "UObject/Interface.h"
#include "Project_JStreamingMovementSource.generated.h"

UINTERFACE(BlueprintType)
class PROJECT_JCORE_API UProject_JStreamingMovementSource : public UInterface
{
	GENERATED_BODY()
};

/** Read-only movement mode for streaming prediction. Pawn velocity remains authoritative for direction. */
class PROJECT_JCORE_API IProject_JStreamingMovementSource
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "World|Streaming")
	bool UsesAirborneStreamingPrediction() const;
};
