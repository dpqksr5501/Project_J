#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayAbilitySpec.h"
#include "Project_JEquipmentManagerComponent.generated.h"

class UProject_JEquipmentItemDefinition;
class UProject_JModularMeshComponent;
class UProject_JEquipmentManagerComponent;

USTRUCT(BlueprintType)
struct FProject_JEquipmentArrayItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	UProject_JEquipmentItemDefinition* ItemDef = nullptr;

	UPROPERTY(NotReplicated)
	UProject_JModularMeshComponent* SpawnedMesh = nullptr;

	UPROPERTY(NotReplicated)
	TArray<FGameplayAbilitySpecHandle> GrantedAbilityHandles;
};

USTRUCT(BlueprintType)
struct FProject_JEquipmentArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FProject_JEquipmentArrayItem> Items;

	UPROPERTY(NotReplicated)
	UProject_JEquipmentManagerComponent* OwnerComponent = nullptr;

	void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);
	void PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize);

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FProject_JEquipmentArrayItem, FProject_JEquipmentArray>(Items, DeltaParms, *this);
	}
};

template<>
struct TStructOpsTypeTraits<FProject_JEquipmentArray> : public TStructOpsTypeTraitsBase2<FProject_JEquipmentArray>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};

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

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Equip an item based on its data definition (Server-Only) */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	void EquipItem(UProject_JEquipmentItemDefinition* ItemDef);

	/** Unequip an item and destroy its spawned visual representation (Server-Only) */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	void UnequipItem(UProject_JEquipmentItemDefinition* ItemDef);

	// Replicated list event handlers
	void OnRep_EquipmentAdded(FProject_JEquipmentArrayItem& Item);
	void OnRep_EquipmentRemoved(FProject_JEquipmentArrayItem& Item);

protected:
	virtual void BeginPlay() override;

private:
	void StartLocalSpawnEquipment(FProject_JEquipmentArrayItem& Item);
	void OnEquipmentMeshLoaded(UProject_JEquipmentItemDefinition* ItemDef);

private:
	UPROPERTY(Replicated)
	FProject_JEquipmentArray EquipmentArray;
};
