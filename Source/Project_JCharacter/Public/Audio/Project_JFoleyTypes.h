#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "Project_JFoleyTypes.generated.h"

UENUM(BlueprintType)
enum class EProject_JFoleySide : uint8 { None, Left, Right };

/** Shared admission/cooldown families: changing Walk to Run must not double a contact. */
UENUM(BlueprintType)
enum class EProject_JFoleyGroup : uint8 { Footstep, Scuff, Jump, Land, Other };

/** Contact location policy is independent of the animation/event naming convention. */
UENUM(BlueprintType)
enum class EProject_JFoleyContact : uint8 { Foot, Hand, Capsule, Socket };

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JFoleyEvent
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foley")
	FGameplayTag Event;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foley")
	EProject_JFoleySide Side = EProject_JFoleySide::None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foley", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foley", meta = (ClampMin = "0.01"))
	float PitchMultiplier = 1.0f;
	/** Age of a confirmed semantic event. Old replication snapshots must stay silent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foley", meta = (ClampMin = "0.0"))
	float AgeSeconds = 0.0f;
};

namespace Project_J::Foley
{
	PROJECT_JCHARACTER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Walk);
	PROJECT_JCHARACTER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Run);
	PROJECT_JCHARACTER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Jump);
	PROJECT_JCHARACTER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Land);
	PROJECT_JCHARACTER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Scuff);
	PROJECT_JCHARACTER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Handplant);
	PROJECT_JCHARACTER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(RunBackwards);
	PROJECT_JCHARACTER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(ScuffPivot);
	PROJECT_JCHARACTER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(ScuffWall);
	PROJECT_JCHARACTER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(RunStrafe);
	PROJECT_JCHARACTER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tumble);
	PROJECT_JCHARACTER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(WalkBackwards);
}
