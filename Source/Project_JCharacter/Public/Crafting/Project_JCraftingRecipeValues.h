#pragma once
#include "CoreMinimal.h"

namespace ProjectJ::Crafting
{
/** Owned capture of one canonical item definition. No UObject or registry owner. */
struct FResolvedLine
{
	FName Definition;
	int32 DefinitionVersion = 1, Level = 1, CountPerBatch = 1, MaxStackCount = 1;
};
struct FRecipeValues
{
	FName RecipeId;
	int32 SchemaVersion = 1, MaxBatches = 1;
	int32 DurationSecondsPerBatch = 0;
	TArray<FResolvedLine> Inputs, Outputs;
};
}
