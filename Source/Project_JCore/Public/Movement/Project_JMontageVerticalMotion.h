#pragma once

#include "Animation/AnimMetaData.h"
#include "Project_JMontageVerticalMotion.generated.h"

/** Plays the montage's authored launch or descent before returning to ordinary movement. */
UCLASS()
class PROJECT_JCORE_API UProject_JMontageVerticalMotion : public UAnimMetaData
{
	GENERATED_BODY()

public:
	/** Montage time when the opening vertical root motion is complete. */
	UPROPERTY(EditAnywhere, Category = "Root Motion", meta = (ClampMin = "0.0", Units = "s"))
	float EndTime = 0.f;

	/** For airborne attacks, stop applying the vertical window when Falling ends. */
	UPROPERTY(EditAnywhere, Category = "Root Motion")
	bool bAirborneOnly = false;
};
