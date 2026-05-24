#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "Project_JModularMeshComponent.generated.h"

/**
 * Custom Skeletal Mesh Component optimized for large-scale MMORPG modular characters.
 * Pillar 5: Nanite Skeletal Extreme Rendering & Modular Character Structure.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JModularMeshComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	UProject_JModularMeshComponent();

	/**
	 * Attaches this modular part to the main character mesh and forces it to use the main mesh's leader pose.
	 * This eliminates redundant animation evaluations for this component.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModularMesh")
	void AttachAndSetLeader(USkeletalMeshComponent* MainMesh);

protected:
	virtual void BeginPlay() override;

	// Overridden to selectively enable physics (e.g. KawaiiPhysics / RigidBody) only for specific bone chains
	// virtual UPhysicsAsset* GetOrBuildMainPhysicsAsset() override;
};
