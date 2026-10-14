#pragma once

#include "UI/Project_JInventoryViewModel.h"
#include "Inventory/Project_JItemDefinition.h"

namespace ProjectJ::UI
{
inline const FLinearColor PanelColor(0.025f, 0.04f, 0.06f, 0.96f);
inline const FLinearColor ItemCellColor(0.07f, 0.10f, 0.13f, 1.0f);

inline FText ItemName(const UProject_JInventoryEntry* Entry)
{
	if (!Entry || !Entry->Item.ItemDef) { return NSLOCTEXT("ProjectJUI", "EmptySlot", "비어 있음"); }
	return Entry->Item.ItemDef->ItemName.IsEmpty() ? FText::FromName(Entry->Item.ItemDef->ItemId) : Entry->Item.ItemDef->ItemName;
}

inline void ActivateItem(UObject* Item)
{
	auto* Entry = Cast<UProject_JInventoryEntry>(Item);
	if (Entry && Entry->Model.IsValid()) { Entry->Model->Activate(Entry); }
}

inline FText EquipmentSlotLabel(EProject_JEquipmentSlot Slot)
{
	// Resolve one value instead of constructing ten FTexts and a TArray per row render.
	switch (Slot)
	{
	case EProject_JEquipmentSlot::Weapon: return NSLOCTEXT("ProjectJUI", "SlotWeapon", "무기");
	case EProject_JEquipmentSlot::Head: return NSLOCTEXT("ProjectJUI", "SlotHead", "머리");
	case EProject_JEquipmentSlot::Chest: return NSLOCTEXT("ProjectJUI", "SlotChest", "몸통");
	case EProject_JEquipmentSlot::Hands: return NSLOCTEXT("ProjectJUI", "SlotHands", "손");
	case EProject_JEquipmentSlot::Legs: return NSLOCTEXT("ProjectJUI", "SlotLegs", "다리");
	case EProject_JEquipmentSlot::Feet: return NSLOCTEXT("ProjectJUI", "SlotFeet", "발");
	case EProject_JEquipmentSlot::Accessory: return NSLOCTEXT("ProjectJUI", "SlotAccessory", "장신구");
	case EProject_JEquipmentSlot::Back: return NSLOCTEXT("ProjectJUI", "SlotBack", "등");
	case EProject_JEquipmentSlot::Mount: return NSLOCTEXT("ProjectJUI", "SlotMount", "탈것");
	default: return FText::GetEmpty();
	}
}
}
