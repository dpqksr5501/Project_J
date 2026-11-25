#include "Backend/Project_JOnlineQuests.h"

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
	Out = Quests.CaptureSnapshot();
	Out.States.RemoveAll([&](const auto& Existing) {
		const auto* Definition = Index.FindRef(Existing.QuestId);
		return !Definition || !Definition->bTravelObjective;
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
	if (Out.Version < 3) Out.Version = Out.States.ContainsByPredicate([](const auto& State) { return !State.Objectives.IsEmpty(); }) ? 2 : 1;
	return Quests.ValidateCommittedQuests(Out, true);
}
}
