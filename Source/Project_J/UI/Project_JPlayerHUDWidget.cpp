#include "UI/Project_JPlayerHUDWidget.h"
#include "UI/Project_JPlayerUIComponent.h"
#include "UI/Project_JCraftingJobsWidget.h"
#include "UI/Project_JUILayoutSettings.h"
#include "UI/Project_JHUDWindow.h"
#include "UI/Project_JMinimapWidget.h"
#include "Game/Project_JQuestComponent.h"
#include "Game/Project_JPlayerState.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "Engine/GameInstance.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/SpinBox.h"
#include "TimerManager.h"
#include "Styling/CoreStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Engine/LocalPlayer.h"
#include "Engine/AssetManager.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "Inventory/Project_JItemDefinition.h"

#include "UI/Project_JWidgetPresentationHelpers.h"
using namespace ProjectJ::UI;

namespace
{
void ConfigureList(UListView* List, TSubclassOf<UProject_JItemWidget> RowClass)
{
	if (auto* Tile = Cast<UProject_JInventoryTileView>(List)) { Tile->ConfigureEntryClass(RowClass); }
	else if (auto* Rows = Cast<UProject_JInventoryListView>(List)) { Rows->ConfigureEntryClass(RowClass); }
}
}

TSharedRef<SWidget> UProject_JPlayerHUDWidget::RebuildWidget()
{
	if (!HUDStyle)
		HUDStyle = LoadObject<UProject_JHUDStyle>(nullptr, TEXT("/Game/UI/DA_ProjectJHUDStyle.DA_ProjectJHUDStyle"));
	if (WidgetTree && !WidgetTree->RootWidget)
		BuildDefaultScreen();
	UClass *RowClass =
		ItemWidgetClass
			? ItemWidgetClass.Get()
			: LoadClass<UProject_JItemWidget>(nullptr, TEXT("/Game/UI/WBP_ProjectJItemRow.WBP_ProjectJItemRow_C"));
	UClass *TileClass =
		InventoryTileWidgetClass
			? InventoryTileWidgetClass.Get()
			: LoadClass<UProject_JItemWidget>(nullptr, TEXT("/Game/UI/WBP_ProjectJItemTile.WBP_ProjectJItemTile_C"));
	ConfigureList(InventoryList, Cast<UProject_JInventoryTileView>(InventoryList) && TileClass ? TileClass : RowClass);
	ConfigureList(EquipmentList, Cast<UProject_JInventoryTileView>(EquipmentList) && TileClass ? TileClass : RowClass);
	if (auto *Tiles = Cast<UProject_JInventoryTileView>(InventoryList); Tiles && HUDStyle)
	{
		Tiles->SetEntryWidth(FMath::Clamp(HUDStyle->ItemSize, 36.f, 64.f));
		Tiles->SetEntryHeight(FMath::Clamp(HUDStyle->ItemSize, 36.f, 64.f));
	}
	return Super::RebuildWidget();
}
UProject_JHUDWindow *UProject_JPlayerHUDWidget::AddWindow(FName Id, const FText &Title, FVector2D Size)
{
	auto *Window = CreateWidget<UProject_JHUDWindow>(
		GetOwningPlayer(), WindowClass ? WindowClass.Get() : UProject_JHUDWindow::StaticClass());
	Window->InitializeWindow(this, Id, Title);
	Window->PreferredSize = Size;
	Window->TakeWidget();
	auto *WindowCanvasSlot = ScreenCanvas->AddChildToCanvas(Window);
	WindowCanvasSlot->SetSize(Size);
	WindowCanvasSlot->SetZOrder(100);
	Window->SetVisibility(ESlateVisibility::Collapsed);
	Windows.Add(Id, Window);
	return Window;
}
void UProject_JPlayerHUDWidget::BuildDefaultScreen()
{
	ScreenCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ScreenCanvas"));
	WidgetTree->RootWidget = ScreenCanvas;
	ScreenCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	SetIsFocusable(true);
	auto Text = [&](const FText &Value, int32 Size = 12)
	{
		auto *Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(Value);
		auto Font = Label->GetFont();
		Font.Size = HUDStyle && Size == 12 ? FMath::Clamp(HUDStyle->FontSize, 10, 18) : Size;
		Label->SetFont(Font);
		return Label;
	};
	auto Command = [&](FName Id, const FText &Label)
	{
		auto *Button = WidgetTree->ConstructWidget<UProject_JUICommandButton>();
		Button->InitializeCommand(this, Id, Label);
		return Button;
	};
	AttributeLabel = Text(FText::GetEmpty(), 16);
	auto *LevelSlot = ScreenCanvas->AddChildToCanvas(AttributeLabel);
	LevelSlot->SetPosition(FVector2D(16, 14));
	LevelSlot->SetAutoSize(true);
	ResourcesBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ResourcesBox"));
	auto *ResourceSlot = ScreenCanvas->AddChildToCanvas(ResourcesBox);
	ResourceSlot->SetPosition(FVector2D(16, 38));
	ResourceSlot->SetSize(FVector2D(210, 54));
	auto *XPColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	auto *XPSlot = ScreenCanvas->AddChildToCanvas(XPColumn);
	XPSlot->SetPosition(FVector2D(16, 98));
	XPSlot->SetSize(FVector2D(210, 25));
	ExperienceLabel = Text(FText::GetEmpty(), 10);
	XPColumn->AddChildToVerticalBox(ExperienceLabel);
	ExperienceBar = WidgetTree->ConstructWidget<UProgressBar>();
	auto *XPSize = WidgetTree->ConstructWidget<USizeBox>();
	XPSize->SetHeightOverride(3);
	XPSize->SetContent(ExperienceBar);
	XPColumn->AddChildToVerticalBox(XPSize);
	ExperienceBar->SetFillColorAndOpacity(FLinearColor(0.85f, 0.65f, 0.16f));
	StatusEffectBox = WidgetTree->ConstructWidget<UWrapBox>();
	StatusEffectBox->SetExplicitWrapSize(true);
	StatusEffectBox->SetWrapSize(220);
	auto *StatusSlot = ScreenCanvas->AddChildToCanvas(StatusEffectBox);
	StatusSlot->SetPosition(FVector2D(16, 140));
	StatusSlot->SetSize(FVector2D(220, 80));
	StatusOverflow = Text(FText::GetEmpty(), 10);
	TargetBox = WidgetTree->ConstructWidget<UVerticalBox>();
	auto *TargetSlot = ScreenCanvas->AddChildToCanvas(TargetBox);
	TargetSlot->SetAnchors(FAnchors(.5f, 0));
	TargetSlot->SetAlignment(FVector2D(.5f, 0));
	TargetSlot->SetPosition(FVector2D(0, 16));
	TargetSlot->SetSize(FVector2D(220, 40));
	TargetLabel = Text(FText::GetEmpty());
	TargetLabel->SetAutoWrapText(true);
	TargetBox->AddChildToVerticalBox(TargetLabel);
	TargetHealth = WidgetTree->ConstructWidget<UProgressBar>();
	auto *TargetSize = WidgetTree->ConstructWidget<USizeBox>();
	TargetSize->SetHeightOverride(5);
	TargetSize->SetContent(TargetHealth);
	TargetHealth->SetFillColorAndOpacity(FLinearColor(.75f, .12f, .15f));
	TargetBox->AddChildToVerticalBox(TargetSize);
	TargetBox->SetVisibility(ESlateVisibility::Collapsed);
	PartyBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PartyBox"));
	auto* PartySlot = ScreenCanvas->AddChildToCanvas(PartyBox);
	PartySlot->SetPosition(FVector2D(16, 230));
	PartySlot->SetSize(FVector2D(220, 220));
	PartyBox->SetVisibility(ESlateVisibility::Collapsed);
	CharacterModuleHost =
		WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CharacterModuleHost"));
	auto *ModuleSlot = ScreenCanvas->AddChildToCanvas(CharacterModuleHost);
	ModuleSlot->SetAnchors(FAnchors(0.5f, 1));
	ModuleSlot->SetAlignment(FVector2D(0.5, 1));
	ModuleSlot->SetPosition(FVector2D(0, -76));
	ModuleSlot->SetAutoSize(true);
	ActionBar = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ActionBar"));
	auto *BarSlot = ScreenCanvas->AddChildToCanvas(ActionBar);
	BarSlot->SetAnchors(FAnchors(0.5f, 1));
	BarSlot->SetAlignment(FVector2D(0.5, 1));
	BarSlot->SetPosition(FVector2D(0, -12));
	BarSlot->SetAutoSize(true);
	NotificationLabel = Text(FText::GetEmpty(), 11);
	NotificationLabel->SetAutoWrapText(true);
	NotificationLabel->SetJustification(ETextJustify::Center);
	auto *NotificationSlot = ScreenCanvas->AddChildToCanvas(NotificationLabel);
	NotificationSlot->SetAnchors(FAnchors(.5f, 1));
	NotificationSlot->SetAlignment(FVector2D(.5f, 1));
	NotificationSlot->SetPosition(FVector2D(0, -120));
	NotificationSlot->SetSize(FVector2D(380, 42));
	NotificationLabel->SetVisibility(ESlateVisibility::Collapsed);
	auto *Menu = WidgetTree->ConstructWidget<UHorizontalBox>();
	MenuButtons = Menu;
	auto *MenuSlot = ScreenCanvas->AddChildToCanvas(Menu);
	MenuSlot->SetAnchors(FAnchors(1, 1));
	MenuSlot->SetAlignment(FVector2D(1, 1));
	MenuSlot->SetPosition(FVector2D(-12, -12));
	MenuSlot->SetAutoSize(true);
	Menu->AddChildToHorizontalBox(Command(TEXT("Bag"), NSLOCTEXT("ProjectJUI", "BagButton", "가방")))
		->SetPadding(FMargin(2, 0));
	Menu->AddChildToHorizontalBox(Command(TEXT("Equipment"), NSLOCTEXT("ProjectJUI", "GearButton", "장비")))
		->SetPadding(FMargin(2, 0));
	Menu->AddChildToHorizontalBox(Command(TEXT("Quests"), NSLOCTEXT("ProjectJUI", "QuestButton", "의뢰")))
		->SetPadding(FMargin(2, 0));
	Menu->AddChildToHorizontalBox(Command(TEXT("Crafting"), NSLOCTEXT("ProjectJUI", "CraftingButton", "제작")))
		->SetPadding(FMargin(2, 0));
	Menu->AddChildToHorizontalBox(Command(TEXT("Settings"), NSLOCTEXT("ProjectJUI", "SettingsButton", "설정")))
		->SetPadding(FMargin(2, 0));
	Minimap = CreateWidget<UProject_JMinimapWidget>(GetOwningPlayer());
	auto *MapSlot = ScreenCanvas->AddChildToCanvas(Minimap);
	MapSlot->SetAnchors(FAnchors(1, 0));
	MapSlot->SetAlignment(FVector2D(1, 0));
	MapSlot->SetPosition(FVector2D(-12, 12));
	MapSlot->SetSize(FVector2D(168, 168));
	auto *TrackerColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	auto *TrackerSlot = ScreenCanvas->AddChildToCanvas(TrackerColumn);
	TrackerSlot->SetAnchors(FAnchors(1, 0));
	TrackerSlot->SetAlignment(FVector2D(1, 0));
	TrackerSlot->SetPosition(FVector2D(-12, 188));
	TrackerSlot->SetSize(FVector2D(220, 160));
	TrackerColumn->AddChildToVerticalBox(
		Command(TEXT("CollapseQuests"), NSLOCTEXT("ProjectJUI", "TrackerHeader", "추적 의뢰")));
	QuestTracker = Text(FText::GetEmpty(), 12);
	QuestTracker->SetAutoWrapText(true);
	TrackerColumn->AddChildToVerticalBox(QuestTracker);
	auto *Bag = AddWindow(TEXT("Bag"), NSLOCTEXT("ProjectJUI", "BagTitle", "가방"), FVector2D(320, 400));
	auto *SearchRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	Bag->BodyBox->AddChildToVerticalBox(SearchRow)->SetPadding(FMargin(0, 0, 0, 6));
	SearchBox = WidgetTree->ConstructWidget<UEditableTextBox>();
	SearchBox->SetHintText(NSLOCTEXT("ProjectJUI", "SearchHint", "아이템 검색"));
	auto SearchStyle = SearchBox->GetWidgetStyle();
	SearchStyle.TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 12));
	SearchStyle.SetForegroundColor(FLinearColor(0.88f, 0.92f, 0.96f));
	SearchStyle.BackgroundImageNormal = *FCoreStyle::Get().GetBrush("WhiteBrush");
	SearchStyle.BackgroundImageHovered = SearchStyle.BackgroundImageNormal;
	SearchStyle.BackgroundImageFocused = SearchStyle.BackgroundImageNormal;
	SearchStyle.BackgroundImageNormal.TintColor = FSlateColor(FLinearColor(0.05f, 0.08f, 0.1f));
	SearchStyle.BackgroundImageHovered.TintColor = SearchStyle.BackgroundImageNormal.TintColor;
	SearchStyle.BackgroundImageFocused.TintColor = SearchStyle.BackgroundImageNormal.TintColor;
	SearchBox->SetWidgetStyle(SearchStyle);
	SearchRow->AddChildToHorizontalBox(SearchBox)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SearchRow->AddChildToHorizontalBox(Command(TEXT("Sort"), NSLOCTEXT("ProjectJUI", "SortButton", "정렬")));
	SearchRow->AddChildToHorizontalBox(Command(TEXT("Filter"), NSLOCTEXT("ProjectJUI", "FilterButton", "분류")));
	auto *BagActions = WidgetTree->ConstructWidget<UHorizontalBox>();
	Bag->BodyBox->AddChildToVerticalBox(BagActions)->SetPadding(FMargin(0, 0, 0, 6));
	BagActions->AddChildToHorizontalBox(Command(TEXT("SplitSelected"), NSLOCTEXT("ProjectJUI", "SplitSelected", "분할")));
	BagActions->AddChildToHorizontalBox(Command(TEXT("MergeSelected"), NSLOCTEXT("ProjectJUI", "MergeSelected", "합치기")));
	BagActions->AddChildToHorizontalBox(Command(TEXT("CancelMerge"), NSLOCTEXT("ProjectJUI", "CancelMerge", "선택 취소")));
	BagActions->SetToolTipText(NSLOCTEXT("ProjectJUI", "BagKeyboard", "방향키: 선택 · Enter: 사용/장착 · Shift+Enter: 분할 · Ctrl+M: 합치기 · Ctrl+F: 검색 · Tab: 버튼 이동"));
	InventoryList = WidgetTree->ConstructWidget<UProject_JInventoryTileView>(UProject_JInventoryTileView::StaticClass(),
																			 TEXT("InventoryList"));
	Bag->BodyBox->AddChildToVerticalBox(InventoryList)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	BagSummary = Text(FText::GetEmpty(), 11);
	Bag->BodyBox->AddChildToVerticalBox(BagSummary);
	StatusLabel = Text(FText::GetEmpty(), 11);
	StatusLabel->SetAutoWrapText(true);
	Bag->BodyBox->AddChildToVerticalBox(StatusLabel);
	auto *Gear = AddWindow(TEXT("Equipment"), NSLOCTEXT("ProjectJUI", "GearTitle", "장비"), FVector2D(250, 350));
	EquipmentList = WidgetTree->ConstructWidget<UProject_JInventoryTileView>(UProject_JInventoryTileView::StaticClass(),
																			 TEXT("EquipmentList"));
	auto *EquipmentTiles = Cast<UProject_JInventoryTileView>(EquipmentList);
	EquipmentTiles->SetEntryWidth(74);
	EquipmentTiles->SetEntryHeight(68);
	Gear->BodyBox->AddChildToVerticalBox(EquipmentList)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	CharacterDetailsLabel = Text(FText::GetEmpty(), 12);
	CharacterDetailsLabel->SetAutoWrapText(true);
	Gear->BodyBox->AddChildToVerticalBox(CharacterDetailsLabel);
	auto *Journal = AddWindow(TEXT("Quests"), NSLOCTEXT("ProjectJUI", "QuestTitle", "의뢰"), FVector2D(340, 400));
    QuestHistoryCommands = WidgetTree->ConstructWidget<UHorizontalBox>();
    Journal->BodyBox->AddChildToVerticalBox(QuestHistoryCommands);
    QuestHistoryStatus = Text(FText::GetEmpty(), 11);
    Journal->BodyBox->AddChildToVerticalBox(QuestHistoryStatus);
	QuestJournal = WidgetTree->ConstructWidget<UScrollBox>();
	Journal->BodyBox->AddChildToVerticalBox(QuestJournal)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    QuestHistoryList = WidgetTree->ConstructWidget<UScrollBox>();
    Journal->BodyBox->AddChildToVerticalBox(QuestHistoryList)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    QuestHistoryList->SetVisibility(ESlateVisibility::Collapsed);
	auto* Crafting = AddWindow(TEXT("Crafting"), NSLOCTEXT("ProjectJUI", "CraftingTitle", "제작 작업"), FVector2D(360, 420));
	CraftingJobsView = CreateWidget<UProject_JCraftingJobsWidget>(GetOwningPlayer());
	if (CraftingJobsView)
	{
		CraftingJobsView->TakeWidget();
		Crafting->BodyBox->AddChildToVerticalBox(CraftingJobsView)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	auto *Skills = AddWindow(TEXT("Skills"), NSLOCTEXT("ProjectJUI", "SkillLibraryTitle", "스킬"), FVector2D(310, 220));
	auto *SkillScroll = WidgetTree->ConstructWidget<UScrollBox>();
	Skills->BodyBox->AddChildToVerticalBox(SkillScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SkillPalette = WidgetTree->ConstructWidget<UWrapBox>();
	SkillScroll->AddChild(SkillPalette);
	auto *Settings =
		AddWindow(TEXT("Settings"), NSLOCTEXT("ProjectJUI", "SettingsTitle", "UI 설정"), FVector2D(270, 440));
	SettingsSummary = Text(FText::GetEmpty(), 11);
	Settings->BodyBox->AddChildToVerticalBox(SettingsSummary)->SetPadding(FMargin(0, 0, 0, 8));
	for (const auto &Pair :
		 {TPair<FName, FText>(TEXT("Keys"), NSLOCTEXT("ProjectJUI", "KeysButton", "단축키 설정")),
	  TPair<FName, FText>(TEXT("Skills"), NSLOCTEXT("ProjectJUI", "SkillLibraryButton", "스킬 등록 창")),
		  TPair<FName, FText>(TEXT("Lock"), NSLOCTEXT("ProjectJUI", "LockToggle", "퀵슬롯 잠금 / 해제")),
		  TPair<FName, FText>(TEXT("Values"), NSLOCTEXT("ProjectJUI", "ValuesToggle", "자원 수치 표시 / 숨김")),
		  TPair<FName, FText>(TEXT("ScaleDown"), NSLOCTEXT("ProjectJUI", "SmallerHUD", "HUD 작게 −")),
		  TPair<FName, FText>(TEXT("ScaleUp"), NSLOCTEXT("ProjectJUI", "LargerHUD", "HUD 크게 +")),
		  TPair<FName, FText>(TEXT("Reset"), NSLOCTEXT("ProjectJUI", "ResetHUD", "퀵슬롯·HUD 기본값 복원")),
		  TPair<FName, FText>(TEXT("Both"), NSLOCTEXT("ProjectJUI", "BothMenus", "가방과 장비 함께 열기"))})
		Settings->BodyBox->AddChildToVerticalBox(Command(Pair.Key, Pair.Value))->SetPadding(FMargin(0, 0, 0, 8));
	SaveStatus = Text(FText::GetEmpty(), 10);
	SaveStatus->SetAutoWrapText(true);
	Settings->BodyBox->AddChildToVerticalBox(SaveStatus);
	SaveRetry = Command(TEXT("RetrySave"), NSLOCTEXT("ProjectJUI", "RetrySave", "저장 다시 시도"));
	Settings->BodyBox->AddChildToVerticalBox(SaveRetry);
	ProfileSaveStatus=Text(FText::GetEmpty(),10);
	ProfileSaveStatus->SetAutoWrapText(true);
	Settings->BodyBox->AddChildToVerticalBox(ProfileSaveStatus);
	auto *KeysWindow = AddWindow(TEXT("Keys"), NSLOCTEXT("ProjectJUI", "KeysTitle", "단축키 설정"), FVector2D(310, 410));
	KeysStatus = Text(NSLOCTEXT("ProjectJUI", "KeysHelp", "변경할 항목 클릭 · 숫자/I/K/O/U/F1~F12"), 11);
	KeysStatus->SetAutoWrapText(true);
	KeysWindow->BodyBox->AddChildToVerticalBox(KeysStatus);
	KeysScroll = WidgetTree->ConstructWidget<UScrollBox>();
	KeysWindow->BodyBox->AddChildToVerticalBox(KeysScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	KeysWindow->BodyBox->AddChildToVerticalBox(Command(TEXT("ResetKeys"), NSLOCTEXT("ProjectJUI", "ResetKeys", "단축키 기본값 복원")));
	auto *SplitWindow = AddWindow(TEXT("Split"), NSLOCTEXT("ProjectJUI", "SplitTitle", "스택 분할"), FVector2D(270, 170));
	SplitLabel = Text(FText::GetEmpty());
	SplitLabel->SetAutoWrapText(true);
	SplitWindow->BodyBox->AddChildToVerticalBox(SplitLabel);
	SplitCount = WidgetTree->ConstructWidget<USpinBox>();
	SplitCount->SetMinValue(1);
	SplitCount->SetDelta(1);
	SplitCount->SetMinFractionalDigits(0);
	SplitCount->SetMaxFractionalDigits(0);
	SplitWindow->BodyBox->AddChildToVerticalBox(SplitCount);
	SplitWindow->BodyBox->AddChildToVerticalBox(Command(TEXT("SplitConfirm"), NSLOCTEXT("ProjectJUI", "SplitConfirm", "분할")));
	SplitWindow->BodyBox->AddChildToVerticalBox(Command(TEXT("Close.Split"), NSLOCTEXT("ProjectJUI", "SplitCancel", "취소")));

}

void UProject_JPlayerHUDWidget::InitializeScreen(UProject_JPlayerUIComponent *Owner)
{
    if (ScreenOwner.Get() != Owner)
    {
        const uint64 Before = QuestPresentationRevision;
        ClearQuestPresentation();
        if (Before == MAX_uint64 || QuestPresentationRevision != Before + 1) return;
    }
	if (LayoutSettings.IsValid()) LayoutSettings->OnSaveStateChanged.RemoveAll(this);
	if (InventoryModel) InventoryModel->OnSplitRequested.RemoveAll(this);
	ScreenOwner = Owner;
	if (auto *LocalPlayer = GetOwningLocalPlayer())
	{
		LayoutSettings = LocalPlayer->GetSubsystem<UProject_JUILayoutSettings>();
		LayoutSettings->OnSaveStateChanged.AddUObject(this, &ThisClass::RefreshSaveStatus);
	}
	RefreshSaveStatus();
	InventoryModel = Owner ? Owner->GetInventoryModel() : nullptr;
	if (InventoryModel) InventoryModel->OnSplitRequested.AddUObject(this, &ThisClass::OpenSplit);
	RefreshInventory();
	RefreshKeySettings();
}
void UProject_JPlayerHUDWidget::NativeConstruct()
{
    bQuestPresentationRetired = false;
	Super::NativeConstruct();
	if (CloseButton)
		CloseButton->OnClicked.AddUniqueDynamic(this, &ThisClass::CloseMenu);
	if (SearchBox)
		SearchBox->OnTextChanged.AddUniqueDynamic(this, &ThisClass::SearchChanged);
}
void UProject_JPlayerHUDWidget::NativeDestruct()
{
    bQuestPresentationRetired = true;
    ClearQuestPresentation();
	if (ScreenOwner.IsValid()) ScreenOwner->ReleaseCraftingScreen(this);
	if (CraftingJobsView) CraftingJobsView->Present(nullptr, nullptr);
	PresentPartyRoster({});
	if (LayoutSettings.IsValid()) LayoutSettings->OnSaveStateChanged.RemoveAll(this);
	if (InventoryModel) InventoryModel->OnSplitRequested.RemoveAll(this);
	if (CloseButton)
		CloseButton->OnClicked.RemoveAll(this);
	if (SearchBox)
		SearchBox->OnTextChanged.RemoveAll(this);
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(SearchTimer);
		GetWorld()->GetTimerManager().ClearTimer(NotificationTimer);
	}
	Super::NativeDestruct();
}
void UProject_JPlayerHUDWidget::RefreshSaveStatus()
{
	if (SaveStatus && LayoutSettings.IsValid()) SaveStatus->SetText(LayoutSettings->GetSaveStatus());
	if (SaveRetry) SaveRetry->SetVisibility(LayoutSettings.IsValid() &&
		LayoutSettings->GetSaveState() == EProject_JUIPreferenceState::Failed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}
void UProject_JPlayerHUDWidget::CloseMenu()
{
	if (ScreenOwner.IsValid())
		ScreenOwner->SetMenuOpen(false);
}
void UProject_JPlayerHUDWidget::UpdateAttributes(const FText &Text)
{
	if (AttributeLabel)
		AttributeLabel->SetText(Text);
}
void UProject_JPlayerHUDWidget::ApplyProfile(UProject_JCharacterUIProfile *Profile)
{
	if (SkillPalette && ScreenOwner.IsValid())
	{
		SkillPalette->ClearChildren();
		const auto &Slots = ScreenOwner->GetPresentedSkills();
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			auto *Button = CreateWidget<UProject_JSkillButton>(
				GetOwningPlayer(), SkillWidgetClass ? SkillWidgetClass.Get() : UProject_JSkillButton::StaticClass());
			Button->Configure(ScreenOwner.Get(), 10 + Index);
			SkillPalette->AddChildToWrapBox(Button)->SetPadding(FMargin(2, 0));
			FProject_JQuickSlotState State;
			State.Binding.Kind = EProject_JQuickSlotKind::Skill;
			State.Binding.InputTag = Slots[Index].InputTag;
			State.Name = Slots[Index].Label;
			State.Icon = Slots[Index].Icon;
			State.bAvailable = true;
			Button->Present(State);
		}
	}
	if (ActionBar)
		ActionBar->ClearChildren();
	SkillButtons.Reset();
	if (CharacterModuleHost)
		CharacterModuleHost->ClearChildren();
	CharacterModule = nullptr;
	if (Profile && Profile->CharacterModuleClass && CharacterModuleHost)
	{
		CharacterModule = CreateWidget<UProject_JCharacterHUDModule>(GetOwningPlayer(), Profile->CharacterModuleClass);
		if (CharacterModule)
			CharacterModuleHost->AddChildToVerticalBox(CharacterModule);
	}
}
void UProject_JPlayerHUDWidget::UpdateCharacter(const FProject_JUIContext &Context,
												const TArray<FProject_JUIResourceState> &Resources,
												const FText &Details)
{
	if (CharacterDetailsLabel)
		CharacterDetailsLabel->SetText(Details);
	if (ResourcesBox)
	{
		if (ResourceLabels.Num() != Resources.Num())
		{
			ResourcesBox->ClearChildren();
			ResourceLabels.Reset();
			ResourceBars.Reset();
			for (const auto &Resource : Resources)
			{
				auto *Label = WidgetTree->ConstructWidget<UTextBlock>();
				auto Font = Label->GetFont();
				Font.Size = 11;
				Label->SetFont(Font);
				ResourcesBox->AddChildToVerticalBox(Label);
				ResourceLabels.Add(Label);
				auto *Bar = WidgetTree->ConstructWidget<UProgressBar>();
				auto *BarSize = WidgetTree->ConstructWidget<USizeBox>();
				BarSize->SetHeightOverride(5);
				BarSize->SetContent(Bar);
				ResourcesBox->AddChildToVerticalBox(BarSize)->SetPadding(FMargin(0, 1, 0, 3));
				ResourceBars.Add(Bar);
			}
		}
		for (int32 Index = 0; Index < Resources.Num(); ++Index)
		{
			const auto &Resource = Resources[Index];
			ResourceLabels[Index]->SetText(FText::Format(NSLOCTEXT("ProjectJUI", "Resource", "{0} {1}/{2}"),
														 Resource.Label,
														 FText::AsNumber(FMath::RoundToInt(Resource.Current)),
														 FText::AsNumber(FMath::RoundToInt(Resource.Maximum))));
			ResourceLabels[Index]->SetVisibility(Preferences.bShowResourceValues ? ESlateVisibility::HitTestInvisible
																				 : ESlateVisibility::Collapsed);
			ResourceBars[Index]->SetPercent(Resource.Fraction);
			ResourceBars[Index]->SetFillColorAndOpacity(Resource.Color);
		}
	}
	if (CharacterModule)
		CharacterModule->PresentCharacter(Context, Resources);
	ApplyPreferences(Preferences);
}
void UProject_JPlayerHUDWidget::SetLayoutIdentity(const FGuid &CharacterId, FName ProfileId)
{
	const FString NewKey = UProject_JUILayoutSave::MakeKey(CharacterId, ProfileId);
	if (NewKey == LayoutKey)
		return;
	MergeSource = nullptr;
	if (WindowStack.Contains(TEXT("Split"))) CloseWindow(TEXT("Split"));
	LayoutKey = NewKey;
	for (const auto &Pair : Windows)
		Pair.Value->SetIdentity(LayoutKey);
	NormalizedMenuPosition = FVector2D(0.2, 0.2);
	if (auto *LocalPlayer = GetOwningLocalPlayer())
		LocalPlayer->GetSubsystem<UProject_JUILayoutSettings>()->Read(LayoutKey, NormalizedMenuPosition);
	ApplyMenuLayout();
}
void UProject_JPlayerHUDWidget::ApplyMenuLayout()
{
	if (!ScreenCanvas)
		return;
	const FVector2D ScreenExtent = ScreenCanvas->GetCachedGeometry().GetLocalSize();
	if (!Windows.IsEmpty())
	{
		for (const auto &Pair : Windows)
			Pair.Value->ApplyLayout(ScreenExtent);
		LastCanvasExtent = ScreenExtent;
		ApplyPreferences(Preferences);
		if (MenuButtons) if (auto *MenuSlot = Cast<UCanvasPanelSlot>(MenuButtons->Slot))
			MenuSlot->SetPosition(FVector2D(-12, ScreenExtent.X < 1050 ? -78 : -12));
		return;
	}
	if (!MenuPanel)
		return;
	if (auto *PanelSlot = Cast<UCanvasPanelSlot>(MenuPanel->Slot))
	{
		const FVector2D Extent = ScreenCanvas->GetCachedGeometry().GetLocalSize();
		if (Extent.X <= 0 || Extent.Y <= 0)
			return;
		const FVector2D Size(FMath::Min(PreferredMenuSize.X, Extent.X), FMath::Min(PreferredMenuSize.Y, Extent.Y));
		PanelSlot->SetSize(Size);
		PanelSlot->SetPosition((Extent - Size) * NormalizedMenuPosition);
		LastCanvasExtent = Extent;
	}
}
void UProject_JPlayerHUDWidget::NativeTick(const FGeometry &Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	ProfileStatusElapsed+=DeltaTime;
	if(ProfileSaveStatus&&ProfileStatusElapsed>=.25f)
	{
		ProfileStatusElapsed=0.f;
		auto* Profiles=GetGameInstance()?GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>():nullptr;
		auto* PC=GetOwningPlayer();
		const FText Current=Profiles?Profiles->GetStatusText(PC?PC->GetPlayerState<AProject_JPlayerState>():nullptr):FText::GetEmpty();
		if(!Current.EqualTo(LastProfileSaveStatus)){LastProfileSaveStatus=Current;ProfileSaveStatus->SetText(Current);}
	}
	const auto* Crafting = Windows.FindRef(TEXT("Crafting")).Get();
	if (CraftingJobsView && Crafting && Crafting->IsVisible() && CraftingJobsView->NeedsClockRefresh())
	{
		CraftingClockElapsed += DeltaTime;
		if (CraftingClockElapsed >= 1.f) { CraftingClockElapsed = 0.f; CraftingJobsView->RefreshClock(); }
	}
	else CraftingClockElapsed = 0.f;
	// Geometry/DPI only; gameplay presentation stays delegate driven.
	if (ScreenCanvas && !ScreenCanvas->GetCachedGeometry().GetLocalSize().Equals(LastCanvasExtent))
		ApplyMenuLayout();
}
void UProject_JPlayerHUDWidget::StoreMenuLayout()
{
	if (auto *LocalPlayer = GetOwningLocalPlayer(); LocalPlayer && !LayoutKey.IsEmpty())
		LocalPlayer->GetSubsystem<UProject_JUILayoutSettings>()->Write(LayoutKey, NormalizedMenuPosition);
}
void UProject_JPlayerHUDWidget::UpdateSkills(const TArray<FText> &Labels, const TArray<bool> &Enabled)
{
	if (!ActionBar)
		return;
	if (SkillButtons.Num() != Labels.Num())
	{
		ActionBar->ClearChildren();
		SkillButtons.Reset();
		for (int32 Index = 0; Index < Labels.Num(); ++Index)
		{
			auto *Button = CreateWidget<UProject_JSkillButton>(
				GetOwningPlayer(), SkillWidgetClass ? SkillWidgetClass.Get() : UProject_JSkillButton::StaticClass());
			Button->Configure(ScreenOwner.Get(), Index);
			ActionBar->AddChildToHorizontalBox(Button)->SetPadding(FMargin(3, 0));
			SkillButtons.Add(Button);
		}
	}
	for (int32 Index = 0; Index < Labels.Num(); ++Index)
	{
		SkillButtons[Index]->UpdateLabel(Labels[Index], Enabled.IsValidIndex(Index) && Enabled[Index]);
		if (ScreenOwner.IsValid() && ScreenOwner->GetPresentedSkills().IsValidIndex(Index))
			SkillButtons[Index]->SetIcon(ScreenOwner->GetPresentedSkills()[Index].Icon);
	}
}
void UProject_JPlayerHUDWidget::SetMenuOpen(bool bOpen)
{
	SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible);
	if (ScreenCanvas)
		ScreenCanvas->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible);
	if (!bOpen)
	{
		CapturingKey = INDEX_NONE;
		SplitSource = nullptr;
		MergeSource = nullptr;
		for (const auto &Pair : Windows)
			Pair.Value->SetVisibility(ESlateVisibility::Collapsed);
		WindowStack.Reset();
	}
	if (MenuPanel)
		MenuPanel->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (bOpen)
	{
		ApplyMenuLayout();
		RefreshInventory();
		FocusTopWindow();
	}
}
void UProject_JPlayerHUDWidget::ResetTransientInteraction()
{
	// Only cancel this screen's drag; another LocalPlayer may own the active operation.
	auto *Drag = Cast<UProject_JItemDragOperation>(UWidgetBlueprintLibrary::GetDragDroppingContent());
	auto *QuickDrag = Cast<UProject_JQuickSlotDrag>(UWidgetBlueprintLibrary::GetDragDroppingContent());
	if ((Drag && Drag->Entry && Drag->Entry->Model == InventoryModel) ||
		(QuickDrag && QuickDrag->Owner == ScreenOwner)) UWidgetBlueprintLibrary::CancelDragDrop();
	CapturingKey = INDEX_NONE;
	SplitSource = nullptr;
	MergeSource = nullptr;
	BagFilter = 0;
	bSortBag = false;
	if (SearchBox) SearchBox->SetText(FText::GetEmpty());
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(SearchTimer);
		GetWorld()->GetTimerManager().ClearTimer(NotificationTimer);
	}
	if (InventoryModel) InventoryModel->SetFilter(FString(), 0, false);
	LastInventoryStatus = FText::GetEmpty();
	if (NotificationLabel) NotificationLabel->SetVisibility(ESlateVisibility::Collapsed);
	for (auto *List : {InventoryList.Get(), EquipmentList.Get()})
		if (List) { List->ClearSelection(); List->SetScrollOffset(0); }
}
void UProject_JPlayerHUDWidget::RefreshInventory()
{
	if (!InventoryModel)
		return;
	auto UpdateList = [](UListView *List, const TArray<TObjectPtr<UProject_JInventoryEntry>> &Entries)
	{
		if (!List)
			return;
		TArray<UObject *> Objects;
		for (UProject_JInventoryEntry *Entry : Entries)
			Objects.Add(Entry);
		if (List->GetListItems() != Objects)
		{
			const float Scroll = List->GetScrollOffset();
			UObject *Selected = List->GetSelectedItem();
			List->SetListItems(Objects);
			List->SetScrollOffset(FMath::Clamp(Scroll, 0.f, FMath::Max(0.f, static_cast<float>(Objects.Num() - 1))));
			if (Objects.Contains(Selected))
				List->SetSelectedItem(Selected);
		}
	};
	UpdateList(InventoryList, InventoryModel->VisibleEntries);
	TArray<TObjectPtr<UProject_JInventoryEntry>> EquipmentOrder;
	for (auto EquipmentSlot :
		 {EProject_JEquipmentSlot::Back, EProject_JEquipmentSlot::Head, EProject_JEquipmentSlot::Accessory,
		  EProject_JEquipmentSlot::Hands, EProject_JEquipmentSlot::Chest, EProject_JEquipmentSlot::Weapon,
		  EProject_JEquipmentSlot::Legs, EProject_JEquipmentSlot::Feet, EProject_JEquipmentSlot::Mount})
		if (const auto *Entry = InventoryModel->EquipmentEntries.FindByPredicate(
				[EquipmentSlot](const auto &Value) { return Value && Value->Slot == EquipmentSlot; }))
			EquipmentOrder.Add(*Entry);
	UpdateList(EquipmentList, EquipmentOrder);
	if (BagSummary)
		BagSummary->SetText(FText::Format(NSLOCTEXT("ProjectJUI", "BagSummary", "{0} / {1} 항목 · {2}"),
										  FText::AsNumber(InventoryModel->VisibleEntries.Num()),
										  FText::AsNumber(InventoryModel->InventoryEntries.Num()),
										  BagFilter == 1   ? NSLOCTEXT("ProjectJUI", "FilterGear", "장비")
										  : BagFilter == 2 ? NSLOCTEXT("ProjectJUI", "FilterItems", "소모품")
														   : NSLOCTEXT("ProjectJUI", "FilterAll", "전체")));
	for (auto *List : {InventoryList.Get(), EquipmentList.Get()})
		if (List)
			for (auto *Row : List->GetDisplayedEntryWidgets())
				if (auto *Item = Cast<UProject_JItemWidget>(Row))
					Item->Render();
	if (StatusLabel)
		StatusLabel->SetText(InventoryModel->Status.IsEmpty() && InventoryModel->InventoryEntries.IsEmpty()
								 ? NSLOCTEXT("ProjectJUI", "EmptyBag", "가방에 아이템이 없습니다")
								 : InventoryModel->Status);
	if (NotificationLabel && !InventoryModel->Status.EqualTo(LastInventoryStatus))
	{
		LastInventoryStatus = InventoryModel->Status;
		NotificationLabel->SetText(LastInventoryStatus);
		NotificationLabel->SetVisibility(LastInventoryStatus.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		if (GetWorld()) GetWorld()->GetTimerManager().SetTimer(NotificationTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{ if (NotificationLabel) NotificationLabel->SetVisibility(ESlateVisibility::Collapsed); }), 4.f, false);
	}
}
FReply UProject_JPlayerHUDWidget::NativeOnPreviewKeyDown(const FGeometry &Geometry, const FKeyEvent &Event)
{
	if (CapturingKey != INDEX_NONE)
	{
		FText Reason;
		if (Event.GetKey() == EKeys::Escape) { CapturingKey = INDEX_NONE; Reason = NSLOCTEXT("ProjectJUI", "KeyCancelled", "키 변경 취소"); }
		else if (!Event.IsRepeat() && !Event.IsAltDown() && !Event.IsControlDown() && !Event.IsShiftDown() && ScreenOwner.IsValid() && ScreenOwner->RebindKey(CapturingKey, Event.GetKey(), Reason))
			CapturingKey = INDEX_NONE;
		else if (Reason.IsEmpty()) Reason = NSLOCTEXT("ProjectJUI", "KeySingle", "조합키 없이 키 하나를 누르세요. Escape로 취소할 수 있습니다.");
		RefreshKeySettings();
		if (KeysStatus) KeysStatus->SetText(Reason);
		return FReply::Handled();
	}
	if (Event.GetKey() == EKeys::Escape && UWidgetBlueprintLibrary::IsDragDropping())
	{
		UWidgetBlueprintLibrary::CancelDragDrop();
		return FReply::Handled();
	}
	// EditableText handles Escape before bubbling, so first return focus to the HUD.
	if (Event.GetKey() == EKeys::Escape && SearchBox &&
		(SearchBox->HasKeyboardFocus() || SearchBox->HasFocusedDescendants()))
		return FReply::Handled().SetUserFocus(TakeWidget(), EFocusCause::SetDirectly);
	const bool bTextFocus = (SearchBox && SearchBox->HasFocusedDescendants()) ||
		(SplitCount && (SplitCount->HasKeyboardFocus() || SplitCount->HasFocusedDescendants()));
	if (!WindowStack.IsEmpty() && WindowStack.Last() == TEXT("Split"))
	{
		if (Event.GetKey() == EKeys::Escape) { CloseWindow(TEXT("Split")); return FReply::Handled(); }
		if (Event.GetKey() == EKeys::Enter && bTextFocus)
		{
			if (!Event.IsRepeat()) ExecuteCommand(TEXT("SplitConfirm"));
			return FReply::Handled();
		}
		return Super::NativeOnPreviewKeyDown(Geometry, Event);
	}
	if (!bTextFocus && !WindowStack.IsEmpty())
	{
		const FName Top = WindowStack.Last();
		UListView *List = Top == TEXT("Bag") ? InventoryList.Get() : Top == TEXT("Equipment") ? EquipmentList.Get() : nullptr;
		const bool bListFocus = List && (List->HasUserFocus(GetOwningPlayer()) || List->HasFocusedDescendants() || HasUserFocus(GetOwningPlayer()));
		if (Top == TEXT("Bag") && Event.IsControlDown() && Event.GetKey() == EKeys::F && SearchBox)
		{
			SearchBox->SetUserFocus(GetOwningPlayer()); return FReply::Handled();
		}
		if (bListFocus && Event.GetKey() == EKeys::Enter && !Event.IsAltDown() && !Event.IsControlDown())
		{
			if (!Event.IsRepeat())
			{
				auto *Entry = Cast<UProject_JInventoryEntry>(List->GetSelectedItem());
				if (Event.IsShiftDown() && Top == TEXT("Bag")) OpenSplit(Entry);
				else ActivateItem(Entry);
			}
			return FReply::Handled();
		}
		if (Top == TEXT("Bag") && bListFocus && Event.IsControlDown() && Event.GetKey() == EKeys::M)
		{
			if (!Event.IsRepeat()) ExecuteCommand(TEXT("MergeSelected"));
			return FReply::Handled();
		}
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Event);
}
FReply UProject_JPlayerHUDWidget::NativeOnKeyDown(const FGeometry &Geometry, const FKeyEvent &Event)
{
	if (Event.GetKey() == EKeys::Escape)
	{
		if (SearchBox && SearchBox->HasKeyboardFocus())
		{
			SetKeyboardFocus();
			return FReply::Handled();
		}
		if (!WindowStack.IsEmpty())
			CloseWindow(WindowStack.Last());
		else
			CloseMenu();
		return FReply::Handled();
	}
	if ((SearchBox && SearchBox->HasKeyboardFocus()) || (SplitCount && SplitCount->HasFocusedDescendants()))
		return Super::NativeOnKeyDown(Geometry, Event);
	if (ScreenOwner.IsValid() && ScreenOwner->HandleMenuKey(Event.GetKey())) return FReply::Handled();
	return Super::NativeOnKeyDown(Geometry, Event);
}
FReply UProject_JPlayerHUDWidget::NativeOnMouseButtonDown(const FGeometry &Geometry, const FPointerEvent &Event)
{
	if (MenuPanel && MenuTitle && Event.GetEffectingButton() == EKeys::LeftMouseButton &&
		MenuTitle->GetCachedGeometry().IsUnderLocation(Event.GetScreenSpacePosition()))
	{
		WindowDragOffset = MenuPanel->GetCachedGeometry().AbsoluteToLocal(Event.GetScreenSpacePosition());
		return UWidgetBlueprintLibrary::DetectDragIfPressed(Event, this, EKeys::LeftMouseButton).NativeReply;
	}
	return Super::NativeOnMouseButtonDown(Geometry, Event);
}
void UProject_JPlayerHUDWidget::NativeOnDragDetected(const FGeometry &, const FPointerEvent &,
													 UDragDropOperation *&Operation)
{
	Operation = NewObject<UDragDropOperation>();
	Operation->Payload = this;
}
bool UProject_JPlayerHUDWidget::NativeOnDrop(const FGeometry &Geometry, const FDragDropEvent &Event,
											 UDragDropOperation *Operation)
{
	if (auto *Window = Operation ? Cast<UProject_JHUDWindow>(Operation->Payload) : nullptr;
		Window && Windows.FindRef(Window->GetWindowId()) == Window)
		return Window->FinishMove(Event);
	if (Operation && Operation->Payload == this && MenuPanel && ScreenCanvas)
	{
		if (auto *PanelSlot = Cast<UCanvasPanelSlot>(MenuPanel->Slot))
		{
			const FVector2D Extent = ScreenCanvas->GetCachedGeometry().GetLocalSize();
			const FVector2D Position =
				ScreenCanvas->GetCachedGeometry().AbsoluteToLocal(Event.GetScreenSpacePosition()) - WindowDragOffset;
			const FVector2D Available = Extent - PanelSlot->GetSize();
			NormalizedMenuPosition = FVector2D(Available.X > 0 ? FMath::Clamp(Position.X / Available.X, 0., 1.) : 0.,
											   Available.Y > 0 ? FMath::Clamp(Position.Y / Available.Y, 0., 1.) : 0.);
			ApplyMenuLayout();
			StoreMenuLayout();
			return true;
		}
	}
	if (auto *Drag = Cast<UProject_JItemDragOperation>(Operation);
		Drag && InventoryModel && InventoryList &&
		InventoryList->GetCachedGeometry().IsUnderLocation(Event.GetScreenSpacePosition()) &&
		InventoryModel->CanDrop(Drag->Entry, EProject_JEquipmentSlot::None, true))
		return InventoryModel->Unequip(Drag->Entry->Item.InstanceId, Drag->Entry->Slot);
	return false;
}

