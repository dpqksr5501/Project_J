#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Net/UnrealNetwork.h"
#include "Engine/StreamableManager.h"

// --- FFastArraySerializer Callbacks ---

void FProject_JEquipmentArray::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize)
{
	for (int32 Index : AddedIndices)
	{
		if (OwnerComponent && Items.IsValidIndex(Index))
		{
			OwnerComponent->OnRep_EquipmentAdded(Items[Index]);
		}
	}
}

void FProject_JEquipmentArray::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize)
{
}

void FProject_JEquipmentArray::PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize)
{
	for (int32 Index : RemovedIndices)
	{
		if (OwnerComponent && Items.IsValidIndex(Index))
		{
			OwnerComponent->OnRep_EquipmentRemoved(Items[Index]);
		}
	}
}

// --- UProject_JEquipmentManagerComponent ---

UProject_JEquipmentManagerComponent::UProject_JEquipmentManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProject_JEquipmentManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UProject_JEquipmentManagerComponent, EquipmentArray);
}

void UProject_JEquipmentManagerComponent::BeginPlay()
{
	Super::BeginPlay();
	EquipmentArray.OwnerComponent = this;
}

void UProject_JEquipmentManagerComponent::EquipItem(UProject_JEquipmentItemDefinition* ItemDef)
{
	FProject_JItemInstanceData ItemInstance;
	ItemInstance.InstanceId = FGuid::NewGuid();
	ItemInstance.ItemDef = ItemDef;
	EquipItemInstance(ItemInstance);
}

void UProject_JEquipmentManagerComponent::RequestEquipItem(UProject_JEquipmentItemDefinition* ItemDef)
{
	if (!ItemDef)
	{
		return;
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		EquipItem(ItemDef);
		return;
	}

	ServerRequestEquipItem(ItemDef);
}

void UProject_JEquipmentManagerComponent::EquipItemInstance(const FProject_JItemInstanceData& ItemInstance)
{
	UProject_JEquipmentItemDefinition* ItemDef = ItemInstance.ItemDef;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !CanCommitEquipItemInstance(ItemInstance))
	{
		return;
	}

	const EProject_JEquipmentSlot Slot = ItemDef->EquipmentSlot;
	if (Slot != EProject_JEquipmentSlot::None)
	{
		const int32 ExistingSlotIndex = FindEquipmentIndexBySlot(Slot);
		if (ExistingSlotIndex != INDEX_NONE)
		{
			RemoveEquipmentAt(ExistingSlotIndex);
		}
	}

	// Add to replicated fast array
	FProject_JEquipmentArrayItem NewItem;
	NewItem.ItemDef = ItemDef;
	NewItem.ItemInstance = ItemInstance;
	if (!NewItem.ItemInstance.InstanceId.IsValid())
	{
		NewItem.ItemInstance.InstanceId = FGuid::NewGuid();
	}
	NewItem.Slot = Slot;
	
	FProject_JEquipmentArrayItem& AddedItem = EquipmentArray.Items.Add_GetRef(NewItem);
	EquipmentArray.MarkItemDirty(AddedItem);
	BroadcastEquipmentEquipped(AddedItem);
}

void UProject_JEquipmentManagerComponent::RequestEquipItemInstance(const FProject_JItemInstanceData& ItemInstance)
{
	if (!ItemInstance.ItemDef)
	{
		return;
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		EquipItemInstance(ItemInstance);
		return;
	}

	ServerRequestEquipItemInstance(ItemInstance);
}

void UProject_JEquipmentManagerComponent::UnequipItem(UProject_JEquipmentItemDefinition* ItemDef)
{
	if (!ItemDef || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	const int32 FoundIndex = FindEquipmentIndexByItem(ItemDef);
	if (FoundIndex != INDEX_NONE)
	{
		RemoveEquipmentAt(FoundIndex);
	}
}

void UProject_JEquipmentManagerComponent::UnequipSlot(EProject_JEquipmentSlot Slot)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	const int32 FoundIndex = FindEquipmentIndexBySlot(Slot);
	if (FoundIndex != INDEX_NONE)
	{
		RemoveEquipmentAt(FoundIndex);
	}
}

void UProject_JEquipmentManagerComponent::RequestUnequipSlot(EProject_JEquipmentSlot Slot)
{
	if (Slot == EProject_JEquipmentSlot::None)
	{
		return;
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		UnequipSlot(Slot);
		return;
	}

	ServerRequestUnequipSlot(Slot);
}

