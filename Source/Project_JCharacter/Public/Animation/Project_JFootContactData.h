#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "Project_JFootContactData.generated.h"

/** Import-time skin samples; runtime never reads/skins render vertices. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JFootSupportInfluence
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Bone;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Position = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Weight = 0.f;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JFootSupportPoint
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FProject_JFootSupportInfluence> Influences;
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JFootSupportSole
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Thigh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Calf;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Foot;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FProject_JFootSupportPoint> Points;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float Clearance = .15f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float MaxLift = 10.f;
};

/** Optional calibration on the actual visible body/footwear mesh, not a character-name override.
 * Re-author after geometry, weights or bind changes. Split parts can contribute to one leg solve. */
UCLASS(BlueprintType, EditInlineNew)
class PROJECT_JCHARACTER_API UProject_JFootContactData : public UAssetUserData
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FProject_JFootSupportSole> Soles;
	static constexpr int32 MaxPointsPerLeg = 256;
};
