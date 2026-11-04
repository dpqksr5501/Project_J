#include "Game/Project_JNativeQuestRewardCommit.h"
#include "Game/Project_JQuestComponent.h"
#include "Game/Project_JPlayerState.h"
#include "Backend/Project_JProfileRules.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Crafting/Project_JCraftingPlan.h"
#include "Inventory/Project_JOwnedItemProjection.h"
#include "Inventory/Project_JItemDefinition.h"
#include "Project_JAbilitySystemComponent.h"
#include "AbilitySystemComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
bool SameQuestValues(const FProject_JQuestSnapshot& A, const FProject_JQuestSnapshot& B)
{
    return A.Version == B.Version && A.States == B.States && A.HistoryCount == B.HistoryCount
        && A.HistoryRootDigest == B.HistoryRootDigest;
}
bool SameLines(const TArray<ProjectJ::Crafting::FResolvedLine>& A, const TArray<ProjectJ::Crafting::FResolvedLine>& B)
{
    if (A.Num() != B.Num()) return false;
    for (int32 Index = 0; Index < A.Num(); ++Index)
        if (A[Index].Definition != B[Index].Definition || A[Index].DefinitionVersion != B[Index].DefinitionVersion
            || A[Index].CountPerBatch != B[Index].CountPerBatch || A[Index].Level != B[Index].Level
            || A[Index].MaxStackCount != B[Index].MaxStackCount) return false;
    return true;
}
bool SameEquipment(const TArray<FProject_JEquipmentArrayItem>& A, const TArray<FProject_JEquipmentArrayItem>& B)
{
    if (A.Num() != B.Num()) return false;
    for (const auto& Item : A)
    {
        const auto* Other = B.FindByPredicate([&](const auto& Entry) { return Entry.Slot == Item.Slot; });
        if (!Other || Item.ItemDef != Other->ItemDef || !(Item.ItemInstance == Other->ItemInstance)) return false;
    }
    return true;
}
}

