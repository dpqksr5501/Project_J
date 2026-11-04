#include "Game/Project_JQuestComponent.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JNativeQuestRewardCommit.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Inventory/Project_JItemDefinition.h"
#include "Engine/GameInstance.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/StrongObjectPtr.h"
#include "HAL/PlatformTime.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
bool IsQuestDefinitionValid(const UProject_JQuestDefinition* Def) { return Def && Def->IsRuntimeValid(); }
int32 QuestSnapshotVersion(const TArray<FProject_JQuestState>& States)
{ return States.ContainsByPredicate([](const auto& S) { return !S.Objectives.IsEmpty(); }) ? 2 : 1; }
bool SnapshotShape(const FProject_JQuestSnapshot& Snapshot, bool bCheckHistoryMembership = true)
{
    if (!Snapshot.HasHydratedHistory() || Snapshot.States.Num() > UProject_JQuestComponent::MaxCurrentQuestStates
        || Snapshot.HistoryCount + Snapshot.States.Num() > ProjectJ::QuestHistory::MaxIds) return false;
    if (Snapshot.Version != 3 && (Snapshot.Version != QuestSnapshotVersion(Snapshot.States) || Snapshot.HistoryCount != 0)) return false;
    TSet<FName> Ids;
    for (const auto& State : Snapshot.States)
    {
        if (!ProjectJ::QuestHistory::IsValidId(State.QuestId) || Ids.Contains(State.QuestId)
            || (Snapshot.Version == 3 && State.bClaimed)
            || (bCheckHistoryMembership && ProjectJ::QuestHistory::Contains(Snapshot.History, State.QuestId))) return false;
        Ids.Add(State.QuestId);
    }
    return true;
}
void AddProgress(int32& Value, int32 Required, int64 Amount)
{ Value += int32(FMath::Min<int64>(Amount, int64(Required) - Value)); }
}
FPrimaryAssetId UProject_JQuestDefinition::GetPrimaryAssetId() const
{ return FPrimaryAssetId(FName(TEXT("Project_JQuestDefinition")), GetFName()); }

bool UProject_JQuestDefinition::IsRuntimeValid() const
{
	if (!ProjectJ::QuestHistory::IsValidId(QuestId) || SchemaVersion < 1 || RewardExperience < 0) return false;
    if (RewardItems.Num() > 8) return false;
    TSet<FName> RewardIds;
    for (const auto& Item : RewardItems)
    {
        if (!ProjectJ::QuestHistory::IsValidId(Item.Definition) || Item.DefinitionVersion < 1 || Item.Count < 1 ||
            Item.Count > 1000000 || Item.Level < 1 || Item.Level > 10000 || RewardIds.Contains(Item.Definition)) return false;
        RewardIds.Add(Item.Definition);
    }
	if (Objectives.IsEmpty()) return RequiredCount > 0 && RequiredCount <= 1000000 && !ObjectiveEvent.IsNone();
	if (Objectives.Num() < 2 || Objectives.Num() > UProject_JQuestComponent::MaxObjectivesPerQuest || !ObjectiveEvent.IsNone() || bTravelObjective || RequiredCount != 1) return false;
	TSet<FName> Seen;
	for (const auto& O : Objectives)
	{
		if (O.ObjectiveId.IsNone() || O.ObjectiveId.ToString().Len() > 128 || Seen.Contains(O.ObjectiveId) || O.RequiredCount < 1 || O.RequiredCount > 1000000) return false;
		Seen.Add(O.ObjectiveId);
		switch (O.Kind)
		{
		case EProject_JQuestObjectiveKind::Event:
			if (O.Event.IsNone() || !O.TargetId.IsNone() || O.TargetVersion != 1) return false; break;
		case EProject_JQuestObjectiveKind::Travel:
			if (!O.Event.IsNone() || !O.TargetId.IsNone() || O.TargetVersion != 1) return false; break;
		case EProject_JQuestObjectiveKind::GatherItem:
		case EProject_JQuestObjectiveKind::CraftRecipe:
			if (!O.Event.IsNone() || O.TargetId.IsNone() || O.TargetId.ToString().Len() > 128 || O.TargetVersion < 1) return false; break;
		default: return false;
		}
	}
	return true;
}
bool UProject_JQuestDefinition::CaptureRewardItems(const UProject_JCharacterDataSubsystem* Data,
    TArray<ProjectJ::Crafting::FResolvedLine>& Out) const
{
    check(IsInGameThread());
    if (RewardItems.IsEmpty()) { Out.Reset(); return true; }
    if (!Data || !Data->ArePrimaryAssetDefinitionsReady() || !IsRuntimeValid()) return false;
    TArray<ProjectJ::Crafting::FResolvedLine> Captured;
    Captured.Reserve(RewardItems.Num());
    for (const auto& Reward : RewardItems)
    {
        const auto* Item = Data->GetItemDefinition(Reward.Definition, Reward.DefinitionVersion);
        if (!IsValid(Item) || Item->ItemId != Reward.Definition || Item->SchemaVersion != Reward.DefinitionVersion || Item->MaxStackCount < 1) return false;
        auto& Value = Captured.AddDefaulted_GetRef();
        Value.Definition = Reward.Definition; Value.DefinitionVersion = Reward.DefinitionVersion;
        Value.Level = Reward.Level; Value.CountPerBatch = Reward.Count; Value.MaxStackCount = Item->MaxStackCount;
    }
    Out = MoveTemp(Captured); return true;
}
bool UProject_JQuestDefinition::IsStateValid(const FProject_JQuestState& State) const
{
	if (!IsRuntimeValid() || State.QuestId != QuestId) return false;
	if (Objectives.IsEmpty()) return State.Objectives.IsEmpty() && State.Count >= 0 && State.Count <= RequiredCount && (!State.bClaimed || State.Count == RequiredCount);
	if (State.Count != 0 || State.Objectives.Num() != Objectives.Num()) return false;
	TSet<FName> Seen;
	for (const auto& S : State.Objectives)
	{
		const auto* O = Objectives.FindByPredicate([&](const auto& V) { return V.ObjectiveId == S.ObjectiveId; });
		if (!O || Seen.Contains(S.ObjectiveId) || S.Count < 0 || S.Count > O->RequiredCount || (State.bClaimed && S.Count != O->RequiredCount)) return false;
		Seen.Add(S.ObjectiveId);
	}
	return true;
}
bool UProject_JQuestDefinition::IsComplete(const FProject_JQuestState& State) const
{
	if (!IsStateValid(State)) return false;
	if (Objectives.IsEmpty()) return State.Count == RequiredCount;
	for (const auto& O : Objectives) if (State.GetObjectiveCount(O.ObjectiveId) != O.RequiredCount) return false;
	return true;
}
bool UProject_JQuestDefinition::HasIncompleteTravel(const FProject_JQuestState& State) const
{
	if (State.bClaimed || !IsStateValid(State)) return false;
	if (Objectives.IsEmpty()) return bTravelObjective && State.Count < RequiredCount;
	for (const auto& O : Objectives)
		if (O.Kind == EProject_JQuestObjectiveKind::Travel && State.GetObjectiveCount(O.ObjectiveId) < O.RequiredCount) return true;
	return false;
}
#if WITH_EDITOR
EDataValidationResult UProject_JQuestDefinition::IsDataValid(FDataValidationContext& Context) const
{
	const auto Base = Super::IsDataValid(Context);
	if (!IsRuntimeValid())
	{
		Context.AddError(NSLOCTEXT("ProjectJQuest", "InvalidObjectives", "Quest needs an unambiguous legacy objective or2–8 unique, bounded objectives with valid event/target fields."));
		return EDataValidationResult::Invalid;
	}
	return Base == EDataValidationResult::Invalid ? Base : EDataValidationResult::Valid;
}
#endif

