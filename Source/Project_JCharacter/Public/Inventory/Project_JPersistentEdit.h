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
	Advancement
};
struct FProject_JPersistentEdit
{
	EProject_JPersistentEdit Kind = EProject_JPersistentEdit::Experience;
	FGuid Request, Instance, Target;
	UProject_JItemDefinition *Definition = nullptr;
	FName Quest, ProgressionDefinition;
	int64 Amount = 0;
	int32 Level = 1, Operation = 0, SourceCount = 0, TargetCount = 0, SourceOrder = INDEX_NONE,
		  TargetOrder = INDEX_NONE;
	EProject_JEquipmentSlot Slot = EProject_JEquipmentSlot::None;
};
using FProject_JPersistentCompletion = TFunction<void(bool /*confirmed*/, bool /*first application*/)>;
using FProject_JPersistentSubmit = TFunction<bool(const FProject_JPersistentEdit &, FProject_JPersistentCompletion)>;
