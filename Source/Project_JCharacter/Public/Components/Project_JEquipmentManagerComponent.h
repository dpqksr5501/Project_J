#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Equipment/Project_JEquipmentTypes.h"
#include "Inventory/Project_JItemInstanceTypes.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Project_JEquipmentManagerComponent.generated.h"

class UProject_JEquipmentItemDefinition;
class UProject_JModularMeshComponent;
class UProject_JEquipmentManagerComponent;
class UProject_JInventoryComponent;

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
 * Manages replicated equipment data on the PlayerState.
 * The visual representation is handled by EquipmentRuntimeComponent on the Character.
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

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	void EquipItem(UProject_JEquipmentItemDefinition* ItemDef);

	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void RequestEquipItem(UProject_JEquipmentItemDefinition* ItemDef);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	void EquipItemInstance(const FProject_JItemInstanceData& ItemInstance);

	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void RequestEquipItemInstance(const FProject_JItemInstanceData& ItemInstance);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	void UnequipItem(UProject_JEquipmentItemDefinition* ItemDef);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Equipment")
	void UnequipSlot(EProject_JEquipmentSlot Slot);

	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void RequestUnequipSlot(EProject_JEquipmentSlot Slot);

	UFUNCTION(BlueprintPure, Category = "Equipment")
	UProject_JEquipmentItemDefinition* GetEquippedItemInSlot(EProject_JEquipmentSlot Slot) const;

	UFUNCTION(BlueprintPure, Category = "Equipment")
	TArray<UProject_JEquipmentItemDefinition*> GetAllEquippedItems() const;

	void OnRep_EquipmentAdded(FProject_JEquipmentArrayItem& Item);
	void OnRep_EquipmentChanged(FProject_JEquipmentArrayItem& Item);
	void OnRep_EquipmentRemoved(FProject_JEquipmentArrayItem& Item);

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION(Server, Reliable)
	void ServerRequestEquipItem(const UProject_JEquipmentItemDefinition* ItemDef);

	UFUNCTION(Server, Reliable)
	void ServerRequestEquipItemInstance(const FProject_JItemInstanceData& ItemInstance);

	UFUNCTION(Server, Reliable)
	void ServerRequestUnequipSlot(EProject_JEquipmentSlot Slot);

	bool CanCommitEquipItemInstance(const FProject_JItemInstanceData& ItemInstance) const;
	bool CanEquipInventoryItemInstance(const FProject_JItemInstanceData& ItemInstance) const;
	UProject_JInventoryComponent* GetOwnerInventoryComponent() const;
	bool SetInventoryEquipmentLock(const FProject_JItemInstanceData& ItemInstance, bool bLocked) const;
	void CommitEquipItemInstance(const FProject_JItemInstanceData& ItemInstance, bool bRequireInventoryOwnership);
	void BroadcastEquipmentEquipped(const FProject_JEquipmentArrayItem& Item);
	void BroadcastEquipmentUnequipped(const FProject_JEquipmentArrayItem& Item);
	int32 FindEquipmentIndexByItem(const UProject_JEquipmentItemDefinition* ItemDef) const;
	int32 FindEquipmentIndexByInstanceId(const FGuid& InstanceId) const;
	int32 FindEquipmentIndexBySlot(EProject_JEquipmentSlot Slot) const;
	void RemoveEquipmentAt(int32 Index);

private:
	UPROPERTY(Replicated)
	FProject_JEquipmentArray EquipmentArray;
};
