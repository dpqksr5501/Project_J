#include "Inventory/Project_JOwnedItemProjection.h"
#include "Backend/Project_JLocalProfileSnapshot.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JCraftingJobsComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Game/Project_JPlayerState.h"
#include "Engine/GameInstance.h"

namespace ProjectJ::OwnedItems
{
namespace
{
bool ResolveImpl(AProject_JPlayerState *Player, const FProject_JLocalProfileSnapshot &Snapshot,
				  TArray<FProject_JItemInstanceData> &Bag, TArray<FProject_JEquipmentArrayItem> &Slots,
				  bool bTrustedApply, FString *Failure, const TArray<FProject_JItemInstanceData>* NativeBefore)
{
    check(IsInGameThread());
    if (!IsValid(Player) || Player->IsActorBeingDestroyed() || !Player->GetGameInstance()) return false;
	if ((Snapshot.Format != 2 && Snapshot.Format != 3 && Snapshot.Format != 4) || !Snapshot.Quests.HasHydratedHistory() ||
        (Snapshot.Format < 3 && !Snapshot.CraftingJobs.IsEmpty()))
		return false;
	auto *Data = Player->GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
	auto *Inventory = Player->GetInventoryComponent();
	auto *Equipment = Player->GetEquipmentManagerComponent();
	auto* JobOwner = Player->GetCraftingJobsComponent();
	if (!Data || !Inventory || !Equipment || !JobOwner || !JobOwner->ValidateCommittedJobs(Snapshot.CraftingJobs) ||
		!ProjectJ::Crafting::ValidateJobState(Snapshot.Items, Snapshot.CraftingJobs)) return false;
	// Refund/restore validates escrow, not today's recipe/output catalog.
	for (const auto& Job : Snapshot.CraftingJobs)
		for (const auto& Item : Job.Escrow)
		{
			const auto* Def = Data->GetItemDefinition(Item.Definition, Item.DefinitionVersion);
			if (!Def || Def->MaxStackCount < 1 || Item.Count > Def->MaxStackCount) return false;
		}
    TMap<FGuid, const FProject_JItemInstanceData*> BeforeById;
    if (NativeBefore) for (const auto& Item : *NativeBefore)
    {
        if (BeforeById.Contains(Item.InstanceId)) return false;
        BeforeById.Add(Item.InstanceId, &Item);
    }
	for (const auto &Value : Snapshot.Items)
	{
		auto *Def = Data->GetItemDefinition(Value.Definition, Value.DefinitionVersion);
		if (!Def)
		{
			if (Failure)
				*Failure = FString::Printf(TEXT("아이템 정의 ID/version 불일치: %s v%d. 저장 원형을 보존하세요"),
										   *Value.Definition.ToString(), Value.DefinitionVersion);
			return false;
		}
		FProject_JItemInstanceData Item;
		Item.InstanceId = Value.Instance;
		Item.ItemDef = Def;
		Item.ItemLevel = Value.Level;
		Item.StackCount = Value.Count;
		Item.BagOrder = Value.Order;
		Item.bIsLocked = Item.bIsEquipped = Value.bInBag && Value.Slot != EProject_JEquipmentSlot::None;
        if (NativeBefore && Value.bInBag)
        {
            if (const auto* Found = BeforeById.Find(Value.Instance))
            {
                const auto& Before = **Found;
                if (Before.ItemDef != Def || Before.bIsEquipped != (Value.Slot != EProject_JEquipmentSlot::None)) return false;
                Item.bIsLocked = Before.bIsLocked; Item.bIsEquipped = Before.bIsEquipped;
            }
            else if (Value.Slot != EProject_JEquipmentSlot::None) return false;
        }
		if (Value.bInBag)
			Bag.Add(Item);
		if (Value.Slot != EProject_JEquipmentSlot::None)
		{
			auto &Entry = Slots.AddDefaulted_GetRef();
			Entry.ItemDef = Cast<UProject_JEquipmentItemDefinition>(Def);
			Entry.ItemInstance = Item;
			Entry.Slot = Value.Slot;
		}
	}
	return (NativeBefore ? Inventory->ValidateNativeRewardItems(Bag, *NativeBefore) : Inventory->ValidatePersistentItems(Bag, bTrustedApply)) &&
		   Equipment->ValidatePersistentEquipment(Slots, Bag, bTrustedApply);
}
}
bool ResolveItems(AProject_JPlayerState* Player, const FProject_JLocalProfileSnapshot& Snapshot,
    TArray<FProject_JItemInstanceData>& Bag, TArray<FProject_JEquipmentArrayItem>& Slots,
    bool bTrustedApply, FString* Failure)
{
    TArray<FProject_JItemInstanceData> ProposedBag; TArray<FProject_JEquipmentArrayItem> ProposedSlots;
    if (!ResolveImpl(Player, Snapshot, ProposedBag, ProposedSlots, bTrustedApply, Failure, nullptr)) return false;
    Bag = MoveTemp(ProposedBag); Slots = MoveTemp(ProposedSlots); return true;
}
bool ResolveNativeGrant(AProject_JPlayerState* Player, const FProject_JLocalProfileSnapshot& Snapshot,
    const TArray<FProject_JItemInstanceData>& BeforeBag, TArray<FProject_JItemInstanceData>& Bag,
    TArray<FProject_JEquipmentArrayItem>& Slots, FString* Failure)
{
    TArray<FProject_JItemInstanceData> ProposedBag; TArray<FProject_JEquipmentArrayItem> ProposedSlots;
    if (!ResolveImpl(Player, Snapshot, ProposedBag, ProposedSlots, true, Failure, &BeforeBag)) return false;
    Bag = MoveTemp(ProposedBag); Slots = MoveTemp(ProposedSlots); return true;
}
bool CaptureItems(AProject_JPlayerState *Player, FProject_JLocalProfileSnapshot &Snapshot, TSet<FGuid>* Locked)
{
    check(IsInGameThread());
    if (!IsValid(Player) || Player->IsActorBeingDestroyed() || !Player->GetGameInstance()) return false;
    auto Proposed = Snapshot; Proposed.Items.Reset();
    TSet<FGuid> CapturedLocks;
	auto *Data = Player->GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
	auto *Inventory = Player->GetInventoryComponent();
	auto *Equipment = Player->GetEquipmentManagerComponent();
	auto* JobOwner = Player->GetCraftingJobsComponent();
	if (!Data || !Inventory || !Equipment || !JobOwner) return false;
	Proposed.CraftingJobs = JobOwner->CaptureJobs();

	for (const auto &Item : Inventory->GetItemInstances())
	{
		if (Locked && Item.bIsLocked) CapturedLocks.Add(Item.InstanceId);
		if (!Item.ItemDef || Data->GetItemDefinition(Item.ItemDef->ItemId, Item.ItemDef->SchemaVersion) != Item.ItemDef)
			return false;
		FProject_JPersistentItem Value;
		Value.Instance = Item.InstanceId;
		Value.Definition = Item.ItemDef->ItemId;
		Value.DefinitionVersion = Item.ItemDef->SchemaVersion;
		Value.Level = Item.ItemLevel;
		Value.Count = Item.StackCount;
		Value.Order = Item.BagOrder;
		Proposed.Items.Add(Value);
	}
	for (const auto &Entry : Equipment->CapturePersistentEquipment())
	{
		const auto &Item = Entry.ItemInstance;
		if (!Item.ItemDef || Data->GetItemDefinition(Item.ItemDef->ItemId, Item.ItemDef->SchemaVersion) != Item.ItemDef)
			return false;
		if (auto *Found = Proposed.Items.FindByPredicate([&](const auto &V) { return V.Instance == Item.InstanceId; }))
		{
			if (Found->Definition != Item.ItemDef->ItemId || Found->Level != Item.ItemLevel || Found->Count != 1 ||
				Found->Slot != EProject_JEquipmentSlot::None)
				return false;
			Found->Slot = Entry.Slot;
		}
		else
		{
			FProject_JPersistentItem Value;
			Value.Instance = Item.InstanceId;
			Value.Definition = Item.ItemDef->ItemId;
			Value.DefinitionVersion = Item.ItemDef->SchemaVersion;
			Value.Level = Item.ItemLevel;
			Value.Count = 1;
			Value.bInBag = false;
			Value.Slot = Entry.Slot;
			Proposed.Items.Add(Value);
		}
	}
	TArray<FProject_JItemInstanceData> Bag;
	TArray<FProject_JEquipmentArrayItem> Slots;
	if (!ResolveItems(Player, Proposed, Bag, Slots)) return false;
    Snapshot = MoveTemp(Proposed);
    if (Locked) *Locked = MoveTemp(CapturedLocks);
    return true;
}
}
