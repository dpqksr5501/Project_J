#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Inventory/Project_JItemDefinition.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJEquipment, Log, All);

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
	for (int32 Index : ChangedIndices)
	{
		if (OwnerComponent && Items.IsValidIndex(Index))
		{
			OwnerComponent->OnRep_EquipmentChanged(Items[Index]);
		}
	}
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
	CommitEquipItemInstance(ItemInstance, false);
}

void UProject_JEquipmentManagerComponent::EquipItemInstance(const FProject_JItemInstanceData& ItemInstance)
{
	CommitEquipItemInstance(ItemInstance, true);
}

void UProject_JEquipmentManagerComponent::RequestEquipItemInstanceById(FGuid InstanceId)
{
	if (!InstanceId.IsValid())
	{
		return;
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		TryEquipItemInstanceById(InstanceId);
		return;
	}

	ServerRequestEquipItemInstanceById(InstanceId);
}

FProject_JEquipmentOperationResult UProject_JEquipmentManagerComponent::TryEquipItemInstanceById(FGuid InstanceId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return FProject_JEquipmentOperationResult::FailureResult(
			EProject_JEquipmentOperationFailure::NotAuthority,
			InstanceId);
	}

	UProject_JInventoryComponent* InventoryComponent = GetOwnerInventoryComponent();
	if (!InventoryComponent)
	{
		return FProject_JEquipmentOperationResult::FailureResult(
			EProject_JEquipmentOperationFailure::InventoryUnavailable,
			InstanceId);
	}

	FProject_JItemInstanceData AuthoritativeItem;
	if (!InventoryComponent->FindItemInstance(InstanceId, AuthoritativeItem))
	{
		return FProject_JEquipmentOperationResult::FailureResult(
			EProject_JEquipmentOperationFailure::ItemNotOwned,
			InstanceId);
	}

	return CommitEquipItemInstance(AuthoritativeItem, true);
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
	return FoundIndex != INDEX_NONE ? Cast<UProject_JEquipmentItemDefinition>(EquipmentArray.Items[FoundIndex].ItemDef) : nullptr;
}

TArray<UProject_JEquipmentItemDefinition*> UProject_JEquipmentManagerComponent::GetAllEquippedItems() const
{
	TArray<UProject_JEquipmentItemDefinition*> Result;
	for (const FProject_JEquipmentArrayItem& Item : EquipmentArray.Items)
	{
		if (UProject_JEquipmentItemDefinition* EquipDef = Cast<UProject_JEquipmentItemDefinition>(Item.ItemDef))
		{
			Result.Add(EquipDef);
		}
	}
	return Result;
}

FString UProject_JEquipmentManagerComponent::GetReplicationDiagnosticSummary() const
{
	// ArrayReplicationKey is transport bookkeeping and can legitimately differ between server/client.
	// Keep it visible in the summary, but exclude it from the payload-equivalence fingerprint.
	uint32 StateHash = 0;
	for (const FProject_JEquipmentArrayItem& Item : EquipmentArray.Items)
	{
		StateHash = HashCombineFast(StateHash, GetTypeHash(Item.ItemInstance.InstanceId));
		StateHash = HashCombineFast(StateHash, GetTypeHash(Item.Slot));
		StateHash = HashCombineFast(StateHash, GetTypeHash(GetNameSafe(Item.ItemDef)));
	}

	return FString::Printf(
		TEXT("Items=%d ArrayKey=%d StateHash=%08X"),
		EquipmentArray.Items.Num(),
		EquipmentArray.ArrayReplicationKey,
		StateHash);
}

FString UProject_JEquipmentManagerComponent::GetReplicationDiagnosticDeltaSummary() const
{
#if !UE_BUILD_SHIPPING
	return FString::Printf(
		TEXT("Add=%d Change=%d Remove=%d"),
		ReplicationDiagnosticAddedCount,
		ReplicationDiagnosticChangedCount,
		ReplicationDiagnosticRemovedCount);
#else
	return TEXT("Unavailable");
#endif
}

void UProject_JEquipmentManagerComponent::ResetReplicationDiagnosticDeltaCounters()
{
#if !UE_BUILD_SHIPPING
	ReplicationDiagnosticAddedCount = 0;
	ReplicationDiagnosticChangedCount = 0;
	ReplicationDiagnosticRemovedCount = 0;
#endif
}

