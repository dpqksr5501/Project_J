#pragma once

#include "Engine/AssetUserData.h"
#include "Project_JDistantMeshPolicy.generated.h"

namespace Project_J::World
{
	inline constexpr int32 MaxCanopyWPODisableDistanceCm = 10000;
}

/** Placement-independent intent. The actual HLOD layer stays on the UE world/actor. */
UENUM()
enum class EProject_JDistantMeshRole : uint8
{
	NearOnly,
	DistantLandmark,
	InstancedCanopy
};

UCLASS(DisplayName = "Project J Distant Mesh Policy", EditInlineNew)
class PROJECT_JCORE_API UProject_JDistantMeshPolicy : public UAssetUserData
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "World")
	EProject_JDistantMeshRole Role = EProject_JDistantMeshRole::DistantLandmark;

	virtual bool IsEditorOnly() const override { return true; }
};
