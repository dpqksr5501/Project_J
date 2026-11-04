#pragma once
#include "CoreMinimal.h"

class UProject_JQuestComponent;
class UProject_JQuestDefinition;
struct FProject_JQuestSnapshot;
namespace ProjectJ::Crafting { struct FResolvedLine; }

/** Synchronous native bundle only. Preflight is all-or-nothing; observers run after every owned value is installed. */
struct PROJECT_J_API FProject_JNativeQuestRewardCommit
{
    static bool Apply(UProject_JQuestComponent* Quest, UProject_JQuestDefinition* Definition,
        const FProject_JQuestSnapshot& Before, const FProject_JQuestSnapshot& After,
        const TArray<ProjectJ::Crafting::FResolvedLine>& FrozenItems);
};
