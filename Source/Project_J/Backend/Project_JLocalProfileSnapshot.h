#pragma once
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Equipment/Project_JEquipmentTypes.h"
#include "Game/Project_JQuestComponent.h"

struct FProject_JPersistentItem
{
	FGuid Instance;
	FName Definition;
	int32 DefinitionVersion = 1, Level = 1, Count = 1, Order = INDEX_NONE;
	EProject_JEquipmentSlot Slot = EProject_JEquipmentSlot::None;
	bool bInBag = true;
};

/** One checkpoint, not a second live state owner. IDs and values only. */
struct FProject_JLocalProfileSnapshot
{
	FGuid Account, Character;
	FString RulesFingerprint;
	FProject_JProgressionSnapshot Progression;
	FProject_JQuestSnapshot Quests;
	/** Format 1 had no item ownership data and requires explicit migration. */
	int32 Format = 2;
	TArray<FProject_JPersistentItem> Items;
	bool Encode(TArray<uint8> &Out) const;
	static bool Decode(const TArray<uint8> &Bytes, FProject_JLocalProfileSnapshot &Out);
};
