#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "UI/Project_JUIModels.h"
#include "Project_JUILayoutSettings.generated.h"

/** Small local preference file. Never stores inventory, skills, or backend character state. */
UCLASS()
class PROJECT_J_API UProject_JUILayoutSave : public USaveGame
{
	GENERATED_BODY()
  public:
	UPROPERTY(SaveGame) int32 Version = 1;
	UPROPERTY(SaveGame) TMap<FString, FVector2D> MenuPositions;
	UPROPERTY(SaveGame) TMap<FString, FProject_JHUDPreferences> HUDPreferences;
	UPROPERTY(SaveGame) FProject_JUIKeys InputKeys;
	static FString MakeKey(const FGuid &CharacterId, FName ProfileId);
	bool Read(const FString &Key, FVector2D &OutPosition) const;
	bool Write(const FString &Key, FVector2D Position);
};

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
	virtual void Deinitialize() override;

  private:
	void EnsureLoaded();
	void Flush();
	FString SlotName() const;
	UPROPERTY(Transient) TObjectPtr<UProject_JUILayoutSave> Save;
	TMap<FString, FVector2D> DirtyPositions;
	TMap<FString, FProject_JHUDPreferences> DirtyHUD;
	bool bKeysDirty = false;
};
