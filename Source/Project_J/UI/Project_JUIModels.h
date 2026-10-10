#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "InputCoreTypes.h"
#include "Project_JUIModels.generated.h"

UENUM(BlueprintType)
enum class EProject_JQuickSlotKind : uint8
{
	Empty,
	Skill,
	Item
};
/** Stable presentation reference, never a saved GAS handle or a duplicated item stack. */
USTRUCT(BlueprintType)
struct FProject_JQuickSlotBinding
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) EProject_JQuickSlotKind Kind = EProject_JQuickSlotKind::Empty;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag InputTag;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FName ItemId;
};
USTRUCT(BlueprintType)
struct FProject_JHUDPreferences
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) TArray<FProject_JQuickSlotBinding> Slots;
	UPROPERTY(SaveGame) bool bLocked = true;
	UPROPERTY(SaveGame) bool bShowResourceValues = true;
	UPROPERTY(SaveGame) float Scale = 1.f;
	bool IsValid() const;
};
USTRUCT(BlueprintType)
struct FProject_JQuickSlotState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FProject_JQuickSlotBinding Binding;
	UPROPERTY(BlueprintReadOnly) FText Name;
	UPROPERTY(BlueprintReadOnly) FText KeyHint;
	UPROPERTY(BlueprintReadOnly) TSoftObjectPtr<UTexture2D> Icon;
	UPROPERTY(BlueprintReadOnly) int32 Quantity = 0;
	UPROPERTY(BlueprintReadOnly) float Cooldown = 0;
	UPROPERTY(BlueprintReadOnly) float CooldownFraction = 0;
	UPROPERTY(BlueprintReadOnly) bool bAvailable = false;
	UPROPERTY(BlueprintReadOnly) FText UnavailableReason;
};

/** Local keyboard preferences shared by this local player, independent of character profiles. */
USTRUCT()
struct FProject_JUIKeys
{
	GENERATED_BODY()
	FProject_JUIKeys();
	UPROPERTY(SaveGame) TArray<FKey> Keys;
	bool IsValid() const;
	bool Rebind(int32 Index, FKey Key);
	static bool IsAllowed(FKey Key);
	static FName WindowAt(int32 Index);
};
UCLASS(BlueprintType)
class PROJECT_J_API UProject_JHUDStyle : public UDataAsset
{
	GENERATED_BODY()
  public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) float QuickSlotSize = 46;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) float ItemSize = 46;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) int32 FontSize = 12;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FLinearColor PanelColor = FLinearColor(0.018f, 0.027f, 0.038f, 0.9f);
};
