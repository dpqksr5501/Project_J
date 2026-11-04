#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Project_JWeaponPresentationActor.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UMeshComponent;

/** Independent weapon rig, not a second character locomotion/retarget graph. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JWeaponSkeletalPart
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName PartId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<USkeletalMesh> DrawnMesh = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<USkeletalMesh> SheathedMesh = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FTransform RelativeTransform = FTransform::Identity;
};

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

	/** Existing static subobject remains serialized exactly as before. */
	UFUNCTION(BlueprintPure, Category = "Weapon Presentation")
	TArray<UMeshComponent*> GetVisualMeshes() const;

	void SetPresentationDrawnState(bool bDrawn);
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	/** Reference-pose parts retain skin/bones without a full ABP. Multi-LOD parts retain engine LOD ticking. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Presentation")
	TArray<FProject_JWeaponSkeletalPart> SkeletalParts;

protected:
	/** Stable attachment and motion pivot. Runtime motion moves this root, never the visual mesh directly. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon Presentation")
	TObjectPtr<USceneComponent> WeaponRoot = nullptr;

	/** Add WeaponGrip_* and WeaponGroundProbe_* sockets to the selected mesh asset. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon Presentation")
	TObjectPtr<UStaticMeshComponent> WeaponMesh = nullptr;

private:
	UPROPERTY(Transient) TArray<TObjectPtr<USkeletalMeshComponent>> SkeletalVisuals;
	bool bPresentationDrawn = false;
};
