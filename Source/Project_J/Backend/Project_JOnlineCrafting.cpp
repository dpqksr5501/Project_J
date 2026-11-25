#include "Backend/Project_JOnlineCrafting.h"

#include "Backend/Project_JOnlineWorldSubsystem.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Components/Project_JCraftingJobsComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Crafting/Project_JCraftingPlan.h"
#include "Crafting/Project_JCraftingRecipeDefinition.h"
#include "Dom/JsonObject.h"
#include "Game/Project_JPlayerState.h"
#include "Inventory/Project_JItemDefinition.h"

namespace ProjectJ::OnlineCrafting
{
namespace
{
bool ReadGuid(const FJsonObject& Row, const TCHAR* Name, FGuid& Out)
{
	FString Text;
	return Row.TryGetStringField(Name, Text) && FGuid::Parse(Text, Out) && Out.IsValid();
}

bool ReadName(const FJsonObject& Row, const TCHAR* Key, FName& Out, EFindName Mode = FNAME_Find)
{
	FString Text;
	if (!Row.TryGetStringField(Key, Text) || Text.IsEmpty() || Text.Len() > 128) return false;
	Out = FName(*Text, Mode);
	return !Out.IsNone();
}

TSharedRef<FJsonObject> WriteTerms(const Crafting::FJobValues& Job)
{
	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("recipe"), Job.RecipeId.ToString());
	Result->SetNumberField(TEXT("recipe_version"), Job.RecipeVersion);
	Result->SetNumberField(TEXT("batches"), Job.Batches);
	Result->SetStringField(TEXT("rules"), Job.RulesDigest);
	Result->SetStringField(TEXT("started_ticks"), LexToString(Job.StartedUtcTicks));
	Result->SetStringField(TEXT("due_ticks"), LexToString(Job.DueUtcTicks));
	TArray<TSharedPtr<FJsonValue>> Outputs;
	for (const auto& Line : Job.Outputs)
	{
		auto Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("definition"), Line.Definition.ToString());
		Out->SetNumberField(TEXT("version"), Line.DefinitionVersion);
		Out->SetNumberField(TEXT("level"), Line.Level);
		Out->SetNumberField(TEXT("count"), Line.CountPerBatch);
		Out->SetNumberField(TEXT("max_stack"), Line.MaxStackCount);
		Outputs.Add(MakeShared<FJsonValueObject>(Out));
	}
	Result->SetArrayField(TEXT("outputs"), MoveTemp(Outputs));
	return Result;
}

TSharedRef<FJsonObject> WriteItemChange(const FProject_JPersistentItem& Item, bool bEscrow)
{
	auto Row = MakeShared<FJsonObject>();
	Row->SetStringField(TEXT("id"), UProject_JOnlineWorldSubsystem::GuidText(Item.Instance));
	Row->SetStringField(TEXT("definition"), Item.Definition.ToString());
	Row->SetNumberField(TEXT("version"), Item.DefinitionVersion);
	Row->SetNumberField(TEXT("count"), Item.Count);
	Row->SetNumberField(TEXT("level"), Item.Level);
	Row->SetNumberField(TEXT("bag_order"), Item.Order);
	Row->SetBoolField(TEXT("escrow"), bEscrow);
	return Row;
}
}

bool ReadItem(const FJsonObject& Row, const TCHAR* Scope, FProject_JPersistentItem& Item)
{
	FString Owner;
	int32 Slot = 0;
	if (!Row.TryGetStringField(TEXT("scope"), Owner) || Owner != Scope
		|| !ReadGuid(Row, TEXT("id"), Item.Instance) || !ReadName(Row, TEXT("definition"), Item.Definition,
			Owner == TEXT("CRAFT_ESCROW") ? FNAME_Add : FNAME_Find)
		|| !Row.TryGetNumberField(TEXT("version"), Item.DefinitionVersion)
		|| !Row.TryGetNumberField(TEXT("count"), Item.Count)
		|| !Row.TryGetNumberField(TEXT("level"), Item.Level)
		|| !Row.TryGetNumberField(TEXT("order"), Item.Order)
		|| !Row.TryGetNumberField(TEXT("equipped_slot"), Slot) || Slot < 0 || Slot > int32(EProject_JEquipmentSlot::Mount)) return false;
	Item.Slot = static_cast<EProject_JEquipmentSlot>(Slot);
	return true;
}

