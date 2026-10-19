#include "Backend/Project_JLocalProfileSnapshot.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace
{
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
} // namespace
bool FProject_JLocalProfileSnapshot::Encode(TArray<uint8> &Out) const
{
	Out.Reset();
	if(Format==1&&!Items.IsEmpty())return false;
	if (!Account.IsValid() || !Character.IsValid() || RulesFingerprint.IsEmpty())
		return false;
	auto Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("format"), Format);
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
	Q->SetNumberField(TEXT("schema"), Quests.Version);
	TArray<TSharedPtr<FJsonValue>> States;
	for (const auto &State : Quests.States)
	{
		auto Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("id"), State.QuestId.ToString().ToLower());
		Item->SetNumberField(TEXT("count"), State.Count);
		Item->SetBoolField(TEXT("claimed"), State.bClaimed);
		Item->SetBoolField(TEXT("tracked"), State.bTracked);
		States.Add(MakeShared<FJsonValueObject>(Item));
	}
	Q->SetArrayField(TEXT("states"), States);
	Root->SetObjectField(TEXT("quests"), Q);
	if (Format == 2)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const auto &Item : Items)
		{
			if (Item.Order == MAX_int32)
				return false;
			auto V = MakeShared<FJsonObject>();
			V->SetStringField(TEXT("instance"), Item.Instance.ToString(EGuidFormats::DigitsWithHyphens));
			V->SetStringField(TEXT("definition"), Item.Definition.ToString().ToLower());
			V->SetNumberField(TEXT("version"), Item.DefinitionVersion);
			V->SetNumberField(TEXT("level"), Item.Level);
			V->SetNumberField(TEXT("count"), Item.Count);
			V->SetNumberField(TEXT("order"), Item.Order + 1);
			V->SetNumberField(TEXT("slot"), uint8(Item.Slot));
			V->SetBoolField(TEXT("bag"), Item.bInBag);
			Values.Add(MakeShared<FJsonValueObject>(V));
		}
		Root->SetArrayField(TEXT("items"), Values);
	}
	FString Text;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
	if (!FJsonSerializer::Serialize(Root, Writer))
		return false;
	FTCHARToUTF8 Bytes(*Text);
	if (Bytes.Length() > 65536)
		return false;
	Out.Append(reinterpret_cast<const uint8 *>(Bytes.Get()), Bytes.Length());
	// The writer must not commit a document that its own bounded reader rejects.
	FProject_JLocalProfileSnapshot Validated;
	if (!Decode(Out, Validated))
	{
		Out.Reset();
		return false;
	}
	return true;
}
bool FProject_JLocalProfileSnapshot::Decode(const TArray<uint8> &Bytes, FProject_JLocalProfileSnapshot &Out)
{
	Out = {};
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
	if (!Integer(*Root, TEXT("format"), Format) || (Format != 1 && Format != 2) ||
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
		!Root->TryGetObjectField(TEXT("quests"), Q) || !Q || !Q->IsValid())
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
	if (!Integer(**Q, TEXT("schema"), Result.Quests.Version) || Result.Quests.Version != 1 ||
		!(*Q)->TryGetArrayField(TEXT("states"), States) || !States || States->Num() > 128)
		return false;
	Seen.Reset();
	for (const auto &Value : *States)
	{
		const TSharedPtr<FJsonObject> *Object = nullptr;
		FString Id;
		FProject_JQuestState State;
		if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object || !Object->IsValid() ||
			!(*Object)->TryGetStringField(TEXT("id"), Id) || !Name(Id, State.QuestId) || Seen.Contains(State.QuestId) ||
			!Integer(**Object, TEXT("count"), State.Count) || State.Count > 1000000 ||
			!(*Object)->TryGetBoolField(TEXT("claimed"), State.bClaimed) ||
			!(*Object)->TryGetBoolField(TEXT("tracked"), State.bTracked))
			return false;
		Seen.Add(State.QuestId);
		Result.Quests.States.Add(State);
	}
	Result.Format = Format;
	if (Format == 2)
	{
		const TArray<TSharedPtr<FJsonValue>> *Values = nullptr;
		if (!Root->TryGetArrayField(TEXT("items"), Values) || !Values || Values->Num() > 4096)
			return false;
		TSet<FGuid> Instances;
		TSet<int32> Orders, Slots;
		for (const auto &Value : *Values)
		{
			const TSharedPtr<FJsonObject> *V = nullptr;
			FString Id, Def;
			FProject_JPersistentItem Item;
			int32 Order = 0, Slot = 0;
			if (!Value.IsValid() || !Value->TryGetObject(V) || !V || !V->IsValid() ||
				!(*V)->TryGetStringField(TEXT("instance"), Id) ||
				!FGuid::ParseExact(Id, EGuidFormats::DigitsWithHyphens, Item.Instance) || !Item.Instance.IsValid() ||
				Instances.Contains(Item.Instance) || !(*V)->TryGetStringField(TEXT("definition"), Def) ||
				!Name(Def, Item.Definition) || !Integer(**V, TEXT("version"), Item.DefinitionVersion) ||
				Item.DefinitionVersion < 1 || !Integer(**V, TEXT("level"), Item.Level) || Item.Level < 1 ||
				Item.Level > 10000 || !Integer(**V, TEXT("count"), Item.Count) || Item.Count < 1 ||
				Item.Count > 1000000 || !Integer(**V, TEXT("order"), Order) || Order == MAX_int32 ||
				!Integer(**V, TEXT("slot"), Slot) || Slot > int32(EProject_JEquipmentSlot::Mount) ||
				!(*V)->TryGetBoolField(TEXT("bag"), Item.bInBag))
				return false;
			if ((Item.bInBag ? (Order == 0 || Orders.Contains(Order)) : (Order != 0 || Slot == 0)) ||
				(Slot != 0 && (Slots.Contains(Slot) || Item.Count != 1)))
				return false;
			Item.Order = Order - 1;
			Item.Slot = EProject_JEquipmentSlot(Slot);
			Instances.Add(Item.Instance);
			if (Item.bInBag)
				Orders.Add(Order);
			if (Slot)
				Slots.Add(Slot);
			Result.Items.Add(Item);
		}
	}
	Out = MoveTemp(Result);
	return true;
}
