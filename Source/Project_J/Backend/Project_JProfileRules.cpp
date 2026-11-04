#include "Backend/Project_JProfileRules.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryWriter.h"

namespace ProjectJ::LocalProfileRules
{
namespace
{
bool Select(const FProject_JQuestDefinitionIndex& Index, const TArray<FName>* Ids, TArray<UProject_JQuestDefinition*>& Out)
{
    if (Ids)
    {
        if (Ids->Num() > 128) return false;
        TSet<FName> Seen;
        for (FName Id : *Ids)
        {
            auto* Def = Index.FindRef(Id);
            if (!Def || Seen.Contains(Id)) return false;
            Seen.Add(Id); Out.Add(Def);
        }
    }
    else
    {
        for (const auto& Pair : Index) { if (!Pair.Value) return false; Out.Add(Pair.Value); }
    }
    Out.Sort([](const auto& A, const auto& B) { return A.QuestId.LexicalLess(B.QuestId); });
    return true;
}
void AppendLegacy(FArchive& Writer, const TArray<UProject_JQuestDefinition*>& Definitions)
{
    for (const auto* Def : Definitions)
    {
        FString Id = Def->QuestId.ToString().ToLower(), Event = Def->ObjectiveEvent.ToString().ToLower();
        int32 Version = Def->SchemaVersion, Required = Def->RequiredCount;
        int64 Reward = Def->RewardExperience; bool Travel = Def->bTravelObjective;
        Writer << Id << Version << Event << Required << Reward << Travel;
    }
}
bool HasCompound(const TArray<UProject_JQuestDefinition*>& Definitions)
{ return Definitions.ContainsByPredicate([](const auto* Def) { return !Def->Objectives.IsEmpty(); }); }
bool AppendItemRewards(FArchive& Writer, const TArray<UProject_JQuestDefinition*>& Definitions,
    const UProject_JCharacterDataSubsystem* Data)
{
    int32 Bundles = 0;
    for (const auto* Definition : Definitions) if (!Definition->RewardItems.IsEmpty()) ++Bundles;
    if (!Bundles) return true; // Preserve the exact original bytes for every empty-bundle scope.
    FString Domain = TEXT("ProjectJ.ProfileQuestItemRewards.1"); Writer << Domain << Bundles;
    for (const auto* Definition : Definitions)
    {
        if (Definition->RewardItems.IsEmpty()) continue;
        TArray<ProjectJ::Crafting::FResolvedLine> Items;
        if (!Definition->CaptureRewardItems(Data, Items)) return false;
        FString Quest = Definition->QuestId.ToString().ToLower(); int32 Count = Items.Num(); Writer << Quest << Count;
        for (const auto& Item : Items)
        {
            FString Id = Item.Definition.ToString().ToLower();
            int32 Version = Item.DefinitionVersion, Level = Item.Level, Quantity = Item.CountPerBatch, Stack = Item.MaxStackCount;
            Writer << Id << Version << Level << Quantity << Stack;
        }
    }
    return true;
}
bool Append(FArchive& Writer, const TArray<UProject_JQuestDefinition*>& Definitions,
    const UProject_JCharacterDataSubsystem* Data)
{
    if (!HasCompound(Definitions)) { AppendLegacy(Writer, Definitions); return AppendItemRewards(Writer, Definitions, Data); }
    for (const auto* Def : Definitions)
    {
        FString Id = Def->QuestId.ToString().ToLower(); int32 Version = Def->SchemaVersion;
        int64 Reward = Def->RewardExperience; bool Compound = !Def->Objectives.IsEmpty();
        Writer << Id << Version << Reward << Compound;
        if (!Compound)
        {
            FString Event = Def->ObjectiveEvent.ToString().ToLower(); int32 Required = Def->RequiredCount; bool Travel = Def->bTravelObjective;
            Writer << Event << Required << Travel;
        }
        else
        {
            auto Ordered = Def->Objectives;
            Ordered.Sort([](const auto& A, const auto& B) { return A.ObjectiveId.LexicalLess(B.ObjectiveId); });
            int32 Count = Ordered.Num(); Writer << Count;
            for (const auto& O : Ordered)
            {
                FString ObjectiveId = O.ObjectiveId.ToString().ToLower(), Event = O.Event.ToString().ToLower(), Target = O.TargetId.ToString().ToLower();
                uint8 Kind = uint8(O.Kind); int32 TargetVersion = O.TargetVersion, Required = O.RequiredCount;
                Writer << ObjectiveId << Kind << Event << Target << TargetVersion << Required;
            }
        }
    }
    return AppendItemRewards(Writer, Definitions, Data);
}
FString Hash(const TArray<uint8>& Bytes)
{
    uint8 Digest[20]; FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Digest); return BytesToHex(Digest, 20);
}
FString QuestDigest(const FProject_JQuestDefinitionIndex& Index, const TArray<FName>& Ids, bool bHistoryScope,
    const UProject_JCharacterDataSubsystem* Data)
{
    TArray<UProject_JQuestDefinition*> Definitions;
    if (!Select(Index, &Ids, Definitions)) return {};
    TArray<uint8> Bytes; FMemoryWriter Writer(Bytes);
    FString Domain = bHistoryScope ? TEXT("ProjectJ.ProfileQuestGuard.3") : (HasCompound(Definitions) ? TEXT("ProjectJ.ProfileQuestGuard.2") : TEXT("ProjectJ.ProfileQuestGuard.1")); Writer << Domain;
    return Append(Writer, Definitions, Data) ? Hash(Bytes) : FString();
}
TArray<FName> ScopeIds(const FProject_JQuestSnapshot& Scope)
{
    TArray<FName> Ids; Ids.Reserve(Scope.States.Num());
    for (const auto& State : Scope.States) Ids.Add(State.QuestId);
    return Ids;
}
}
FString Fingerprint(const UProject_JProgressionComponent& P, const FProject_JQuestDefinitionIndex& Index,
    const UProject_JCharacterClassDefinition* Class, const TArray<UProject_JCharacterAdvancementDefinition*>& History,
    const FProject_JQuestSnapshot* Scope)
{
    TArray<uint8> Bytes; FMemoryWriter Writer(Bytes);
    int32 Maximum = P.MaximumLevel; auto Requirements = P.ExperienceRequirements;
    Writer << Maximum << Requirements;
    FString ClassId = Class ? Class->ClassId.ToString().ToLower() : TEXT("none");
    int32 ClassVersion = Class ? Class->SchemaVersion : 1, Floor = Class ? Class->StartingLevel : 1;
    if (ClassVersion < 1 || Maximum < 1 || Maximum > 10000) return {};
    Writer << ClassId << ClassVersion << Floor;
    for (const auto* Adv : History)
    {
        if (!Adv || Adv->SchemaVersion < 1) return {};
        FString Id = Adv->AdvancementId.ToString().ToLower(); int32 Version = Adv->SchemaVersion; Writer << Id << Version;
    }
    TArray<UProject_JQuestDefinition*> Definitions;
    TArray<FName> Ids;
    if (Scope)
    {
        if ((Scope->Version < 1 || Scope->Version > 3) || Scope->States.Num() > UProject_JQuestComponent::MaxCurrentQuestStates
            || !Scope->HasHydratedHistory() || (Scope->Version < 3 && Scope->HistoryCount != 0)
            || (Scope->Version == 3 && Scope->States.ContainsByPredicate([](const auto& State) { return State.bClaimed; }))) return {};
        Ids = ScopeIds(*Scope);
        FString Domain = Scope->Version == 1 ? TEXT("ProjectJ.ProfileRules.QuestStates.1") :
            (Scope->Version == 2 ? TEXT("ProjectJ.ProfileRules.QuestStates.2") : TEXT("ProjectJ.ProfileRules.QuestStates.3")); Writer << Domain;
    }
    if (!Select(Index, Scope ? &Ids : nullptr, Definitions)) return {};
    if ((!Scope || Scope->Version == 1) && HasCompound(Definitions)) return {};
    const auto* Instance = P.GetOwner() ? P.GetOwner()->GetGameInstance() : nullptr;
    const auto* Data = Instance ? Instance->GetSubsystem<UProject_JCharacterDataSubsystem>() : nullptr;
    return Append(Writer, Definitions, Data) ? Hash(Bytes) : FString();
}
bool Matches(const FProject_JLocalProfileSnapshot& Snapshot, const UProject_JProgressionComponent& P,
    const FProject_JQuestDefinitionIndex& Index, const UProject_JCharacterClassDefinition* Class,
    const TArray<UProject_JCharacterAdvancementDefinition*>& History)
{
    return !Snapshot.RulesFingerprint.IsEmpty() &&
        (Snapshot.RulesFingerprint == Fingerprint(P, Index, Class, History, &Snapshot.Quests) ||
         (Snapshot.Quests.Version == 1 && Snapshot.RulesFingerprint == Fingerprint(P, Index, Class, History, nullptr)));
}
bool FQuestGuard::Capture(const FProject_JQuestDefinitionIndex& Index, const FProject_JQuestSnapshot& Snapshot,
    const UProject_JCharacterDataSubsystem* Data)
{
    if (Snapshot.Version < 1 || Snapshot.Version > 3 || Snapshot.States.Num() > UProject_JQuestComponent::MaxCurrentQuestStates
        || !Snapshot.HasHydratedHistory() || (Snapshot.Version < 3 && Snapshot.HistoryCount != 0)
        || (Snapshot.Version == 3 && Snapshot.States.ContainsByPredicate([](const auto& State) { return State.bClaimed; }))) return false;
    auto ProposedIds = ScopeIds(Snapshot); auto ProposedDigest = QuestDigest(Index, ProposedIds, Snapshot.Version == 3, Data);
    if (ProposedDigest.IsEmpty()) return false;
    Ids = MoveTemp(ProposedIds); Digest = MoveTemp(ProposedDigest); bHistoryScope = Snapshot.Version == 3; return true;
}
bool FQuestGuard::IsCurrent(const FProject_JQuestDefinitionIndex& Index, const UProject_JCharacterDataSubsystem* Data) const
{ return !Digest.IsEmpty() && Digest == QuestDigest(Index, Ids, bHistoryScope, Data); }
}
