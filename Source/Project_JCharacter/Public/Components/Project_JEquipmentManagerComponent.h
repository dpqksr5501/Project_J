#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Equipment/Project_JEquipmentTypes.h"
#include "Inventory/Project_JItemInstanceTypes.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayAbilitySpec.h"
#include "Project_JEquipmentManagerComponent.generated.h"

class UProject_JEquipmentItemDefinition;
class UProject_JModularMeshComponent;
class UProject_JEquipmentManagerComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProject_JEquipmentChangedSignature, EProject_JEquipmentSlot, Slot, UProject_JEquipmentItemDefinition*, ItemDef);

USTRUCT(BlueprintType)
struct FProject_JEquipmentArrayItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	UProject_JEquipmentItemDefinition* ItemDef = nullptr;

	UPROPERTY()
	FProject_JItemInstanceData ItemInstance;

	UPROPERTY()
	EProject_JEquipmentSlot Slot = EProject_JEquipmentSlot::None;

	UPROPERTY(NotReplicated)
	UProject_JModularMeshComponent* SpawnedMesh = nullptr;

	UPROPERTY(NotReplicated)
	TArray<FGameplayAbilitySpecHandle> GrantedAbilityHandles;

	UPROPERTY(NotReplicated)
	int32 LocalVisualRequestId = 0;
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

	UPROPERTY(BlueprintAssignable, Category = "Equipment")
	FProject_JEquipmentChangedSignature OnEquipmentEquipped;

	UPROPERTY(BlueprintAssignable, Category = "Equipment")
	FProject_JEquipmentChangedSignature OnEquipmentUnequipped;

	/** Equip an item based on its data definition (Server-Only) */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	void EquipItem(UProject_JEquipmentItemDefinition* ItemDef);

	/** Client-safe request path. Server validates and then commits the replicated equipment change. */
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void RequestEquipItem(UProject_JEquipmentItemDefinition* ItemDef);

	/** Equip an owned item instance. This is the preferred path once inventory exists. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	void EquipItemInstance(const FProject_JItemInstanceData& ItemInstance);

	/** Client-safe request path for inventory-backed equipment. */
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void RequestEquipItemInstance(const FProject_JItemInstanceData& ItemInstance);

	/** Unequip an item and destroy its spawned visual representation (Server-Only) */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	void UnequipItem(UProject_JEquipmentItemDefinition* ItemDef);

	/** Unequip whatever item currently occupies the requested slot (Server-Only) */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	void UnequipSlot(EProject_JEquipmentSlot Slot);

	/** Client-safe request path for unequipping a slot. */
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void RequestUnequipSlot(EProject_JEquipmentSlot Slot);

	UFUNCTION(BlueprintPure, Category = "Equipment")
	UProject_JEquipmentItemDefinition* GetEquippedItemInSlot(EProject_JEquipmentSlot Slot) const;

	// Replicated list event handlers
	void OnRep_EquipmentAdded(FProject_JEquipmentArrayItem& Item);
	void OnRep_EquipmentRemoved(FProject_JEquipmentArrayItem& Item);

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION(Server, Reliable)
	void ServerRequestEquipItem(const UProject_JEquipmentItemDefinition* ItemDef);

	UFUNCTION(Server, Reliable)
	void ServerRequestEquipItemInstance(FProject_JItemInstanceData ItemInstance);

	UFUNCTION(Server, Reliable)
	void ServerRequestUnequipSlot(EProject_JEquipmentSlot Slot);

	bool CanCommitEquipItemInstance(const FProject_JItemInstanceData& ItemInstance) const;
	void BroadcastEquipmentEquipped(const FProject_JEquipmentArrayItem& Item);
	void BroadcastEquipmentUnequipped(const FProject_JEquipmentArrayItem& Item);
	void ApplyEquipmentStatModifiers(const UProject_JEquipmentItemDefinition* ItemDef, float Sign) const;
	void StartLocalSpawnEquipment(FProject_JEquipmentArrayItem& Item);
	void OnEquipmentMeshLoaded(FGuid InstanceId, UProject_JEquipmentItemDefinition* ItemDef, int32 VisualRequestId);
	void RefreshCurrentWeaponAnimProfile(const UProject_JEquipmentItemDefinition* ExcludedItemDef = nullptr);
	int32 FindEquipmentIndexByItem(const UProject_JEquipmentItemDefinition* ItemDef) const;
	int32 FindEquipmentIndexByInstanceId(const FGuid& InstanceId) const;
	int32 FindEquipmentIndexBySlot(EProject_JEquipmentSlot Slot) const;
	void RemoveEquipmentAt(int32 Index);

private:
	UPROPERTY(Replicated)
	FProject_JEquipmentArray EquipmentArray;
};