void UProject_JPlayerHUDWidget::UpdateQuickSlots(const TArray<FProject_JQuickSlotState> &States)
{
	if (!ActionBar)
		return;
	if (SkillButtons.Num() != States.Num())
	{
		ActionBar->ClearChildren();
		SkillButtons.Reset();
		for (int32 Index = 0; Index < States.Num(); ++Index)
		{
			auto *Button = CreateWidget<UProject_JSkillButton>(
				GetOwningPlayer(), SkillWidgetClass ? SkillWidgetClass.Get() : UProject_JSkillButton::StaticClass());
			Button->Configure(ScreenOwner.Get(), Index);
			ActionBar->AddChildToHorizontalBox(Button)->SetPadding(FMargin(2, 0));
			SkillButtons.Add(Button);
		}
	}
	for (int32 Index = 0; Index < States.Num(); ++Index)
		SkillButtons[Index]->Present(States[Index]);
}
void UProject_JPlayerHUDWidget::UpdateExperience(int64 Current, int64 Required)
{
	if (ExperienceBar)
		ExperienceBar->SetPercent(Required > 0 ? static_cast<float>(static_cast<double>(Current) / Required) : 1.f);
	if (ExperienceLabel)
		ExperienceLabel->SetText(Required > 0
									 ? FText::Format(NSLOCTEXT("ProjectJUI", "Experience", "EXP {0}%"),
													 FText::AsNumber(FMath::FloorToInt(100. * Current / Required)))
									 : NSLOCTEXT("ProjectJUI", "LevelCap", "MAX"));
}
void UProject_JPlayerHUDWidget::ToggleWindow(FName Id)
{
	UProject_JHUDWindow *Window = Windows.FindRef(Id);
	if (!Window)
	{
		if (ScreenOwner.IsValid())
			ScreenOwner->SetMenuOpen(!ScreenOwner->IsMenuOpen());
		return;
	}
	if (WindowStack.Contains(Id))
	{
		CloseWindow(Id);
		return;
	}
	Window->SetVisibility(ESlateVisibility::Visible);
	RaiseWindow(Id);
	if (ScreenOwner.IsValid())
		ScreenOwner->SetMenuOpen(true);
	ApplyMenuLayout();
	RefreshQuests();
	if (Id == TEXT("Crafting") && CraftingJobsView)
	{
		CraftingClockElapsed = 0.f;
		CraftingJobsView->RefreshClock();
	}
	FocusTopWindow();
}
void UProject_JPlayerHUDWidget::CloseWindow(FName Id)
{
	if (Id == TEXT("Bag")) MergeSource = nullptr;
	if (Id == TEXT("Keys")) CapturingKey = INDEX_NONE;
	if (Id == TEXT("Split")) SplitSource = nullptr;
	if (UProject_JHUDWindow *Window = Windows.FindRef(Id))
		Window->SetVisibility(ESlateVisibility::Collapsed);
	WindowStack.Remove(Id);
	if (WindowStack.IsEmpty())
	{
		if (ScreenOwner.IsValid())
			ScreenOwner->SetMenuOpen(false);
	}
	else
		FocusTopWindow();
	UpdateModalInteractivity();
}
void UProject_JPlayerHUDWidget::RaiseWindow(FName Id)
{
	if (!Windows.Contains(Id))
		return;
	WindowStack.Remove(Id);
	WindowStack.Add(Id);
	for (int32 Index = 0; Index < WindowStack.Num(); ++Index)
		if (UProject_JHUDWindow *Window = Windows.FindRef(WindowStack[Index]))
			if (auto *WindowCanvasSlot = Cast<UCanvasPanelSlot>(Window->Slot))
				WindowCanvasSlot->SetZOrder(100 + Index);
	UpdateModalInteractivity();
}
void UProject_JPlayerHUDWidget::UpdateModalInteractivity()
{
	const bool bSplit = !WindowStack.IsEmpty() && WindowStack.Last() == TEXT("Split");
	for (const auto &Pair : Windows) Pair.Value->SetIsEnabled(!bSplit || Pair.Key == TEXT("Split"));
	if (MenuButtons) MenuButtons->SetIsEnabled(!bSplit);
}
void UProject_JPlayerHUDWidget::FocusTopWindow()
{
	if (!GetOwningPlayer()) return;
	UpdateModalInteractivity();
	const FName Top = WindowStack.IsEmpty() ? NAME_None : WindowStack.Last();
	if (Top == TEXT("Split") && SplitCount) { SplitCount->SetUserFocus(GetOwningPlayer()); return; }
	UListView *List = Top == TEXT("Bag") ? InventoryList.Get() : Top == TEXT("Equipment") ? EquipmentList.Get() : nullptr;
	if (List)
	{
		if (!List->GetSelectedItem() && List->GetNumItems() > 0) List->SetSelectedIndex(0);
		List->SetUserFocus(GetOwningPlayer());
	}
	else SetUserFocus(GetOwningPlayer());
}
void UProject_JPlayerHUDWidget::ApplyPreferences(const FProject_JHUDPreferences &Value)
{
	Preferences = Value;
	const float Extent = ScreenCanvas ? ScreenCanvas->GetCachedGeometry().GetLocalSize().X : 0;
	const float SlotSize = HUDStyle ? FMath::Clamp(HUDStyle->QuickSlotSize, 32.f, 64.f) : 46.f;
	const float Scale =
		Extent > 0 ? FMath::Min(Value.Scale, FMath::Max(0.5f, (Extent - 24) / (10 * (SlotSize + 4)))) : Value.Scale;
	if (SettingsSummary)
		SettingsSummary->SetText(FText::Format(NSLOCTEXT("ProjectJUI", "SettingsState", "퀵슬롯: {0} · HUD: {1}%"),
											   Value.bLocked ? NSLOCTEXT("ProjectJUI", "QuickLocked", "잠금")
															 : NSLOCTEXT("ProjectJUI", "QuickUnlocked", "편집"),
											   FText::AsNumber(FMath::RoundToInt(Value.Scale * 100))));
	if (ActionBar)
	{
		ActionBar->SetRenderTransformPivot(FVector2D(0.5, 1));
		ActionBar->SetRenderScale(FVector2D(Scale, Scale));
	}
	if (ResourcesBox)
	{
		ResourcesBox->SetRenderTransformPivot(FVector2D(0, 0));
		ResourcesBox->SetRenderScale(FVector2D(Scale, Scale));
	}
	if (AttributeLabel)
	{
		AttributeLabel->SetRenderTransformPivot(FVector2D(0, 0));
		AttributeLabel->SetRenderScale(FVector2D(Scale, Scale));
	}
	if (ExperienceLabel && ExperienceLabel->GetParent())
		if (auto *XPSlot = Cast<UCanvasPanelSlot>(ExperienceLabel->GetParent()->Slot))
			XPSlot->SetPosition(FVector2D(
				16, 38 + FMath::Max(9, ResourceLabels.Num() * (Value.bShowResourceValues ? 24 : 9)) * Scale + 6));
	if (ExperienceLabel)
		ExperienceLabel->SetVisibility(Value.bShowResourceValues ? ESlateVisibility::HitTestInvisible
																 : ESlateVisibility::Collapsed);
	if (StatusEffectBox) if (auto *StatusSlot = Cast<UCanvasPanelSlot>(StatusEffectBox->Slot))
		StatusSlot->SetPosition(FVector2D(16, 38 + FMath::Max(9, ResourceLabels.Num() * (Value.bShowResourceValues ? 24 : 9)) * Scale + 42));
}
void UProject_JPlayerHUDWidget::SearchChanged(const FText &)
{
	if (GetWorld())
		GetWorld()->GetTimerManager().SetTimer(SearchTimer, this, &ThisClass::ApplySearch, 0.15f, false);
}
void UProject_JPlayerHUDWidget::ApplySearch()
{
	if (InventoryModel)
		InventoryModel->SetFilter(SearchBox ? SearchBox->GetText().ToString() : FString(), BagFilter, bSortBag);
}
void UProject_JPlayerHUDWidget::ExecuteCommand(FName Command)
{
	const FString Value = Command.ToString();
	if (Command == TEXT("SplitSelected")) { OpenSplit(InventoryList ? Cast<UProject_JInventoryEntry>(InventoryList->GetSelectedItem()) : nullptr); return; }
	if (Command == TEXT("CancelMerge")) { MergeSource = nullptr; if (InventoryModel) { InventoryModel->SetLocalStatus(FText::GetEmpty()); } return; }
	if (Command == TEXT("MergeSelected"))
	{
		auto *Selected = InventoryList ? Cast<UProject_JInventoryEntry>(InventoryList->GetSelectedItem()) : nullptr;
		if (!InventoryModel || !Selected || !Selected->Item.IsValid() || Selected->Item.bIsLocked || InventoryModel->bPending) return;
		if (MergeSource)
		{
			if (MergeSource->Item.InstanceId == Selected->Item.InstanceId) return;
			if (InventoryModel->DropInBag(MergeSource, Selected, true)) MergeSource = nullptr;
		}
		else
		{
			MergeSource = DuplicateObject<UProject_JInventoryEntry>(Selected, this);
			InventoryModel->SetLocalStatus(NSLOCTEXT("ProjectJUI", "MergeChooseTarget", "합칠 대상 스택을 선택한 뒤 합치기를 다시 누르세요."));
		}
		return;
	}
	if (Value.StartsWith(TEXT("Rebind.")))
	{
		const int32 Index = FCString::Atoi(*Value.Mid(7));
		if (ScreenOwner.IsValid() && ScreenOwner->GetInputKeys().Keys.IsValidIndex(Index))
		{
			CapturingKey = Index;
			if (KeysStatus) KeysStatus->SetText(NSLOCTEXT("ProjectJUI", "KeyCapture", "새 키를 누르세요 · Escape 취소"));
			SetKeyboardFocus();
		}
		return;
	}
	if (Command == TEXT("RetrySave")) { if (LayoutSettings.IsValid()) LayoutSettings->FlushNow(); return; }
	if (Command == TEXT("ResetKeys")) { CapturingKey = INDEX_NONE; if (ScreenOwner.IsValid()) ScreenOwner->ResetKeys(); RefreshKeySettings(); return; }
	if (Command == TEXT("SplitConfirm"))
	{
		if (InventoryModel && SplitSource && SplitCount && InventoryModel->Split(SplitSource, FMath::RoundToInt(SplitCount->GetValue()))) CloseWindow(TEXT("Split"));
		return;
	}
	if (Value.StartsWith(TEXT("Close.")))
	{
		CloseWindow(FName(*Value.Mid(6)));
		return;
	}
	if (Command == TEXT("Bag") || Command == TEXT("Equipment") || Command == TEXT("Quests") ||
		Command == TEXT("Skills") || Command == TEXT("Settings") || Command == TEXT("Keys") || Command == TEXT("Crafting"))
	{
		ToggleWindow(Command);
		return;
	}
	if (Command == TEXT("Both"))
	{
		if (!WindowStack.Contains(TEXT("Bag")))
			ToggleWindow(TEXT("Bag"));
		if (!WindowStack.Contains(TEXT("Equipment")))
			ToggleWindow(TEXT("Equipment"));
		return;
	}
	if (Command == TEXT("Sort"))
	{
		bSortBag = !bSortBag;
		ApplySearch();
		return;
	}
	if (Command == TEXT("Filter"))
	{
		BagFilter = (BagFilter + 1) % 3;
		ApplySearch();
		return;
	}
    if (Command == TEXT("QuestCurrent") || Command == TEXT("QuestHistory") || Command == TEXT("HistoryPrevious") ||
        Command == TEXT("HistoryNext") || Command == TEXT("HistoryRefresh"))
    {
        if (bQuestPresentationRetired || !ScreenOwner.IsValid() || !WindowStack.Contains(TEXT("Quests"))) return;
        const TStrongObjectPtr<UProject_JQuestComponent> Quests(ScreenOwner->GetQuestComponent());
        if (!Quests.IsValid() || QuestRowSource.Get() != Quests.Get()) return;
        if (Command == TEXT("QuestCurrent")) { bShowingQuestHistory = false; RefreshQuests(); return; }
        if (Command == TEXT("QuestHistory"))
        {
            const bool bEnter = !bShowingQuestHistory;
            bShowingQuestHistory = true;
            if (bEnter) RequestQuestHistoryPage(0); else RefreshQuests();
            return;
        }
        if (!bShowingQuestHistory) return;
        if (Command == TEXT("HistoryRefresh")) { RequestQuestHistoryPage(0); return; }
        const auto Summary = Quests->GetHistorySummary();
        const auto Page = Quests->GetHistoryPage();
        if (!Page.bHasPage || !Summary.Revision.IsValid() || Page.Summary.Revision != Summary.Revision || Page.Summary.Count != Summary.Count) return;
        const int32 Ordinal = Page.Ordinal + (Command == TEXT("HistoryPrevious") ? -1 : 1);
        if (Ordinal >= 0 && static_cast<int64>(Ordinal) * 32 < Summary.Count) RequestQuestHistoryPage(Ordinal);
        return;
    }
	if (Command == TEXT("CollapseQuests"))
	{
		bQuestsCollapsed = !bQuestsCollapsed;
		RefreshQuests();
		return;
	}
	if (Value.StartsWith(TEXT("Claim.")))
	{
		if (ScreenOwner.IsValid())
			if (auto *Quests = ScreenOwner->GetQuestComponent())
				Quests->RequestClaim(FName(*Value.Mid(6)));
		return;
	}
	if (Value.StartsWith(TEXT("Track.")))
	{
		if (ScreenOwner.IsValid())
			if (auto *Quests = ScreenOwner->GetQuestComponent())
				if (const auto *State = Quests->States.FindByPredicate([&](const auto &S)
																	   { return S.QuestId == FName(*Value.Mid(6)); }))
					Quests->RequestTracking(State->QuestId, !State->bTracked);
		return;
	}
	if (ScreenOwner.IsValid())
		ScreenOwner->ChangePreferences(Command);
}
void UProject_JPlayerHUDWidget::RetireQuestRow(const FProject_JQuestJournalRow& Row)
{
    const TStrongObjectPtr<UProject_JUICommandButton> Action(Row.Action);
    const TStrongObjectPtr<UVerticalBox> Column(Row.Column);
    // Clear the actual command target before disabling a focused button can run focus callbacks.
    if (IsValid(Action.Get()))
    {
        Action->InitializeCommand(nullptr, NAME_None, FText::GetEmpty());
        Action->SetIsEnabled(false);
    }
    if (IsValid(Column.Get())) Column->RemoveFromParent();
}

