#include "Components/Project_JModularMeshComponent.h"
#include "Engine/SkeletalMesh.h"

UProject_JModularMeshComponent::UProject_JModularMeshComponent()
{
	// Default optimization: don't tick animations if not rendered, relying on the main mesh's significance or leader pose.
	VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	PrimaryComponentTick.bCanEverTick = true;
	bEnableUpdateRateOptimizations = true;
}

void UProject_JModularMeshComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UProject_JModularMeshComponent::AttachAndSetLeader(USkeletalMeshComponent* MainMesh)
{
	if (!MainMesh) return;

	// Attach to the main mesh
	AttachToComponent(MainMesh, FAttachmentTransformRules::KeepRelativeTransform);

	// Force this component to blindly follow the main mesh's evaluated pose.
	// This zeroes out the animation evaluation overhead for modular parts (armor, boots, etc.).
	SetLeaderPoseComponent(MainMesh, true, true);
}