UProject_JQuestComponent::UProject_JQuestComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
	DefaultDefinitionPaths.Add(TSoftObjectPtr<UProject_JQuestDefinition>(FSoftObjectPath(TEXT("/Game/UI/Gameplay/DA_ProjectJFirstSteps.DA_ProjectJFirstSteps"))));
}
void UProject_JQuestComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty> &OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UProject_JQuestComponent, States, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(UProject_JQuestComponent, HistorySummary, COND_OwnerOnly);
}
void UProject_JQuestComponent::BeginPlay()
{
	bEnding = false;
	Super::BeginPlay();
	PrepareForProfile();
    RefreshHistorySummary();
}
bool UProject_JQuestComponent::EnsureDefinitionsResolved()
{
	check(IsInGameThread());
	if (!Definitions.IsEmpty()) return true; // Explicit authoring/test override.
	if (bDefinitionsAttempted) return false;
	bDefinitionsAttempted = true;
	TArray<TObjectPtr<UProject_JQuestDefinition>> Proposed;
	TArray<TStrongObjectPtr<UProject_JQuestDefinition>> KeepResolved;
	TSet<FName> Ids;
	for (const auto& Path : DefaultDefinitionPaths)
	{
		auto* Def = Path.LoadSynchronous();
		if (!IsQuestDefinitionValid(Def) || Ids.Contains(Def->QuestId)) return false;
		Ids.Add(Def->QuestId); KeepResolved.Emplace(Def); Proposed.Add(Def);
	}
	if (Proposed.IsEmpty()) return false;
	Definitions = MoveTemp(Proposed); return true;
}
bool UProject_JQuestComponent::PrepareForProfile()
{
	check(IsInGameThread());
	if (!EnsureDefinitionsResolved()) return false;
	if (!GetOwner() || !GetOwner()->HasAuthority() || bDefaultsInitialized) return true;
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
	bDefaultsInitialized = true; // A later BeginPlay must not reinsert restored-away defaults.
	TArray<FName> InitialIds;
	for (const UProject_JQuestDefinition* Def : Definitions) if (Def && Def->bTravelObjective) InitialIds.Add(Def->QuestId);
	for (FName Id : InitialIds)
	{
		if (!CanPublish()) return false;
		// Publication can replace Definitions and collect their old UObjects.
		// IDs survive that callback; only current legacy defaults may be accepted.
		if (const auto* Def = FindDefinition(Id); Def && Def->bTravelObjective) Accept(Id);
	}
	return CanPublish();
}
void UProject_JQuestComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Retire();
	Super::EndPlay(Reason);
}
void UProject_JQuestComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Retire();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}
bool UProject_JQuestComponent::CanPublish() const
{
	return !bEnding && !IsBeingDestroyed() && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed() &&
		   GetWorld() && !GetWorld()->bIsTearingDown;
}
bool UProject_JQuestComponent::CanMutate() const
{
	check(IsInGameThread());
	return CanPublish() && GetOwner()->HasAuthority() && !bChanging && (!PersistentAdmission || PersistentAdmission());
}
void UProject_JQuestComponent::Retire()
{
	bEnding = true;
	if (auto *World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TravelTimer);
	}
	TravelTimer.Invalidate();
	TravelPawn.Reset();
	TravelRemainder = 0;
	LastTravelSampleTime = -1; LastTravelSpeed = 0;
	OnChanged.Clear();
    PendingHistoryRequest.Invalidate(); PendingHistoryRevision.Invalidate();
    HistoryPage = {};
    OnHistoryPageChanged.Clear();
}
void UProject_JQuestComponent::ReconcileTravelSampling(const FProject_JQuestDefinitionIndex* Index)
{
	FProject_JQuestDefinitionIndex LocalIndex;
	if (!Index) { BuildDefinitionIndex(LocalIndex); Index = &LocalIndex; }
	const bool bActive = CanPublish() && GetOwner()->HasAuthority() &&
						 States.ContainsByPredicate([Index](const FProject_JQuestState &State) {
							 const auto *Def = Index->FindRef(State.QuestId);
							 return Def && Def->HasIncompleteTravel(State);
						 });
	if (!bActive)
	{
		if (auto *World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(TravelTimer);
		}
		TravelTimer.Invalidate();
		TravelPawn.Reset();
		TravelRemainder = 0;
		LastTravelSampleTime = -1; LastTravelSpeed = 0;
	}
	else if (!GetWorld()->GetTimerManager().TimerExists(TravelTimer))
	{
		// Travel consumes one absolute-time position sample. Catch-up callbacks in
		// the same frame cannot reconstruct skipped movement and repeat all scans.
		FTimerManagerTimerParameters Parameters;
		Parameters.bLoop = true; Parameters.bMaxOncePerFrame = true;
		GetWorld()->GetTimerManager().SetTimer(TravelTimer, this, &ThisClass::SampleTravel, 0.5f, Parameters);
	}
}
UProject_JQuestDefinition *UProject_JQuestComponent::FindDefinition(FName Id) const
{
	UProject_JQuestDefinition *Match = nullptr;
	for (UProject_JQuestDefinition *Def : Definitions)
		if (Def && Def->QuestId == Id)
		{
			if (Match)
				return nullptr;
			Match = Def;
		}
	return IsQuestDefinitionValid(Match) ? Match : nullptr;
}
void UProject_JQuestComponent::BuildDefinitionIndex(FProject_JQuestDefinitionIndex& Out) const
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_QuestDefinitionIndex);
	Out.Reset(); Out.Reserve(Definitions.Num());
	for (UProject_JQuestDefinition* Def : Definitions)
	{
		if (!Def) continue;
		// A duplicate ID is ambiguous even if one of the definitions is invalid.
		if (Out.Contains(Def->QuestId)) Out[Def->QuestId] = nullptr;
		else Out.Add(Def->QuestId, IsQuestDefinitionValid(Def) ? Def : nullptr);
	}
}
void UProject_JQuestComponent::Publish(const FProject_JQuestDefinitionIndex* Index)
{
	if (!CanPublish())
	{
		return;
	}
    RefreshHistorySummary();
	ReconcileTravelSampling(Index);
	GetOwner()->ForceNetUpdate();
	OnChanged.Broadcast();
}
void UProject_JQuestComponent::OnRep_States()
{
	if (CanPublish() && EnsureDefinitionsResolved())
	{
		OnChanged.Broadcast();
	}
}
bool UProject_JQuestComponent::NormalizeClaimed(FProject_JQuestSnapshot& Snapshot)
{
    if (!SnapshotShape(Snapshot, false)) return false;
    // Existing schema3 is already normalized. Its hydrated immutable value is not re-scanned on hot edits.
    if (Snapshot.Version == 3) return true;
    auto Proposed = Snapshot;
    FString Failure;
    for (int32 Index = 0; Index < Proposed.States.Num();)
    {
        if (!Proposed.States[Index].bClaimed) { ++Index; continue; }
        ProjectJ::QuestHistory::FViewPtr Next;
        if (!ProjectJ::QuestHistory::Append(Proposed.History, Proposed.States[Index].QuestId, Next, Failure)) return false;
        Proposed.History = MoveTemp(Next);
        Proposed.States.RemoveAt(Index);
    }
    Proposed.HistoryCount = ProjectJ::QuestHistory::Count(Proposed.History);
    Proposed.HistoryRootDigest = ProjectJ::QuestHistory::RootDigest(Proposed.History);
    if (Proposed.HistoryCount > 0) Proposed.Version = 3;
    Snapshot = MoveTemp(Proposed);
    return true;
}
bool UProject_JQuestComponent::PlanClaimCompletion(FName Id, FProject_JQuestSnapshot& Snapshot)
{
    auto Proposed = Snapshot;
    if (!NormalizeClaimed(Proposed)) return false;
    const int32 Index = Proposed.States.IndexOfByPredicate([Id](const auto& State) { return State.QuestId == Id; });
    if (Index == INDEX_NONE || Proposed.States[Index].bClaimed) return false;
    ProjectJ::QuestHistory::FViewPtr Next;
    FString Failure;
    if (!ProjectJ::QuestHistory::Append(Proposed.History, Id, Next, Failure)) return false;
    Proposed.History = MoveTemp(Next);
    Proposed.HistoryCount = ProjectJ::QuestHistory::Count(Proposed.History);
    Proposed.HistoryRootDigest = ProjectJ::QuestHistory::RootDigest(Proposed.History);
    Proposed.States.RemoveAt(Index);
    Proposed.Version = 3;
    Snapshot = MoveTemp(Proposed);
    return true;
}
bool UProject_JQuestComponent::PlanAcceptance(FName Id, FProject_JQuestSnapshot& Proposed) const
{
    if (!SnapshotShape(Proposed)) return false;
    // Legacy rows must still satisfy their original definitions before any detail is retired.
    for (const auto& State : Proposed.States)
        if (State.bClaimed) { const auto* LegacyDef = FindDefinition(State.QuestId); if (!LegacyDef || !LegacyDef->IsStateValid(State)) return false; }
    auto Planned = Proposed;
    const auto* Def = FindDefinition(Id);
    if (!Def || !NormalizeClaimed(Planned) || Planned.States.Num() >= MaxCurrentQuestStates
        || Planned.HistoryCount + Planned.States.Num() >= ProjectJ::QuestHistory::MaxIds
        || ProjectJ::QuestHistory::Contains(Planned.History, Id)
        || Planned.States.ContainsByPredicate([Id](const auto& State) { return State.QuestId == Id; })) return false;
    FProject_JQuestState State; State.QuestId = Id;
    for (const auto& Objective : Def->Objectives) { auto& Value = State.Objectives.AddDefaulted_GetRef(); Value.ObjectiveId = Objective.ObjectiveId; }
    State.Objectives.Sort([](const auto& A, const auto& B) { return A.ObjectiveId.LexicalLess(B.ObjectiveId); });
    Planned.States.Add(MoveTemp(State));
    if (Planned.Version != 3) Planned.Version = QuestSnapshotVersion(Planned.States);
    Proposed = MoveTemp(Planned);
    return true;
}
bool UProject_JQuestComponent::Accept(FName Id)
{
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
    if (!CanMutate()) return false;
    auto Proposed = CaptureSnapshot();
    if (!PlanAcceptance(Id, Proposed)) return false;
    if (SubmitPersistentEdit)
    {
        FProject_JPersistentEdit Edit; Edit.Kind = EProject_JPersistentEdit::AcceptCompoundQuest; Edit.Quest = Id;
        Edit.Request = CreatePersistentRequest ? CreatePersistentRequest() : FGuid();
        return SubmitPersistentEdit(Edit, {});
    }
    InstallCommittedQuests(Proposed); Publish(); return true;
}
bool UProject_JQuestComponent::PlanTracking(FName Id, bool bTracked, FProject_JQuestSnapshot& Proposed)
{
	auto* State = Proposed.States.FindByPredicate([Id](const auto& Value) { return Value.QuestId == Id; });
	if (!State || State->bClaimed || State->bTracked == bTracked) return false;
	State->bTracked = bTracked;
	return true;
}
bool UProject_JQuestComponent::PlanActivityProgress(FProject_JQuestSnapshot& Proposed, EProject_JQuestObjectiveKind Kind,
	FName TargetId, int32 TargetVersion, int64 Amount, const FProject_JQuestDefinitionIndex& Index) const
{
	check(IsInGameThread());
	if ((Kind != EProject_JQuestObjectiveKind::GatherItem && Kind != EProject_JQuestObjectiveKind::CraftRecipe) ||
		TargetId.IsNone() || TargetVersion < 1 || Amount < 1 || !SnapshotShape(Proposed, false)) return false;
	TSet<FName> QuestIds;
	for (auto& State : Proposed.States)
	{
		if (QuestIds.Contains(State.QuestId)) return false;
		QuestIds.Add(State.QuestId);
		const auto* Def = Index.FindRef(State.QuestId);
		if (!Def || !Def->IsStateValid(State)) return false;
		if (State.bClaimed) continue;
		for (const auto& O : Def->Objectives)
			if (O.Kind == Kind && O.TargetId == TargetId && O.TargetVersion == TargetVersion)
			{
				auto* S = State.Objectives.FindByPredicate([&](const auto& V) { return V.ObjectiveId == O.ObjectiveId; });
				AddProgress(S->Count, O.RequiredCount, Amount);
			}
	}
	return true;
}
void UProject_JQuestComponent::CollectProgressInputs(FName Event, int32 Count,
	const FProject_JQuestDefinitionIndex& Index, const TSet<FName>* TravelEvents,
	TArray<FProject_JQuestProgressInput>& Out) const
{
	for (const auto& State : States)
	{
		const auto* Def = Index.FindRef(State.QuestId);
		if (!Def || State.bClaimed || !Def->IsStateValid(State)) continue;
		if (Def->Objectives.IsEmpty())
		{
			if ((TravelEvents ? TravelEvents->Contains(Def->ObjectiveEvent) : Def->ObjectiveEvent == Event) && State.Count < Def->RequiredCount)
				Out.Add({State.QuestId, NAME_None, State.Count, Def->RequiredCount, Count});
		}
		else for (const auto& Objective : Def->Objectives)
			if ((TravelEvents ? Objective.Kind == EProject_JQuestObjectiveKind::Travel :
				(Objective.Kind == EProject_JQuestObjectiveKind::Event && Objective.Event == Event)) &&
				State.GetObjectiveCount(Objective.ObjectiveId) < Objective.RequiredCount)
				Out.Add({State.QuestId, Objective.ObjectiveId, State.GetObjectiveCount(Objective.ObjectiveId), Objective.RequiredCount, Count});
	}
}
void UProject_JQuestComponent::RecordObjective(FName Event, int32 Count)
{
	TryRecordObjective(Event, Count);
}
bool UProject_JQuestComponent::TryRecordObjective(FName Event, int32 Count)
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_QuestObjective);
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
	if (!CanPublish() || !GetOwner()->HasAuthority() || bChanging || Event.IsNone() || Count <= 0 || Count > 1000000) return false;
	FProject_JQuestDefinitionIndex Index;
	BuildDefinitionIndex(Index);
	TArray<FProject_JQuestProgressInput> Inputs;
	CollectProgressInputs(Event, Count, Index, nullptr, Inputs);
	if (Inputs.IsEmpty()) return !SubmitPersistentProgress || SubmitPersistentProgress(Inputs);
	if (SubmitPersistentProgress) return SubmitPersistentProgress(Inputs);
	if (!CanMutate()) return false;
	for (const auto& Input : Inputs)
	{
		auto& State = *States.FindByPredicate([&](const auto& Value) { return Value.QuestId == Input.Quest; });
		if (Input.Objective.IsNone()) AddProgress(State.Count, Input.Required, Input.Amount);
		else
		{
			auto& Objective = *State.Objectives.FindByPredicate([&](const auto& Value) { return Value.ObjectiveId == Input.Objective; });
			AddProgress(Objective.Count, Input.Required, Input.Amount);
		}
	}
	Publish(&Index);
	return true;
}
bool UProject_JQuestComponent::Claim(FName Id)
{
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
    if (!CanMutate()) return false;
    auto Before = CaptureSnapshot();
    if (!SnapshotShape(Before)) return false;
    for (const auto& State : Before.States)
        if (State.bClaimed) { const auto* LegacyDef = FindDefinition(State.QuestId); if (!LegacyDef || !LegacyDef->IsStateValid(State)) return false; }
    if (GetCompletionStatus(Id) == EProject_JQuestCompletionStatus::Completed) return static_cast<bool>(SubmitPersistentEdit);
    const auto* State = Before.States.FindByPredicate([Id](const auto& Value) { return Value.QuestId == Id; });
    auto* Definition = FindDefinition(Id);
    if (!State || !Definition || !Definition->IsComplete(*State)) return false;
    const int64 Reward = Definition->RewardExperience;
    auto Proposed = Before;
    if (!PlanClaimCompletion(Id, Proposed)) return false;
    if (SubmitPersistentEdit)
    {
        FProject_JPersistentEdit Edit; Edit.Kind = EProject_JPersistentEdit::Claim; Edit.Quest = Id;
        Edit.Request = CreatePersistentRequest ? CreatePersistentRequest() : FGuid();
        return SubmitPersistentEdit(Edit, {});
    }
    const TStrongObjectPtr<UProject_JProgressionComponent> Progression(GetOwner()->FindComponentByClass<UProject_JProgressionComponent>());
    if (!Progression) return false;
    if (!Definition->RewardItems.IsEmpty())
    {
        const auto* GI = GetOwner()->GetGameInstance();
        TArray<ProjectJ::Crafting::FResolvedLine> FrozenItems;
        if (!ValidateCommittedQuests(Proposed, true) ||
            !Definition->CaptureRewardItems(GI ? GI->GetSubsystem<UProject_JCharacterDataSubsystem>() : nullptr, FrozenItems)) return false;
        TGuardValue<bool> Guard(bChanging, true);
        return FProject_JNativeQuestRewardCommit::Apply(this, Definition, Before, Proposed, FrozenItems);
    }
    TGuardValue<bool> Guard(bChanging, true);
    // Completion ownership is installed before XP delegates. No pointer into States survives callbacks.
    InstallCommittedQuests(Proposed);
    if (Reward > 0 && Progression->GetNextLevelExperience() > 0 && !Progression->GrantExperience(Reward))
    {
        InstallCommittedQuests(Before); return false;
    }
    Publish(); return true;
}
bool UProject_JQuestComponent::AllowRequest()
{
	if (!CanMutate())
	{
		return false;
	}
	if (!UProject_JCommandBudgetComponent::AdmitClientCommand(GetOwner()))
	{
		return false;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (RequestWindow < 0 || Now - RequestWindow >= 1)
	{
		RequestWindow = Now;
		RequestCount = 0;
	}
	return ++RequestCount <= 20;
}
void UProject_JQuestComponent::RequestClaim(FName Id)
{
	if (CanPublish())
	{
		ServerClaim(Id);
	}
}
void UProject_JQuestComponent::ServerClaim_Implementation(FName Id)
{
	if (AllowRequest())
		Claim(Id);
}
void UProject_JQuestComponent::RequestTracking(FName Id, bool bTracked)
{
	if (CanPublish())
	{
		ServerTracking(Id, bTracked);
	}
}
void UProject_JQuestComponent::ServerTracking_Implementation(FName Id, bool bTracked)
{
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
	if (!AllowRequest())
		return;
	if (auto *State = States.FindByPredicate([Id](const auto &Value) { return Value.QuestId == Id; });
		State && !State->bClaimed && State->bTracked != bTracked)
	{
		if (SubmitPersistentEdit)
		{
			FProject_JPersistentEdit Edit;
			Edit.Kind = EProject_JPersistentEdit::SetQuestTracking;
			Edit.Quest = Id;
			Edit.Amount = bTracked ? 1 : 0;
			Edit.Request = CreatePersistentRequest ? CreatePersistentRequest() : FGuid();
			SubmitPersistentEdit(Edit, {});
			return;
		}
		State->bTracked = bTracked;
		Publish();
	}
}
void UProject_JQuestComponent::SampleTravel()
{
	SampleTravelAt(GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0);
}
void UProject_JQuestComponent::SampleTravelAt(double Now)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_QuestTravel);
	if (!CanPublish() || !GetOwner()->HasAuthority() || bChanging) return;
	FProject_JQuestDefinitionIndex Index;
	BuildDefinitionIndex(Index);
	TSet<FName> Events;
	for (const auto& State : States)
	{
		const auto* Def = Index.FindRef(State.QuestId);
		if (Def && Def->HasIncompleteTravel(State)) { if (Def->Objectives.IsEmpty()) Events.Add(Def->ObjectiveEvent); else Events.Add(NAME_None); }
	}
	if (Events.IsEmpty()) { ReconcileTravelSampling(&Index); return; }
	auto* PS = Cast<APlayerState>(GetOwner());
	auto* Pawn = PS ? PS->GetPawn() : nullptr;
	if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed()) { TravelPawn.Reset(); TravelRemainder = 0; LastTravelSampleTime = -1; LastTravelSpeed = 0; return; }
	const FVector Position = Pawn->GetActorLocation();
	const FVector Velocity = Pawn->GetVelocity();
	if (Position.ContainsNaN() || Velocity.ContainsNaN() || !FMath::IsFinite(Now) || Now < 0)
	{ TravelPawn.Reset(); TravelRemainder = 0; LastTravelSampleTime = -1; LastTravelSpeed = 0; return; }
	const double Speed = Velocity.Size2D();
	if (!FMath::IsFinite(Speed)) { TravelPawn.Reset(); TravelRemainder = 0; LastTravelSampleTime = -1; LastTravelSpeed = 0; return; }
	const double Elapsed = Now - LastTravelSampleTime;
	if (TravelPawn.Get() == Pawn && LastTravelSampleTime >= 0 && Elapsed > 0 && Elapsed <= 2.0)
	{
		const double Distance = FVector::Dist2D(Position, LastLocation);
		// Speed/time admission permits legitimate fast mounts while rejecting warp-sized
		// segments. This is quest accounting, not a replacement for movement authority.
		const double MaximumSpeed = FMath::Max(Speed, LastTravelSpeed);
		const double MaximumDistance = FMath::Min(100000.0, MaximumSpeed * Elapsed * 1.5 + 100.0);
		if (MaximumSpeed > UE_SMALL_NUMBER && Distance > 0 && Distance <= MaximumDistance)
			TravelRemainder = FMath::Min(1000000., TravelRemainder + Distance / 100.);
	}
	else if (TravelPawn.Get() != Pawn) TravelRemainder = 0;
	TravelPawn = Pawn; LastLocation = Position;
	LastTravelSampleTime = Now; LastTravelSpeed = Speed;
	// Only publish after the committed snapshot has been installed. Pending distance
	// is bounded and belongs to this pawn; it never mutates the in-flight snapshot.
	const int32 Count = FMath::FloorToInt(TravelRemainder);
	if (Count <= 0) return;
	if (SubmitPersistentProgress)
	{
		TArray<FProject_JQuestProgressInput> Inputs;
		CollectProgressInputs(NAME_None, Count, Index, &Events, Inputs);
		if (Inputs.IsEmpty() || SubmitPersistentProgress(Inputs)) TravelRemainder -= Count;
		return;
	}
	if (!CanMutate()) return;
	TravelRemainder -= Count;
	bool bChanged = false;
	for (auto& State : States)
	{
		const auto* Def = Index.FindRef(State.QuestId);
		if (!Def || State.bClaimed || !Def->IsStateValid(State)) continue;
		if (Def->Objectives.IsEmpty())
		{
			if (Events.Contains(Def->ObjectiveEvent) && State.Count < Def->RequiredCount)
			{ AddProgress(State.Count, Def->RequiredCount, Count); bChanged = true; }
		}
		else for (const auto& O : Def->Objectives)
			if (O.Kind == EProject_JQuestObjectiveKind::Travel)
			{
				auto* S = State.Objectives.FindByPredicate([&](const auto& V) { return V.ObjectiveId == O.ObjectiveId; });
				if (S->Count < O.RequiredCount) { AddProgress(S->Count, O.RequiredCount, Count); bChanged = true; }
			}
	}
	if (bChanged) Publish(&Index); // All objectives see one sample before delegates/checkpoint capture.
}
FProject_JQuestSnapshot UProject_JQuestComponent::CaptureSnapshot() const
{
    FProject_JQuestSnapshot Snapshot;
    Snapshot.States = States;
    if (!GetOwner() || !GetOwner()->HasAuthority())
    {
        Snapshot.Version = 3;
        Snapshot.HistoryCount = -1; // The owner-only summary is not a complete hydrated archive.
        return Snapshot;
    }
    Snapshot.History = History;
    Snapshot.HistoryCount = ProjectJ::QuestHistory::Count(History);
    Snapshot.HistoryRootDigest = ProjectJ::QuestHistory::RootDigest(History);
    Snapshot.Version = SnapshotVersion == 3 || Snapshot.HistoryCount > 0 ? 3 : QuestSnapshotVersion(States);
    return Snapshot;
}
bool UProject_JQuestComponent::InstallCommittedQuests(const FProject_JQuestSnapshot& Snapshot)
{
    if (!Snapshot.HasHydratedHistory()) return false;
    const bool Changed = States != Snapshot.States || ProjectJ::QuestHistory::Count(History) != Snapshot.HistoryCount
        || ProjectJ::QuestHistory::RootDigest(History) != Snapshot.HistoryRootDigest;
    States = Snapshot.States; History = Snapshot.History; SnapshotVersion = Snapshot.Version;
    RefreshHistorySummary();
    return Changed;
}
bool UProject_JQuestComponent::CanRestoreSnapshot(const FProject_JQuestSnapshot& Snapshot, bool bReplacePristineDefaults, bool bTrustedValidation) const
{
    const auto* StateOwner = Cast<APlayerState>(GetOwner());
    const bool Pristine = bReplacePristineDefaults && StateOwner && !StateOwner->GetPawn()
        && ProjectJ::QuestHistory::Count(History) == 0
        && !States.ContainsByPredicate([](const auto& State) { return State.Count != 0 || State.bClaimed || !State.bTracked
            || State.Objectives.ContainsByPredicate([](const auto& Objective) { return Objective.Count != 0; }); });
    return (bTrustedValidation ? CanPublish() && GetOwner()->HasAuthority() && !bChanging : CanMutate()) && (States.IsEmpty() || Pristine) && ProjectJ::QuestHistory::Count(History) == 0
        && ValidateCommittedQuests(Snapshot, bTrustedValidation);
}
bool UProject_JQuestComponent::RestoreSnapshot(const FProject_JQuestSnapshot& Snapshot, bool bReplacePristineDefaults)
{
    if (!CanRestoreSnapshot(Snapshot, bReplacePristineDefaults)) return false;
    InstallCommittedQuests(Snapshot); Publish(); return true;
}
bool UProject_JQuestComponent::ValidateCommittedQuests(const FProject_JQuestSnapshot& Snapshot, bool bTrustedApply) const
{
    const bool Admitted = bTrustedApply ? CanPublish() && GetOwner()->HasAuthority() && !bChanging : CanMutate();
    if (!Admitted || !SnapshotShape(Snapshot, false)) return false;
    FProject_JQuestDefinitionIndex Index;
    BuildDefinitionIndex(Index);
    const bool SameHistory = Snapshot.History == History;
    TSet<FName> CurrentIds;
    if (SameHistory) for (const auto& Current : States) CurrentIds.Add(Current.QuestId);
    for (const auto& State : Snapshot.States)
    {
        const auto* Definition = Index.FindRef(State.QuestId);
        if (!Definition || !Definition->IsStateValid(State)) return false;
        // Existing current IDs were validated when installed. Hot progress must not probe history again.
        const bool ExistingCurrent = SameHistory && CurrentIds.Contains(State.QuestId);
        if (!ExistingCurrent && ProjectJ::QuestHistory::Contains(Snapshot.History, State.QuestId)) return false;
    }
    return true;
}

