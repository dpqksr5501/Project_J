#include "UI/Project_JPlayerHUDWidget.h"
#include "UI/Project_JPlayerUIComponent.h"
#include "UI/Project_JUILayoutSettings.h"
#include "UI/Project_JHUDWindow.h"
#include "UI/Project_JMinimapWidget.h"
#include "Game/Project_JQuestComponent.h"
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
#include "Inventory/Project_JItemDefinition.h"

#include "UI/Project_JWidgetPresentationHelpers.h"
using namespace ProjectJ::UI;

UProject_JInventoryListView::UProject_JInventoryListView(const FObjectInitializer &Initializer) : Super(Initializer)
{
	EntryWidgetClass = UProject_JItemWidget::StaticClass();
	// SObjectTableRow gates even NativeOnDrop behind the list's drop flag.
	bAllowDragDrop = true;
	SetSelectionMode(ESelectionMode::Single);
	SetWheelScrollMultiplier(1.f);
}
void UProject_JInventoryListView::ConfigureEntryClass(TSubclassOf<UProject_JItemWidget> InClass)
{
	// SObjectTableRow routes double clicks to the list, bypassing the row's Native event.
	OnItemDoubleClicked().RemoveAll(this);
	OnItemDoubleClicked().AddUObject(this, &ThisClass::HandleItemDoubleClicked);
	if (InClass && EntryWidgetClass != InClass)
	{
		EntryWidgetClass = InClass;
		RegenerateAllEntries();
	}
}
void UProject_JInventoryListView::HandleItemDoubleClicked(UObject *Item)
{
	ActivateItem(Item);
}
UProject_JInventoryTileView::UProject_JInventoryTileView(const FObjectInitializer &Initializer) : Super(Initializer)
{
	EntryWidgetClass = UProject_JItemWidget::StaticClass();
	bAllowDragDrop = true;
	SetEntryWidth(46);
	SetEntryHeight(46);
	SetSelectionMode(ESelectionMode::Single);
	SetWheelScrollMultiplier(1.f);
}
void UProject_JInventoryTileView::ConfigureEntryClass(TSubclassOf<UProject_JItemWidget> InClass)
{
	OnItemDoubleClicked().RemoveAll(this);
	OnItemDoubleClicked().AddUObject(this, &ThisClass::HandleItemDoubleClicked);
	if (InClass && EntryWidgetClass != InClass)
	{
		EntryWidgetClass = InClass;
		RegenerateAllEntries();
	}
}
void UProject_JInventoryTileView::HandleItemDoubleClicked(UObject *Item)
{
	ActivateItem(Item);
}

TSharedRef<SWidget> UProject_JItemWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		ItemFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ItemFrame"));
		ItemFrame->SetBrushColor(bCompactTile ? ItemCellColor : PanelColor);
		ItemFrame->SetPadding(FMargin(2));
		WidgetTree->RootWidget = ItemFrame;
		ItemIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("ItemIcon"));
		ItemIcon->SetVisibility(ESlateVisibility::Hidden);
		auto *IconSize = WidgetTree->ConstructWidget<USizeBox>();
		IconSize->SetWidthOverride(36);
		IconSize->SetHeightOverride(36);
		IconSize->SetContent(ItemIcon);
		ItemLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ItemLabel"));
		ItemLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.88f, 0.92f)));
		if (bCompactTile)
		{
			auto Font = ItemLabel->GetFont();
			Font.Size = 10;
			ItemLabel->SetFont(Font);
			auto *Overlay = WidgetTree->ConstructWidget<UOverlay>();
			ItemFrame->SetContent(Overlay);
			auto *IconSlot = Overlay->AddChildToOverlay(IconSize);
			IconSlot->SetHorizontalAlignment(HAlign_Center);
			IconSlot->SetVerticalAlignment(VAlign_Center);
			auto *LabelSlot = Overlay->AddChildToOverlay(ItemLabel);
			LabelSlot->SetHorizontalAlignment(HAlign_Center);
			LabelSlot->SetVerticalAlignment(VAlign_Center);
			ItemLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
			ItemQuantityLabel =
				WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ItemQuantityLabel"));
			ItemQuantityLabel->SetFont(Font);
			ItemQuantityLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
			auto *QuantitySlot = Overlay->AddChildToOverlay(ItemQuantityLabel);
			QuantitySlot->SetHorizontalAlignment(HAlign_Right);
			QuantitySlot->SetVerticalAlignment(VAlign_Bottom);
			ItemLockLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ItemLockLabel"));
			ItemLockLabel->SetFont(Font);
			ItemLockLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
			auto *LockSlot = Overlay->AddChildToOverlay(ItemLockLabel);
			LockSlot->SetHorizontalAlignment(HAlign_Left);
			LockSlot->SetVerticalAlignment(VAlign_Top);
		}
		else
		{
			auto *Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			ItemFrame->SetContent(Row);
			Row->AddChildToHorizontalBox(IconSize);
			auto *LabelSlot = Row->AddChildToHorizontalBox(ItemLabel);
			LabelSlot->SetPadding(FMargin(12, 0));
			LabelSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
	return Super::RebuildWidget();
}