void UProject_JEquipmentManagerComponent::ServerRequestEquipItemInstanceById_Implementation(FGuid InstanceId)
{
	const FProject_JEquipmentOperationResult Result = TryEquipItemInstanceById(InstanceId);
	if (!Result.bSucceeded)
	{
		UE_LOG(
			LogProjectJEquipment,
			Verbose,
			TEXT("Equipment request rejected. Owner=%s InstanceId=%s Slot=%d Reason=%s"),
			*GetNameSafe(GetOwner()),
			*InstanceId.ToString(),
			static_cast<int32>(Result.Slot),
			LexToString(Result.Failure));
	}
}

void UProject_JEquipmentManagerComponent::ServerRequestUnequipSlot_Implementation(EProject_JEquipmentSlot Slot)
{
	UnequipSlot(Slot);
}

bool UProject_JEquipmentManagerComponent::CanCommitEquipItemInstance(const FProject_JItemInstanceData& ItemInstance) const
{
	UProject_JEquipmentItemDefinition* EquipDef = Cast<UProject_JEquipmentItemDefinition>(ItemInstance.ItemDef);
	if (!EquipDef || EquipDef->EquipmentSlot == EProject_JEquipmentSlot::None)
	{
		return false;
	}

	if (ItemInstance.InstanceId.IsValid() && FindEquipmentIndexByInstanceId(ItemInstance.InstanceId) != INDEX_NONE)
	{
		return false;
	}

	return true;
}

EProject_JEquipmentOperationFailure UProject_JEquipmentManagerComponent::ValidateInventoryItemInstance(const FProject_JItemInstanceData& ItemInstance) const
{
	if (!ItemInstance.InstanceId.IsValid())
	{
		return EProject_JEquipmentOperationFailure::InvalidRequest;
	}

	const UProject_JInventoryComponent* InventoryComponent = GetOwnerInventoryComponent();
	if (!InventoryComponent)
	{
		return EProject_JEquipmentOperationFailure::InventoryUnavailable;
	}

	FProject_JItemInstanceData OwnedItemInstance;
	if (!InventoryComponent->FindItemInstance(ItemInstance.InstanceId, OwnedItemInstance))
	{
		return EProject_JEquipmentOperationFailure::ItemNotOwned;
	}

	if (OwnedItemInstance.ItemDef != ItemInstance.ItemDef || OwnedItemInstance.StackCount <= 0)
	{
		return EProject_JEquipmentOperationFailure::InvalidRequest;
	}

	if (OwnedItemInstance.bIsLocked)
	{
		return EProject_JEquipmentOperationFailure::ItemLocked;
	}

	return EProject_JEquipmentOperationFailure::None;
}

UProject_JInventoryComponent* UProject_JEquipmentManagerComponent::GetOwnerInventoryComponent() const
{
	const AActor* OwnerActor = GetOwner();
	return OwnerActor ? OwnerActor->FindComponentByClass<UProject_JInventoryComponent>() : nullptr;
}

bool UProject_JEquipmentManagerComponent::SetInventoryEquipmentLock(const FProject_JItemInstanceData& ItemInstance, bool bLocked) const
{
	if (!ItemInstance.InstanceId.IsValid())
	{
		return true;
	}

	UProject_JInventoryComponent* InventoryComponent = GetOwnerInventoryComponent();
	if (!InventoryComponent)
	{
		return false;
	}

	return InventoryComponent->SetItemInstanceLocked(ItemInstance.InstanceId, bLocked, bLocked);
}