void UProject_JPlayerHUDWidget::ClearQuestPresentation(const UProject_JPlayerUIComponent* ExpectedOwner)
{
    // A previous controller can retain this screen after public InitializeScreen hands it to another live owner.
    // An expired owner does not prevent cleanup of its orphaned rows.
    if (ExpectedOwner && ScreenOwner.IsValid() && ScreenOwner.Get() != ExpectedOwner) return;
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    if (QuestPresentationRevision == MAX_uint64) bQuestPresentationRetired = true;
    else ++QuestPresentationRevision;
    const uint64 Revision = QuestPresentationRevision;
    const TStrongObjectPtr<UProject_JQuestComponent> PreviousSource(QuestRowSource.Get());
    const FGuid PreviousRequest = QuestHistoryRequest;
    if (QuestHistorySourceRevision == MAX_uint64) bQuestPresentationRetired = true;
    else ++QuestHistorySourceRevision;
    auto PreviousButtons = MoveTemp(QuestHistoryButtons);
    auto PreviousLabels = MoveTemp(QuestHistoryRows);
    QuestHistoryButtons.Reset(); QuestHistoryRows.Reset(); QuestHistoryRequest.Invalidate(); bShowingQuestHistory = false;
    auto Previous = MoveTemp(QuestJournalRows);
    QuestJournalRows.Reset(); QuestRowSource.Reset();
    // A focus callback may collect garbage after the map was detached. Pin every old widget first.
    TArray<TStrongObjectPtr<UWidget>> Pins;
    for (const auto& Pair : Previous)
    {
        if (IsValid(Pair.Value.Column)) Pins.Emplace(Pair.Value.Column.Get());
        if (IsValid(Pair.Value.Label)) Pins.Emplace(Pair.Value.Label.Get());
        if (IsValid(Pair.Value.Action)) Pins.Emplace(Pair.Value.Action.Get());
    }
    for (const auto& Button : PreviousButtons) if (IsValid(Button)) Pins.Emplace(Button.Get());
    for (const auto& Label : PreviousLabels) if (IsValid(Label)) Pins.Emplace(Label.Get());
    if (PreviousSource.IsValid() && PreviousRequest.IsValid()) PreviousSource->CancelHistoryPageRequest(PreviousRequest);
    for (const auto& Button : PreviousButtons) if (IsValid(Button))
    {
        Button->InitializeCommand(nullptr, NAME_None, FText::GetEmpty());
        Button->SetIsEnabled(false); Button->RemoveFromParent();
    }
    for (const auto& Label : PreviousLabels) if (IsValid(Label)) Label->RemoveFromParent();
    for (const auto& Pair : Previous) RetireQuestRow(Pair.Value);
    if (QuestPresentationRevision != Revision) return; // A newer source/refresh owns its output.
    if (QuestHistoryStatus) QuestHistoryStatus->SetText(FText::GetEmpty());
    if (QuestPresentationRevision != Revision) return;
    if (QuestHistoryList) QuestHistoryList->SetVisibility(ESlateVisibility::Collapsed);
    if (QuestPresentationRevision != Revision) return;
    if (QuestJournal) QuestJournal->SetVisibility(ESlateVisibility::Visible);
    if (QuestPresentationRevision != Revision) return;
    if (QuestTracker) QuestTracker->SetText(FText::GetEmpty());
    if (QuestPresentationRevision != Revision) return;
    if (Minimap) Minimap->SetQuestMarkers({});
}