bool ReadJobs(const FJsonObject& Snapshot, TArray<Crafting::FJobValues>& Jobs)
{
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (!Snapshot.TryGetArrayField(TEXT("crafting_jobs"), Rows) || Rows->Num() > Crafting::MaxJobs) return false;
	for (const auto& Value : *Rows)
	{
		const TSharedPtr<FJsonObject>* Row = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Escrow = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
		FString Started, Due;
		auto& Job = Jobs.AddDefaulted_GetRef();
		if (!Value.IsValid() || !Value->TryGetObject(Row) || !Row
			|| !ReadGuid(**Row, TEXT("id"), Job.Id) || !ReadName(**Row, TEXT("recipe"), Job.RecipeId, FNAME_Add)
			|| !(*Row)->TryGetNumberField(TEXT("recipe_version"), Job.RecipeVersion)
			|| !(*Row)->TryGetNumberField(TEXT("batches"), Job.Batches) || !(*Row)->TryGetStringField(TEXT("rules"), Job.RulesDigest)
			|| !(*Row)->TryGetStringField(TEXT("started_ticks"), Started) || !LexTryParseString(Job.StartedUtcTicks, *Started)
			|| !(*Row)->TryGetStringField(TEXT("due_ticks"), Due) || !LexTryParseString(Job.DueUtcTicks, *Due)
			|| !(*Row)->TryGetArrayField(TEXT("escrow"), Escrow) || Escrow->Num() > Crafting::MaxEscrowEntries
			|| !(*Row)->TryGetArrayField(TEXT("outputs"), Outputs) || Outputs->Num() > 8) return false;
		for (const auto& Item : *Escrow)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Item.IsValid() || !Item->TryGetObject(Object) || !Object || !ReadItem(**Object, TEXT("CRAFT_ESCROW"), Job.Escrow.AddDefaulted_GetRef()))
				return false;
		}
		for (const auto& Out : *Outputs)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			auto& Line = Job.Outputs.AddDefaulted_GetRef();
			if (!Out.IsValid() || !Out->TryGetObject(Object) || !Object || !ReadName(**Object, TEXT("definition"), Line.Definition, FNAME_Add)
				|| !(*Object)->TryGetNumberField(TEXT("version"), Line.DefinitionVersion)
				|| !(*Object)->TryGetNumberField(TEXT("level"), Line.Level)
				|| !(*Object)->TryGetNumberField(TEXT("count"), Line.CountPerBatch)
				|| !(*Object)->TryGetNumberField(TEXT("max_stack"), Line.MaxStackCount)) return false;
		}
	}
	return true;
}

