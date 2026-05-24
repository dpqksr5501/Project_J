#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JEquipmentManagerComponent.generated.h"

class UProject_JEquipmentItemDefinition;
class UProject_JModularMeshComponent;

/**
 * Manages dynamically attached equipment pieces.
 * Spawns modular meshes and synchronizes them with the main skeletal mesh.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JEquipmentManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JEquipmentManagerComponent();

	/** Equip an item based on its data definition */
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void EquipItem(UProject_JEquipmentItemDefinition* ItemDef);

	/** Unequip an item and destroy its spawned visual representation */
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void UnequipItem(UProject_JEquipmentItemDefinition* ItemDef);

protected:
	virtual void BeginPlay() override;

private:
	// Tracks currently spawned modular meshes linked to their definitions
	UPROPERTY()
	TMap<UProject_JEquipmentItemDefinition*, UProject_JModularMeshComponent*> EquippedMeshes;
};