UProject_JEquipmentItemDefinition* UProject_JEquipmentManagerComponent::GetEquippedItemInSlot(EProject_JEquipmentSlot Slot) const
{
	const int32 FoundIndex = FindEquipmentIndexBySlot(Slot);
	return FoundIndex != INDEX_NONE ? EquipmentArray.Items[FoundIndex].ItemDef : nullptr;
}

TArray<UProject_JEquipmentItemDefinition*> UProject_JEquipmentManagerComponent::GetAllEquippedItems() const
{
	TArray<UProject_JEquipmentItemDefinition*> Result;
	for (const FProject_JEquipmentArrayItem& Item : EquipmentArray.Items)
	{
		if (Item.ItemDef)
		{
			Result.Add(Item.ItemDef);
		}
	}
	return Result;
}

void UProject_JEquipmentManagerComponent::ServerRequestEquipItem_Implementation(const UProject_JEquipmentItemDefinition* ItemDef)
{
	EquipItem(const_cast<UProject_JEquipmentItemDefinition*>(ItemDef));
}

void UProject_JEquipmentManagerComponent::ServerRequestEquipItemInstance_Implementation(const FProject_JItemInstanceData& ItemInstance)
{
	EquipItemInstance(ItemInstance);
}

void UProject_JEquipmentManagerComponent::ServerRequestUnequipSlot_Implementation(EProject_JEquipmentSlot Slot)
{
	UnequipSlot(Slot);
}

bool UProject_JEquipmentManagerComponent::CanCommitEquipItemInstance(const FProject_JItemInstanceData& ItemInstance) const
{
	if (!ItemInstance.ItemDef || ItemInstance.ItemDef->EquipmentSlot == EProject_JEquipmentSlot::None)
	{
		return false;
	}

	if (ItemInstance.InstanceId.IsValid() && FindEquipmentIndexByInstanceId(ItemInstance.InstanceId) != INDEX_NONE)
	{
		return false;
	}

	return true;
}

void UProject_JEquipmentManagerComponent::RemoveEquipmentAt(int32 Index)
{
	if (!EquipmentArray.Items.IsValidIndex(Index))
	{
		return;
	}

	BroadcastEquipmentUnequipped(EquipmentArray.Items[Index]);

	EquipmentArray.Items.RemoveAt(Index);
	EquipmentArray.MarkArrayDirty();
}

void UProject_JEquipmentManagerComponent::OnRep_EquipmentAdded(FProject_JEquipmentArrayItem& Item)
{
	BroadcastEquipmentEquipped(Item);
}

void UProject_JEquipmentManagerComponent::OnRep_EquipmentRemoved(FProject_JEquipmentArrayItem& Item)
{
	BroadcastEquipmentUnequipped(Item);
}

void UProject_JEquipmentManagerComponent::BroadcastEquipmentEquipped(const FProject_JEquipmentArrayItem& Item)
{
	if (Item.ItemDef)
	{
		OnEquipmentEquipped.Broadcast(Item.Slot, Item.ItemDef);
	}
}

void UProject_JEquipmentManagerComponent::BroadcastEquipmentUnequipped(const FProject_JEquipmentArrayItem& Item)
{
	if (Item.ItemDef)
	{
		OnEquipmentUnequipped.Broadcast(Item.Slot, Item.ItemDef);
	}
}


int32 UProject_JEquipmentManagerComponent::FindEquipmentIndexByItem(const UProject_JEquipmentItemDefinition* ItemDef) const
{
	for (int32 Index = 0; Index < EquipmentArray.Items.Num(); ++Index)
	{
		if (EquipmentArray.Items[Index].ItemDef == ItemDef)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

int32 UProject_JEquipmentManagerComponent::FindEquipmentIndexByInstanceId(const FGuid& InstanceId) const
{
	if (!InstanceId.IsValid())
	{
		return INDEX_NONE;
	}

	for (int32 Index = 0; Index < EquipmentArray.Items.Num(); ++Index)
	{
		if (EquipmentArray.Items[Index].ItemInstance.InstanceId == InstanceId)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

int32 UProject_JEquipmentManagerComponent::FindEquipmentIndexBySlot(EProject_JEquipmentSlot Slot) const
{
	if (Slot == EProject_JEquipmentSlot::None)
	{
		return INDEX_NONE;
	}

	for (int32 Index = 0; Index < EquipmentArray.Items.Num(); ++Index)
	{
		if (EquipmentArray.Items[Index].Slot == Slot)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}
