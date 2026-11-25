#pragma once

#include "Game/Project_JQuestComponent.h"
#include "Inventory/Project_JPersistentEdit.h"

class FJsonObject;
class UProject_JCharacterDataSubsystem;
class UProject_JProgressionComponent;

namespace ProjectJ::OnlineQuests
{
bool Prepare(const UProject_JQuestComponent& Quests, const FProject_JPersistentEdit& Edit, FJsonObject& Command);
bool PrepareClaim(const UProject_JQuestComponent& Quests, const UProject_JProgressionComponent& Progression,
	const UProject_JCharacterDataSubsystem& Definitions, FName Quest, int64 ProfileRevision, FJsonObject& Command);
bool PrepareProgress(const UProject_JQuestComponent& Quests, TConstArrayView<FProject_JQuestProgressInput> Inputs, FJsonObject& Command);
bool Read(const UProject_JQuestComponent& Quests, const FJsonObject& Reply, FProject_JQuestSnapshot& Out);
/** A validated STALE reply advances Count without publishing any old page IDs. */
bool ReadHistoryPage(const FJsonObject& Reply, int32 Ordinal, int32& Count, TArray<FName>& Ids);
}
