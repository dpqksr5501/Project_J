#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Project_JCameraContextProfile.generated.h"

/** Context-driven dynamic action camera modes (Stage A: Giant Boss, Mount, Soft-Lock). */
UENUM(BlueprintType)
enum class EProject_JCameraContextMode : uint8
{
	OnFoot,
	CombatSoftLock,
	GiantBossZoomOut,
	GroundMount,
	FlyingMountDive
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JCameraContextPreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "100.0", ClampMax = "2500.0"))
	float TargetArmLength = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "50.0", ClampMax = "130.0"))
	float FieldOfView = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FVector SocketOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float InterpSpeed = 6.0f;
};

UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JCameraPolicyAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	TMap<EProject_JCameraContextMode, FProject_JCameraContextPreset> Presets;

	/** Resolves a valid preset for the given mode with deterministic defaults when unauthored. */
	FProject_JCameraContextPreset ResolvePreset(EProject_JCameraContextMode Mode) const
	{
		if (const FProject_JCameraContextPreset* Found = Presets.Find(Mode))
		{
			return *Found;
		}
		FProject_JCameraContextPreset Default;
		switch (Mode)
		{
		case EProject_JCameraContextMode::CombatSoftLock:
			Default.TargetArmLength = 340.0f; Default.FieldOfView = 86.0f; Default.SocketOffset = FVector(0.f, 55.f, 20.f); Default.InterpSpeed = 8.0f;
			break;
		case EProject_JCameraContextMode::GiantBossZoomOut:
			Default.TargetArmLength = 780.0f; Default.FieldOfView = 98.0f; Default.SocketOffset = FVector(0.f, 0.f, 95.f); Default.InterpSpeed = 3.5f;
			break;
		case EProject_JCameraContextMode::GroundMount:
			Default.TargetArmLength = 520.0f; Default.FieldOfView = 94.0f; Default.SocketOffset = FVector(0.f, 0.f, 45.f); Default.InterpSpeed = 5.0f;
			break;
		case EProject_JCameraContextMode::FlyingMountDive:
			Default.TargetArmLength = 640.0f; Default.FieldOfView = 108.0f; Default.SocketOffset = FVector(0.f, 0.f, 65.f); Default.InterpSpeed = 4.0f;
			break;
		case EProject_JCameraContextMode::OnFoot:
		default:
			Default.TargetArmLength = 400.0f; Default.FieldOfView = 90.0f; Default.SocketOffset = FVector::ZeroVector; Default.InterpSpeed = 6.0f;
			break;
		}
		return Default;
	}
};
