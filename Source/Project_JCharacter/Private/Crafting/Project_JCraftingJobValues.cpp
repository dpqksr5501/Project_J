#include "Crafting/Project_JCraftingJobValues.h"
#include "Misc/DateTime.h"
#include "Misc/Timespan.h"

namespace ProjectJ::Crafting
{
bool ValidateJobState(TConstArrayView<FProject_JPersistentItem> Items, TConstArrayView<FJobValues> Jobs)
{
	if (Jobs.Num() > MaxJobs || Items.Num() > MaxOwnedEntries) return false;
	int32 Owned = Items.Num();
	for (const auto& Job : Jobs)
	{
		if (Job.Escrow.IsEmpty() || Job.Escrow.Num() > MaxEscrowEntries ||
			Owned > MaxOwnedEntries - Job.Escrow.Num()) return false;
		Owned += Job.Escrow.Num();
	}
	TSet<FGuid> ItemIds, JobIds;
	ItemIds.Reserve(Owned); JobIds.Reserve(Jobs.Num());
	for (const auto& Item : Items)
	{
		if (!Item.Instance.IsValid() || ItemIds.Contains(Item.Instance)) return false;
		ItemIds.Add(Item.Instance);
	}
	for (const auto& Job : Jobs)
	{
		if (Job.SchemaVersion != 1 || !Job.Id.IsValid() || JobIds.Contains(Job.Id) ||
			Job.RecipeId.IsNone() || Job.RecipeId.ToString().Len() > 128 || Job.RecipeVersion < 1 ||
			Job.Batches < 1 || Job.Batches > 100 || Job.RulesDigest.Len() != 40 ||
			Job.StartedUtcTicks < 0 || Job.DueUtcTicks <= Job.StartedUtcTicks ||
			Job.DueUtcTicks > FDateTime::MaxValue().GetTicks()) return false;
		const int64 Duration = Job.DueUtcTicks - Job.StartedUtcTicks;
		if (Duration > int64(MaxJobSeconds) * ETimespan::TicksPerSecond ||
			Duration % ETimespan::TicksPerSecond != 0) return false;
		for (const TCHAR Digit : Job.RulesDigest) if (!FChar::IsHexDigit(Digit)) return false;
		JobIds.Add(Job.Id);
		TSet<FName> Outputs;
		if (Job.Outputs.IsEmpty() || Job.Outputs.Num() > 8) return false;
		for (const auto& Line : Job.Outputs)
		{
			if (Line.Definition.IsNone() || Line.Definition.ToString().Len() > 128 ||
				Line.DefinitionVersion < 1 || Line.Level < 1 || Line.Level > 10000 ||
				Line.CountPerBatch < 1 || Line.CountPerBatch > 1000000 || Line.MaxStackCount < 1 ||
				Outputs.Contains(Line.Definition)) return false;
			Outputs.Add(Line.Definition);
		}
		for (const auto& Item : Job.Escrow)
		{
			if (!Item.Instance.IsValid() || ItemIds.Contains(Item.Instance) || Item.Definition.IsNone() ||
				Item.Definition.ToString().Len() > 128 || Item.DefinitionVersion < 1 || Item.Level < 1 ||
				Item.Level > 10000 || Item.Count < 1 || !Item.bInBag || Item.Slot != EProject_JEquipmentSlot::None ||
				Item.Order < 0 || Item.Order == MAX_int32) return false;
			ItemIds.Add(Item.Instance);
		}
	}
	return true;
}
}
