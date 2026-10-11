#pragma once

#include "CoreMinimal.h"
#include "Project_JStrafeUpperBodyFacing.generated.h"

struct FProject_JAnimThreadSafeData;

/** Presentation only. Pose Search keeps its original, uncorrected history. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JStrafeUpperBodyFacingPolicy
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing")
	bool bEnabled = true;
	/** Roles on the Master driving skeleton, before visible-character retargeting. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing|Rig")
	FName RootBone = TEXT("root");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing|Rig")
	FName SpineRootBone = TEXT("spine_01");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing|Rig")
	FName ChestBone = TEXT("spine_05");
	/** Keep small authored torso yaw rather than locking the chest to the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing", meta = (ClampMin = "0", ClampMax = "30", Units = "deg"))
	float Tolerance = 8.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing", meta = (ClampMin = "0", ClampMax = "60", Units = "deg"))
	float MaxCorrection = 40.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing", meta = (ClampMin = "0.01", ClampMax = "0.5", Units = "s"))
	float HalfLife = .04f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing", meta = (ClampMin = "30", ClampMax = "720", Units = "deg/s"))
	float MaxCorrectionRate = 360.f;
	/** Only forward input. Side/backward locomotion retains its authored poses. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing", meta = (ClampMin = "0", ClampMax = "45", Units = "deg"))
	float ForwardCone = 25.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing", meta = (ClampMin = "10", Units = "cm/s"))
	float MinimumSpeed = 150.f;
	/** Fade out when the whole visual body is still catching up to the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing", meta = (ClampMin = "0", ClampMax = "60", Units = "deg"))
	float RootFadeStart = 25.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing", meta = (ClampMin = "1", ClampMax = "90", Units = "deg"))
	float RootFadeEnd = 45.f;
};

/** Value-only contract published on GT, consumed by the pose node on workers. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JStrafeUpperBodyFacingInput
{
	GENERATED_BODY()
	bool bEnabled = false;
	bool bCanRelease = false;
	float ActorYaw = 0.f;
	float TargetYaw = 0.f;
	float Strength = 0.f;
	FProject_JStrafeUpperBodyFacingPolicy Policy;
};

namespace Project_J::Animation
{
/** Shared ownership check so upper/lower presentation cannot disagree on Turn owners. */
PROJECT_JCHARACTER_API bool IsStrafeFacingTurnOwned(const FProject_JAnimThreadSafeData& Data);
PROJECT_JCHARACTER_API FProject_JStrafeUpperBodyFacingInput BuildStrafeUpperBodyFacingInput(
	const FProject_JAnimThreadSafeData& Data, const FProject_JStrafeUpperBodyFacingPolicy& Policy,
	bool bPrimaryLocalPlayer, bool bActionOwner, bool bConsoleEnabled, float ActorYaw);
PROJECT_JCHARACTER_API float UpperBodyFacingDemand(float Error, float Strength,
	const FProject_JStrafeUpperBodyFacingPolicy& Policy);
PROJECT_JCHARACTER_API float StepUpperBodyFacing(float Current, float Target, float DeltaTime,
	const FProject_JStrafeUpperBodyFacingPolicy& Policy);
PROJECT_JCHARACTER_API float UpperBodyRootFacingWeight(float Error, const FProject_JStrafeUpperBodyFacingPolicy& Policy);
}