bool PrepareCommand(AProject_JPlayerState& Player, UProject_JCharacterDataSubsystem& Definitions,
	const FProject_JPersistentEdit& Edit, int64 Revision, FJsonObject& Command)
{
	using Kind = EProject_JPersistentEdit;
	using namespace Crafting;
	const bool bImmediate = Edit.Kind == Kind::Craft;
	const bool bStart = Edit.Kind == Kind::StartCraftJob;
	const bool bCollect = Edit.Kind == Kind::CollectCraftJob;
	const bool bCancel = Edit.Kind == Kind::CancelCraftJob;
	const bool bGather = Edit.Kind == Kind::Gather;
	if (Revision <= 0 || (!bImmediate && !bStart && !bCollect && !bCancel && !bGather)) return false;
	const FGuid JobId = bStart ? Edit.Request : Edit.CraftingJob;
	FProject_JLocalProfileSnapshot Before, After;
	TSet<FGuid> Locked;
	TMap<FGuid, FProject_JPersistentItem> Original;
	TSet<FGuid> EscrowIds;
	auto* CraftingJobs = Player.GetCraftingJobsComponent();
	auto* Inventory = Player.GetInventoryComponent();
	if (!IsValid(CraftingJobs) || !IsValid(Inventory)) return false;
	Before.CraftingJobs = CraftingJobs->CaptureJobs();
	for (const auto& Live : Inventory->GetItemInstances())
	{
		if (!Live.IsValid()) return false;
		auto& Item = Before.Items.AddDefaulted_GetRef();
		Item.Instance = Live.InstanceId;
		Item.Definition = Live.ItemDef->ItemId;
		Item.DefinitionVersion = Live.ItemDef->SchemaVersion;
		Item.Count = Live.StackCount;
		Item.Level = Live.ItemLevel;
		Item.Order = Live.BagOrder;
		if (Live.bIsLocked) Locked.Add(Item.Instance);
		Original.Add(Item.Instance, Item);
	}
	// Equipped items stay in the same bag slot but are locked for consumption and output stacking.
	for (const auto& Job : Before.CraftingJobs)
	{
		if (Job.Id != JobId) continue;
		for (const auto& Item : Job.Escrow) { Original.Add(Item.Instance, Item); EscrowIds.Add(Item.Instance); }
	}

	EPlanResult Result = EPlanResult::InvalidRecipe;
	if (bGather)
	{
		const auto* Def = Edit.Definition;
		if (!Def || Def->ItemId.IsNone() || Edit.Amount < 1 || Edit.Amount > Def->MaxStackCount
			|| Definitions.GetItemDefinition(Def->ItemId, Def->SchemaVersion) != Def) return false;
		const FResolvedLine Grant{Def->ItemId, Def->SchemaVersion, Edit.Level, int32(Edit.Amount), Def->MaxStackCount};
		Command.SetStringField(TEXT("gather_item"), Def->ItemId.ToString());
		Command.SetNumberField(TEXT("gather_version"), Def->SchemaVersion);
		Command.SetNumberField(TEXT("gather_count"), Edit.Amount);
		if (!Edit.Quest.IsNone()) Command.SetStringField(TEXT("gather_event"), Edit.Quest.ToString());
		Result = PlanGrant({Grant}, Locked, Before, After);
	}
	else if (bImmediate || bStart)
	{
		FRecipeValues Recipe;
		FString Rules;
		const auto* Definition = Definitions.GetCraftingRecipe(Edit.CraftingRecipe, Edit.CraftingRecipeVersion);
		if (!Definition || !Definition->Capture(Definitions, Recipe, Rules) || Rules != Edit.CraftingRulesDigest) return false;
		Command.SetStringField(TEXT("recipe"), Recipe.RecipeId.ToString());
		Command.SetNumberField(TEXT("recipe_version"), Recipe.SchemaVersion);
		Command.SetNumberField(TEXT("batches"), Edit.CraftingBatches);
		Command.SetStringField(TEXT("rules"), Rules);
		Result = bStart ? StartJob(Recipe, Edit.CraftingBatches, Rules, JobId, FDateTime::UtcNow().GetTicks(), Locked, Before, After)
			: Plan(Recipe, Edit.CraftingBatches, Locked, Before, After);
	}
	else
	{
		const auto* Job = Before.CraftingJobs.FindByPredicate([&](const auto& Candidate) { return Candidate.Id == JobId; });
		if (!Job) return false;
		if (bCollect)
			for (const auto& Line : Job->Outputs)
			{
				const auto* Def = Definitions.GetItemDefinition(Line.Definition, Line.DefinitionVersion);
				if (!Def || Def->MaxStackCount != Line.MaxStackCount) return false;
			}
		if (bCancel)
			for (const auto& Item : Job->Escrow)
			{
				const auto* Def = Definitions.GetItemDefinition(Item.Definition, Item.DefinitionVersion);
				if (!Def || Item.Count > Def->MaxStackCount) return false;
			}
		Result = bCollect ? CollectJob(JobId, FDateTime::UtcNow().GetTicks(), Locked, Before, After)
			: CancelJob(JobId, Locked, Before, After);
	}
	if (Result != EPlanResult::Success) return false;
	TArray<TSharedPtr<FJsonValue>> Changes;
	for (const auto& Item : After.Items)
	{
		FProject_JPersistentItem Old;
		const bool bExisting = Original.RemoveAndCopyValue(Item.Instance, Old);
		if (!bExisting || EscrowIds.Contains(Item.Instance) || !(Old == Item))
			Changes.Add(MakeShared<FJsonValueObject>(WriteItemChange(Item, false)));
	}
	if (bStart)
	{
		const auto& Job = After.CraftingJobs.Last();
		Command.SetObjectField(TEXT("job_terms"), WriteTerms(Job));
		for (const auto& Item : Job.Escrow)
		{
			Original.Remove(Item.Instance);
			Changes.Add(MakeShared<FJsonValueObject>(WriteItemChange(Item, true)));
		}
	}
	for (auto& Pair : Original)
	{
		if (EscrowIds.Contains(Pair.Key)) continue; // PostgreSQL retires the collected escrow with its job.
		Pair.Value.Count = 0;
		Changes.Add(MakeShared<FJsonValueObject>(WriteItemChange(Pair.Value, false)));
	}
	if (Changes.Num() > 256) return false;
	Command.SetStringField(TEXT("expected_revision"), LexToString(Revision));
	Command.SetStringField(TEXT("kind"), bGather ? TEXT("gather") : bImmediate ? TEXT("craft") : bStart ? TEXT("start") : bCollect ? TEXT("collect") : TEXT("cancel"));
	if (!bImmediate && !bGather) Command.SetStringField(TEXT("job_id"), UProject_JOnlineWorldSubsystem::GuidText(JobId));
	Command.SetArrayField(TEXT("changes"), MoveTemp(Changes));
	return true;
}
}
