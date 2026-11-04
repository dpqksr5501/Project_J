#pragma once
#include "CoreMinimal.h"
#include "Equipment/Project_JEquipmentTypes.h"

/** Owned item values shared by checkpoints and crafting escrow. No live writer. */
struct FProject_JPersistentItem
{
	FGuid Instance;
	FName Definition;
	int32 DefinitionVersion = 1, Level = 1, Count = 1, Order = INDEX_NONE;
	EProject_JEquipmentSlot Slot = EProject_JEquipmentSlot::None;
	bool bInBag = true;
	bool operator==(const FProject_JPersistentItem& Other) const
	{
		return Instance == Other.Instance && Definition == Other.Definition && DefinitionVersion == Other.DefinitionVersion &&
			Level == Other.Level && Count == Other.Count && Order == Other.Order && Slot == Other.Slot && bInBag == Other.bInBag;
	}
};
