#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inventory/Project_JItemInstanceTypes.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Project_JInventoryComponent.generated.h"

class UProject_JItemDefinition;
class UProject_JEquipmentItemDefinition;
class UProject_JInventoryComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FProject_JInventoryItemChangedSignature, const FProject_JItemInstanceData&, ItemInstance);

USTRUCT(BlueprintType)
struct FProject_JInventoryArrayItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	FProject_JItemInstanceData ItemInstance;
};

USTRUCT(BlueprintType)
struct FProject_JInventoryArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FProject_JInventoryArrayItem> Items;

	UPROPERTY(NotReplicated)
	TObjectPtr<UProject_JInventoryComponent> OwnerComponent = nullptr;

	void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);
	void PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize);

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FProject_JInventoryArrayItem, FProject_JInventoryArray>(Items, DeltaParms, *this);
	}
};

template<>
struct TStructOpsTypeTraits<FProject_JInventoryArray> : public TStructOpsTypeTraitsBase2<FProject_JInventoryArray>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};

UCLASS(ClassGroup=(Inventory), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JInventoryComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FProject_JInventoryItemChangedSignature OnItemAdded;

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FProject_JInventoryItemChangedSignature OnItemChanged;

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FProject_JInventoryItemChangedSignature OnItemRemoved;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	FProject_JItemInstanceData AddItemDefinition(UProject_JItemDefinition* ItemDef, int32 StackCount = 1, int32 ItemLevel = 1);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool RemoveItemInstance(FGuid InstanceId);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool SetItemStackCount(FGuid InstanceId, int32 NewStackCount);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool AddItemStackCount(FGuid InstanceId, int32 DeltaStackCount);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool ConsumeItemStack(FGuid InstanceId, int32 CountToConsume = 1);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool SetItemInstanceLocked(FGuid InstanceId, bool bLocked, bool bEquipped = false);

	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool HasItemInstance(FGuid InstanceId) const;

	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool IsItemInstanceLocked(FGuid InstanceId) const;

	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool CanRemoveItemInstance(FGuid InstanceId, int32 Count = 1) const;

	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool CanMoveItemInstance(FGuid InstanceId, int32 Count = 1) const;

	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool FindItemInstance(FGuid InstanceId, FProject_JItemInstanceData& OutItemInstance) const;

	/** Development diagnostics: compact state identity for verifying FastArray replication across PIE worlds. */
	FString GetReplicationDiagnosticSummary() const;

	/** Development diagnostics: client-side FastArray delta callbacks observed since the last reset. */
	FString GetReplicationDiagnosticDeltaSummary() const;
	void ResetReplicationDiagnosticDeltaCounters();

	void HandleReplicatedItemAdded(const FProject_JInventoryArrayItem& Item);
	void HandleReplicatedItemChanged(const FProject_JInventoryArrayItem& Item);
	void HandleReplicatedItemRemoved(const FProject_JInventoryArrayItem& Item);

protected:
	virtual void BeginPlay() override;

private:
	bool CanCommitItemInstance(const FProject_JItemInstanceData& ItemInstance) const;
	int32 FindItemIndex(FGuid InstanceId) const;

	UPROPERTY(Replicated)
	FProject_JInventoryArray InventoryArray;

#if !UE_BUILD_SHIPPING
	int32 ReplicationDiagnosticAddedCount = 0;
	int32 ReplicationDiagnosticChangedCount = 0;
	int32 ReplicationDiagnosticRemovedCount = 0;
#endif
};