bool FProject_JNativeQuestRewardCommit::Apply(UProject_JQuestComponent* Quest, UProject_JQuestDefinition* Definition,
    const FProject_JQuestSnapshot& Before, const FProject_JQuestSnapshot& After,
    const TArray<ProjectJ::Crafting::FResolvedLine>& FrozenItems)
{
    check(IsInGameThread());
    if (!IsValid(Quest) || !IsValid(Definition) || FrozenItems.IsEmpty() || !Before.HasHydratedHistory()
        || !After.HasHydratedHistory() || !Quest->bChanging || Quest->SubmitPersistentEdit) return false;
    const TStrongObjectPtr<UProject_JQuestComponent> KeepQuest(Quest);
    const TStrongObjectPtr<UProject_JQuestDefinition> KeepDefinition(Definition);
    const TStrongObjectPtr<AProject_JPlayerState> Player(Cast<AProject_JPlayerState>(Quest->GetOwner()));
    if (!Player || !Quest->CanPublish() || !Player->HasAuthority()) return false;
    const TStrongObjectPtr<UWorld> World(Player->GetWorld());
    const TStrongObjectPtr<UGameInstance> Game(World ? World->GetGameInstance() : nullptr);
    const TStrongObjectPtr<UProject_JCharacterDataSubsystem> Data(Game ? Game->GetSubsystem<UProject_JCharacterDataSubsystem>() : nullptr);
    const TStrongObjectPtr<UProject_JInventoryComponent> Inventory(Player->GetInventoryComponent());
    const TStrongObjectPtr<UProject_JEquipmentManagerComponent> Equipment(Player->GetEquipmentManagerComponent());
    const TStrongObjectPtr<UProject_JProgressionComponent> Progression(Player->FindComponentByClass<UProject_JProgressionComponent>());
    if (!World || !Game || !Data || !Data->ArePrimaryAssetDefinitionsReady() || !Inventory || !Equipment || !Progression || Inventory->bEditingBag
        || Inventory->bUsingItem || Progression->bChanging || Inventory->SubmitPersistentEdit || Progression->SubmitPersistentEdit
        || Progression->bRebuildCommittedClass || Progression->bRebuildCommittedAdvancements
        || Progression->bInitializeCommittedAttributes || !Progression->CommittedGrantsToAdd.IsEmpty()) return false;
    const auto Session = Player->CaptureServerSession();
    const FGuid Character = Player->GetCharacterId().Value;
    const FName QuestId = Definition->QuestId;
    const int64 Reward = Definition->RewardExperience;
    const auto OriginalProgression = Progression->CaptureSnapshot();
    const int32 OriginalProgressionRevision = Progression->State.Revision;
    const auto* OriginalClass = Progression->State.ClassDefinition.Get();
    const auto* OriginalAdvancement = Progression->State.Advancement.Get();
    const auto OriginalRewardItems = Definition->RewardItems;
    const int32 OriginalMaximumLevel = Progression->MaximumLevel;
    const auto OriginalExperienceRequirements = Progression->ExperienceRequirements;
    const auto OriginalBag = Inventory->GetItemInstances();
    const auto OriginalEquipment = Equipment->CapturePersistentEquipment();
    int32 ExpectedBagKey = Inventory->InventoryArray.ArrayReplicationKey;
    int32 ExpectedProgressionRevision = OriginalProgressionRevision;
    const auto GrantedASC = Progression->GrantedAbilitySystem;
    TArray<TStrongObjectPtr<UProject_JItemDefinition>> PinnedDefinitions;
    for (const auto& Item : OriginalBag) if (Item.ItemDef) PinnedDefinitions.Emplace(Item.ItemDef.Get());
    for (const auto& Entry : OriginalEquipment) if (Entry.ItemInstance.ItemDef) PinnedDefinitions.Emplace(Entry.ItemInstance.ItemDef.Get());
    FProject_JQuestDefinitionIndex CapturedIndex;
    Quest->BuildDefinitionIndex(CapturedIndex);
    ProjectJ::LocalProfileRules::FQuestGuard RulesGuard;
    if (!RulesGuard.Capture(CapturedIndex, Before, Data.Get())) return false;
    const int32 QuestSchema = Definition->SchemaVersion;
    TGuardValue<bool> BagGuard(Inventory->bEditingBag, true);
    // These are normal virtual/native admission boundaries. No owned value is installed yet.
    const TStrongObjectPtr<UAbilitySystemComponent> ASC(Progression->GetASC());
    if (!ASC) return false;
    const TStrongObjectPtr<UProject_JAbilitySystemComponent> ProjectASC(Cast<UProject_JAbilitySystemComponent>(ASC.Get()));
    const uint64 ActorInfoRevision = ProjectASC ? ProjectASC->GetActorInfoRevision() : 0;
    const TWeakObjectPtr<AActor> ASCOwner(ASC->GetOwnerActor()), Avatar(ASC->GetAvatarActor());
    if (!Inventory->HasMutationAuthority() || !Progression->CanMutate() || !Data->ArePrimaryAssetDefinitionsReady()) return false;
    const auto RegistryRefreshes = Data->GetRegistryStats().Refreshes;
    const auto RegistryFailures = Data->GetRegistryStats().FailedRefreshes;
    TArray<UProject_JItemDefinition*> FrozenDefinitions;
    auto ExpectedQuest = &Before;
    auto ExpectedProgression = OriginalProgression;
    const auto PureCurrent = [&]
    {
        return IsValid(Player.Get()) && !Player->IsActorBeingDestroyed() && Player->HasAuthority()
            && Player->GetWorld() == World.Get() && !World->bIsTearingDown && World->GetGameInstance() == Game.Get()
            && Player->GetCharacterId().Value == Character && Player->IsServerSessionCurrent(Session)
            && Game->GetSubsystem<UProject_JCharacterDataSubsystem>() == Data.Get()
            && Data->ArePrimaryAssetDefinitionsReady() && Data->GetRegistryStats().Refreshes == RegistryRefreshes && Data->GetRegistryStats().FailedRefreshes == RegistryFailures
            && !Quest->SubmitPersistentEdit && !Inventory->SubmitPersistentEdit && !Progression->SubmitPersistentEdit
            && Quest->CanPublish() && Quest->bChanging && Quest->GetOwner() == Player.Get()
            && Player->FindComponentByClass<UProject_JQuestComponent>() == Quest
            && Player->GetInventoryComponent() == Inventory.Get() && Inventory->CanPublishState()
            && Player->GetEquipmentManagerComponent() == Equipment.Get() && !Equipment->IsBeingDestroyed()
            && Equipment->IsAcceptingRequests() && Progression->GetOwner() == Player.Get() && !Progression->bEnding
            && !Progression->IsBeingDestroyed() && Player->FindComponentByClass<UProject_JProgressionComponent>() == Progression.Get()
            && Progression->GrantedAbilitySystem == GrantedASC && Progression->State.Revision == ExpectedProgressionRevision
            && Progression->State.Level == ExpectedProgression.Level && Progression->Experience == ExpectedProgression.Experience
            && Progression->State.ClassDefinition.Get() == OriginalClass && Progression->State.Advancement.Get() == OriginalAdvancement
            && Inventory->InventoryArray.ArrayReplicationKey == ExpectedBagKey
            && Quest->History == ExpectedQuest->History && Quest->States == ExpectedQuest->States
            && IsValid(Definition) && Quest->FindDefinition(QuestId) == Definition && Definition->QuestId == QuestId
            && Definition->SchemaVersion == QuestSchema && Definition->RewardExperience == Reward && Definition->RewardItems == OriginalRewardItems
            && SameEquipment(OriginalEquipment, Equipment->CapturePersistentEquipment());
    };
    const auto Current = [&]
    {
        if (!PureCurrent()) return false;
        const auto* CurrentASC = Progression->GetASC();
        if (CurrentASC != ASC.Get() || !IsValid(ASC.Get()) || ASC->IsBeingDestroyed()
            || TWeakObjectPtr<AActor>(ASC->GetOwnerActor()) != ASCOwner || TWeakObjectPtr<AActor>(ASC->GetAvatarActor()) != Avatar
            || (ASCOwner.IsValid() && ASCOwner->IsActorBeingDestroyed()) || (Avatar.IsValid() && Avatar->IsActorBeingDestroyed())
            || (ProjectASC && (ProjectASC->IsActorInfoChanging() || ProjectASC->GetActorInfoRevision() != ActorInfoRevision))
            || !PureCurrent()) return false;
        for (int32 Index = 0; Index < FrozenDefinitions.Num(); ++Index)
            if (!IsValid(FrozenDefinitions[Index]) || Data->GetItemDefinition(FrozenItems[Index].Definition, FrozenItems[Index].DefinitionVersion) != FrozenDefinitions[Index]
                || FrozenDefinitions[Index]->MaxStackCount != FrozenItems[Index].MaxStackCount) return false;
        return true;
    };
    if (!Current() || !SameQuestValues(Quest->CaptureSnapshot(), Before)) return false;
    const auto* ClaimedState = Before.States.FindByPredicate([QuestId](const auto& State) { return State.QuestId == QuestId; });
    auto PlannedQuest = Before;
    if (!ClaimedState || !Definition->IsComplete(*ClaimedState)
        || !UProject_JQuestComponent::PlanClaimCompletion(QuestId, PlannedQuest) || !SameQuestValues(PlannedQuest, After)) return false;
    TArray<ProjectJ::Crafting::FResolvedLine> CurrentItems;
    if (!Definition->CaptureRewardItems(Data.Get(), CurrentItems) || !SameLines(FrozenItems, CurrentItems)) return false;
    for (const auto& Line : FrozenItems)
    {
        auto* ItemDefinition = Data->GetItemDefinition(Line.Definition, Line.DefinitionVersion);
        if (!IsValid(ItemDefinition) || ItemDefinition->MaxStackCount != Line.MaxStackCount) return false;
        PinnedDefinitions.Emplace(ItemDefinition);
        FrozenDefinitions.Add(ItemDefinition);
    }
    FProject_JLocalProfileSnapshot BeforeItems;
    BeforeItems.Quests = Before;
    BeforeItems.Progression = OriginalProgression;
    TSet<FGuid> Locked;
    if (!ProjectJ::OwnedItems::CaptureItems(Player.Get(), BeforeItems, &Locked) || !Current()) return false;
    FProject_JLocalProfileSnapshot PlannedItems;
    if (ProjectJ::Crafting::PlanGrant(FrozenItems, Locked, BeforeItems, PlannedItems) != ProjectJ::Crafting::EPlanResult::Success) return false;
    TArray<FProject_JItemInstanceData> Bag;
    TArray<FProject_JEquipmentArrayItem> Slots;
    if (!ProjectJ::OwnedItems::ResolveNativeGrant(Player.Get(), PlannedItems, OriginalBag, Bag, Slots)
        || !SameEquipment(OriginalEquipment, Slots)) return false;
    for (const auto& Item : Bag) if (Item.ItemDef) PinnedDefinitions.Emplace(Item.ItemDef.Get());
    auto PlannedProgression = OriginalProgression;
    if (Reward > 0 && Progression->GetNextLevelExperience() > 0 && !Progression->PlanExperience(Reward, PlannedProgression)) return false;
    const bool ProgressionChanged = PlannedProgression.Level != OriginalProgression.Level || PlannedProgression.Experience != OriginalProgression.Experience;
    if ((ProgressionChanged && !Progression->ValidateCommittedExperience(PlannedProgression, true))
        || !Inventory->ValidateNativeRewardItems(Bag, OriginalBag)
        || !Equipment->ValidatePersistentEquipment(Slots, Bag, true) || !Current()) return false;
    // Reserve the existing progression writer only after its public preflight has completed.
    TGuardValue<bool> ProgressionGuard(Progression->bChanging, true);
    // GetASC is the last virtual boundary. A pure, captured-source/value fence follows it before all writes.
    if (!Current() || !PureCurrent() || Progression->MaximumLevel != OriginalMaximumLevel
        || Progression->ExperienceRequirements != OriginalExperienceRequirements
        || !Inventory->ValidateNativeRewardItems(Bag, OriginalBag)
        || !Equipment->ValidatePersistentEquipment(Slots, Bag, true)) return false;
    FProject_JQuestDefinitionIndex FinalIndex;
    Quest->BuildDefinitionIndex(FinalIndex);
    if (!RulesGuard.IsCurrent(FinalIndex, Data.Get()) || !PureCurrent()) return false;
    Inventory->InstallPreparedItems(Bag);
    if (ProgressionChanged) Progression->InstallPreparedExperience(PlannedProgression);
    Quest->InstallCommittedQuests(After);
    ExpectedBagKey = Inventory->InventoryArray.ArrayReplicationKey;
    ExpectedProgression = PlannedProgression;
    ExpectedQuest = &After;
    // All values are now paid/owned. Callback retirement suppresses later notifications; it never rolls the bundle back.
    if (ProgressionChanged)
    {
        Progression->PublishPreparedExperience(Current);
        ExpectedProgressionRevision = OriginalProgressionRevision == MAX_int32 ? 1 : OriginalProgressionRevision + 1;
    }
    if (Current()) Inventory->PublishPreparedItems(OriginalBag, Current);
    if (Current()) Quest->Publish();
    return true;
}
