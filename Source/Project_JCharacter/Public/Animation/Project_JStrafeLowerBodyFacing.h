#pragma once

#include "CoreMinimal.h"
#include "Animation/Project_JStrafeUpperBodyFacing.h"
#include "Project_JStrafeLowerBodyFacing.generated.h"

struct FProject_JAnimThreadSafeData;

/** Post-history presentation for forward Strafe Run. Feet remain pose constraints. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JStrafeLowerBodyFacingPolicy
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing")
	bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rig")
	FName RootBone = TEXT("root");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rig")
	FName PelvisBone = TEXT("pelvis");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rig")
	FName SpineRootBone = TEXT("spine_01");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rig")
	FName LeftThighBone = TEXT("thigh_l");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rig")
	FName LeftCalfBone = TEXT("calf_l");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rig")
	FName LeftFootBone = TEXT("foot_l");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rig")
	FName RightThighBone = TEXT("thigh_r");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rig")
	FName RightCalfBone = TEXT("calf_r");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rig")
	FName RightFootBone = TEXT("foot_r");
	/** Preserve small authored hip swing instead of locking pelvis to velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing", meta = (ClampMin = "0", ClampMax = "30", Units = "deg"))
	float Tolerance = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing", meta = (ClampMin = "0", ClampMax = "30", Units = "deg"))
	float MaxCorrection = 25.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing", meta = (ClampMin = "0.01", ClampMax = "0.5", Units = "s"))
	float HalfLife = .08f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing", meta = (ClampMin = "30", ClampMax = "360", Units = "deg/s"))
	float MaxCorrectionRate = 180.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing", meta = (ClampMin = "0", ClampMax = "45", Units = "deg"))
	float ForwardCone = 25.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facing", meta = (ClampMin = "10", Units = "cm/s"))
	float MinimumSpeed = 150.f;
};

struct PROJECT_JCHARACTER_API FProject_JStrafeLowerBodyFacingInput
{
	FProject_JStrafeUpperBodyFacingInput Facing;
	FProject_JStrafeLowerBodyFacingPolicy Policy;
};

namespace Project_J::Animation
{
PROJECT_JCHARACTER_API FProject_JStrafeLowerBodyFacingInput BuildStrafeLowerBodyFacingInput(
	const FProject_JAnimThreadSafeData& Data, const FProject_JStrafeLowerBodyFacingPolicy& Policy,
	bool bPrimaryLocalPlayer, bool bActionOwner, bool bConsoleEnabled, float ActorYaw, const FVector2D& LocalMoveInput);

/** Solve an already evaluated leg against its original foot transform, without stretching. */
PROJECT_JCHARACTER_API bool SolveFacingLeg(const FTransform& Pelvis, const FTransform& ThighLocal,
	const FTransform& CalfLocal, const FTransform& FootLocal, const FTransform& FootTarget,
	FTransform& OutThigh, FTransform& OutCalf, FTransform& OutFoot);
}
