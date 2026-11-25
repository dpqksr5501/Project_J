#include "Backend/Project_JOnlineQuests.h"

#include "CharacterClass/Project_JProgressionComponent.h"
#include "Dom/JsonObject.h"

namespace ProjectJ::OnlineQuests
{
namespace
{
bool ReadName(const FJsonObject& Object, const TCHAR* Field, FName& Out)
{
	FString Text;
	if (!Object.TryGetStringField(Field, Text) || Text.IsEmpty() || Text.Len() > 64) return false;
	Out = FName(*Text, FNAME_Find);
	return !Out.IsNone();
}
}

static TSharedRef<FJsonObject> WriteDefinition(const UProject_JQuestDefinition& Definition)
{
	auto Row = MakeShared<FJsonObject>();
	Row->SetStringField(TEXT("id"), Definition.QuestId.ToString().ToLower());
	Row->SetNumberField(TEXT("count"), 0);
	Row->SetBoolField(TEXT("tracked"), true);
	Row->SetNumberField(TEXT("definition_version"), Definition.SchemaVersion);
	if (Definition.Objectives.IsEmpty())
	{
		if (!Definition.bTravelObjective) Row->SetStringField(TEXT("event"), Definition.ObjectiveEvent.ToString());
		Row->SetNumberField(TEXT("required"), Definition.RequiredCount);
	}
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const auto& Goal : Definition.Objectives)
	{
		auto Value = MakeShared<FJsonObject>();
		Value->SetStringField(TEXT("id"), Goal.ObjectiveId.ToString());
		Value->SetNumberField(TEXT("count"), 0);
		Value->SetNumberField(TEXT("required"), Goal.RequiredCount);
		if (Goal.Kind == EProject_JQuestObjectiveKind::Event) Value->SetStringField(TEXT("event"), Goal.Event.ToString());
		else if (Goal.Kind == EProject_JQuestObjectiveKind::CraftRecipe)
		{
			Value->SetStringField(TEXT("recipe"), Goal.TargetId.ToString());
			Value->SetNumberField(TEXT("version"), Goal.TargetVersion);
		}
		else if (Goal.Kind == EProject_JQuestObjectiveKind::GatherItem)
		{
			Value->SetStringField(TEXT("gather"), Goal.TargetId.ToString());
			Value->SetNumberField(TEXT("version"), Goal.TargetVersion);
		}
		Values.Add(MakeShared<FJsonValueObject>(Value));
	}
	Row->SetArrayField(TEXT("objectives"), MoveTemp(Values));
	return Row;
}

bool Prepare(const UProject_JQuestComponent& Quests, const FProject_JPersistentEdit& Edit, FJsonObject& Command)
{
	const FString Key = Edit.Quest.ToString().ToLower();
	if (Edit.Quest.IsNone() || Key.Len() > 64) return false;
	Command.SetStringField(TEXT("quest"), Key);
	if (Edit.Kind == EProject_JPersistentEdit::SetQuestTracking)
	{
		Command.SetStringField(TEXT("kind"), TEXT("track"));
		Command.SetBoolField(TEXT("tracked"), Edit.Amount != 0);
		return true;
	}
	if (Edit.Kind != EProject_JPersistentEdit::AcceptCompoundQuest) return false;
	auto Proposal = Quests.CaptureSnapshot();
	if (!Quests.PlanAcceptance(Edit.Quest, Proposal)) return false;
	FProject_JQuestDefinitionIndex Index;
	Quests.BuildDefinitionIndex(Index);
	const auto* Definition = Index.FindRef(Edit.Quest);
	if (!Definition) return false;
	Command.SetStringField(TEXT("kind"), TEXT("accept"));
	Command.SetObjectField(TEXT("row"), WriteDefinition(*Definition));
	return true;
}

