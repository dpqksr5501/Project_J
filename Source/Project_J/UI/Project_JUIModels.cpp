#include "UI/Project_JUIModels.h"

FProject_JUIKeys::FProject_JUIKeys()
{
	Keys = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven,
		EKeys::Eight, EKeys::Nine, EKeys::Zero, EKeys::I, EKeys::K, EKeys::O, EKeys::U};
}
bool FProject_JUIKeys::IsAllowed(FKey Key)
{
	// Keep movement, combat, modifiers, console and Escape available. Expand via a full gameplay remapper later.
	static const TSet<FKey> Allowed = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero, EKeys::I, EKeys::K, EKeys::O, EKeys::U,
		EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5, EKeys::F6, EKeys::F7, EKeys::F8,
		EKeys::F9, EKeys::F10, EKeys::F11, EKeys::F12};
	return Allowed.Contains(Key);
}
bool FProject_JUIKeys::IsValid() const
{
	if (Keys.Num() != 14) return false;
	TSet<FKey> Seen;
	for (FKey Key : Keys) { if (!IsAllowed(Key) || Seen.Contains(Key)) return false; Seen.Add(Key); }
	return true;
}
bool FProject_JUIKeys::Rebind(int32 Index, FKey Key)
{
	if (!IsValid() || !Keys.IsValidIndex(Index) || !IsAllowed(Key)) return false;
	const int32 Other = Keys.IndexOfByKey(Key);
	if (Other != INDEX_NONE) Swap(Keys[Index], Keys[Other]);
	else Keys[Index] = Key;
	return true;
}
FName FProject_JUIKeys::WindowAt(int32 Index)
{
	static const TArray<FName> Windows = {TEXT("Bag"), TEXT("Equipment"), TEXT("Quests"), TEXT("Settings")};
	return Windows.IsValidIndex(Index - 10) ? Windows[Index - 10] : NAME_None;
}
bool FProject_JHUDPreferences::IsValid() const
{
	if (Slots.Num() != 10 || !FMath::IsFinite(Scale) || Scale < 0.75f || Scale > 1.5f)
		return false;
	for (const auto &Slot : Slots)
	{
		if (Slot.Kind == EProject_JQuickSlotKind::Skill)
		{
			if (!Slot.InputTag.IsValid() || !Slot.ItemId.IsNone())
				return false;
		}
		else if (Slot.Kind == EProject_JQuickSlotKind::Item)
		{
			if (Slot.ItemId.IsNone() || Slot.InputTag.IsValid())
				return false;
		}
		else if (Slot.Kind != EProject_JQuickSlotKind::Empty || Slot.InputTag.IsValid() || !Slot.ItemId.IsNone())
			return false;
	}
	return true;
}
