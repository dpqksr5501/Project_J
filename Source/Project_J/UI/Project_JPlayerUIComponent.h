#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "UI/Project_JCharacterUIProfile.h"
#include "UI/Project_JUIModels.h"
#include "TimerManager.h"
#include "Project_JPlayerUIComponent.generated.h"

class UProject_JPlayerHUDWidget;
class UProject_JInventoryViewModel;
class UProject_JInventoryEntry;
class UProject_JAbilitySystemComponent;
class UProject_JProgressionComponent;
class APawn;
class AProject_JPlayerCharacter;
class AProject_JPlayerState;
class UProject_JStatusEffectModel;
struct FOnAttributeChangeData;

/** Per local Controller screen lifetime; authoritative state stays on PlayerState. */
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent))
class PROJECT_J_API UProject_JPlayerUIComponent : public UActorComponent
{
	GENERATED_BODY()
  public:
	UProject_JPlayerUIComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void RefreshSources();
	void BindMenuInput(class UInputComponent *Input);
	UFUNCTION(BlueprintCallable) void ToggleMenu();
	UFUNCTION(BlueprintCallable) void SetMenuOpen(bool bOpen);
	UFUNCTION(BlueprintPure) bool IsMenuOpen() const
	{
		return bMenuOpen;
	}
	UFUNCTION(BlueprintPure) UProject_JInventoryViewModel *GetInventoryModel() const
	{
		return InventoryModel;
	}
	UFUNCTION(BlueprintCallable) void PressSkill(int32 Index);
	UFUNCTION(BlueprintCallable) void ReleaseSkill(int32 Index);
	UPROPERTY(EditAnywhere, Category = "UI|Skin") TSubclassOf<UProject_JPlayerHUDWidget> ScreenClass;
	UPROPERTY(EditAnywhere, Category = "UI|Skills") TArray<FProject_JUISkillSlot> SkillSlots;
	UPROPERTY(EditAnywhere, Category = "UI|Profiles") TObjectPtr<UProject_JUIProfileCatalog> ProfileCatalog;
	UFUNCTION(BlueprintPure) UProject_JCharacterUIProfile *GetActiveProfile() const
	{
		return ActiveProfile;
	}
	const TArray<FProject_JUISkillSlot> &GetPresentedSkills() const
	{
		return PresentedSkills;
	}
	void ToggleWindow(FName Id);
	bool BindItemSlot(int32 Index, const UProject_JInventoryEntry *Entry);
	bool MoveQuickSlot(int32 From, int32 To, const FString &Identity, const FProject_JQuickSlotBinding &Original);
	void ClearQuickSlot(int32 Index);
	void ChangePreferences(FName Command);
	bool CanEditQuickSlots() const
	{
		return !HUDPreferences.bLocked;
	}
	const FString &GetLayoutIdentity() const
	{
		return PreferencesKey;
	}
	FProject_JQuickSlotBinding GetQuickBinding(int32 Index) const;
	class UProject_JQuestComponent *GetQuestComponent() const;
	UProject_JHUDStyle *GetHUDStyle() const;
	const FProject_JUIKeys &GetInputKeys() const { return InputKeys; }
	bool RebindKey(int32 Index, FKey Key, FText &Reason);
	void ResetKeys();
	bool HandleMenuKey(FKey Key);
	UFUNCTION(BlueprintCallable) void SetObservedTarget(AActor *Target);

  private:
	bool IsKeyReserved(FKey Key) const;
	UFUNCTION() void RebuildKeyBindings();
	FProject_JUIKeys InputKeys;
	TWeakObjectPtr<class UInputComponent> MenuInput;
	UPROPERTY() TObjectPtr<UProject_JStatusEffectModel> StatusEffects;
	void OnStatusEffectsChanged();
	void UpdateObservedTarget();
	TWeakObjectPtr<AActor> ObservedTarget;
	FTimerHandle TargetTimer;
	FDelegateHandle ActivationHandle;
	void RunRuntimeSmoke();
	void PrepareRuntimeProfile();
	FTimerHandle RuntimeSmokeTimer;
	void OnQuestsChanged();
	void LoadPreferences();
	void StorePreferences();
	FProject_JHUDPreferences HUDPreferences;
	FString PreferencesKey;
	TWeakObjectPtr<class UProject_JQuestComponent> BoundQuests;
	UFUNCTION() void OnPawnChanged(APawn *OldPawn, APawn *NewPawn);
	UFUNCTION() void OnInventoryPresentationChanged();
	void ClearSources();
	void UpdateAttributes(const FOnAttributeChangeData &Data);
	void UpdateHUD();
	void UpdateSkills();
	void QueueSkillRefresh();
	void RefreshPresentation();
	void BindResourceAttributes();
	void ReleaseAllSkills();
	UPROPERTY(Transient) TObjectPtr<UProject_JPlayerHUDWidget> Screen;
	UPROPERTY(Transient) TObjectPtr<UProject_JInventoryViewModel> InventoryModel;
	TWeakObjectPtr<UProject_JAbilitySystemComponent> BoundASC;
	TWeakObjectPtr<UProject_JProgressionComponent> BoundProgression;
	TWeakObjectPtr<AProject_JPlayerState> BoundState;
	TMap<FGameplayAttribute, FDelegateHandle> AttributeHandles;
	/** Capture the original tag so a profile switch never releases a different input. */
	TMap<int32, FGameplayTag> PressedSkills;
	UPROPERTY(Transient) TObjectPtr<UProject_JCharacterUIProfile> ActiveProfile;
	UPROPERTY(Transient) TArray<FProject_JUISkillSlot> PresentedSkills;
	TArray<FProject_JUIResourceDefinition> PresentedResources;
	FProject_JUIContext UIContext;
	bool bPresentationInitialized = false;
	TWeakObjectPtr<AProject_JPlayerCharacter> PressedPawn;
	FTimerHandle SkillsTimer;
	FTimerHandle SkillsRefreshTimer;
	bool bMenuOpen = false;
	bool bPreviousCursor = false;
	bool bEnding = false;
};
