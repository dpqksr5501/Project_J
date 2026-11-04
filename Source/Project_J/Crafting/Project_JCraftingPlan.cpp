#include "Crafting/Project_JCraftingPlan.h"
#include "Crafting/Project_JCraftingJobValues.h"

namespace ProjectJ::Crafting
{
namespace
{
bool ValidateLines(const TArray<FResolvedLine>& Lines, int32 Maximum)
{
	if (Lines.IsEmpty() || Lines.Num() > Maximum) return false;
	TSet<FName> Ids;
	for (const auto& Line : Lines)
	{
		if (Line.Definition.IsNone() || Line.DefinitionVersion < 1 ||
			Line.Level < 1 || Line.Level > 10000 || Line.CountPerBatch < 1 ||
			Line.CountPerBatch > 1000000 || Line.MaxStackCount < 1 ||
			Ids.Contains(Line.Definition)) return false;
		Ids.Add(Line.Definition);
	}
	return true;
}
bool ValidateRecipe(const FRecipeValues& Recipe, int32 Batches)
{
	return !Recipe.RecipeId.IsNone() && Recipe.SchemaVersion >= 1 && Recipe.MaxBatches >= 1 &&
		Recipe.MaxBatches <= 100 && Batches >= 1 && Batches <= Recipe.MaxBatches &&
		Recipe.DurationSecondsPerBatch >= 0 && Recipe.DurationSecondsPerBatch <= 86400 &&
		ValidateLines(Recipe.Inputs, 16) && ValidateLines(Recipe.Outputs, 8);
}
bool Matches(const FProject_JPersistentItem& Item, const FResolvedLine& Line)
{
	return Item.Definition == Line.Definition &&
		Item.DefinitionVersion == Line.DefinitionVersion && Item.Level == Line.Level;
}
bool PrepareInventory(const FProject_JLocalProfileSnapshot& Before,
	TSet<FGuid>& KnownIds, TArray<int32>& Ordered)
{
	if ((Before.Format != 2 && Before.Format != 3 && Before.Format != 4) || !Before.Quests.HasHydratedHistory() ||
		(Before.Format < 3 && !Before.CraftingJobs.IsEmpty()) ||
		!ValidateJobState(Before.Items, Before.CraftingJobs)) return false;
	TSet<int32> BagOrders;
	KnownIds.Reserve(Before.Items.Num()); BagOrders.Reserve(Before.Items.Num());
	Ordered.Reserve(Before.Items.Num());
	for (int32 Index = 0; Index < Before.Items.Num(); ++Index)
	{
		const auto& Item = Before.Items[Index];
		if (!Item.Instance.IsValid() || KnownIds.Contains(Item.Instance) ||
			Item.Definition.IsNone() || Item.DefinitionVersion < 1 || Item.Level < 1 ||
			Item.Level > 10000 || Item.Count < 1 ||
			(!Item.bInBag && Item.Slot == EProject_JEquipmentSlot::None)) return false;
		KnownIds.Add(Item.Instance);
		if (Item.bInBag)
		{
			if (Item.Order < 0 || Item.Order == MAX_int32 || BagOrders.Contains(Item.Order)) return false;
			BagOrders.Add(Item.Order); Ordered.Add(Index);
		}
	}
	for (const auto& Job : Before.CraftingJobs)
		for (const auto& Item : Job.Escrow) KnownIds.Add(Item.Instance);
	Ordered.Sort([&](int32 Left, int32 Right) { return Before.Items[Left].Order < Before.Items[Right].Order; });
	return true;
}
bool MakeItemId(TSet<FGuid>& KnownIds, FGuid& Out)
{
	Out = FGuid::NewGuid();
	if (!Out.IsValid() || KnownIds.Contains(Out)) return false;
	KnownIds.Add(Out);
	return true;
}
int32 EscrowEntries(const TArray<FJobValues>& Jobs)
{
	int32 Count = 0;
	for (const auto& Job : Jobs) Count += Job.Escrow.Num();
	return Count;
}
EPlanResult ConsumeInputs(const FRecipeValues& Recipe, int32 Batches,
	const TSet<FGuid>& LockedInstances, FProject_JLocalProfileSnapshot& Proposed,
	const TArray<int32>& Ordered, TSet<FGuid>& KnownIds, TArray<FProject_JPersistentItem>* Escrow)
{
	TMap<FName, int32> InputById;
	TArray<int64> Needed, LockedAvailable;
	for (int32 Index = 0; Index < Recipe.Inputs.Num(); ++Index)
	{
		InputById.Add(Recipe.Inputs[Index].Definition, Index);
		Needed.Add(int64(Recipe.Inputs[Index].CountPerBatch) * Batches);
		LockedAvailable.Add(0);
	}
	// Equipment contributes only to the locked-material explanation, never consumption.
	for (const auto& Item : Proposed.Items)
	{
		if (Item.bInBag) continue;
		const int32* LineIndex = InputById.Find(Item.Definition);
		if (LineIndex && Matches(Item, Recipe.Inputs[*LineIndex])) LockedAvailable[*LineIndex] += Item.Count;
	}
	TArray<int32, TInlineAllocator<MaxEscrowEntries>> SplitEscrow;
	for (const int32 Index : Ordered)
	{
		auto& Item = Proposed.Items[Index];
		const int32* LineIndex = InputById.Find(Item.Definition);
		if (!LineIndex || !Matches(Item, Recipe.Inputs[*LineIndex])) continue;
		if (Item.Count > Recipe.Inputs[*LineIndex].MaxStackCount) return EPlanResult::InvalidInventory;
		if (Item.Slot != EProject_JEquipmentSlot::None || LockedInstances.Contains(Item.Instance))
		{
			LockedAvailable[*LineIndex] += Item.Count;
			continue;
		}
		const int32 Taken = int32(FMath::Min(Needed[*LineIndex], int64(Item.Count)));
		if (Escrow && Taken > 0)
		{
			if (Escrow->Num() >= MaxEscrowEntries) return EPlanResult::JobCapacity;
			const int32 EscrowIndex = Escrow->Add(Item);
			(*Escrow)[EscrowIndex].Count = Taken;
			if (Taken != Item.Count) SplitEscrow.Add(EscrowIndex);
		}
		Item.Count -= Taken; Needed[*LineIndex] -= Taken;
	}
	bool bMissing = false, bAllMissingCoveredByLocks = true;
	for (int32 Index = 0; Index < Needed.Num(); ++Index)
	{
		if (Needed[Index] <= 0) continue;
		bMissing = true;
		bAllMissingCoveredByLocks &= LockedAvailable[Index] >= Needed[Index];
	}
	if (bMissing) return bAllMissingCoveredByLocks ? EPlanResult::LockedMaterials : EPlanResult::MissingMaterials;
	// A full move keeps its identity; only a split receives a distinct escrow UUID.
	// Generate after material admission and only in the caller's receipt-miss plan.
	for (const int32 Index : SplitEscrow)
		if (!MakeItemId(KnownIds, (*Escrow)[Index].Instance)) return EPlanResult::InvalidInventory;
	return EPlanResult::Success;
}
EPlanResult PackOutputs(const TArray<FResolvedLine>& Outputs, int32 Batches,
	const TSet<FGuid>& LockedInstances, FProject_JLocalProfileSnapshot& Proposed,
	const TArray<int32>& Ordered, TSet<FGuid>& KnownIds)
{
	TMap<FName, int32> OutputById;
	TArray<int64> Remaining;
	for (int32 Index = 0; Index < Outputs.Num(); ++Index)
	{
		OutputById.Add(Outputs[Index].Definition, Index);
		Remaining.Add(int64(Outputs[Index].CountPerBatch) * Batches);
	}
	// Only surviving, unlocked fungible stacks receive output. Never resurrect a
	// fully consumed identity, merge into equipment, or borrow another job's escrow.
	for (const int32 Index : Ordered)
	{
		auto& Item = Proposed.Items[Index];
		if (Item.Count == 0 || Item.Slot != EProject_JEquipmentSlot::None || LockedInstances.Contains(Item.Instance)) continue;
		const int32* LineIndex = OutputById.Find(Item.Definition);
		if (!LineIndex || !Matches(Item, Outputs[*LineIndex])) continue;
		const auto& Line = Outputs[*LineIndex];
		if (Item.Count > Line.MaxStackCount) return EPlanResult::InvalidInventory;
		const int32 Added = int32(FMath::Min(Remaining[*LineIndex], int64(Line.MaxStackCount) - Item.Count));
		Item.Count += Added; Remaining[*LineIndex] -= Added;
	}
	int32 LiveEntries = 0;
	int64 NextOrder = 0, NewEntries = 0;
	for (const auto& Item : Proposed.Items)
	{
		if (Item.Count == 0) continue;
		++LiveEntries;
		if (Item.bInBag) NextOrder = FMath::Max(NextOrder, int64(Item.Order) + 1);
	}
	for (int32 Index = 0; Index < Remaining.Num(); ++Index)
	{
		const int64 Maximum = Outputs[Index].MaxStackCount;
		NewEntries += (Remaining[Index] + Maximum - 1) / Maximum;
	}
	// Bound final ownership before any output GUID/allocation loop. Other saved
	// jobs remain owners even though they are not visible in the bag.
	if (int64(LiveEntries) + NewEntries + EscrowEntries(Proposed.CraftingJobs) > MaxOwnedEntries ||
		(NewEntries > 0 && NextOrder + NewEntries > MAX_int32)) return EPlanResult::BagFull;
	Proposed.Items.RemoveAll([](const auto& Item) { return Item.Count == 0; });
	Proposed.Items.Reserve(LiveEntries + int32(NewEntries));
	for (int32 Index = 0; Index < Remaining.Num(); ++Index)
	{
		const auto& Line = Outputs[Index];
		while (Remaining[Index] > 0)
		{
			FProject_JPersistentItem Item;
			if (!MakeItemId(KnownIds, Item.Instance)) return EPlanResult::InvalidInventory;
			Item.Definition = Line.Definition; Item.DefinitionVersion = Line.DefinitionVersion;
			Item.Level = Line.Level; Item.Count = int32(FMath::Min(Remaining[Index], int64(Line.MaxStackCount)));
			Item.Order = int32(NextOrder++); Remaining[Index] -= Item.Count;
			Proposed.Items.Add(MoveTemp(Item));
		}
	}
	return EPlanResult::Success;
}
bool IsUtcTime(int64 Ticks) { return Ticks >= 0 && Ticks <= FDateTime::MaxValue().GetTicks(); }
}

EPlanResult PlanGrant(const TArray<FResolvedLine>& Outputs, const TSet<FGuid>& LockedInstances,
	const FProject_JLocalProfileSnapshot& Before, FProject_JLocalProfileSnapshot& Out)
{
	if (!ValidateLines(Outputs, 8)) return EPlanResult::InvalidRecipe;
	TSet<FGuid> KnownIds; TArray<int32> Ordered;
	if (!PrepareInventory(Before, KnownIds, Ordered)) return EPlanResult::InvalidInventory;
	auto Proposed = Before;
	const auto Packed = PackOutputs(Outputs, 1, LockedInstances, Proposed, Ordered, KnownIds);
	if (Packed != EPlanResult::Success) return Packed;
	Out = MoveTemp(Proposed);
	return EPlanResult::Success;
}

EPlanResult Plan(const FRecipeValues& Recipe, int32 Batches,
	const TSet<FGuid>& LockedInstances, const FProject_JLocalProfileSnapshot& Before,
	FProject_JLocalProfileSnapshot& Out)
{
	if (!ValidateRecipe(Recipe, Batches) || Recipe.DurationSecondsPerBatch != 0) return EPlanResult::InvalidRecipe;
	TSet<FGuid> KnownIds; TArray<int32> Ordered;
	if (!PrepareInventory(Before, KnownIds, Ordered)) return EPlanResult::InvalidInventory;
	auto Proposed = Before;
	const auto Consumed = ConsumeInputs(Recipe, Batches, LockedInstances, Proposed, Ordered, KnownIds, nullptr);
	if (Consumed != EPlanResult::Success) return Consumed;
	const auto Packed = PackOutputs(Recipe.Outputs, Batches, LockedInstances, Proposed, Ordered, KnownIds);
	if (Packed != EPlanResult::Success) return Packed;
	Out = MoveTemp(Proposed);
	return EPlanResult::Success;
}

EPlanResult StartJob(const FRecipeValues& Recipe, int32 Batches, const FString& RulesDigest,
	const FGuid& JobId, int64 NowUtcTicks, const TSet<FGuid>& LockedInstances,
	const FProject_JLocalProfileSnapshot& Before, FProject_JLocalProfileSnapshot& Out)
{
	if (!ValidateRecipe(Recipe, Batches) || Recipe.DurationSecondsPerBatch <= 0) return EPlanResult::InvalidRecipe;
	const int64 Seconds = int64(Recipe.DurationSecondsPerBatch) * Batches;
	if (Seconds > MaxJobSeconds || !IsUtcTime(NowUtcTicks) ||
		NowUtcTicks > FDateTime::MaxValue().GetTicks() - Seconds * ETimespan::TicksPerSecond) return EPlanResult::InvalidTime;
	TSet<FGuid> KnownIds; TArray<int32> Ordered;
	if (!PrepareInventory(Before, KnownIds, Ordered) || !JobId.IsValid() || RulesDigest.Len() != 40) return EPlanResult::InvalidInventory;
	for (const TCHAR Digit : RulesDigest) if (!FChar::IsHexDigit(Digit)) return EPlanResult::InvalidInventory;
	for (const auto& Job : Before.CraftingJobs) if (Job.Id == JobId) return EPlanResult::InvalidInventory;
	if (Before.CraftingJobs.Num() >= MaxJobs) return EPlanResult::JobCapacity;
	auto Proposed = Before;
	FJobValues Job;
	Job.Id = JobId; Job.RecipeId = Recipe.RecipeId; Job.RecipeVersion = Recipe.SchemaVersion;
	Job.Batches = Batches; Job.RulesDigest = RulesDigest; Job.Outputs = Recipe.Outputs;
	Job.StartedUtcTicks = NowUtcTicks; Job.DueUtcTicks = NowUtcTicks + Seconds * ETimespan::TicksPerSecond;
	const auto Consumed = ConsumeInputs(Recipe, Batches, LockedInstances, Proposed, Ordered, KnownIds, &Job.Escrow);
	if (Consumed != EPlanResult::Success) return Consumed;
	Proposed.Items.RemoveAll([](const auto& Item) { return Item.Count == 0; });
	if (Proposed.Items.Num() + EscrowEntries(Proposed.CraftingJobs) + Job.Escrow.Num() > MaxOwnedEntries) return EPlanResult::BagFull;
	Proposed.CraftingJobs.Add(MoveTemp(Job));
	Proposed.Format = FMath::Max(3, Before.Format); // Preserve a paged history while promoting older timed-job ownership.
	if (!ValidateJobState(Proposed.Items, Proposed.CraftingJobs)) return EPlanResult::InvalidInventory;
	Out = MoveTemp(Proposed);
	return EPlanResult::Success;
}

EPlanResult CollectJob(const FGuid& JobId, int64 NowUtcTicks,
	const TSet<FGuid>& LockedInstances, const FProject_JLocalProfileSnapshot& Before,
	FProject_JLocalProfileSnapshot& Out)
{
	TSet<FGuid> KnownIds; TArray<int32> Ordered;
	if (!PrepareInventory(Before, KnownIds, Ordered)) return EPlanResult::InvalidInventory;
	const int32 Index = Before.CraftingJobs.IndexOfByPredicate([&](const FJobValues& Job) { return Job.Id == JobId; });
	if (Index == INDEX_NONE) return EPlanResult::JobNotFound;
	if (!IsUtcTime(NowUtcTicks)) return EPlanResult::InvalidTime;
	const auto& Job = Before.CraftingJobs[Index];
	if (NowUtcTicks < Job.DueUtcTicks) return EPlanResult::NotReady;
	auto Proposed = Before;
	Proposed.CraftingJobs.RemoveAt(Index);
	const auto Packed = PackOutputs(Job.Outputs, Job.Batches, LockedInstances, Proposed, Ordered, KnownIds);
	if (Packed != EPlanResult::Success) return Packed;
	if (!ValidateJobState(Proposed.Items, Proposed.CraftingJobs)) return EPlanResult::InvalidInventory;
	Out = MoveTemp(Proposed);
	return EPlanResult::Success;
}

EPlanResult CancelJob(const FGuid& JobId, const TSet<FGuid>& LockedInstances,
	const FProject_JLocalProfileSnapshot& Before, FProject_JLocalProfileSnapshot& Out)
{
	TSet<FGuid> KnownIds; TArray<int32> Ordered;
	if (!PrepareInventory(Before, KnownIds, Ordered)) return EPlanResult::InvalidInventory;
	const int32 Index = Before.CraftingJobs.IndexOfByPredicate([&](const FJobValues& Job) { return Job.Id == JobId; });
	if (Index == INDEX_NONE) return EPlanResult::JobNotFound;
	const auto& Job = Before.CraftingJobs[Index];
	int64 NextOrder = 0;
	for (const auto& Item : Before.Items) if (Item.bInBag) NextOrder = FMath::Max(NextOrder, int64(Item.Order) + 1);
	if (NextOrder + Job.Escrow.Num() > MAX_int32) return EPlanResult::BagFull;
	auto Proposed = Before;
	// Refund each escrow identity intact, apart from its current bag position.
	// Existing locked/equipped stacks are neither consumed nor merged into.
	(void)LockedInstances;
	for (auto Item : Job.Escrow)
	{
		Item.Order = int32(NextOrder++);
		Proposed.Items.Add(MoveTemp(Item));
	}
	Proposed.CraftingJobs.RemoveAt(Index);
	if (!ValidateJobState(Proposed.Items, Proposed.CraftingJobs)) return EPlanResult::InvalidInventory;
	Out = MoveTemp(Proposed);
	return EPlanResult::Success;
}
}
