#pragma once
#include "CoreMinimal.h"
#include "Backend/Project_JLocalProfileSnapshot.h"
#include "Crafting/Project_JCraftingRecipeValues.h"

namespace ProjectJ::Crafting
{
enum class EPlanResult : uint8
{
	Success, InvalidRecipe, InvalidInventory, MissingMaterials, LockedMaterials, BagFull,
	JobNotFound, NotReady, JobCapacity, InvalidTime
};
/** Specific crafting value planner. No live writer, registry, IO, callbacks or
 * authority bypass. Caller validates the canonical prepared intent and commits
 * this proposed snapshot through the existing profile transaction. Out remains
 * unchanged on failure. Serialization byte limits are still checked by Encode. */
EPlanResult Plan(const FRecipeValues& Recipe, int32 Batches,
	const TSet<FGuid>& LockedInstances, const FProject_JLocalProfileSnapshot& Before,
	FProject_JLocalProfileSnapshot& Out);

/** One bounded output bundle, using the exact crafting output packer. No input recipe fiction.
 * Frozen canonical values1..8, fixed batch1; output GUIDs belong in the caller's receipt-miss plan. */
EPlanResult PlanGrant(const TArray<FResolvedLine>& Outputs, const TSet<FGuid>& LockedInstances,
	const FProject_JLocalProfileSnapshot& Before, FProject_JLocalProfileSnapshot& Out);

EPlanResult StartJob(const FRecipeValues& Recipe, int32 Batches, const FString& RulesDigest,
	const FGuid& JobId, int64 NowUtcTicks, const TSet<FGuid>& LockedInstances,
	const FProject_JLocalProfileSnapshot& Before, FProject_JLocalProfileSnapshot& Out);
EPlanResult CollectJob(const FGuid& JobId, int64 NowUtcTicks,
	const TSet<FGuid>& LockedInstances, const FProject_JLocalProfileSnapshot& Before,
	FProject_JLocalProfileSnapshot& Out);
EPlanResult CancelJob(const FGuid& JobId, const TSet<FGuid>& LockedInstances,
	const FProject_JLocalProfileSnapshot& Before, FProject_JLocalProfileSnapshot& Out);
}
