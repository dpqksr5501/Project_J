#pragma once
#include "Backend/Project_JLocalProfileSnapshot.h"

class UProject_JCharacterClassDefinition;
class UProject_JCharacterAdvancementDefinition;
class UProject_JProgressionComponent;
class UProject_JCharacterDataSubsystem;

namespace ProjectJ::LocalProfileRules
{
/** Null scope is the exact legacy all-catalog digest. New captures bind current detail IDs; schema3 completion history never requires retired definitions. */
FString Fingerprint(const UProject_JProgressionComponent& Progression, const FProject_JQuestDefinitionIndex& Index,
    const UProject_JCharacterClassDefinition* Class, const TArray<UProject_JCharacterAdvancementDefinition*>& History,
    const FProject_JQuestSnapshot* Scope);
bool Matches(const FProject_JLocalProfileSnapshot& Snapshot, const UProject_JProgressionComponent& Progression,
    const FProject_JQuestDefinitionIndex& Index, const UProject_JCharacterClassDefinition* Class,
    const TArray<UProject_JCharacterAdvancementDefinition*>& History);

/** Derived GT admission guard, not quest state or a worker. At most128 accepted IDs plus one digest.
 * A new accepted ID can extend the scope only after the preceding scope still matches current rules. */
struct FQuestGuard
{
    bool Capture(const FProject_JQuestDefinitionIndex& Index, const FProject_JQuestSnapshot& Snapshot,
        const UProject_JCharacterDataSubsystem* Data = nullptr);
    bool IsCurrent(const FProject_JQuestDefinitionIndex& Index, const UProject_JCharacterDataSubsystem* Data = nullptr) const;
private:
    TArray<FName> Ids;
    FString Digest;
    bool bHistoryScope = false;
};
}
