#pragma once

#include "Crafting/Project_JCraftingJobValues.h"
#include "Inventory/Project_JPersistentEdit.h"

class AProject_JPlayerState;
class FJsonObject;
class UProject_JCharacterDataSubsystem;

namespace ProjectJ::OnlineCrafting
{
bool ReadItem(const FJsonObject& Row, const TCHAR* Scope, FProject_JPersistentItem& Item);
bool ReadJobs(const FJsonObject& Snapshot, TArray<Crafting::FJobValues>& Jobs);
bool PrepareCommand(AProject_JPlayerState& Player, UProject_JCharacterDataSubsystem& Definitions,
	const FProject_JPersistentEdit& Edit, int64 Revision, FJsonObject& Command);
}