void UProject_JPlayerHUDWidget::RefreshQuests()
{
    if (bQuestPresentationRetired) return;
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<UProject_JPlayerUIComponent> Owner(ScreenOwner.Get());
    const TStrongObjectPtr<UProject_JQuestComponent> Quests(Owner.IsValid() ? Owner->GetQuestComponent() : nullptr);
    if (!Owner.IsValid() || !IsValid(Quests.Get()) || Quests->IsBeingDestroyed()) { ClearQuestPresentation(); return; }
    if (QuestRowSource.Get() != Quests.Get())
    {
        const uint64 Before = QuestPresentationRevision;
        ClearQuestPresentation();
        if (Before == MAX_uint64 || QuestPresentationRevision != Before + 1 || ScreenOwner.Get() != Owner.Get()
            || Owner->GetQuestComponent() != Quests.Get()) return;
        QuestRowSource = Quests.Get();
    }
    if (QuestPresentationRevision == MAX_uint64) { ClearQuestPresentation(); return; }
    const uint64 Revision = ++QuestPresentationRevision;
    const auto IsCurrent = [&]
    {
        return !bQuestPresentationRetired && QuestPresentationRevision == Revision && ScreenOwner.Get() == Owner.Get()
            && IsValid(Owner.Get()) && !Owner->IsBeingDestroyed() && Owner->GetQuestComponent() == Quests.Get()
            && IsValid(Quests.Get()) && !Quests->IsBeingDestroyed() && QuestRowSource.Get() == Quests.Get();
    };
    // The authoritative quest-state contract is bounded128; never retain an ever-seen quest history in the view.
    const auto States = Quests->States;
    if (States.Num() > 128) { ClearQuestPresentation(); return; }
    TSet<FName> Desired;
    for (const auto& State : States)
    {
        if (State.bClaimed) continue; // Legacy claimed details belong to paged history, not current rows.
        if (State.QuestId.IsNone() || Desired.Contains(State.QuestId)) { ClearQuestPresentation(); return; }
        Desired.Add(State.QuestId);
    }
    TArray<FProject_JQuestJournalRow> Removed;
    for (auto It = QuestJournalRows.CreateIterator(); It; ++It)
        if (!Desired.Contains(It.Key())) { Removed.Add(It.Value()); It.RemoveCurrent(); }
    TArray<TStrongObjectPtr<UWidget>> RemovedPins;
    for (const auto& Row : Removed)
    {
        if (IsValid(Row.Column)) RemovedPins.Emplace(Row.Column.Get());
        if (IsValid(Row.Label)) RemovedPins.Emplace(Row.Label.Get());
        if (IsValid(Row.Action)) RemovedPins.Emplace(Row.Action.Get());
    }
    for (const auto& Row : Removed) { RetireQuestRow(Row); if (!IsCurrent()) return; }
    TArray<TObjectPtr<UVerticalBox>> OrderedColumns;
	TArray<FText> Tracked;
	TArray<FProject_JMapMarker> Markers;
	const bool bJournalVisible = WindowStack.Contains(TEXT("Quests")) && !bShowingQuestHistory;
	const float OldScroll = QuestJournal ? QuestJournal->GetScrollOffset() : 0;
	if (Quests.IsValid())
		for (const auto &State : States)
		{
            if (State.bClaimed) continue;
            const TStrongObjectPtr<UProject_JQuestDefinition> Def(Quests->FindDefinition(State.QuestId));
            if (!IsCurrent()) return;
            if (!Def.IsValid())
            {
                if (const auto* Existing = QuestJournalRows.Find(State.QuestId))
                {
                    const auto Row = *Existing; QuestJournalRows.Remove(State.QuestId); RetireQuestRow(Row);
                    if (!IsCurrent()) return;
                }
                continue;
            }
			const bool bComplete = Def->IsComplete(State);
			FText Progress, JournalProgress;
			if (Def->Objectives.IsEmpty())
			{
				Progress = FText::Format(NSLOCTEXT("ProjectJUI", "QuestProgress", "{0} · {1}/{2}"), Def->Title,
					FText::AsNumber(State.Count), FText::AsNumber(Def->RequiredCount));
				JournalProgress = Progress;
			}
			else
			{
				int32 CompleteObjectives = 0;
				TArray<FText> Lines{Def->Title};
				for (const auto& O : Def->Objectives)
				{
					const int32 Count = State.GetObjectiveCount(O.ObjectiveId);
					CompleteObjectives += Count == O.RequiredCount;
					Lines.Add(FText::Format(NSLOCTEXT("ProjectJUI", "QuestObjectiveProgress", "{0} · {1}/{2}"),
						O.Title.IsEmpty() ? FText::FromName(O.ObjectiveId) : O.Title, FText::AsNumber(Count), FText::AsNumber(O.RequiredCount)));
				}
				Progress = FText::Format(NSLOCTEXT("ProjectJUI", "CompoundQuestProgress", "{0} · {1}/{2} 목표"),
					Def->Title, FText::AsNumber(CompleteObjectives), FText::AsNumber(Def->Objectives.Num()));
				JournalProgress = FText::Join(FText::FromString(TEXT("\n")), Lines);
			}
			if (State.bTracked && !State.bClaimed)
			{
				if (Tracked.Num() < 3)
					Tracked.Add(Progress);
				if (Def->bHasMapTarget)
				{
					FProject_JMapMarker Marker;
					Marker.Position = Def->MapTarget;
					Marker.Label = Def->Title;
					Markers.Add(Marker);
				}
			}
			if (bJournalVisible && QuestJournal)
			{
                if (!WidgetTree) return; // Preserve a skin without native journal controls.
                FProject_JQuestJournalRow Row = QuestJournalRows.FindRef(State.QuestId);
                if (!IsValid(Row.Column) || !IsValid(Row.Label) || !IsValid(Row.Action))
                {
                    RetireQuestRow(Row);
                    if (!IsCurrent()) return;
                    Row.Column = WidgetTree->ConstructWidget<UVerticalBox>();
                    Row.Label = WidgetTree->ConstructWidget<UTextBlock>();
                    auto Font = Row.Label->GetFont(); Font.Size = 12; Row.Label->SetFont(Font);
                    Row.Label->SetAutoWrapText(true);
                    Row.Column->AddChildToVerticalBox(Row.Label)->SetPadding(FMargin(0, 0, 0, 6));
                    Row.Action = WidgetTree->ConstructWidget<UProject_JUICommandButton>();
                    Row.Column->AddChildToVerticalBox(Row.Action)->SetPadding(FMargin(0, 0, 0, 14));
                    QuestJournalRows.Add(State.QuestId, Row);
                }
                const TStrongObjectPtr<UVerticalBox> Column(Row.Column);
                const TStrongObjectPtr<UTextBlock> Label(Row.Label);
                const TStrongObjectPtr<UProject_JUICommandButton> Action(Row.Action);
                Label->SetText(JournalProgress); Label->SetToolTipText(Def->Description);
                if (!IsCurrent()) return;
                Action->InitializeCommand(
					this, FName(*((bComplete ? TEXT("Claim.") : TEXT("Track.")) + State.QuestId.ToString())),
					State.bClaimed	 ? NSLOCTEXT("ProjectJUI", "Claimed", "완료")
					: bComplete		 ? NSLOCTEXT("ProjectJUI", "ClaimReward", "보상 받기")
					: State.bTracked ? NSLOCTEXT("ProjectJUI", "Untrack", "추적 해제")
									 : NSLOCTEXT("ProjectJUI", "Track", "추적"));
                if (!IsCurrent()) return;
                Action->SetIsEnabled(!State.bClaimed);
                if (!IsCurrent()) return;
                OrderedColumns.Add(Column.Get());
			}
		}
    if (!IsCurrent()) return;
    if (bJournalVisible && QuestJournal)
    {
        bool SameOrder = QuestJournal->GetChildrenCount() == OrderedColumns.Num();
        for (int32 Index = 0; SameOrder && Index < OrderedColumns.Num(); ++Index)
            SameOrder = QuestJournal->GetChildAt(Index) == OrderedColumns[Index];
        if (!SameOrder)
        {
            QuestJournal->ClearChildren();
            if (!IsCurrent()) return;
            for (const auto& Column : OrderedColumns)
            {
                QuestJournal->AddChild(Column.Get());
                if (!IsCurrent()) return;
            }
        }
    }
	if (QuestTracker)
	{
		QuestTracker->SetText(FText::Join(FText::FromString(TEXT("\n")), Tracked));
		QuestTracker->SetVisibility(bQuestsCollapsed ? ESlateVisibility::Collapsed
													 : ESlateVisibility::HitTestInvisible);
	}
    if (!IsCurrent()) return;
	if (Minimap)
		Minimap->SetQuestMarkers(MoveTemp(Markers));
    if (!IsCurrent()) return;
	if (bJournalVisible && QuestJournal)
		QuestJournal->SetScrollOffset(OldScroll);
    if (IsCurrent()) RefreshQuestHistory();
}


