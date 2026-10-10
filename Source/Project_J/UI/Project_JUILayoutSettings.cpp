#include "UI/Project_JUILayoutSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LocalPlayer.h"
#include "Misc/CoreDelegates.h"

namespace
{
class FSlotStorage final : public FProject_JUILayoutStorage
{
  public:
	bool Exists(const FString &Slot) override { return UGameplayStatics::DoesSaveGameExist(Slot, 0); }
	UProject_JUILayoutSave *Load(const FString &Slot) override
	{
		return Cast<UProject_JUILayoutSave>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	}
	bool Store(UProject_JUILayoutSave *Value, const FString &Slot) override
	{
		return UGameplayStatics::SaveGameToSlot(Value, Slot, 0);
	}
};
}
FString UProject_JUILayoutSave::MakeKey(const FGuid &CharacterId, FName ProfileId)
{
	return CharacterId.IsValid() && !ProfileId.IsNone()
		? CharacterId.ToString(EGuidFormats::Digits) + TEXT("/") + ProfileId.ToString() : FString();
}
bool UProject_JUILayoutSave::Read(const FString &Key, FVector2D &OutPosition) const
{
	const auto *Value = MenuPositions.Find(Key);
	if (Version < 1 || Version > CurrentVersion || Key.IsEmpty() || !Value || !FMath::IsFinite(Value->X) || !FMath::IsFinite(Value->Y)) return false;
	OutPosition = FVector2D(FMath::Clamp(Value->X, 0., 1.), FMath::Clamp(Value->Y, 0., 1.));
	return true;
}
bool UProject_JUILayoutSave::Write(const FString &Key, FVector2D Position)
{
	if (Version != CurrentVersion || Key.IsEmpty() || !FMath::IsFinite(Position.X) || !FMath::IsFinite(Position.Y)) return false;
	Position = FVector2D(FMath::Clamp(Position.X, 0., 1.), FMath::Clamp(Position.Y, 0., 1.));
	if (const auto *Previous = MenuPositions.Find(Key); Previous && *Previous == Position) return false;
	MenuPositions.Add(Key, Position);
	return true;
}
bool UProject_JUILayoutSave::Normalize()
{
	if (Version < 1 || Version > CurrentVersion) return false;
	Version = CurrentVersion;
	for (auto It = MenuPositions.CreateIterator(); It; ++It)
		if (It.Key().IsEmpty() || !FMath::IsFinite(It.Value().X) || !FMath::IsFinite(It.Value().Y)) It.RemoveCurrent();
		else It.Value() = FVector2D(FMath::Clamp(It.Value().X, 0., 1.), FMath::Clamp(It.Value().Y, 0., 1.));
	for (auto It = HUDPreferences.CreateIterator(); It; ++It)
		if (It.Key().IsEmpty() || !It.Value().IsValid()) It.RemoveCurrent();
	if (!InputKeys.IsValid()) InputKeys = FProject_JUIKeys();
	return true;
}
void UProject_JUILayoutSettings::Initialize(FSubsystemCollectionBase &Collection)
{
	Super::Initialize(Collection);
	ExitHandle = FCoreDelegates::OnPreExit.AddUObject(this, &ThisClass::BeforeExit);
}
FString UProject_JUILayoutSettings::SlotName() const
{
	return FString::Printf(TEXT("ProjectJ_UI_Player_%d"), GetLocalPlayer() ? GetLocalPlayer()->GetControllerId() : 0);
}
void UProject_JUILayoutSettings::SetState(EProject_JUIPreferenceState State)
{
	if (SaveState == State) return;
	SaveState = State;
	OnSaveStateChanged.Broadcast();
}
void UProject_JUILayoutSettings::EnsureLoaded()
{
	if (Save) return;
	if (!Storage) Storage = MakeShared<FSlotStorage>();
	const bool bExists = Storage->Exists(SlotName());
	Save = bExists ? Storage->Load(SlotName()) : nullptr;
	if (Save && Save->Version > UProject_JUILayoutSave::CurrentVersion)
	{
		Save = NewObject<UProject_JUILayoutSave>(this);
		SetState(EProject_JUIPreferenceState::NewerVersion);
		return;
	}
	const bool bMigration = Save && Save->Version == 1;
	if (!Save || !Save->Normalize())
	{
		Save = Storage->Exists(SlotName() + TEXT("_Backup")) ? Storage->Load(SlotName() + TEXT("_Backup")) : nullptr;
		if (Save && Save->Version > UProject_JUILayoutSave::CurrentVersion)
		{
			Save = NewObject<UProject_JUILayoutSave>(this);
			SetState(EProject_JUIPreferenceState::NewerVersion);
			return;
		}
		if (Save && Save->Normalize())
		{
			bRewriteNeeded = true;
			SetState(EProject_JUIPreferenceState::Recovered);
		}
		else
		{
			Save = NewObject<UProject_JUILayoutSave>(this);
			if (bExists) { bRewriteNeeded = true; SetState(EProject_JUIPreferenceState::Recovered); }
		}
	}
	else bRewriteNeeded = bMigration;
	if (bRewriteNeeded) Schedule();
}
void UProject_JUILayoutSettings::Schedule(float Delay)
{
	if (bShuttingDown || SaveState == EProject_JUIPreferenceState::NewerVersion || SaveTicker.IsValid()) return;
	SaveTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		SaveTicker.Reset();
		FlushNow();
		return false;
	}), Delay);
}
bool UProject_JUILayoutSettings::FlushNow()
{
	if (SaveTicker.IsValid()) { FTSTicker::RemoveTicker(SaveTicker); SaveTicker.Reset(); }
	if (!Save) return true;
	if (SaveState == EProject_JUIPreferenceState::NewerVersion) return false;
	if (DirtyPositions.IsEmpty() && DirtyHUD.IsEmpty() && !bKeysDirty && !bRewriteNeeded) return true;
	// Small synchronous writes are coalesced. No async write races with shutdown or another PIE player.
	auto *Latest = Storage->Exists(SlotName()) ? Storage->Load(SlotName()) : nullptr;
	if (Latest && Latest->Version > UProject_JUILayoutSave::CurrentVersion)
	{
		SetState(EProject_JUIPreferenceState::NewerVersion);
		return false;
	}
	const bool bHealthy = Latest && Latest->Normalize();
	bool bSuccess = !bHealthy || Storage->Store(Latest, SlotName() + TEXT("_Backup"));
	if (bHealthy)
	{
		// Keep changes to other characters/windows written by another local screen.
		for (const auto &Entry : DirtyPositions) Latest->Write(Entry.Key, Entry.Value);
		for (const auto &Entry : DirtyHUD) Latest->HUDPreferences.Add(Entry.Key, Entry.Value);
		if (bKeysDirty) Latest->InputKeys = Save->InputKeys;
		Save = Latest;
	}
	if (bSuccess) bSuccess = Storage->Store(Save, SlotName());
	if (bSuccess)
	{
		DirtyPositions.Reset(); DirtyHUD.Reset(); bKeysDirty = false; bRewriteNeeded = false; FailureCount = 0;
		SetState(EProject_JUIPreferenceState::Saved);
	}
	else
	{
		++FailureCount;
		SetState(EProject_JUIPreferenceState::Failed);
		if (FailureCount <= 3) Schedule(static_cast<float>(1 << FailureCount));
	}
	return bSuccess;
}
bool UProject_JUILayoutSettings::Read(const FString &Key, FVector2D &Position) { EnsureLoaded(); return Save->Read(Key, Position); }
void UProject_JUILayoutSettings::Write(const FString &Key, FVector2D Position)
{
	EnsureLoaded();
	if (!Save->Write(Key, Position)) return;
	DirtyPositions.Add(Key, Save->MenuPositions[Key]);
	if (SaveState != EProject_JUIPreferenceState::NewerVersion) SetState(EProject_JUIPreferenceState::Pending);
	Schedule();
}
bool UProject_JUILayoutSettings::ReadHUD(const FString &Key, FProject_JHUDPreferences &Out)
{
	EnsureLoaded();
	const auto *Value = Save->HUDPreferences.Find(Key);
	if (Key.IsEmpty() || !Value || !Value->IsValid()) return false;
	Out = *Value;
	return true;
}
void UProject_JUILayoutSettings::WriteHUD(const FString &Key, const FProject_JHUDPreferences &Value)
{
	if (Key.IsEmpty() || !Value.IsValid()) return;
	EnsureLoaded();
	Save->HUDPreferences.Add(Key, Value); DirtyHUD.Add(Key, Value);
	if (SaveState != EProject_JUIPreferenceState::NewerVersion) SetState(EProject_JUIPreferenceState::Pending);
	Schedule();
}
FProject_JUIKeys UProject_JUILayoutSettings::ReadKeys() { EnsureLoaded(); return Save->InputKeys; }
void UProject_JUILayoutSettings::WriteKeys(const FProject_JUIKeys &Value)
{
	if (!Value.IsValid()) return;
	EnsureLoaded(); Save->InputKeys = Value; bKeysDirty = true;
	if (SaveState != EProject_JUIPreferenceState::NewerVersion) SetState(EProject_JUIPreferenceState::Pending);
	Schedule();
}
void UProject_JUILayoutSettings::BeforeExit() { bShuttingDown = true; FlushNow(); }
void UProject_JUILayoutSettings::Deinitialize()
{
	FCoreDelegates::OnPreExit.Remove(ExitHandle);
	BeforeExit(); Save = nullptr; Storage.Reset(); OnSaveStateChanged.Clear();
	Super::Deinitialize();
}
FText UProject_JUILayoutSettings::GetSaveStatus() const
{
	switch (SaveState)
	{
	case EProject_JUIPreferenceState::Pending: return NSLOCTEXT("ProjectJUI", "SavePending", "변경 적용됨 · 저장 대기");
	case EProject_JUIPreferenceState::Saved: return NSLOCTEXT("ProjectJUI", "SaveDone", "이 기기에 저장됨");
	case EProject_JUIPreferenceState::Recovered: return NSLOCTEXT("ProjectJUI", "SaveRecovered", "이전 설정 또는 기본값으로 복구됨");
	case EProject_JUIPreferenceState::Failed: return NSLOCTEXT("ProjectJUI", "SaveFailed", "저장 실패 · 현재 설정 유지 · 다시 시도 가능");
	case EProject_JUIPreferenceState::NewerVersion: return NSLOCTEXT("ProjectJUI", "SaveFuture", "최신 버전 설정 파일 · 현재 변경은 이번 실행에만 적용");
	default: return NSLOCTEXT("ProjectJUI", "SaveReady", "UI 설정은 이 기기에 저장됩니다");
	}
}