void UProject_JItemWidget::ReleaseIcon()
{
	IconRequest.Reset();
	if (ItemIcon)
	{
		ItemIcon->SetBrushFromTexture(nullptr);
		ItemIcon->SetVisibility(ESlateVisibility::Hidden);
	}
}
void UProject_JItemWidget::NativeOnListItemObjectSet(UObject *ItemObject)
{
	IUserObjectListEntry::NativeOnListItemObjectSet(ItemObject);
	ReleaseIcon();
	Entry = Cast<UProject_JInventoryEntry>(ItemObject);
	Render();
}
void UProject_JItemWidget::NativeOnEntryReleased()
{
	ReleaseIcon();
	Entry = nullptr;
	IUserListEntry::NativeOnEntryReleased();
}
void UProject_JItemWidget::BeginDestroy()
{
	IconRequest.Reset();
	Super::BeginDestroy();
}

void UProject_JItemWidget::NativeDestruct()
{
	ReleaseIcon();
	Entry = nullptr;
	Super::NativeDestruct();
}

void UProject_JItemWidget::Render()
{
	if (!Entry)
		return;
	if (ItemFrame) ItemFrame->SetBrushColor(IsListItemSelected() ? FLinearColor(0.18f, 0.32f, 0.42f) :
		(bCompactTile ? ItemCellColor : PanelColor));
	const FText SlotLabel = ProjectJ::UI::EquipmentSlotLabel(Entry->Slot);
	const FText State = Entry->Item.bIsEquipped ? NSLOCTEXT("ProjectJUI", "Equipped", "장착")
						: Entry->Item.bIsLocked ? NSLOCTEXT("ProjectJUI", "Locked", "잠김")
												: FText::GetEmpty();
	if (ItemLabel)
		ItemLabel->SetText(
			Entry->bEquipmentEntry
				? FText::Format(NSLOCTEXT("ProjectJUI", "SlotRow", "{0}  ·  {1}"), SlotLabel, ItemName(Entry))
				: FText::Format(NSLOCTEXT("ProjectJUI", "ItemRow", "{0}  ×{1}  {2}"), ItemName(Entry),
								FText::AsNumber(Entry->Item.StackCount), State));
	if (bCompactTile && ItemLabel)
		ItemLabel->SetText(!Entry->Item.ItemDef					? SlotLabel
						   : (!ItemIcon || !ItemIcon->GetBrush().GetResourceObject()) ? FText::FromString(ItemName(Entry).ToString().Left(2))
																: FText::GetEmpty());
	if (ItemQuantityLabel)
		ItemQuantityLabel->SetText(Entry->Item.StackCount > 1 ? FText::AsNumber(Entry->Item.StackCount)
															  : FText::GetEmpty());
	if (ItemLockLabel)
		ItemLockLabel->SetText(Entry->Item.bIsLocked ? FText::FromString(TEXT("L")) : FText::GetEmpty());
	SetToolTipText(Entry->Model.IsValid() ? Entry->Model->BuildTooltip(Entry) : FText::GetEmpty());
	const FSoftObjectPath Desired =
		Entry->Item.ItemDef ? Entry->Item.ItemDef->Icon.ToSoftObjectPath() : FSoftObjectPath();
	if (!IconRequest.Select(Desired)) { return; }
	if (ItemIcon)
	{
		ItemIcon->SetBrushFromTexture(nullptr);
		ItemIcon->SetVisibility(ESlateVisibility::Hidden);
	}
	if (Desired.IsNull()) { return; }
	IconRequest.Load(this, [WeakThis = TWeakObjectPtr<UProject_JItemWidget>(this), Desired](UTexture2D* Texture, uint64 Generation)
	{
		if (auto* Self = WeakThis.Get(); Self && Self->ItemIcon)
		{
			Self->ItemIcon->SetBrushFromTexture(Texture);
			Self->ItemIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (Self->IconRequest.IsCurrent(Desired, Generation) && Self->bCompactTile && Self->ItemLabel)
			{ Self->ItemLabel->SetText(FText::GetEmpty()); }
		}
	});
}