int32 UProject_JQuestComponent::GetCompletedCount() const
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return HistorySummary.Revision.IsValid() ? HistorySummary.Count : 0;
    int32 Count = ProjectJ::QuestHistory::Count(History);
    for (const auto& State : States) if (State.bClaimed) ++Count;
    return Count;
}
EProject_JQuestCompletionStatus UProject_JQuestComponent::GetCompletionStatus(FName Id) const
{
    if (!ProjectJ::QuestHistory::IsValidId(Id) || !CanPublish()) return EProject_JQuestCompletionStatus::Unknown;
    if (GetOwner()->HasAuthority())
    {
        if (ProjectJ::QuestHistory::Contains(History, Id)
            || States.ContainsByPredicate([Id](const auto& State) { return State.QuestId == Id && State.bClaimed; }))
            return EProject_JQuestCompletionStatus::Completed;
        return EProject_JQuestCompletionStatus::NotCompleted;
    }
    if (!HistorySummary.Revision.IsValid()) return EProject_JQuestCompletionStatus::Unknown;
    if (HistorySummary.Count == 0) return EProject_JQuestCompletionStatus::NotCompleted;
    if (HistoryPage.bHasPage && HistoryPage.Summary == HistorySummary && HistoryPage.Ids.Contains(Id))
        return EProject_JQuestCompletionStatus::Completed;
    return EProject_JQuestCompletionStatus::Unknown;
}
void UProject_JQuestComponent::RefreshHistorySummary()
{
    if (!CanPublish() || !GetOwner()->HasAuthority()) return;
    // Legacy rows are bounded128. Paged mode is O(1); never flatten or hash the full history here.
    TArray<FName> LegacyIds;
    for (const auto& State : States) if (State.bClaimed) LegacyIds.Add(State.QuestId);
    const FString& Root = ProjectJ::QuestHistory::RootDigest(History);
    const int32 Count = ProjectJ::QuestHistory::Count(History) + LegacyIds.Num();
    if (HistorySummary.Revision.IsValid() && HistorySummary.Count == Count && SummaryRootDigest == Root && SummaryLegacyIds == LegacyIds) return;
    HistorySummary.Count = Count; HistorySummary.Revision = FGuid::NewGuid();
    SummaryRootDigest = Root; SummaryLegacyIds = MoveTemp(LegacyIds);
    if (HistoryPage.bHasPage && !(HistoryPage.Summary == HistorySummary)) HistoryPage.Status = EProject_JQuestHistoryPageStatus::Stale;
}
bool UProject_JQuestComponent::GetCompletionPageValues(int32 Ordinal, TArray<FName>& Out) const
{
    if (Ordinal < 0 || Ordinal >= ProjectJ::QuestHistory::MaxIds / ProjectJ::QuestHistory::QueryPageSize) return false;
    if (ProjectJ::QuestHistory::Count(History) > 0)
        return ProjectJ::QuestHistory::GetRecentPage(History, Ordinal * ProjectJ::QuestHistory::QueryPageSize,
            ProjectJ::QuestHistory::QueryPageSize, Out);
    TArray<FName> LegacyIds;
    for (int32 Index = States.Num() - 1; Index >= 0; --Index) if (States[Index].bClaimed) LegacyIds.Add(States[Index].QuestId);
    const int32 Offset = Ordinal * ProjectJ::QuestHistory::QueryPageSize;
    if (LegacyIds.IsEmpty() ? Offset != 0 : Offset >= LegacyIds.Num()) return false;
    TArray<FName> Result;
    for (int32 Index = Offset; Index < LegacyIds.Num() && Result.Num() < ProjectJ::QuestHistory::QueryPageSize; ++Index) Result.Add(LegacyIds[Index]);
    Out = MoveTemp(Result); return true;
}
FProject_JQuestHistoryPageView UProject_JQuestComponent::GetHistoryPage() const
{
    auto Result = HistoryPage;
    if (PendingHistoryRequest.IsValid())
    {
        Result.Request = PendingHistoryRequest; Result.RequestedOrdinal = PendingHistoryOrdinal;
        Result.Status = FPlatformTime::Seconds() - HistoryRequestStarted >= 10 ? EProject_JQuestHistoryPageStatus::Unknown : EProject_JQuestHistoryPageStatus::Pending;
    }
    else if (Result.bHasPage && !(Result.Summary == HistorySummary)) Result.Status = EProject_JQuestHistoryPageStatus::Stale;
    return Result;
}
FGuid UProject_JQuestComponent::RequestCompletedPage(int32 Ordinal)
{
    if (!CanPublish() || bRequestingHistoryPage || !HistorySummary.Revision.IsValid() || Ordinal < 0
        || Ordinal >= ProjectJ::QuestHistory::MaxIds / ProjectJ::QuestHistory::QueryPageSize) return {};
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
    const auto* RequestOwner = Cast<AProject_JPlayerState>(GetOwner());
    if (!RequestOwner) return {};
    const FGuid Character = RequestOwner->GetCharacterId().Value;
    const FGuid Request = FGuid::NewGuid(), Revision = HistorySummary.Revision;
    PendingHistoryRequest = Request; PendingHistoryRevision = Revision; PendingHistoryCharacter = Character; PendingHistoryOrdinal = Ordinal;
    HistoryRequestStarted = FPlatformTime::Seconds();
    TGuardValue<bool> Guard(bRequestingHistoryPage, true);
    OnHistoryPageChanged.Broadcast();
    if (CanPublish() && PendingHistoryRequest == Request && HistorySummary.Revision == Revision
        && GetOwner() == RequestOwner && RequestOwner->GetCharacterId().Value == Character)
        ServerRequestCompletedPage(Request, Ordinal, Revision);
    return Request;
}
void UProject_JQuestComponent::CancelHistoryPageRequest(FGuid ExpectedNonce)
{
    if (!ExpectedNonce.IsValid() || PendingHistoryRequest != ExpectedNonce) return;
    PendingHistoryRequest.Invalidate(); PendingHistoryRevision.Invalidate(); HistoryRequestStarted = -1;
}
void UProject_JQuestComponent::ServerRequestCompletedPage_Implementation(FGuid Request, int32 Ordinal, FGuid ExpectedRevision)
{
    if (!CanPublish() || !GetOwner()->HasAuthority() || !Request.IsValid() || !ExpectedRevision.IsValid()
        || Ordinal < 0 || Ordinal >= ProjectJ::QuestHistory::MaxIds / ProjectJ::QuestHistory::QueryPageSize) return;
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<AProject_JPlayerState> KeepState(Cast<AProject_JPlayerState>(GetOwner()));
    if (!KeepState) return;
    const auto Session = KeepState->CaptureServerSession();
    const FGuid Character = KeepState->GetCharacterId().Value;
    const auto IsCurrent = [&]
    {
        return CanPublish() && GetOwner() == KeepState.Get() && KeepState->GetCharacterId().Value == Character
            && KeepState->FindComponentByClass<ThisClass>() == this && KeepState->IsServerSessionCurrent(Session);
    };
    if (!IsCurrent() || !UProject_JCommandBudgetComponent::AdmitClientCommand(KeepState.Get()) || !IsCurrent()) return;
    const double Now = FPlatformTime::Seconds();
    if (Now < NextHistoryRequestTime) return;
    NextHistoryRequestTime = Now + 0.5;
    RefreshHistorySummary();
    TArray<FName> Ids;
    const bool Accepted = ExpectedRevision == HistorySummary.Revision && GetCompletionPageValues(Ordinal, Ids);
    if (IsCurrent()) ClientCompletedPage(Request, Ordinal, HistorySummary, Ids, Accepted);
}
void UProject_JQuestComponent::ClientCompletedPage_Implementation(FGuid Request, int32 Ordinal,
    FProject_JQuestHistorySummary Summary, const TArray<FName>& Ids, bool bAccepted)
{
    if (!CanPublish() || !Request.IsValid() || PendingHistoryRequest != Request || PendingHistoryOrdinal != Ordinal) return;
    const auto* ReplyOwner = Cast<AProject_JPlayerState>(GetOwner());
    const bool Matches = ReplyOwner && ReplyOwner->GetCharacterId().Value == PendingHistoryCharacter
        && PendingHistoryRevision == Summary.Revision && Summary == HistorySummary;
    PendingHistoryRequest.Invalidate(); PendingHistoryRevision.Invalidate(); HistoryRequestStarted = -1;
    bool ValidIds = Ids.Num() <= ProjectJ::QuestHistory::QueryPageSize;
    TSet<FName> Seen;
    for (FName Id : Ids) { if (!ProjectJ::QuestHistory::IsValidId(Id) || Seen.Contains(Id)) ValidIds = false; Seen.Add(Id); }
    const bool ValidShape = Summary.Count >= 0 && Summary.Count <= ProjectJ::QuestHistory::MaxIds
        && Ordinal >= 0 && Ordinal < ProjectJ::QuestHistory::MaxIds / ProjectJ::QuestHistory::QueryPageSize;
    const int32 Offset = ValidShape ? Ordinal * ProjectJ::QuestHistory::QueryPageSize : 0;
    const int32 ExpectedSize = ValidShape ? FMath::Min(ProjectJ::QuestHistory::QueryPageSize, Summary.Count - Offset) : -1;
    if (bAccepted && Matches && ValidIds && ValidShape && ExpectedSize >= 0 && Ids.Num() == ExpectedSize)
    {
        HistoryPage.Summary = Summary; HistoryPage.Ordinal = Ordinal; HistoryPage.Ids = Ids;
        HistoryPage.Request = Request; HistoryPage.RequestedOrdinal = Ordinal;
        HistoryPage.bHasPage = true; HistoryPage.Status = EProject_JQuestHistoryPageStatus::Fresh;
    }
    else HistoryPage.Status = HistoryPage.bHasPage && !(HistoryPage.Summary == HistorySummary)
        ? EProject_JQuestHistoryPageStatus::Stale : EProject_JQuestHistoryPageStatus::Rejected;
    OnHistoryPageChanged.Broadcast();
}
void UProject_JQuestComponent::OnRep_HistorySummary()
{
    if (!CanPublish()) return;
    if (HistoryPage.bHasPage && !(HistoryPage.Summary == HistorySummary)) HistoryPage.Status = EProject_JQuestHistoryPageStatus::Stale;
    OnHistoryPageChanged.Broadcast();
}
