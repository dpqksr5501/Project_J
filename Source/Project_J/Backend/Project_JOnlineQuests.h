#pragma once

#include "Game/Project_JQuestComponent.h"
#include "Inventory/Project_JPersistentEdit.h"

class FJsonObject;

namespace ProjectJ::OnlineQuests
{
bool Prepare(const UProject_JQuestComponent& Quests, const FProject_JPersistentEdit& Edit, FJsonObject& Command);
bool PrepareProgress(const UProject_JQuestComponent& Quests, TConstArrayView<FProject_JQuestProgressInput> Inputs, FJsonObject& Command);
bool Read(const UProject_JQuestComponent& Quests, const FJsonObject& Reply, FProject_JQuestSnapshot& Out);
}
