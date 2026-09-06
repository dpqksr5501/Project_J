#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Project_JWeaponPresentationActor.generated.h"

class UStaticMeshComponent;
class USceneComponent;

/**
 * Common client-side visual actor for every equippable weapon.
 *
 * This actor owns no combat authority, hit detection, inventory state, or
 * network transform replication. Those concerns remain on the owning
 * character's equipment/ability systems. Weapon Blueprint children should
 * only author their mesh, material/VFX, and standardized mesh sockets.
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class PROJECT_JCHARACTER_API AProject_JWeaponPresentationActor : public AActor
{
	GENERATED_BODY()

public:
	AProject_JWeaponPresentationActor();

	UFUNCTION(BlueprintPure, Category = "Weapon Presentation")
	UStaticMeshComponent* GetWeaponMesh() const { return WeaponMesh; }

protected:
	/** Stable attachment and motion pivot. Runtime motion moves this root, never the visual mesh directly. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon Presentation")
	TObjectPtr<USceneComponent> WeaponRoot = nullptr;

	/** Add WeaponGrip_* and WeaponGroundProbe_* sockets to the selected mesh asset. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon Presentation")
	TObjectPtr<UStaticMeshComponent> WeaponMesh = nullptr;
};