FProject_JEquipmentOperationResult UProject_JEquipmentManagerComponent::CommitEquipItemInstance(
	const FProject_JItemInstanceData& ItemInstance,
	bool bRequireInventoryOwnership)
{
	UProject_JEquipmentItemDefinition* ItemDef = Cast<UProject_JEquipmentItemDefinition>(ItemInstance.ItemDef);
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return FProject_JEquipmentOperationResult::FailureResult(
			EProject_JEquipmentOperationFailure::NotAuthority,
			ItemInstance.InstanceId);
	}

	if (!ItemDef)
	{
		return FProject_JEquipmentOperationResult::FailureResult(
			EProject_JEquipmentOperationFailure::InvalidDefinition,
			ItemInstance.InstanceId);
	}

	if (ItemDef->EquipmentSlot == EProject_JEquipmentSlot::None)
	{
		return FProject_JEquipmentOperationResult::FailureResult(
			EProject_JEquipmentOperationFailure::InvalidSlot,
			ItemInstance.InstanceId);
	}

	if (!CanCommitEquipItemInstance(ItemInstance))
	{
		return FProject_JEquipmentOperationResult::FailureResult(
			FindEquipmentIndexByInstanceId(ItemInstance.InstanceId) != INDEX_NONE
				? EProject_JEquipmentOperationFailure::AlreadyEquipped
				: EProject_JEquipmentOperationFailure::InvalidRequest,
			ItemInstance.InstanceId,
			ItemDef->EquipmentSlot);
	}

	if (bRequireInventoryOwnership)
	{
		const EProject_JEquipmentOperationFailure InventoryFailure = ValidateInventoryItemInstance(ItemInstance);
		if (InventoryFailure != EProject_JEquipmentOperationFailure::None)
		{
			return FProject_JEquipmentOperationResult::FailureResult(
				InventoryFailure,
				ItemInstance.InstanceId,
				ItemDef->EquipmentSlot);
		}
	}

	const EProject_JEquipmentSlot Slot = ItemDef->EquipmentSlot;
	const int32 ConflictIndex = FindEquipmentIndexBySlot(Slot);
	bool bIncomingItemLocked = false;
	if (bRequireInventoryOwnership)
	{
		if (!SetInventoryEquipmentLock(ItemInstance, true))
		{
			return FProject_JEquipmentOperationResult::FailureResult(
				EProject_JEquipmentOperationFailure::InventoryLockFailed,
				ItemInstance.InstanceId,
				Slot);
		}
		bIncomingItemLocked = true;
	}

	// Lock the incoming item before mutating the occupied slot. If replacement
	// fails, restore the incoming inventory state and keep the previous item.
	if (ConflictIndex != INDEX_NONE && !RemoveEquipmentAt(ConflictIndex))
	{
		if (bIncomingItemLocked)
		{
			SetInventoryEquipmentLock(ItemInstance, false);
		}
		return FProject_JEquipmentOperationResult::FailureResult(
			EProject_JEquipmentOperationFailure::InvalidRequest,
			ItemInstance.InstanceId,
			Slot);
	}

	FProject_JEquipmentArrayItem& NewItem = EquipmentArray.Items.Add_GetRef(FProject_JEquipmentArrayItem());
	NewItem.ItemDef = ItemDef;
	NewItem.ItemInstance = ItemInstance;
	NewItem.Slot = Slot;

	EquipmentArray.MarkItemDirty(NewItem);
	BroadcastEquipmentEquipped(NewItem);

	return FProject_JEquipmentOperationResult::Success(ItemInstance.InstanceId, Slot);
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
	for (int32 Index = 0; Index < EquipmentArray.Items.Num(); ++Index)
	{
		if (EquipmentArray.Items[Index].Slot == Slot)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool UProject_JEquipmentManagerComponent::RemoveEquipmentAt(int32 Index)
{
	if (!EquipmentArray.Items.IsValidIndex(Index))
	{
		return false;
	}

	const FProject_JEquipmentArrayItem RemovedItem = EquipmentArray.Items[Index];
	SetInventoryEquipmentLock(RemovedItem.ItemInstance, false);
	BroadcastEquipmentUnequipped(RemovedItem);

	EquipmentArray.Items.RemoveAt(Index);
	EquipmentArray.MarkArrayDirty();
	return true;
}

void UProject_JEquipmentManagerComponent::OnRep_EquipmentAdded(FProject_JEquipmentArrayItem& Item)
{
#if !UE_BUILD_SHIPPING
	++ReplicationDiagnosticAddedCount;
#endif
	BroadcastEquipmentEquipped(Item);
}

void UProject_JEquipmentManagerComponent::OnRep_EquipmentChanged(FProject_JEquipmentArrayItem& Item)
{
#if !UE_BUILD_SHIPPING
	++ReplicationDiagnosticChangedCount;
#endif
	// Slot / Item changes on existing element
}

void UProject_JEquipmentManagerComponent::OnRep_EquipmentRemoved(FProject_JEquipmentArrayItem& Item)
{
#if !UE_BUILD_SHIPPING
	++ReplicationDiagnosticRemovedCount;
#endif
	BroadcastEquipmentUnequipped(Item);
}
