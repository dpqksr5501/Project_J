#pragma once

#include "Animation/AnimMetaData.h"
#include "Project_JMontageLocomotionHandoff.generated.h"

class UAnimMontage;

/** Authored points where a montage can share its pose with locomotion. Times are montage positions. */
UCLASS()
class PROJECT_JCORE_API UProject_JMontageLocomotionHandoff : public UAnimMetaData
{
	GENERATED_BODY()

public:
	/** End of gameplay root motion and start of the lower-body handoff on a full-body montage. */
	UPROPERTY(EditAnywhere, Category="Locomotion Handoff", meta=(ClampMin="-1.0", Units="s"))
	float RecoveryStart = -1.f;

	/** Blend time when releasing the legs to locomotion, in montage seconds. */
	UPROPERTY(EditAnywhere, Category="Locomotion Handoff", meta=(ClampMin="0.01", Units="s"))
	float RecoveryBlendTime = .10f;

	/** Optional grounded lower-body pose interval for an upper-body cast. Empty leaves the legs to locomotion. */
	UPROPERTY(EditAnywhere, Category="Locomotion Handoff")
	FVector2f StationaryPoseWindow = FVector2f::ZeroVector;

	UPROPERTY(EditAnywhere, Category="Locomotion Handoff", meta=(ClampMin="0.0", ClampMax="1.0"))
	float StationaryPoseWeight = 0.f;

#if WITH_EDITOR
	FString GetValidationFailure(const UAnimMontage& Montage) const;
#endif
};
