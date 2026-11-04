#pragma once
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Crafting/Project_JCraftingJobValues.h"
#include "Inventory/Project_JPersistentItemValues.h"
#include "Game/Project_JQuestComponent.h"

struct FProject_JStoredProfileCore;

/** One fully hydrated checkpoint, not a second live state owner. IDs and values only. */
struct FProject_JLocalProfileSnapshot
{
	FGuid Account, Character;
	FString RulesFingerprint;
	FProject_JProgressionSnapshot Progression;
	FProject_JQuestSnapshot Quests;
	/** Format 1 had no item ownership data and requires explicit migration. */
	int32 Format = 3;
	TArray<FProject_JPersistentItem> Items;
	/** Persistent escrow and agreed terms, never a second live state writer. */
	TArray<ProjectJ::Crafting::FJobValues> CraftingJobs;
	bool Encode(TArray<uint8> &Out) const;
	/** Legacy inline / empty-history documents only. Nonempty Format4 requires the storage reader. */
	static bool Decode(const TArray<uint8> &Bytes, FProject_JLocalProfileSnapshot &Out);
	/** Explicit metadata-only decode. Positive history heads deliberately have no attached history value. */
	static bool DecodeStoredCore(const TArray<uint8>& Bytes, FProject_JStoredProfileCore& Out);
};

/** Never publish HotValues as a ready profile until its history is hydrated and verified. */
struct FProject_JStoredProfileCore
{
	FProject_JLocalProfileSnapshot HotValues;
};