void UProject_JItemWidget::NativeOnItemSelectionChanged(bool bSelected)
{
	IUserObjectListEntry::NativeOnItemSelectionChanged(bSelected);
	Render();
}
FReply UProject_JItemWidget::NativeOnMouseButtonDown(const FGeometry &Geometry, const FPointerEvent &Event)
{
	if (Entry && Entry->Model.IsValid() && !Entry->bEquipmentEntry && Event.IsShiftDown() && Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		Entry->Model->OnSplitRequested.Broadcast(Entry);
		return FReply::Handled();
	}
	if (Entry && Entry->Item.InstanceId.IsValid() && Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// DetectDrag consumes the click, so explicitly keep the owning list's selection in sync.
		if (auto *List = Cast<UListView>(GetOwningListView()))
		{
			List->SetSelectedItem(Entry);
			List->SetUserFocus(GetOwningPlayer());
		}
		return UWidgetBlueprintLibrary::DetectDragIfPressed(Event, this, EKeys::LeftMouseButton).NativeReply;
	}
	if (Event.GetEffectingButton() == EKeys::RightMouseButton && Entry && Entry->Model.IsValid())
	{
		Entry->Model->Activate(Entry);
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(Geometry, Event);
}
FReply UProject_JItemWidget::NativeOnMouseButtonDoubleClick(const FGeometry &Geometry, const FPointerEvent &Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton && Entry && Entry->Model.IsValid())
	{
		Entry->Model->Activate(Entry);
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDoubleClick(Geometry, Event);
}
void UProject_JItemWidget::NativeOnDragDetected(const FGeometry &, const FPointerEvent &,
												UDragDropOperation *&Operation)
{
	if (!Entry || !Entry->Model.IsValid() || Entry->Model->bPending || !Entry->Item.InstanceId.IsValid())
		return;
	auto *Drag = NewObject<UProject_JItemDragOperation>();
	// Freeze drag identity; the live list object may change while FastArray updates.
	Drag->Entry = DuplicateObject<UProject_JInventoryEntry>(Entry, Drag);
	Drag->Pivot = EDragPivot::MouseDown;
	auto *Ghost = NewObject<UTextBlock>(Drag);
	Ghost->SetText(ItemName(Entry));
	Drag->DefaultDragVisual = Ghost;
	Operation = Drag;
}
bool UProject_JItemWidget::NativeOnDrop(const FGeometry &, const FDragDropEvent &Event, UDragDropOperation *Operation)
{
	if (ItemFrame)
		ItemFrame->SetBrushColor(bCompactTile ? ItemCellColor : PanelColor);
	auto *Drag = Cast<UProject_JItemDragOperation>(Operation);
	if (Entry && Entry->Model.IsValid() && Drag && Drag->Entry && !Entry->bEquipmentEntry && !Drag->Entry->bEquipmentEntry)
		return Entry->Model->DropInBag(Drag->Entry, Entry, Event.IsControlDown());
	if (!Entry || !Entry->Model.IsValid() || !Drag ||
		!Entry->Model->CanDrop(Drag->Entry, Entry->Slot, !Entry->bEquipmentEntry))
		return false;
	return Entry->bEquipmentEntry ? Entry->Model->Equip(Drag->Entry->Item.InstanceId, Entry->Slot)
								  : Entry->Model->Unequip(Drag->Entry->Item.InstanceId, Drag->Entry->Slot);
}
void UProject_JItemWidget::NativeOnDragEnter(const FGeometry &Geometry, const FDragDropEvent &Event,
											 UDragDropOperation *Operation)
{
	Super::NativeOnDragEnter(Geometry, Event, Operation);
	auto *Drag = Cast<UProject_JItemDragOperation>(Operation);
	if (ItemFrame && Entry && Entry->Model.IsValid() && Drag)
		ItemFrame->SetBrushColor((Drag->Entry && !Drag->Entry->bEquipmentEntry && !Entry->bEquipmentEntry
			? Entry->Model->CanDropInBag(Drag->Entry, Entry, Event.IsControlDown())
			: Entry->Model->CanDrop(Drag->Entry, Entry->Slot, !Entry->bEquipmentEntry))
									 ? FLinearColor(0.08f, 0.3f, 0.22f, 1)
									 : FLinearColor(0.3f, 0.08f, 0.08f, 1));
}
void UProject_JItemWidget::NativeOnDragLeave(const FDragDropEvent &Event, UDragDropOperation *Operation)
{
	if (ItemFrame)
		ItemFrame->SetBrushColor(bCompactTile ? ItemCellColor : PanelColor);
	Super::NativeOnDragLeave(Event, Operation);
}

