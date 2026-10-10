#include "UI/Project_JUILayoutSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LocalPlayer.h"

FString UProject_JUILayoutSave::MakeKey(const FGuid &CharacterId, FName ProfileId)
{
	return CharacterId.IsValid() && !ProfileId.IsNone()
			   ? CharacterId.ToString(EGuidFormats::Digits) + TEXT("/") + ProfileId.ToString()
			   : FString();
}
bool UProject_JUILayoutSave::Read(const FString &Key, FVector2D &OutPosition) const
{
	const auto *Value = MenuPositions.Find(Key);
	if (Version != 1 || Key.IsEmpty() || !Value || !FMath::IsFinite(Value->X) || !FMath::IsFinite(Value->Y))
		return false;
	OutPosition = FVector2D(FMath::Clamp(Value->X, 0., 1.), FMath::Clamp(Value->Y, 0., 1.));
	return true;
}
bool UProject_JUILayoutSave::Write(const FString &Key, FVector2D Position)
{
	if (Version != 1 || Key.IsEmpty() || !FMath::IsFinite(Position.X) || !FMath::IsFinite(Position.Y))
		return false;
	Position = FVector2D(FMath::Clamp(Position.X, 0., 1.), FMath::Clamp(Position.Y, 0., 1.));
	if (const auto *Previous = MenuPositions.Find(Key); Previous && *Previous == Position)
		return false;
	MenuPositions.Add(Key, Position);
	return true;
}
FString UProject_JUILayoutSettings::SlotName() const
{
	return FString::Printf(TEXT("ProjectJ_UI_Player_%d"), GetLocalPlayer()->GetControllerId());
}
void UProject_JUILayoutSettings::EnsureLoaded()
{
	if (Save)
		return;
	if (UGameplayStatics::DoesSaveGameExist(SlotName(), 0))
		Save = Cast<UProject_JUILayoutSave>(UGameplayStatics::LoadGameFromSlot(SlotName(), 0));
	if (!Save || Save->Version != 1)
		Save = NewObject<UProject_JUILayoutSave>(this);
}
bool UProject_JUILayoutSettings::Read(const FString &Key, FVector2D &Position)
{
	EnsureLoaded();
	return Save->Read(Key, Position);
}
void UProject_JUILayoutSettings::Write(const FString &Key, FVector2D Position)
{
	EnsureLoaded();
	if (Save->Write(Key, Position))
		DirtyPositions.Add(Key, Save->MenuPositions[Key]);
	Flush();
}
void UProject_JUILayoutSettings::Flush()
{
	if ((!DirtyPositions.IsEmpty() || !DirtyHUD.IsEmpty() || bKeysDirty) && Save)
	{
		// Multiple PIE clients can share one local save slot. Merge only this screen's changed keys.
		auto *Latest = UGameplayStatics::DoesSaveGameExist(SlotName(), 0)
						   ? Cast<UProject_JUILayoutSave>(UGameplayStatics::LoadGameFromSlot(SlotName(), 0))
						   : nullptr;
		if (Latest && Latest->Version == 1)
		{
			for (const auto &Entry : DirtyPositions)
				Latest->Write(Entry.Key, Entry.Value);
			for (const auto &Entry : DirtyHUD)
				Latest->HUDPreferences.Add(Entry.Key, Entry.Value);
			if (bKeysDirty) Latest->InputKeys = Save->InputKeys;
			Save = Latest;
		}
		if (UGameplayStatics::SaveGameToSlot(Save, SlotName(), 0))
		{
			DirtyPositions.Reset();
			DirtyHUD.Reset();
			bKeysDirty = false;
		}
		else
			UE_LOG(LogTemp, Warning, TEXT("ProjectJ UI layout preference could not be saved; retained in memory."));
	}
}
void UProject_JUILayoutSettings::Deinitialize()
{
	Flush();
	Save = nullptr;
	Super::Deinitialize();
}

bool UProject_JUILayoutSettings::ReadHUD(const FString &Key, FProject_JHUDPreferences &Out)
{
	EnsureLoaded();
	const auto *Value = Save->HUDPreferences.Find(Key);
	if (Key.IsEmpty() || !Value || !Value->IsValid())
		return false;
	Out = *Value;
	return true;
}
void UProject_JUILayoutSettings::WriteHUD(const FString &Key, const FProject_JHUDPreferences &Value)
{
	if (Key.IsEmpty() || !Value.IsValid())
		return;
	EnsureLoaded();
	Save->HUDPreferences.Add(Key, Value);
	DirtyHUD.Add(Key, Value);
	Flush();
}

FProject_JUIKeys UProject_JUILayoutSettings::ReadKeys()
{
	EnsureLoaded();
	return Save->InputKeys.IsValid() ? Save->InputKeys : FProject_JUIKeys();
}
void UProject_JUILayoutSettings::WriteKeys(const FProject_JUIKeys &Value)
{
	if (!Value.IsValid()) return;
	EnsureLoaded();
	Save->InputKeys = Value;
	bKeysDirty = true;
	Flush();
}