void UProject_JPlayerHUDWidget::RequestQuestHistoryPage(int32 Ordinal)
{
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<UProject_JPlayerUIComponent> Owner(ScreenOwner.Get());
    const TStrongObjectPtr<UProject_JQuestComponent> Quests(Owner.IsValid() ? Owner->GetQuestComponent() : nullptr);
    const uint64 SourceRevision = QuestHistorySourceRevision;
    if (bQuestPresentationRetired || !bShowingQuestHistory || !Owner.IsValid() || !Quests.IsValid() || QuestRowSource.Get() != Quests.Get()) return;
    const FGuid Request = Quests->RequestCompletedPage(Ordinal);
    if (bQuestPresentationRetired || QuestHistorySourceRevision != SourceRevision || ScreenOwner.Get() != Owner.Get() ||
        Owner->GetQuestComponent() != Quests.Get() || QuestRowSource.Get() != Quests.Get())
    {
        if (Request.IsValid()) Quests->CancelHistoryPageRequest(Request);
        return;
    }
    // A nested request may supersede this one while local reply/UI callbacks run.
    if (Request.IsValid() && Quests->GetHistoryPage().Request == Request) QuestHistoryRequest = Request;
    RefreshQuests();
}

void UProject_JPlayerHUDWidget::RefreshQuestHistory()
{
    if (!QuestHistoryCommands || !QuestHistoryList || !QuestHistoryStatus || !WidgetTree) return; // Optional skin controls.
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<UProject_JPlayerUIComponent> Owner(ScreenOwner.Get());
    const TStrongObjectPtr<UProject_JQuestComponent> Quests(Owner.IsValid() ? Owner->GetQuestComponent() : nullptr);
    const uint64 Revision = QuestPresentationRevision;
    const auto IsCurrent = [&]
    {
        return !bQuestPresentationRetired && QuestPresentationRevision == Revision && ScreenOwner.Get() == Owner.Get() &&
            Owner.IsValid() && !Owner->IsBeingDestroyed() && Quests.IsValid() && !Quests->IsBeingDestroyed() &&
            Owner->GetQuestComponent() == Quests.Get() && QuestRowSource.Get() == Quests.Get();
    };
    if (!IsCurrent()) return;
    if (QuestHistoryButtons.IsEmpty())
    {
        const FName Commands[] = {TEXT("QuestCurrent"), TEXT("QuestHistory"), TEXT("HistoryPrevious"), TEXT("HistoryNext"), TEXT("HistoryRefresh")};
        const FText Labels[] = {NSLOCTEXT("ProjectJUI", "QuestCurrent", "진행"), NSLOCTEXT("ProjectJUI", "QuestHistory", "완료"),
            NSLOCTEXT("ProjectJUI", "HistoryPrevious", "이전"), NSLOCTEXT("ProjectJUI", "HistoryNext", "다음"), NSLOCTEXT("ProjectJUI", "HistoryRefresh", "새로고침")};
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Commands); ++Index)
        {
            auto* Button = WidgetTree->ConstructWidget<UProject_JUICommandButton>();
            QuestHistoryButtons.Add(Button);
            Button->InitializeCommand(this, Commands[Index], Labels[Index]);
            if (!IsCurrent()) return;
            QuestHistoryCommands->AddChildToHorizontalBox(Button);
            if (!IsCurrent()) return;
        }
    }
    const auto Summary = Quests->GetHistorySummary();
    const auto Page = Quests->GetHistoryPage();
    if (Page.Ids.Num() > 32 || QuestHistoryRows.Num() > 32 || QuestHistoryButtons.Num() != 5) return;
    const bool bConfirmed = Page.bHasPage && Summary.Revision.IsValid() && Page.Summary.Revision == Summary.Revision && Page.Summary.Count == Summary.Count;
    FText State = NSLOCTEXT("ProjectJUI", "HistoryUnknown", "아직 조회하지 않았습니다.");
    if (Page.bHasPage) State = bConfirmed ? NSLOCTEXT("ProjectJUI", "HistoryConfirmed", "확인한 페이지") : NSLOCTEXT("ProjectJUI", "HistoryStale", "이전 확인 페이지 · 새로고침 필요");
    if (Page.Status == EProject_JQuestHistoryPageStatus::Pending) State = NSLOCTEXT("ProjectJUI", "HistoryRequested", "조회 요청됨 · 응답이 없으면 다시 조회하세요.");
    else if (Page.Status == EProject_JQuestHistoryPageStatus::Rejected || Page.Status == EProject_JQuestHistoryPageStatus::Unknown)
        State = NSLOCTEXT("ProjectJUI", "HistoryUnavailable", "조회가 확인되지 않았습니다 · 다시 조회할 수 있습니다.");
    const FText Count = Summary.Revision.IsValid() ? FText::AsNumber(Summary.Count) : NSLOCTEXT("ProjectJUI", "HistoryCountUnknown", "미확인");
    const FText PageNumber = Page.bHasPage ? FText::Format(NSLOCTEXT("ProjectJUI", "HistoryPageNumber", "{0}/{1}쪽"),
        FText::AsNumber(Page.Ordinal + 1), FText::AsNumber(FMath::Max(1, (Page.Summary.Count + 31) / 32))) : FText::GetEmpty();
    QuestHistoryStatus->SetText(FText::Format(NSLOCTEXT("ProjectJUI", "HistorySummary", "완료 {0} · {1} · {2}"), Count, PageNumber, State));
    if (!IsCurrent()) return;
    QuestHistoryButtons[0]->SetIsEnabled(bShowingQuestHistory);
    if (!IsCurrent()) return;
    QuestHistoryButtons[1]->SetIsEnabled(true);
    if (!IsCurrent()) return;
    QuestHistoryButtons[2]->SetIsEnabled(bShowingQuestHistory && bConfirmed && Page.Ordinal > 0);
    if (!IsCurrent()) return;
    QuestHistoryButtons[3]->SetIsEnabled(bShowingQuestHistory && bConfirmed && static_cast<int64>(Page.Ordinal + 1) * 32 < Summary.Count);
    if (!IsCurrent()) return;
    QuestHistoryButtons[4]->SetIsEnabled(bShowingQuestHistory); // Never disabled by Pending/silent admission rejection.
    if (!IsCurrent()) return;
    if (QuestJournal) QuestJournal->SetVisibility(bShowingQuestHistory ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
    if (!IsCurrent()) return;
    QuestHistoryList->SetVisibility(bShowingQuestHistory ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (!IsCurrent() || !bShowingQuestHistory) return;
    while (QuestHistoryRows.Num() < Page.Ids.Num())
    {
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        QuestHistoryRows.Add(Label); Label->SetAutoWrapText(true);
        QuestHistoryList->AddChild(Label);
        if (!IsCurrent()) return;
    }
    for (int32 Index = 0; Index < QuestHistoryRows.Num(); ++Index)
    {
        const TStrongObjectPtr<UTextBlock> Label(QuestHistoryRows[Index]);
        const FName Id = Page.Ids.IsValidIndex(Index) ? Page.Ids[Index] : NAME_None;
        const TStrongObjectPtr<UProject_JQuestDefinition> Definition(Id.IsNone() ? nullptr : Quests->FindDefinition(Id));
        if (!IsCurrent()) return;
        Label->SetText(Definition.IsValid() && !Definition->Title.IsEmpty() ? Definition->Title : (Id.IsNone() ? FText::GetEmpty() : FText::FromName(Id)));
        if (!IsCurrent()) return;
        Label->SetVisibility(Page.Ids.IsValidIndex(Index) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        if (!IsCurrent()) return;
    }
}

void UProject_JPlayerHUDWidget::RefreshKeySettings()
{
	if (!KeysScroll || !ScreenOwner.IsValid()) return;
	KeysScroll->ClearChildren();
	const auto &Keys = ScreenOwner->GetInputKeys().Keys;
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		const FText Action = Index < 10
			? FText::Format(NSLOCTEXT("ProjectJUI", "QuickKeyAction", "퀵슬롯 {0}"), FText::AsNumber(Index + 1))
			: Index == 10 ? NSLOCTEXT("ProjectJUI", "BagKeyAction", "가방")
			: Index == 11 ? NSLOCTEXT("ProjectJUI", "GearKeyAction", "장비")
			: Index == 12 ? NSLOCTEXT("ProjectJUI", "QuestKeyAction", "의뢰")
			: NSLOCTEXT("ProjectJUI", "SettingsKeyAction", "설정");
		auto *Button = WidgetTree->ConstructWidget<UProject_JUICommandButton>();
		Button->InitializeCommand(this, FName(*FString::Printf(TEXT("Rebind.%d"), Index)),
			FText::Format(NSLOCTEXT("ProjectJUI", "KeyRow", "{0} : {1}"), Action, Keys[Index].GetDisplayName()));
		KeysScroll->AddChild(Button);
	}
}
void UProject_JPlayerHUDWidget::OpenSplit(UProject_JInventoryEntry *Entry)
{
	if (!InventoryModel || !InventoryModel->IsCurrentEntry(Entry) || InventoryModel->bPending || !Entry->Item.IsValid() ||
		Entry->Item.bIsLocked || Entry->Item.bIsEquipped || Entry->Item.StackCount <= 1)
	{
		if (InventoryModel && !InventoryModel->bPending) { InventoryModel->SetLocalStatus(NSLOCTEXT("ProjectJUI", "CannotSplit", "분할할 잠기지 않은 스택을 선택하세요. 수량이 2개 이상이어야 합니다.")); }
		return;
	}
	SplitSource = DuplicateObject<UProject_JInventoryEntry>(Entry, this);
	if (SplitLabel) SplitLabel->SetText(FText::Format(NSLOCTEXT("ProjectJUI", "SplitDetails", "{0} · 보유 {1}"), ItemName(Entry), FText::AsNumber(Entry->Item.StackCount)));
	if (SplitCount)
	{
		SplitCount->SetMaxValue(Entry->Item.StackCount - 1);
		SplitCount->SetMinSliderValue(1);
		SplitCount->SetMaxSliderValue(Entry->Item.StackCount - 1);
		SplitCount->SetValue(FMath::Max(1, Entry->Item.StackCount / 2));
	}
	if (!WindowStack.Contains(TEXT("Split"))) ToggleWindow(TEXT("Split"));
	else RaiseWindow(TEXT("Split"));
}

void UProject_JPlayerHUDWidget::PresentStatusEffects(const TArray<FProject_JStatusEffectState> &States)
{
	if (!StatusEffectBox) return;
	TArray<FActiveGameplayEffectHandle> Handles;
	for (int32 I = 0; I < FMath::Min(12, States.Num()); ++I) Handles.Add(States[I].Handle);
	if (Handles != StatusHandles)
	{
		StatusEffectBox->ClearChildren();
		StatusIcons.Reset();
		StatusHandles = Handles;
		for (int32 I = 0; I < Handles.Num(); ++I)
		{
			auto *Icon = CreateWidget<UProject_JSkillButton>(GetOwningPlayer());
			Icon->TakeWidget();
			if (auto *Size = Cast<USizeBox>(Icon->GetRootWidget())) { Size->SetWidthOverride(32); Size->SetHeightOverride(32); }
			StatusEffectBox->AddChildToWrapBox(Icon)->SetPadding(FMargin(0, 0, 4, 4));
			StatusIcons.Add(Icon);
		}
		StatusEffectBox->AddChildToWrapBox(StatusOverflow);
	}
	for (int32 I = 0; I < StatusIcons.Num(); ++I)
	{
		const auto &Effect = States[I];
		FProject_JQuickSlotState State;
		State.Binding.Kind = EProject_JQuickSlotKind::Skill;
		State.Name = Effect.Name;
		State.Icon = Effect.Icon;
		State.KeyHint = FText::Format(NSLOCTEXT("ProjectJUI", "EffectKindAndStacks", "{0}{1}"), FText::FromString(Effect.bDebuff ? TEXT("−") : TEXT("+")), Effect.StackCount > 1 ? FText::AsNumber(Effect.StackCount) : FText::GetEmpty());
		State.Cooldown = FMath::Max(0.f, Effect.Remaining);
		State.CooldownFraction = Effect.Duration > 0 ? FMath::Clamp(Effect.Remaining / Effect.Duration, 0.f, 1.f) : 0;
		State.bAvailable = true;
		StatusIcons[I]->Present(State);
		StatusIcons[I]->SetColorAndOpacity(Effect.bDebuff ? FLinearColor(1.f, .7f, .7f) : FLinearColor(.85f, 1.f, .9f));
		StatusIcons[I]->SetToolTipText(FText::Format(NSLOCTEXT("ProjectJUI", "EffectTooltip", "{0} · {1}\n{2}\n중첩 {3} · {4}"),
			Effect.Name, Effect.bDebuff ? NSLOCTEXT("ProjectJUI", "Debuff", "약화") : NSLOCTEXT("ProjectJUI", "Buff", "강화"), Effect.Description,
			FText::AsNumber(Effect.StackCount), Effect.Duration > 0
			? FText::Format(NSLOCTEXT("ProjectJUI", "EffectTime", "남은 시간 {0}초"), FText::AsNumber(FMath::CeilToInt(Effect.Remaining)))
			: NSLOCTEXT("ProjectJUI", "EffectInfinite", "지속 효과")));
	}
	if (StatusOverflow) StatusOverflow->SetText(States.Num() > 12 ? FText::Format(NSLOCTEXT("ProjectJUI", "EffectOverflow", "+{0}"), FText::AsNumber(States.Num()-12)) : FText::GetEmpty());
	StatusEffectBox->SetVisibility(States.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
}
void UProject_JPlayerHUDWidget::PresentTarget(const FText &Name, float Current, float Maximum)
{
	if (!TargetBox || !TargetLabel || !TargetHealth) return;
	TargetBox->SetVisibility(Name.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	TargetLabel->SetText(FText::Format(NSLOCTEXT("ProjectJUI", "TargetHealth", "{0} · {1}/{2}"), Name, FText::AsNumber(FMath::RoundToInt(Current)), FText::AsNumber(FMath::RoundToInt(Maximum))));
	TargetHealth->SetPercent(Maximum > 0 ? FMath::Clamp(Current/Maximum, 0.f, 1.f) : 0);
}

bool UProject_JPlayerHUDWidget::ValidateRuntimeLayout() const
{
	if (!ScreenCanvas || !InventoryList || !EquipmentList || !KeysScroll || !SplitCount || !Minimap || !StatusEffectBox || SkillButtons.Num() != 10) return false;
	const auto Extent = ScreenCanvas->GetCachedGeometry().GetLocalSize();
	if (Extent.X <= 0 || Extent.Y <= 0 || !HUDStyle) return false;
	for (const auto &Pair : Windows)
	{
		const auto *CanvasSlot = Cast<UCanvasPanelSlot>(Pair.Value->Slot);
		if (!CanvasSlot) return false;
		const auto Position = CanvasSlot->GetPosition(), Size = CanvasSlot->GetSize();
		if (Position.X < -.1 || Position.Y < -.1 || Size.X <= 0 || Size.Y <= 0 || Position.X + Size.X > Extent.X + .1 || Position.Y + Size.Y > Extent.Y + .1) return false;
	}
	return true;
}


void UProject_JPlayerHUDWidget::PresentPartyRoster(const TArray<FProject_JPartyMemberSummary>& Members)
{
    if (!PartyBox || !WidgetTree) { PresentedPartyMemberCount = 0; return; }
    if (Members.Num() > ProjectJMaxPartyRosterMembers) { PresentPartyRoster({}); return; }
    // Validate a single coherent post-delta view. Never resolve a remote Pawn/ASC.
    TSet<FGuid> Ids;
    FName Group;
    for (int32 Index = 0; Index < Members.Num(); ++Index)
    {
        const auto& Member = Members[Index];
        if (!Member.CharacterId.IsValid() || Member.PartyId.IsNone() || Member.Order != Index
            || Ids.Contains(Member.CharacterId) || (!Group.IsNone() && Group != Member.PartyId)
            || Member.HealthPermille < 0 || Member.HealthPermille > 1000 || Member.DisplayName.Len() > 64)
        {
            PresentPartyRoster({}); return;
        }
        Group = Member.PartyId; Ids.Add(Member.CharacterId);
    }
    while (PartyLabels.Num() < Members.Num())
    {
        auto* Row = WidgetTree->ConstructWidget<UVerticalBox>();
        PartyBox->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 0, 0, 4));
        auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
        auto Font = Label->GetFont(); Font.Size = 11; Label->SetFont(Font);
        Row->AddChildToVerticalBox(Label);
        auto* Health = WidgetTree->ConstructWidget<UProgressBar>();
        auto* Size = WidgetTree->ConstructWidget<USizeBox>();
        Size->SetHeightOverride(4); Size->SetContent(Health); Row->AddChildToVerticalBox(Size);
        PartyLabels.Add(Label); PartyHealthBars.Add(Health);
    }
    for (int32 Index = 0; Index < PartyLabels.Num(); ++Index)
    {
        auto* Label = PartyLabels[Index].Get(); auto* Health = PartyHealthBars[Index].Get();
        auto* Row = Label ? Label->GetParent() : nullptr;
        if (Index >= Members.Num())
        {
            Label->SetText(FText::GetEmpty()); Health->SetPercent(0);
            if (Row) Row->SetVisibility(ESlateVisibility::Collapsed);
            continue;
        }
        const auto& Member = Members[Index];
        const FText Name = Member.DisplayName.IsEmpty()
            ? FText::Format(NSLOCTEXT("ProjectJUI", "PartyMemberNumber", "파티원 {0}"), FText::AsNumber(Index + 1))
            : FText::FromString(Member.DisplayName);
        const FText Leader = Member.bLeader ? NSLOCTEXT("ProjectJUI", "PartyLeaderBadge", " · 파티장") : FText::GetEmpty();
        const FText Presence = !Member.bAvailableHere ? NSLOCTEXT("ProjectJUI", "PartyUnavailable", "연결 정보 없음")
            : !Member.bHealthKnown ? NSLOCTEXT("ProjectJUI", "PartyResourcesPending", "연결 중")
            : FText::Format(NSLOCTEXT("ProjectJUI", "PartyHealthPercent", "체력 {0}"), FText::AsPercent(Member.HealthPermille / 1000.f));
        Label->SetText(Member.bAvailableHere
            ? FText::Format(NSLOCTEXT("ProjectJUI", "PartyOnlineRow", "{0} · Lv.{1}{2} · {3}"), Name, FText::AsNumber(Member.Level), Leader, Presence)
            : FText::Format(NSLOCTEXT("ProjectJUI", "PartyUnavailableRow", "{0}{1} · {2}"), Name, Leader, Presence));
        Health->SetPercent(Member.bHealthKnown ? Member.HealthPermille / 1000.f : 0);
        Health->SetFillColorAndOpacity(Member.bAvailableHere ? FLinearColor(.18f, .68f, .28f) : FLinearColor(.25f, .25f, .25f));
        if (Row) Row->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    PresentedPartyMemberCount = Members.Num();
    PartyBox->SetVisibility(Members.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UProject_JPlayerHUDWidget::PresentCraftingJobs(UProject_JCraftingJobsComponent* Source)
{
	if (CraftingJobsView) CraftingJobsView->Present(ScreenOwner.Get(), Source);
}
void UProject_JPlayerHUDWidget::RefreshCraftingRequest(UProject_JCraftingJobsComponent* Source)
{
	if (!CraftingJobsView) return;
	if (!Source) CraftingJobsView->Present(ScreenOwner.Get(), nullptr);
	else CraftingJobsView->RefreshClock();
}
int32 UProject_JPlayerHUDWidget::GetPresentedCraftingJobCount() const
{
	return CraftingJobsView ? CraftingJobsView->GetPresentedJobCount() : 0;
}
