#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "Blueprint/DragDropOperation.h"
#include "Components/ListView.h"
#include "Components/TileView.h"
#include "UI/Project_JCharacterUIProfile.h"
#include "UI/Project_JInventoryViewModel.h"
#include "UI/Project_JUIModels.h"
#include "UI/Project_JStatusEffects.h"
#include "Project_JPlayerHUDWidget.generated.h"

class UTextBlock;
class UImage;
class UBorder;
class UCanvasPanel;
class UButton;
class UHorizontalBox;
class UVerticalBox;
class UProgressBar;
class UProject_JPlayerUIComponent;
struct FStreamableHandle;
class UProject_JHUDWindow;
class UProject_JMinimapWidget;
class UEditableTextBox;
class UScrollBox;
class UWrapBox;

UCLASS()
class PROJECT_J_API UProject_JSkillButton : public UUserWidget
{
	GENERATED_BODY()
  public:
	void Configure(UProject_JPlayerUIComponent *Owner, int32 Index);
	void UpdateLabel(const FText &Text, bool bAvailable);
	void SetIcon(TSoftObjectPtr<UTexture2D> Icon);
	void Present(const FProject_JQuickSlotState &State);

  protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry &, const FPointerEvent &) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry &, const FPointerEvent &) override;
	virtual void NativeDestruct() override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent &Event) override;
	virtual void NativeOnDragDetected(const FGeometry &, const FPointerEvent &, UDragDropOperation *&) override;
	virtual bool NativeOnDrop(const FGeometry &, const FDragDropEvent &, UDragDropOperation *) override;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> SkillLabel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> SkillIcon;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> KeyLabel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> QuantityLabel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UProgressBar> CooldownBar;

  private:
	TWeakObjectPtr<UProject_JPlayerUIComponent> ScreenOwner;
	int32 SlotIndex = INDEX_NONE;
	bool bAvailable = false;
	bool bPressed = false;
	bool bShowingIconFallback = false;
	TSharedPtr<FStreamableHandle> IconHandle;
	FSoftObjectPath IconPath;
};

UCLASS()
class PROJECT_J_API UProject_JQuickSlotDrag : public UDragDropOperation
{
	GENERATED_BODY()
  public:
	TWeakObjectPtr<UProject_JPlayerUIComponent> Owner;
	int32 SourceIndex = INDEX_NONE;
	FString Identity;
	FProject_JQuickSlotBinding Binding;
};

UCLASS()
class PROJECT_J_API UProject_JItemDragOperation : public UDragDropOperation
{
	GENERATED_BODY()
  public:
	UPROPERTY() TObjectPtr<UProject_JInventoryEntry> Entry;
};

/** Designer may replace this with a child Widget Blueprint; optional named controls receive native updates. */
UCLASS()
class PROJECT_J_API UProject_JItemWidget : public UUserWidget, public IUserObjectListEntry
{
	GENERATED_BODY()
  public:
	void Render();
	UPROPERTY(EditDefaultsOnly, Category = "UI|Skin") bool bCompactTile = false;

  protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeOnListItemObjectSet(UObject *ItemObject) override;
	virtual void NativeOnEntryReleased() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry &Geometry, const FPointerEvent &Event) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry &Geometry, const FPointerEvent &Event) override;
	virtual void NativeOnDragDetected(const FGeometry &Geometry, const FPointerEvent &Event,
									  UDragDropOperation *&Operation) override;
	virtual bool NativeOnDrop(const FGeometry &Geometry, const FDragDropEvent &Event,
							  UDragDropOperation *Operation) override;
	virtual void NativeOnDragEnter(const FGeometry &Geometry, const FDragDropEvent &Event,
								   UDragDropOperation *Operation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent &Event, UDragDropOperation *Operation) override;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ItemLabel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ItemQuantityLabel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ItemLockLabel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> ItemIcon;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UBorder> ItemFrame;

  private:
	UPROPERTY() TObjectPtr<UProject_JInventoryEntry> Entry;
	TSharedPtr<FStreamableHandle> IconHandle;
	FSoftObjectPath IconPath;
	void ReleaseIcon();
};

