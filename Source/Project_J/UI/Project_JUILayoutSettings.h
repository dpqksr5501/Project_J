#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "UI/Project_JUIModels.h"
#include "Project_JUILayoutSettings.generated.h"

/** Small local preference file. Never stores inventory, skills, or backend character state. */
UCLASS()
class PROJECT_J_API UProject_JUILayoutSave : public USaveGame
{
	GENERATED_BODY()
  public:
	static constexpr int32 CurrentVersion = 2;
	UPROPERTY(SaveGame) int32 Version = CurrentVersion;
	UPROPERTY(SaveGame) TMap<FString, FVector2D> MenuPositions;
	UPROPERTY(SaveGame) TMap<FString, FProject_JHUDPreferences> HUDPreferences;
	UPROPERTY(SaveGame) FProject_JUIKeys InputKeys;
	static FString MakeKey(const FGuid &CharacterId, FName ProfileId);
	bool Read(const FString &Key, FVector2D &OutPosition) const;
	bool Write(const FString &Key, FVector2D Position);
	bool Normalize();
};

/** Storage boundary also permits fault tests without touching the user's save file. */
class PROJECT_J_API FProject_JUILayoutStorage
{
  public:
	virtual ~FProject_JUILayoutStorage() = default;
	virtual bool Exists(const FString &Slot) = 0;
	virtual UProject_JUILayoutSave *Load(const FString &Slot) = 0;
	virtual bool Store(UProject_JUILayoutSave *Value, const FString &Slot) = 0;
};

UENUM(BlueprintType)
enum class EProject_JUIPreferenceState : uint8 { Ready, Pending, Saved, Recovered, Failed, NewerVersion };

UCLASS()
class PROJECT_J_API UProject_JUILayoutSettings : public ULocalPlayerSubsystem
{
	GENERATED_BODY()
  public:
	bool Read(const FString &Key, FVector2D &Position);
	void Write(const FString &Key, FVector2D Position);
	bool ReadHUD(const FString &Key, FProject_JHUDPreferences &Out);
	void WriteHUD(const FString &Key, const FProject_JHUDPreferences &Value);
	FProject_JUIKeys ReadKeys();
	void WriteKeys(const FProject_JUIKeys &Value);
	virtual void Initialize(FSubsystemCollectionBase &Collection) override;
	virtual void Deinitialize() override;
	bool FlushNow();
	EProject_JUIPreferenceState GetSaveState() const { return SaveState; }
	FText GetSaveStatus() const;
	DECLARE_MULTICAST_DELEGATE(FSaveStateChanged);
	FSaveStateChanged OnSaveStateChanged;
#if WITH_DEV_AUTOMATION_TESTS
	void SetStorageForTesting(TSharedRef<FProject_JUILayoutStorage> InStorage) { check(!Save); Storage = InStorage; }
#endif

  private:
	void EnsureLoaded();
	void Schedule(float Delay = 0.75f);
	void SetState(EProject_JUIPreferenceState State);
	void BeforeExit();
	FString SlotName() const;
	UPROPERTY(Transient) TObjectPtr<UProject_JUILayoutSave> Save;
	TMap<FString, FVector2D> DirtyPositions;
	TMap<FString, FProject_JHUDPreferences> DirtyHUD;
	bool bKeysDirty = false;
	bool bRewriteNeeded = false;
	bool bShuttingDown = false;
	int32 FailureCount = 0;
	EProject_JUIPreferenceState SaveState = EProject_JUIPreferenceState::Ready;
	FTSTicker::FDelegateHandle SaveTicker;
	FDelegateHandle ExitHandle;
	TSharedPtr<FProject_JUILayoutStorage> Storage;
};
