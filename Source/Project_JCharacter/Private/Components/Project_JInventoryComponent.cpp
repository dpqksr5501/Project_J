#include "Components/Project_JInventoryComponent.h"

#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Net/UnrealNetwork.h"

void FProject_JInventoryArray::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize)
{
	for (const int32 Index : AddedIndices)
	{
		if (OwnerComponent && Items.IsValidIndex(Index))
		{
			OwnerComponent->HandleReplicatedItemAdded(Items[Index]);
		}
	}
}

void FProject_JInventoryArray::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize)
{
	for (const int32 Index : ChangedIndices)
	{
		if (OwnerComponent && Items.IsValidIndex(Index))
		{
			OwnerComponent->HandleReplicatedItemChanged(Items[Index]);
		}
	}
}

void FProject_JInventoryArray::PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize)
{
	for (const int32 Index : RemovedIndices)
	{
		if (OwnerComponent && Items.IsValidIndex(Index))
		{
			OwnerComponent->HandleReplicatedItemRemoved(Items[Index]);
		}
	}
}

UProject_JInventoryComponent::UProject_JInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProject_JInventoryComponent::BeginPlay()
{
	Super::BeginPlay();
	InventoryArray.OwnerComponent = this;
}

void UProject_JInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UProject_JInventoryComponent, InventoryArray, COND_OwnerOnly);
}

FProject_JItemInstanceData UProject_JInventoryComponent::AddItemDefinition(UProject_JEquipmentItemDefinition* ItemDef, int32 StackCount, int32 ItemLevel)
{
	FProject_JItemInstanceData NewItem;
	NewItem.InstanceId = FGuid::NewGuid();
	NewItem.ItemDef = ItemDef;
	NewItem.StackCount = FMath::Max(1, StackCount);
	NewItem.ItemLevel = FMath::Max(1, ItemLevel);

	if (!GetOwner() || !GetOwner()->HasAuthority() || !CanCommitItemInstance(NewItem))
	{
		return FProject_JItemInstanceData();
	}

	FProject_JInventoryArrayItem& AddedItem = InventoryArray.Items.Add_GetRef(FProject_JInventoryArrayItem());
	AddedItem.ItemInstance = NewItem;
	InventoryArray.MarkItemDirty(AddedItem);
	OnItemAdded.Broadcast(AddedItem.ItemInstance);
	return AddedItem.ItemInstance;
}

bool UProject_JInventoryComponent::RemoveItemInstance(FGuid InstanceId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	const int32 FoundIndex = FindItemIndex(InstanceId);
	if (FoundIndex == INDEX_NONE)
	{
		return false;
	}

	if (!CanRemoveItemInstance(InstanceId, 1))
	{
		return false;
	}

	const FProject_JInventoryArrayItem RemovedItem = InventoryArray.Items[FoundIndex];
	OnItemRemoved.Broadcast(RemovedItem.ItemInstance);
	InventoryArray.Items.RemoveAt(FoundIndex);
	InventoryArray.MarkArrayDirty();
	return true;
}

bool UProject_JInventoryComponent::SetItemStackCount(FGuid InstanceId, int32 NewStackCount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || NewStackCount < 0)
	{
		return false;
	}

	const int32 FoundIndex = FindItemIndex(InstanceId);
	if (FoundIndex == INDEX_NONE)
	{
		return false;
	}

	FProject_JInventoryArrayItem& Item = InventoryArray.Items[FoundIndex];
	if (NewStackCount > Item.ItemInstance.StackCount && Item.ItemInstance.bIsLocked)
	{
		return false;
	}

	if (NewStackCount < Item.ItemInstance.StackCount && !CanRemoveItemInstance(InstanceId, Item.ItemInstance.StackCount - NewStackCount))
	{
		return false;
	}

	if (NewStackCount == 0)
	{
		const FProject_JInventoryArrayItem RemovedItem = Item;
		OnItemRemoved.Broadcast(RemovedItem.ItemInstance);
		InventoryArray.Items.RemoveAt(FoundIndex);
		InventoryArray.MarkArrayDirty();
		return true;
	}

	Item.ItemInstance.StackCount = NewStackCount;
	InventoryArray.MarkItemDirty(Item);
	OnItemChanged.Broadcast(Item.ItemInstance);
	return true;
}

