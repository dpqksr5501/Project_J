#pragma once
#include "CoreMinimal.h"
#include "Crafting/Project_JCraftingRecipeValues.h"
#include "Inventory/Project_JPersistentItemValues.h"

namespace ProjectJ::Crafting
{
inline constexpr int32 MaxJobs = 8;
inline constexpr int32 MaxEscrowEntries = 64;
inline constexpr int32 MaxJobSeconds = 604800;
inline constexpr int32 MaxOwnedEntries = 4096;

/** Agreed production terms and owned input escrow, independent of any Avatar/station. */
struct FJobValues
{
	int32 SchemaVersion = 1;
	FGuid Id;
	FName RecipeId;
	int32 RecipeVersion = 1, Batches = 1;
	FString RulesDigest;
	int64 StartedUtcTicks = 0, DueUtcTicks = 0;
	TArray<FProject_JPersistentItem> Escrow;
	TArray<FResolvedLine> Outputs;
	bool operator==(const FJobValues& Other) const
	{
		if (SchemaVersion != Other.SchemaVersion || Id != Other.Id || RecipeId != Other.RecipeId ||
			RecipeVersion != Other.RecipeVersion || Batches != Other.Batches || RulesDigest != Other.RulesDigest ||
			StartedUtcTicks != Other.StartedUtcTicks || DueUtcTicks != Other.DueUtcTicks || Escrow != Other.Escrow ||
			Outputs.Num() != Other.Outputs.Num()) return false;
		for (int32 Index = 0; Index < Outputs.Num(); ++Index)
		{
			const auto& A = Outputs[Index]; const auto& B = Other.Outputs[Index];
			if (A.Definition != B.Definition || A.DefinitionVersion != B.DefinitionVersion || A.Level != B.Level ||
				A.CountPerBatch != B.CountPerBatch || A.MaxStackCount != B.MaxStackCount) return false;
		}
		return true;
	}
};

/** Shared structural/identity admission; canonical item definitions remain GT-owned.
 * Checks live+escrow ownership together without mutating values or allocating output items. */
PROJECT_JCHARACTER_API bool ValidateJobState(TConstArrayView<FProject_JPersistentItem> Items,
	TConstArrayView<FJobValues> Jobs);
}
