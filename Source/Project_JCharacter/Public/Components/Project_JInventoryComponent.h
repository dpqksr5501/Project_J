#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inventory/Project_JItemInstanceTypes.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Project_JInventoryComponent.generated.h"

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
	FProject_JItemInstanceData AddItemDefinition(UProject_JEquipmentItemDefinition* ItemDef, int32 StackCount = 1, int32 ItemLevel = 1);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Inventory")
	bool RemoveItemInstance(FGuid InstanceId);

	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool HasItemInstance(FGuid InstanceId) const;

	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool FindItemInstance(FGuid InstanceId, FProject_JItemInstanceData& OutItemInstance) const;

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
};
