#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "Project_JHandGripProfile.generated.h"

UENUM(BlueprintType)
enum class EProject_JMissingPalmPolicy : uint8
{
	LegacyWristOrigin UMETA(DisplayName = "Legacy Wrist Origin (Compatibility)"),
	DisableContactIK UMETA(DisplayName = "Disable Contact IK")
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JArmBendStability
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elbow Stability")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elbow Stability")
	FVector FallbackPoleAxis = FVector::YAxisVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elbow Stability", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HoldBendRatio = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elbow Stability", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReliableBendRatio = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elbow Stability", meta = (ClampMin = "1.0", Units = "deg/s"))
	float ReacquireDegreesPerSecond = 720.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Elbow Stability", meta = (ClampMin = "0.01", Units = "s"))
	float MaxHistorySeconds = 0.25f;
	bool IsValid() const
	{
		return !FallbackPoleAxis.ContainsNaN() && !FallbackPoleAxis.IsNearlyZero() &&
			FMath::IsFinite(HoldBendRatio) && FMath::IsFinite(ReliableBendRatio) &&
			FMath::IsFinite(ReacquireDegreesPerSecond) && FMath::IsFinite(MaxHistorySeconds) &&
			HoldBendRatio >= 0.0f && ReliableBendRatio > HoldBendRatio && ReliableBendRatio <= 1.0f &&
			ReacquireDegreesPerSecond > 0.0f && MaxHistorySeconds > 0.0f;
	}
};

/** Optional anatomical roles. A missing Hand is inferred from its palm socket; upper/elbow must be set together. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JGripArmBones
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hand IK")
	FName Shoulder = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hand IK")
	FName Elbow = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hand IK")
	FName Hand = NAME_None;
};

/** Body alignment is independent of weapon sockets and gameplay class. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JHandGripCalibration
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Hand IK")
	FName PrimaryPalmSocketName = TEXT("PalmGrip_R");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Hand IK")
	FName SecondaryPalmSocketName = TEXT("PalmGrip_L");
	/** Preserve old assets by default. New body profiles should disable IK for a missing palm anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Hand IK")
	EProject_JMissingPalmPolicy MissingPalmPolicy = EProject_JMissingPalmPolicy::LegacyWristOrigin;
	/** Local to the weapon contact socket, before the palm-to-wrist conversion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Hand IK")
	FTransform PrimaryHandOffset = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Hand IK")
	FTransform SecondaryHandOffset = FTransform::Identity;
	/** Component-space pole targets consumed by an authored Two Bone IK graph. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Hand IK")
	FVector PrimaryElbowTarget = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Hand IK")
	FVector SecondaryElbowTarget = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Hand IK")
	FProject_JGripArmBones PrimaryArm;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Hand IK")
	FProject_JGripArmBones SecondaryArm;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Hand IK")
	FProject_JArmBendStability PrimaryBendStability;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|Hand IK")
	FProject_JArmBendStability SecondaryBendStability;
};

/** Reusable per-skeleton/body contact profile; contains no class, attack, or equipment ownership. */
UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JHandGripProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hand IK")
	FProject_JHandGripCalibration Calibration;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