bool PrepareClaim(const UProject_JQuestComponent& Quests, const UProject_JProgressionComponent& Progression,
	const UProject_JCharacterDataSubsystem& Definitions, FName Quest, int64 ProfileRevision, FJsonObject& Command)
{
	const auto* Definition = Quests.FindDefinition(Quest);
	if (!Definition || !Definition->IsRuntimeValid() || ProfileRevision <= 0) return false;
	const auto Snapshot = Quests.CaptureSnapshot();
	const auto* State = Snapshot.States.FindByPredicate([Quest](const auto& Value) { return Value.QuestId == Quest; });
	if (!State || !Definition->IsComplete(*State)) return false;
	FProject_JProgressionSnapshot Experience = Progression.CaptureSnapshot();
	if (Definition->RewardExperience > 0 && Progression.GetNextLevelExperience() > 0
		&& !Progression.PlanExperience(Definition->RewardExperience, Experience, true)) return false;
	TArray<Crafting::FResolvedLine> Rewards;
	if (!Definition->CaptureRewardItems(&Definitions, Rewards)) return false;
	TArray<TSharedPtr<FJsonValue>> Items;
	Items.Reserve(Rewards.Num());
	for (const auto& Reward : Rewards)
	{
		auto Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("definition"), Reward.Definition.ToString());
		Item->SetNumberField(TEXT("version"), Reward.DefinitionVersion);
		Item->SetNumberField(TEXT("level"), Reward.Level);
		Item->SetNumberField(TEXT("quantity"), Reward.CountPerBatch);
		Item->SetNumberField(TEXT("max_stack"), Reward.MaxStackCount);
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}
	Command.SetStringField(TEXT("kind"), TEXT("claim"));
	Command.SetStringField(TEXT("quest"), Quest.ToString().ToLower());
	Command.SetNumberField(TEXT("definition_version"), Definition->SchemaVersion);
	Command.SetStringField(TEXT("expected_profile_revision"), LexToString(ProfileRevision));
	Command.SetNumberField(TEXT("reward_level"), Experience.Level);
	Command.SetStringField(TEXT("reward_xp"), LexToString(Experience.Experience));
	Command.SetArrayField(TEXT("reward_items"), MoveTemp(Items));
	return true;
}

bool PrepareProgress(const UProject_JQuestComponent& Quests, TConstArrayView<FProject_JQuestProgressInput> Inputs, FJsonObject& Command)
{
	FProject_JQuestDefinitionIndex Index;
	Quests.BuildDefinitionIndex(Index);
	TMap<FName, TMap<FName, int32>> ByQuest;
	for (const auto& Input : Inputs)
	{
		if (Input.Amount <= 0 || Input.Amount > 1000000 || !Index.Contains(Input.Quest)) return false;
		int32& Amount = ByQuest.FindOrAdd(Input.Quest).FindOrAdd(Input.Objective);
		Amount = FMath::Min(1000000, Amount + Input.Amount);
	}
	TArray<TSharedPtr<FJsonValue>> Changes;
	for (auto& Pair : ByQuest)
	{
		auto Group = MakeShared<FJsonObject>();
		Group->SetObjectField(TEXT("row"), WriteDefinition(*Index.FindChecked(Pair.Key)));
		TArray<TSharedPtr<FJsonValue>> Deltas;
		for (const auto& [Objective, Amount] : Pair.Value)
		{
			auto Delta = MakeShared<FJsonObject>();
			Delta->SetStringField(TEXT("id"), Objective.IsNone() ? TEXT("") : Objective.ToString());
			Delta->SetNumberField(TEXT("amount"), Amount);
			Deltas.Add(MakeShared<FJsonValueObject>(Delta));
		}
		Group->SetArrayField(TEXT("deltas"), MoveTemp(Deltas));
		Changes.Add(MakeShared<FJsonValueObject>(Group));
	}
	Command.SetStringField(TEXT("kind"), TEXT("progress"));
	Command.SetArrayField(TEXT("changes"), MoveTemp(Changes));
	return true;
}