bool UProject_JInventoryComponent::AddItemStackCount(FGuid InstanceId, int32 DeltaStackCount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	if (DeltaStackCount == 0)
	{
		return true;
	}

	const int32 FoundIndex = FindItemIndex(InstanceId);
	if (FoundIndex == INDEX_NONE)
	{
		return false;
	}

	const int32 CurrentStackCount = InventoryArray.Items[FoundIndex].ItemInstance.StackCount;
	return SetItemStackCount(InstanceId, CurrentStackCount + DeltaStackCount);
}

bool UProject_JInventoryComponent::ConsumeItemStack(FGuid InstanceId, int32 CountToConsume)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	if (CountToConsume <= 0)
	{
		return false;
	}

	const int32 FoundIndex = FindItemIndex(InstanceId);
	if (FoundIndex == INDEX_NONE)
	{
		return false;
	}

	const int32 CurrentStackCount = InventoryArray.Items[FoundIndex].ItemInstance.StackCount;
	if (CurrentStackCount < CountToConsume)
	{
		return false;
	}

	return SetItemStackCount(InstanceId, CurrentStackCount - CountToConsume);
}

bool UProject_JInventoryComponent::SetItemInstanceLocked(FGuid InstanceId, bool bLocked, bool bEquipped)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	const int32 FoundIndex = FindItemIndex(InstanceId);
	if (FoundIndex == INDEX_NONE)
	{
		return false;
	}

	FProject_JInventoryArrayItem& Item = InventoryArray.Items[FoundIndex];
	Item.ItemInstance.bIsLocked = bLocked;
	Item.ItemInstance.bIsEquipped = bLocked && bEquipped;
	InventoryArray.MarkItemDirty(Item);
	OnItemChanged.Broadcast(Item.ItemInstance);
	return true;
}

bool UProject_JInventoryComponent::HasItemInstance(FGuid InstanceId) const
{
	return FindItemIndex(InstanceId) != INDEX_NONE;
}

bool UProject_JInventoryComponent::IsItemInstanceLocked(FGuid InstanceId) const
{
	const int32 FoundIndex = FindItemIndex(InstanceId);
	return FoundIndex != INDEX_NONE && InventoryArray.Items[FoundIndex].ItemInstance.bIsLocked;
}

bool UProject_JInventoryComponent::CanRemoveItemInstance(FGuid InstanceId, int32 Count) const
{
	if (Count <= 0)
	{
		return false;
	}

	const int32 FoundIndex = FindItemIndex(InstanceId);
	if (FoundIndex == INDEX_NONE)
	{
		return false;
	}

	const FProject_JItemInstanceData& ItemInstance = InventoryArray.Items[FoundIndex].ItemInstance;
	return
		ItemInstance.IsValid() &&
		!ItemInstance.bIsLocked &&
		ItemInstance.StackCount >= Count;
}

bool UProject_JInventoryComponent::CanMoveItemInstance(FGuid InstanceId, int32 Count) const
{
	return CanRemoveItemInstance(InstanceId, Count);
}

bool UProject_JInventoryComponent::FindItemInstance(FGuid InstanceId, FProject_JItemInstanceData& OutItemInstance) const
{
	const int32 FoundIndex = FindItemIndex(InstanceId);
	if (FoundIndex == INDEX_NONE)
	{
		return false;
	}

	OutItemInstance = InventoryArray.Items[FoundIndex].ItemInstance;
	return true;
}

void UProject_JInventoryComponent::HandleReplicatedItemAdded(const FProject_JInventoryArrayItem& Item)
{
	OnItemAdded.Broadcast(Item.ItemInstance);
}

void UProject_JInventoryComponent::HandleReplicatedItemChanged(const FProject_JInventoryArrayItem& Item)
{
	OnItemChanged.Broadcast(Item.ItemInstance);
}

void UProject_JInventoryComponent::HandleReplicatedItemRemoved(const FProject_JInventoryArrayItem& Item)
{
	OnItemRemoved.Broadcast(Item.ItemInstance);
}

bool UProject_JInventoryComponent::CanCommitItemInstance(const FProject_JItemInstanceData& ItemInstance) const
{
	return ItemInstance.ItemDef && ItemInstance.InstanceId.IsValid() && ItemInstance.StackCount > 0 && FindItemIndex(ItemInstance.InstanceId) == INDEX_NONE;
}

int32 UProject_JInventoryComponent::FindItemIndex(FGuid InstanceId) const
{
	if (!InstanceId.IsValid())
	{
		return INDEX_NONE;
	}

	for (int32 Index = 0; Index < InventoryArray.Items.Num(); ++Index)
	{
		if (InventoryArray.Items[Index].ItemInstance.InstanceId == InstanceId)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}
