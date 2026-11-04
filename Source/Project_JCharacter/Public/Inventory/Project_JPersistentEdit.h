#pragma once
#include "CoreMinimal.h"
#include "Equipment/Project_JEquipmentTypes.h"
class UProject_JItemDefinition;

/** GT-only command admission to the optional persistence consumer in the game
 * module. Pointer resolves to a stable ID before native I/O. Not a state owner. */
enum class EProject_JPersistentEdit : uint8
{
	Experience,
	Claim,
	Add,
	Stack,
	Remove,
	Equip,
	Unequip,
	Bag,
	Use,
	Class,
	Advancement,
	Craft,
	Gather,
	/** Historical name retained for receipt compatibility; accepts legacy and compound quests. */
	AcceptCompoundQuest,
	SetQuestTracking,
	StartCraftJob,
	CollectCraftJob,
	CancelCraftJob
};
struct FProject_JPersistentEdit
{
	EProject_JPersistentEdit Kind = EProject_JPersistentEdit::Experience;
	FGuid Request, Instance, Target;
	/** Saved job identity for collect/cancel. Start uses Request as the new job ID. */
	FGuid CraftingJob;
	UProject_JItemDefinition *Definition = nullptr;
	FName Quest, ProgressionDefinition;
	/** Keep this prepared logical intent unchanged when retrying/querying a receipt. */
	FName CraftingRecipe;
	int32 CraftingRecipeVersion = 1, CraftingBatches = 1;
	FString CraftingRulesDigest;
	/** SetQuestTracking uses the exact desired value 0 or 1, included in the existing intent bytes. */
	int64 Amount = 0;
	int32 Level = 1, Operation = 0, SourceCount = 0, TargetCount = 0, SourceOrder = INDEX_NONE,
		  TargetOrder = INDEX_NONE;
	EProject_JEquipmentSlot Slot = EProject_JEquipmentSlot::None;
};
using FProject_JPersistentCompletion = TFunction<void(bool /*confirmed*/, bool /*first application*/)>;
using FProject_JPersistentSubmit = TFunction<bool(const FProject_JPersistentEdit &, FProject_JPersistentCompletion)>;