UCLASS()
class PROJECT_J_API UProject_JInventoryListView : public UListView
{
	GENERATED_BODY()
  public:
	UProject_JInventoryListView(const FObjectInitializer &Initializer);
	void ConfigureEntryClass(TSubclassOf<UProject_JItemWidget> InClass);

  private:
	void HandleItemDoubleClicked(UObject *Item);
};

UCLASS()
class PROJECT_J_API UProject_JInventoryTileView : public UTileView
{
	GENERATED_BODY()
  public:
	UProject_JInventoryTileView(const FObjectInitializer &Initializer);
	void ConfigureEntryClass(TSubclassOf<UProject_JItemWidget> InClass);

  private:
	void HandleItemDoubleClicked(UObject *Item);
};

/** Native usable screen plus BP skin contract. Gameplay requests remain in the model. */
UCLASS()
class PROJECT_J_API UProject_JPlayerHUDWidget : public UUserWidget
{
	GENERATED_BODY()
  public:
	void InitializeScreen(UProject_JPlayerUIComponent *Owner);
	void UpdateAttributes(const FText &Text);
	void UpdateSkills(const TArray<FText> &Labels, const TArray<bool> &Enabled);
	void SetMenuOpen(bool bOpen);
	void ApplyProfile(UProject_JCharacterUIProfile *Profile);
	void UpdateCharacter(const FProject_JUIContext &Context, const TArray<FProject_JUIResourceState> &Resources,
						 const FText &Details);
	void SetLayoutIdentity(const FGuid &CharacterId, FName ProfileId);
	void UpdateQuickSlots(const TArray<FProject_JQuickSlotState> &States);
	void UpdateExperience(int64 Current, int64 Required);
	void RefreshQuests();
	void ToggleWindow(FName Id);
	void CloseWindow(FName Id);
	void RaiseWindow(FName Id);
	bool HasOpenWindows() const
	{
		return !WindowStack.IsEmpty();
	}
	void ExecuteCommand(FName Command);
	void ApplyPreferences(const FProject_JHUDPreferences &Value);
	void RefreshKeySettings();
	void OpenSplit(UProject_JInventoryEntry *Entry);
	void PresentStatusEffects(const TArray<FProject_JStatusEffectState> &States);
	void PresentTarget(const FText &Name, float Current, float Maximum);
	bool ValidateRuntimeLayout() const;
	UFUNCTION(BlueprintCallable) void RefreshInventory();
	UPROPERTY(EditDefaultsOnly, Category = "UI|Skin") TSubclassOf<UProject_JItemWidget> ItemWidgetClass;
	UPROPERTY(EditDefaultsOnly, Category = "UI|Skin") TSubclassOf<UProject_JItemWidget> InventoryTileWidgetClass;
	UPROPERTY(EditDefaultsOnly, Category = "UI|Skin") TSubclassOf<UProject_JSkillButton> SkillWidgetClass;
	UPROPERTY(EditDefaultsOnly, Category = "UI|Skin") TSubclassOf<UProject_JHUDWindow> WindowClass;
	UPROPERTY(EditDefaultsOnly, Category = "UI|Skin") TObjectPtr<UProject_JHUDStyle> HUDStyle;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UProject_JInventoryViewModel> InventoryModel;

  protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry &Geometry, float DeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry &Geometry, const FKeyEvent &Event) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry &Geometry, const FKeyEvent &Event) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry &Geometry, const FPointerEvent &Event) override;
	virtual void NativeOnDragDetected(const FGeometry &Geometry, const FPointerEvent &Event,
									  UDragDropOperation *&Operation) override;
	virtual bool NativeOnDrop(const FGeometry &Geometry, const FDragDropEvent &Event,
							  UDragDropOperation *Operation) override;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> AttributeLabel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> StatusLabel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UListView> InventoryList;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UListView> EquipmentList;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> CharacterDetailsLabel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UVerticalBox> ResourcesBox;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UVerticalBox> CharacterModuleHost;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UBorder> MenuPanel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> MenuTitle;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> CloseButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UCanvasPanel> ScreenCanvas;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UHorizontalBox> ActionBar;

  private:
	UPROPERTY() TObjectPtr<UWrapBox> StatusEffectBox;
	UPROPERTY() TObjectPtr<UTextBlock> StatusOverflow;
	UPROPERTY() TArray<TObjectPtr<UProject_JSkillButton>> StatusIcons;
	TArray<FActiveGameplayEffectHandle> StatusHandles;
	UPROPERTY() TObjectPtr<UVerticalBox> TargetBox;
	UPROPERTY() TObjectPtr<UTextBlock> TargetLabel;
	UPROPERTY() TObjectPtr<UProgressBar> TargetHealth;
	UPROPERTY() TObjectPtr<UHorizontalBox> MenuButtons;
	UPROPERTY() TObjectPtr<UScrollBox> KeysScroll;
	UPROPERTY() TObjectPtr<UTextBlock> KeysStatus;
	UPROPERTY() TObjectPtr<UTextBlock> SplitLabel;
	UPROPERTY() TObjectPtr<class USpinBox> SplitCount;
	UPROPERTY() TObjectPtr<UProject_JInventoryEntry> SplitSource;
	UPROPERTY() TObjectPtr<UProject_JInventoryEntry> MergeSource;
	UPROPERTY() TObjectPtr<UTextBlock> NotificationLabel;
	FText LastInventoryStatus;
	FTimerHandle NotificationTimer;
	int32 CapturingKey = INDEX_NONE;
	UPROPERTY() TMap<FName, TObjectPtr<UProject_JHUDWindow>> Windows;
	TArray<FName> WindowStack;
	UPROPERTY() TObjectPtr<UProject_JMinimapWidget> Minimap;
	UPROPERTY() TObjectPtr<UTextBlock> QuestTracker;
	UPROPERTY() TObjectPtr<UProgressBar> ExperienceBar;
	UPROPERTY() TObjectPtr<UTextBlock> ExperienceLabel;
	UPROPERTY() TObjectPtr<UEditableTextBox> SearchBox;
	UPROPERTY() TObjectPtr<UScrollBox> QuestJournal;
	UPROPERTY() TObjectPtr<UTextBlock> BagSummary;
	UPROPERTY() TObjectPtr<UTextBlock> SettingsSummary;
	UPROPERTY() TObjectPtr<UTextBlock> SaveStatus;
	UPROPERTY() TObjectPtr<class UProject_JUICommandButton> SaveRetry;
	TWeakObjectPtr<class UProject_JUILayoutSettings> LayoutSettings;
	void RefreshSaveStatus();
	UPROPERTY() TObjectPtr<UWrapBox> SkillPalette;
	FTimerHandle SearchTimer;
	UFUNCTION() void SearchChanged(const FText &Text);
	void ApplySearch();
	int32 BagFilter = 0;
	bool bSortBag = false;
	bool bQuestsCollapsed = false;
	FProject_JHUDPreferences Preferences;
	UProject_JHUDWindow *AddWindow(FName Id, const FText &Title, FVector2D Size);
	UPROPERTY() TWeakObjectPtr<UProject_JPlayerUIComponent> ScreenOwner;
	UFUNCTION() void CloseMenu();
	FVector2D WindowDragOffset = FVector2D::ZeroVector;
	UPROPERTY() TArray<TObjectPtr<UProject_JSkillButton>> SkillButtons;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> ResourceLabels;
	UPROPERTY() TArray<TObjectPtr<UProgressBar>> ResourceBars;
	UPROPERTY() TObjectPtr<UProject_JCharacterHUDModule> CharacterModule;
	FString LayoutKey;
	FVector2D NormalizedMenuPosition = FVector2D(0.2, 0.2);
	FVector2D LastCanvasExtent = FVector2D::ZeroVector;
	FVector2D PreferredMenuSize = FVector2D(900, 540);
	void ApplyMenuLayout();
	void StoreMenuLayout();
	void BuildDefaultScreen();
};