bool Read(const UProject_JQuestComponent& Quests, const FJsonObject& Reply, FProject_JQuestSnapshot& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (!Reply.TryGetArrayField(TEXT("quests"), Rows) || Rows->Num() > UProject_JQuestComponent::MaxCurrentQuestStates) return false;
	FProject_JQuestDefinitionIndex Index;
	Quests.BuildDefinitionIndex(Index);
	const TArray<TSharedPtr<FJsonValue>>* TravelClaims = nullptr;
	if (!Reply.TryGetArrayField(TEXT("completed_travel_quests"), TravelClaims)
		|| TravelClaims->Num() > UProject_JQuestComponent::MaxCurrentQuestStates) return false;
	TArray<FName> CompletedIds;
	for (const auto& Value : *TravelClaims)
	{
		FString Id;
		if (!Value || !Value->TryGetString(Id) || Id.IsEmpty() || Id.Len() > 64) return false;
		const FName Name(*Id, FNAME_Find);
		const auto* Definition = Index.FindRef(Name);
		if (Definition && Definition->bTravelObjective) CompletedIds.AddUnique(Name);
	}
	if (Reply.HasField(TEXT("completed_quest")))
	{
		FName Completed;
		if (!ReadName(Reply, TEXT("completed_quest"), Completed)) return false;
		CompletedIds.AddUnique(Completed);
	}
	Out = Quests.CaptureSnapshot();
	Out.States.RemoveAll([&](const auto& Existing) {
		const auto* Definition = Index.FindRef(Existing.QuestId);
		return !Definition || !Definition->bTravelObjective || CompletedIds.Contains(Existing.QuestId);
	});
	for (const auto& Value : *Rows)
	{
		const TSharedPtr<FJsonObject>* Row = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Goals = nullptr;
		FProject_JQuestState State;
		int32 Version = 0;
		if (!Value || !Value->TryGetObject(Row) || !Row || !ReadName(**Row, TEXT("id"), State.QuestId)
			|| !(*Row)->TryGetNumberField(TEXT("count"), State.Count)
			|| !(*Row)->TryGetBoolField(TEXT("tracked"), State.bTracked)
			|| !(*Row)->TryGetNumberField(TEXT("definition_version"), Version)
			|| !(*Row)->TryGetArrayField(TEXT("objectives"), Goals) || Goals->Num() > 64) return false;
		const auto* Definition = Index.FindRef(State.QuestId);
		if (!Definition || Definition->SchemaVersion != Version) return false;
		for (const auto& Goal : *Goals)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			auto& Progress = State.Objectives.AddDefaulted_GetRef();
			if (!Goal || !Goal->TryGetObject(Object) || !Object || !ReadName(**Object, TEXT("id"), Progress.ObjectiveId)
				|| !(*Object)->TryGetNumberField(TEXT("count"), Progress.Count)) return false;
		}
		State.Objectives.Sort([](const auto& A, const auto& B) { return A.ObjectiveId.LexicalLess(B.ObjectiveId); });
		Out.States.RemoveAll([&](const auto& Existing) { return Existing.QuestId == State.QuestId; });
		Out.States.Add(MoveTemp(State));
	}
	for (FName Completed : CompletedIds)
	{
		if (Out.States.ContainsByPredicate([Completed](const auto& State) { return State.QuestId == Completed; })) return false;
		if (!QuestHistory::Contains(Out.History, Completed))
		{
			QuestHistory::FViewPtr History;
			FString Failure;
			if (!QuestHistory::Append(Out.History, Completed, History, Failure)) return false;
			Out.History = MoveTemp(History);
			Out.HistoryCount = QuestHistory::Count(Out.History);
			Out.HistoryRootDigest = QuestHistory::RootDigest(Out.History);
			Out.Version = 3;
		}
	}
	if (Out.Version < 3) Out.Version = Out.States.ContainsByPredicate([](const auto& State) { return !State.Objectives.IsEmpty(); }) ? 2 : 1;
	return Quests.ValidateCommittedQuests(Out, true);
}

bool ReadHistoryPage(const FJsonObject& Reply, int32 Ordinal, int32& Count, TArray<FName>& Ids)
{
	FString Status;
	int32 Observed = INDEX_NONE;
	if (!Reply.TryGetStringField(TEXT("status"), Status) || !Reply.TryGetNumberField(TEXT("count"), Observed)
		|| Observed < 0 || Observed > QuestHistory::MaxIds || Ordinal < 0
		|| Ordinal >= QuestHistory::MaxIds / QuestHistory::QueryPageSize) return false;
	if (Status == TEXT("STALE"))
	{
		Count = Observed;
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	const int32 PageSize = FMath::Min(QuestHistory::QueryPageSize, Observed - Ordinal * QuestHistory::QueryPageSize);
	if (Status != TEXT("OK") || (PageSize <= 0 && (Observed != 0 || Ordinal != 0))
		|| !Reply.TryGetArrayField(TEXT("ids"), Rows) || Rows->Num() != PageSize) return false;
	TArray<FName> Page;
	Page.Reserve(PageSize);
	for (const auto& Row : *Rows)
	{
		FString Id;
		if (!Row || !Row->TryGetString(Id) || Id.IsEmpty() || Id.Len() > QuestHistory::MaxIdLength) return false;
		// The DB can contain IDs whose definitions were retired since their claim.
		const FName Name(*Id);
		if (!QuestHistory::IsValidId(Name) || Page.Contains(Name)) return false;
		Page.Add(Name);
	}
	Ids = MoveTemp(Page);
	Count = Observed;
	return true;
}
}
