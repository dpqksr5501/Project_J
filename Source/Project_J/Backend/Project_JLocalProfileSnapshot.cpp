#include "Backend/Project_JLocalProfileSnapshot.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool HistoryDigest(const FString& Value)
{
    if (Value.Len() != ProjectJ::QuestHistory::DigestLength) return false;
    for (TCHAR Ch : Value) if (!((Ch >= '0' && Ch <= '9') || (Ch >= 'A' && Ch <= 'F'))) return false;
    return true;
}
bool Integer(const FJsonObject &Object, const TCHAR *Field, int32 &Out)
{
	double Value = 0;
	if (!Object.TryGetNumberField(Field, Value) || !FMath::IsFinite(Value) || Value < 0 || Value > MAX_int32 ||
		Value != FMath::FloorToDouble(Value))
		return false;
	Out = static_cast<int32>(Value);
	return true;
}
bool Name(const FString &Value, FName &Out, bool bAllowNone = false)
{
	if (Value.IsEmpty() || Value.Len() > 128)
		return false;
	for (TCHAR Ch : Value)
		if (!Ch)
			return false;
	Out = FName(*Value);
	return bAllowNone || !Out.IsNone();
}

TSharedPtr<FJsonObject> EncodeItem(const FProject_JPersistentItem& Item)
{
	if (Item.Order == MAX_int32) return {};
	auto V = MakeShared<FJsonObject>();
	V->SetStringField(TEXT("instance"), Item.Instance.ToString(EGuidFormats::DigitsWithHyphens));
	V->SetStringField(TEXT("definition"), Item.Definition.ToString().ToLower());
	V->SetNumberField(TEXT("version"), Item.DefinitionVersion);
	V->SetNumberField(TEXT("level"), Item.Level);
	V->SetNumberField(TEXT("count"), Item.Count);
	V->SetNumberField(TEXT("order"), Item.Order + 1);
	V->SetNumberField(TEXT("slot"), uint8(Item.Slot));
	V->SetBoolField(TEXT("bag"), Item.bInBag);
	return V;
}
bool DecodeItem(const FJsonObject& V, FProject_JPersistentItem& Item)
{
	FString Id, Def; int32 Order = 0, Slot = 0;
	if (V.Values.Num() != 8 || !V.TryGetStringField(TEXT("instance"), Id) ||
		!FGuid::ParseExact(Id, EGuidFormats::DigitsWithHyphens, Item.Instance) || !Item.Instance.IsValid() ||
		!V.TryGetStringField(TEXT("definition"), Def) || !Name(Def, Item.Definition) ||
		!Integer(V, TEXT("version"), Item.DefinitionVersion) || Item.DefinitionVersion < 1 ||
		!Integer(V, TEXT("level"), Item.Level) || Item.Level < 1 || Item.Level > 10000 ||
		!Integer(V, TEXT("count"), Item.Count) || Item.Count < 1 || Item.Count > 1000000 ||
		!Integer(V, TEXT("order"), Order) || Order == MAX_int32 ||
		!Integer(V, TEXT("slot"), Slot) || Slot > int32(EProject_JEquipmentSlot::Mount) ||
		!V.TryGetBoolField(TEXT("bag"), Item.bInBag)) return false;
	Item.Order = Order - 1; Item.Slot = EProject_JEquipmentSlot(Slot);
	return true;
}
bool CanonicalTicks(const FJsonObject& Object, const TCHAR* Field, int64& Out)
{
	const TSharedPtr<FJsonValue> Value = Object.TryGetField(Field);
	if (!Value.IsValid() || Value->Type != EJson::String) return false;
	const FString Text = Value->AsString();
	if (Text.IsEmpty() || Text.Len() > 19 || (Text.Len() > 1 && Text[0] == '0')) return false;
	int64 Parsed = 0;
	for (const TCHAR Digit : Text)
	{
		if (Digit < '0' || Digit > '9' || Parsed > (MAX_int64 - (Digit - '0')) / 10) return false;
		Parsed = Parsed * 10 + (Digit - '0');
	}
	Out = Parsed;
	return true;
}
} // namespace
bool FProject_JLocalProfileSnapshot::Encode(TArray<uint8> &Out) const
{
    const int32 StoredFormat = Quests.HistoryCount > 0 ? 4 : Format;
    if ((Format != 1 && Format != 2 && Format != 3 && Format != 4) ||
        (Format == 1 && (!Items.IsEmpty() || Quests.HistoryCount > 0)) ||
        (StoredFormat < 3 && !CraftingJobs.IsEmpty()) || !ProjectJ::Crafting::ValidateJobState(Items, CraftingJobs) ||
        !Quests.HasHydratedHistory()) return false;
	if (!Account.IsValid() || !Character.IsValid() || RulesFingerprint.IsEmpty())
		return false;
	auto Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("format"), StoredFormat);
	Root->SetStringField(TEXT("account"), Account.ToString(EGuidFormats::DigitsWithHyphens));
	Root->SetStringField(TEXT("character"), Character.ToString(EGuidFormats::DigitsWithHyphens));
	Root->SetStringField(TEXT("rules"), RulesFingerprint);
	auto P = MakeShared<FJsonObject>();
	P->SetNumberField(TEXT("schema"), Progression.SchemaVersion);
	P->SetStringField(TEXT("class"), Progression.ClassId.ToString().ToLower());
	P->SetNumberField(TEXT("level"), Progression.Level);
	P->SetStringField(TEXT("xp"),
					  FString::Printf(TEXT("%lld"), Progression.Experience)); // No int64 -> JSON double rounding.
	TArray<TSharedPtr<FJsonValue>> History;
	for (const auto Id : Progression.AdvancementHistory)
		History.Add(MakeShared<FJsonValueString>(Id.ToString().ToLower()));
	P->SetArrayField(TEXT("history"), History);
	Root->SetObjectField(TEXT("progression"), P);
	auto Q = MakeShared<FJsonObject>();
	Q->SetNumberField(TEXT("schema"), StoredFormat == 4 ? 3 : Quests.Version);
    if (StoredFormat == 4)
    {
        Q->SetNumberField(TEXT("history_count"), Quests.HistoryCount);
        Q->SetStringField(TEXT("history_root"), Quests.HistoryRootDigest);
    }
	TArray<TSharedPtr<FJsonValue>> States;
	for (const auto &State : Quests.States)
	{
		auto Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("id"), State.QuestId.ToString().ToLower());
		Item->SetNumberField(TEXT("count"), State.Count);
		Item->SetBoolField(TEXT("claimed"), State.bClaimed);
		Item->SetBoolField(TEXT("tracked"), State.bTracked);
		if (!State.Objectives.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> Objectives;
			for (const auto& StateObjective : State.Objectives)
			{
				auto O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("id"), StateObjective.ObjectiveId.ToString().ToLower());
				O->SetNumberField(TEXT("count"), StateObjective.Count);
				Objectives.Add(MakeShared<FJsonValueObject>(O));
			}
			Item->SetArrayField(TEXT("objectives"), Objectives);
		}
		States.Add(MakeShared<FJsonValueObject>(Item));
	}
	Q->SetArrayField(TEXT("states"), States);
	Root->SetObjectField(TEXT("quests"), Q);
	if (StoredFormat >= 2)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const auto& Item : Items)
		{
			auto V = EncodeItem(Item); if (!V) return false;
			Values.Add(MakeShared<FJsonValueObject>(V));
		}
		Root->SetArrayField(TEXT("items"), Values);
	}
	if (StoredFormat >= 3)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const auto& Job : CraftingJobs)
		{
			auto J = MakeShared<FJsonObject>();
			J->SetNumberField(TEXT("schema"), Job.SchemaVersion);
			J->SetStringField(TEXT("id"), Job.Id.ToString(EGuidFormats::DigitsWithHyphens));
			J->SetStringField(TEXT("recipe"), Job.RecipeId.ToString().ToLower());
			J->SetNumberField(TEXT("recipe_version"), Job.RecipeVersion);
			J->SetNumberField(TEXT("batches"), Job.Batches);
			J->SetStringField(TEXT("rules"), Job.RulesDigest);
			J->SetStringField(TEXT("started_utc_ticks"), FString::Printf(TEXT("%lld"), Job.StartedUtcTicks));
			J->SetStringField(TEXT("due_utc_ticks"), FString::Printf(TEXT("%lld"), Job.DueUtcTicks));
			TArray<TSharedPtr<FJsonValue>> Escrow, Outputs;
			for (const auto& Item : Job.Escrow)
			{
				auto V = EncodeItem(Item); if (!V) return false;
				Escrow.Add(MakeShared<FJsonValueObject>(V));
			}
			for (const auto& Line : Job.Outputs)
			{
				auto L = MakeShared<FJsonObject>();
				L->SetStringField(TEXT("definition"), Line.Definition.ToString().ToLower());
				L->SetNumberField(TEXT("version"), Line.DefinitionVersion);
				L->SetNumberField(TEXT("level"), Line.Level);
				L->SetNumberField(TEXT("per_batch"), Line.CountPerBatch);
				L->SetNumberField(TEXT("max_stack"), Line.MaxStackCount);
				Outputs.Add(MakeShared<FJsonValueObject>(L));
			}
			J->SetArrayField(TEXT("escrow"), Escrow); J->SetArrayField(TEXT("outputs"), Outputs);
			Values.Add(MakeShared<FJsonValueObject>(J));
		}
		Root->SetArrayField(TEXT("jobs"), Values);
	}
	FString Text;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
	if (!FJsonSerializer::Serialize(Root, Writer))
		return false;
	FTCHARToUTF8 Bytes(*Text);
	if (Bytes.Length() > 65536)
		return false;
	TArray<uint8> Encoded;
	Encoded.Append(reinterpret_cast<const uint8 *>(Bytes.Get()), Bytes.Length());
	// The writer must not commit a document that its own bounded reader rejects.
    FProject_JStoredProfileCore Validated;
    if (!DecodeStoredCore(Encoded, Validated) || Validated.HotValues.Quests.HistoryCount != Quests.HistoryCount ||
        Validated.HotValues.Quests.HistoryRootDigest != Quests.HistoryRootDigest) return false;
	Out = MoveTemp(Encoded);
	return true;
}
bool FProject_JLocalProfileSnapshot::Decode(const TArray<uint8>& Bytes, FProject_JLocalProfileSnapshot& Out)
{
    FProject_JStoredProfileCore Core;
    if (!DecodeStoredCore(Bytes, Core) || !Core.HotValues.Quests.HasHydratedHistory()) return false;
    Out = MoveTemp(Core.HotValues);
    return true;
}
bool FProject_JLocalProfileSnapshot::DecodeStoredCore(const TArray<uint8>& Bytes, FProject_JStoredProfileCore& Out)
{
	if (Bytes.IsEmpty() || Bytes.Num() > 65536)
		return false;
	FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR *>(Bytes.GetData()), Bytes.Num());
	auto Reader = TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get()));
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		return false;
	FProject_JLocalProfileSnapshot Result;
	int32 Format = 0;
	FString AccountText, CharacterText;
	// These persisted formats are closed shapes. Silently ignoring a new field
	// would discard it on the next confirmed encode; require an explicit format
	// or migration instead. Required typed fields and business bounds follow.
	if (!Integer(*Root, TEXT("format"), Format) || (Format != 1 && Format != 2 && Format != 3 && Format != 4) ||
		Root->Values.Num() != (Format == 1 ? 6 : Format == 2 ? 7 : 8) ||
		(Format < 3 && Root->HasField(TEXT("jobs"))) || (Format == 1 && Root->HasField(TEXT("items"))) ||
		!Root->TryGetStringField(TEXT("account"), AccountText) ||
		!Root->TryGetStringField(TEXT("character"), CharacterText) ||
		!FGuid::ParseExact(AccountText, EGuidFormats::DigitsWithHyphens, Result.Account) ||
		!FGuid::ParseExact(CharacterText, EGuidFormats::DigitsWithHyphens, Result.Character) ||
		!Result.Account.IsValid() || !Result.Character.IsValid() ||
		!Root->TryGetStringField(TEXT("rules"), Result.RulesFingerprint) || Result.RulesFingerprint.Len() != 40)
		return false;
	const TSharedPtr<FJsonObject> *P = nullptr;
	const TSharedPtr<FJsonObject> *Q = nullptr;
	if (!Root->TryGetObjectField(TEXT("progression"), P) || !P || !P->IsValid() ||
		!Root->TryGetObjectField(TEXT("quests"), Q) || !Q || !Q->IsValid() ||
		(*P)->Values.Num() != 5 || (*Q)->Values.Num() != (Format == 4 ? 4 : 2))
		return false;
	FString Class, XP;
	if (!Integer(**P, TEXT("schema"), Result.Progression.SchemaVersion) ||
		(Result.Progression.SchemaVersion != 1 && Result.Progression.SchemaVersion != 2) ||
		!(*P)->TryGetStringField(TEXT("class"), Class) || !Name(Class, Result.Progression.ClassId, true) ||
		!Integer(**P, TEXT("level"), Result.Progression.Level) || !(*P)->TryGetStringField(TEXT("xp"), XP) ||
		XP.IsEmpty() || XP.Len() > 19)
		return false;
	int64 Experience = 0;
	for (TCHAR Ch : XP)
	{
		if (Ch < '0' || Ch > '9' || Experience > (MAX_int64 - (Ch - '0')) / 10)
			return false;
		Experience = Experience * 10 + (Ch - '0');
	}
	Result.Progression.Experience = Experience;
	const TArray<TSharedPtr<FJsonValue>> *History = nullptr;
	if (!(*P)->TryGetArrayField(TEXT("history"), History) || !History || History->Num() > 256)
		return false;
	TSet<FName> Seen;
	for (const auto &Value : *History)
	{
		FString Id;
		FName Parsed;
		if (!Value.IsValid() || !Value->TryGetString(Id) || !Name(Id, Parsed) || Seen.Contains(Parsed))
			return false;
		Seen.Add(Parsed);
		Result.Progression.AdvancementHistory.Add(Parsed);
	}
	const TArray<TSharedPtr<FJsonValue>> *States = nullptr;
    if (!Integer(**Q, TEXT("schema"), Result.Quests.Version) ||
        (Format == 4 ? Result.Quests.Version != 3 : (Result.Quests.Version != 1 && Result.Quests.Version != 2)) ||
        !(*Q)->TryGetArrayField(TEXT("states"), States) || !States || States->Num() > 128) return false;
    if (Format == 4 && (!Integer(**Q, TEXT("history_count"), Result.Quests.HistoryCount) ||
        !(*Q)->TryGetStringField(TEXT("history_root"), Result.Quests.HistoryRootDigest) ||
        Result.Quests.HistoryCount > ProjectJ::QuestHistory::MaxIds ||
        int64(Result.Quests.HistoryCount) + States->Num() > ProjectJ::QuestHistory::MaxIds ||
        (Result.Quests.HistoryCount == 0 ? !Result.Quests.HistoryRootDigest.IsEmpty() : !HistoryDigest(Result.Quests.HistoryRootDigest)))) return false;
	Seen.Reset();
	bool HasCompound = false;
	for (const auto &Value : *States)
	{
		const TSharedPtr<FJsonObject> *Object = nullptr;
		FString Id;
		FProject_JQuestState State;
		if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object || !Object->IsValid() ||
			(*Object)->Values.Num() != ((*Object)->HasField(TEXT("objectives")) ? 5 : 4) ||
			!(*Object)->TryGetStringField(TEXT("id"), Id) || !Name(Id, State.QuestId) || Seen.Contains(State.QuestId) ||
			!Integer(**Object, TEXT("count"), State.Count) || State.Count > 1000000 ||
			!(*Object)->TryGetBoolField(TEXT("claimed"), State.bClaimed) ||
			!(*Object)->TryGetBoolField(TEXT("tracked"), State.bTracked) || (Format == 4 && State.bClaimed))
			return false;
		const TArray<TSharedPtr<FJsonValue>>* Objectives = nullptr;
		if ((*Object)->HasField(TEXT("objectives")))
		{
			if ((Result.Quests.Version != 2 && Result.Quests.Version != 3) || State.Count != 0 || !(*Object)->TryGetArrayField(TEXT("objectives"), Objectives) ||
				!Objectives || Objectives->Num() < 2 || Objectives->Num() > UProject_JQuestComponent::MaxObjectivesPerQuest) return false;
			TSet<FName> ObjectiveIds;
			for (const auto& ObjectiveValue : *Objectives)
			{
				const TSharedPtr<FJsonObject>* O = nullptr; FString ObjectiveId;
				FProject_JQuestObjectiveState Objective;
				if (!ObjectiveValue.IsValid() || !ObjectiveValue->TryGetObject(O) || !O || !O->IsValid() ||
					(*O)->Values.Num() != 2 ||
					!(*O)->TryGetStringField(TEXT("id"), ObjectiveId) || !Name(ObjectiveId, Objective.ObjectiveId) ||
					ObjectiveIds.Contains(Objective.ObjectiveId) || !Integer(**O, TEXT("count"), Objective.Count) || Objective.Count > 1000000) return false;
				ObjectiveIds.Add(Objective.ObjectiveId); State.Objectives.Add(Objective);
			}
			HasCompound = true;
		}
		Seen.Add(State.QuestId);
		Result.Quests.States.Add(State);
	}
	if (Result.Quests.Version != 3 && ((Result.Quests.Version == 2) != HasCompound)) return false;
	Result.Format = Format;
	if (Format >= 2)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Root->TryGetArrayField(TEXT("items"), Values) || !Values || Values->Num() > ProjectJ::Crafting::MaxOwnedEntries) return false;
		TSet<FGuid> Instances; TSet<int32> Orders, Slots;
		for (const auto& Value : *Values)
		{
			const TSharedPtr<FJsonObject>* V = nullptr; FProject_JPersistentItem Item;
			if (!Value.IsValid() || !Value->TryGetObject(V) || !V || !V->IsValid() || !DecodeItem(**V, Item) ||
				Instances.Contains(Item.Instance) ||
				(Item.bInBag ? (Item.Order < 0 || Orders.Contains(Item.Order)) : (Item.Order != INDEX_NONE || Item.Slot == EProject_JEquipmentSlot::None)) ||
				(Item.Slot != EProject_JEquipmentSlot::None && (Slots.Contains(int32(Item.Slot)) || Item.Count != 1))) return false;
			Instances.Add(Item.Instance);
			if (Item.bInBag) Orders.Add(Item.Order);
			if (Item.Slot != EProject_JEquipmentSlot::None) Slots.Add(int32(Item.Slot));
			Result.Items.Add(Item);
		}
	}
	if (Format >= 3)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Root->TryGetArrayField(TEXT("jobs"), Values) || !Values || Values->Num() > ProjectJ::Crafting::MaxJobs) return false;
		for (const auto& Value : *Values)
		{
			const TSharedPtr<FJsonObject>* J = nullptr; ProjectJ::Crafting::FJobValues Job;
			FString Id, Recipe;
			const TArray<TSharedPtr<FJsonValue>> *Escrow = nullptr, *Outputs = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(J) || !J || !J->IsValid() || (*J)->Values.Num() != 10 ||
				!Integer(**J, TEXT("schema"), Job.SchemaVersion) || !(*J)->TryGetStringField(TEXT("id"), Id) ||
				!FGuid::ParseExact(Id, EGuidFormats::DigitsWithHyphens, Job.Id) ||
				!(*J)->TryGetStringField(TEXT("recipe"), Recipe) || !Name(Recipe, Job.RecipeId) ||
				!Integer(**J, TEXT("recipe_version"), Job.RecipeVersion) || !Integer(**J, TEXT("batches"), Job.Batches) ||
				!(*J)->TryGetStringField(TEXT("rules"), Job.RulesDigest) ||
				!CanonicalTicks(**J, TEXT("started_utc_ticks"), Job.StartedUtcTicks) || !CanonicalTicks(**J, TEXT("due_utc_ticks"), Job.DueUtcTicks) ||
				!(*J)->TryGetArrayField(TEXT("escrow"), Escrow) || !Escrow || Escrow->IsEmpty() || Escrow->Num() > ProjectJ::Crafting::MaxEscrowEntries ||
				!(*J)->TryGetArrayField(TEXT("outputs"), Outputs) || !Outputs || Outputs->IsEmpty() || Outputs->Num() > 8) return false;
			for (const auto& Entry : *Escrow)
			{
				const TSharedPtr<FJsonObject>* V = nullptr; FProject_JPersistentItem Item;
				if (!Entry.IsValid() || !Entry->TryGetObject(V) || !V || !V->IsValid() || !DecodeItem(**V, Item)) return false;
				Job.Escrow.Add(Item);
			}
			for (const auto& Entry : *Outputs)
			{
				const TSharedPtr<FJsonObject>* L = nullptr; FString Definition; ProjectJ::Crafting::FResolvedLine Line;
				if (!Entry.IsValid() || !Entry->TryGetObject(L) || !L || !L->IsValid() || (*L)->Values.Num() != 5 ||
					!(*L)->TryGetStringField(TEXT("definition"), Definition) || !Name(Definition, Line.Definition) ||
					!Integer(**L, TEXT("version"), Line.DefinitionVersion) || !Integer(**L, TEXT("level"), Line.Level) ||
					!Integer(**L, TEXT("per_batch"), Line.CountPerBatch) || !Integer(**L, TEXT("max_stack"), Line.MaxStackCount)) return false;
				Job.Outputs.Add(Line);
			}
			Result.CraftingJobs.Add(MoveTemp(Job));
		}
	}
	if (!ProjectJ::Crafting::ValidateJobState(Result.Items, Result.CraftingJobs)) return false;
	Out.HotValues = MoveTemp(Result);
	return true;
}
