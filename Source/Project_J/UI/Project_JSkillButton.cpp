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
#include "UObject/StrongObjectPtr.h"

#include "UI/Project_JWidgetPresentationHelpers.h"
using namespace ProjectJ::UI;

TSharedRef<SWidget> UProject_JSkillButton::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		auto *Size = WidgetTree->ConstructWidget<USizeBox>();
		const auto *Theme = ScreenOwner.IsValid() ? ScreenOwner->GetHUDStyle() : nullptr;
		const float Dimension = Theme ? FMath::Clamp(Theme->QuickSlotSize, 32.f, 64.f) : 46.f;
		Size->SetWidthOverride(Dimension);
		Size->SetHeightOverride(Dimension);
		WidgetTree->RootWidget = Size;
		auto *Frame = WidgetTree->ConstructWidget<UBorder>();
		Frame->SetBrushColor(Theme ? Theme->PanelColor : PanelColor);
		Frame->SetPadding(FMargin(2));
		Size->SetContent(Frame);
		auto *Overlay = WidgetTree->ConstructWidget<UOverlay>();
		Frame->SetContent(Overlay);
		SkillIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("SkillIcon"));
		Overlay->AddChildToOverlay(SkillIcon);
		SkillIcon->SetVisibility(ESlateVisibility::Collapsed);
		auto MakeText = [&](const TCHAR *Name, int32 FontSize, EHorizontalAlignment H, EVerticalAlignment V)
		{
			auto *Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
			auto Font = Text->GetFont();
			Font.Size = FontSize;
			Text->SetFont(Font);
			auto *Slot = Overlay->AddChildToOverlay(Text);
			Slot->SetHorizontalAlignment(H);
			Slot->SetVerticalAlignment(V);
			Text->SetVisibility(ESlateVisibility::HitTestInvisible);
			return Text;
		};
		SkillLabel = MakeText(TEXT("SkillLabel"), 14, HAlign_Center, VAlign_Center);
		KeyLabel = MakeText(TEXT("KeyLabel"), 10, HAlign_Left, VAlign_Top);
		QuantityLabel = MakeText(TEXT("QuantityLabel"), 11, HAlign_Right, VAlign_Bottom);
		CooldownBar = WidgetTree->ConstructWidget<UProgressBar>();
		auto *CooldownSize = WidgetTree->ConstructWidget<USizeBox>();
		CooldownSize->SetHeightOverride(3);
		CooldownSize->SetContent(CooldownBar);
		auto *CooldownSlot = Overlay->AddChildToOverlay(CooldownSize);
		CooldownSlot->SetVerticalAlignment(VAlign_Bottom);
	}
	return Super::RebuildWidget();
}
void UProject_JSkillButton::Present(const FProject_JQuickSlotState &State)
{
	bAvailable = State.bAvailable;
	SetIcon(State.Icon);
	if (KeyLabel)
		KeyLabel->SetText(State.KeyHint);
	if (QuantityLabel)
		QuantityLabel->SetText(State.Binding.Kind == EProject_JQuickSlotKind::Item ? FText::AsNumber(State.Quantity)
																				   : FText::GetEmpty());
	if (SkillLabel)
	{
		bShowingIconFallback = State.Cooldown <= 0 && (!SkillIcon || !SkillIcon->GetBrush().GetResourceObject()) && State.Binding.Kind != EProject_JQuickSlotKind::Empty;
		SkillLabel->SetText(State.Cooldown > 0 ? FText::AsNumber(FMath::CeilToInt(State.Cooldown))
							: bShowingIconFallback
								? FText::FromString(State.Name.ToString().Left(2))
								: FText::GetEmpty());
		SkillLabel->SetColorAndOpacity(
			FSlateColor(State.bAvailable ? FLinearColor::White : FLinearColor(0.5f, 0.55f, 0.6f)));
	}
	if (CooldownBar)
	{
		CooldownBar->SetPercent(State.CooldownFraction);
		CooldownBar->SetVisibility(State.Cooldown > 0 ? ESlateVisibility::HitTestInvisible
													  : ESlateVisibility::Collapsed);
	}
	SetToolTipText(State.Binding.Kind == EProject_JQuickSlotKind::Empty
					   ? NSLOCTEXT("ProjectJUI", "QuickEmpty", "빈 퀵슬롯 · 잠금 해제 후 드래그하여 등록")
					   : FText::Format(NSLOCTEXT("ProjectJUI", "QuickTooltip",
												 "{0} · 키 {1}\n잠금 해제 후 드래그: 이동 · 우클릭: 해제"),
									   State.Name, State.KeyHint));
	if (!State.UnavailableReason.IsEmpty()) SetToolTipText(FText::Format(NSLOCTEXT("ProjectJUI", "QuickBlockedTooltip", "{0}\n{1}"), GetToolTipText(), State.UnavailableReason));
	if (SkillIcon)
		SkillIcon->SetColorAndOpacity(State.bAvailable ? FLinearColor::White : FLinearColor(0.35f, 0.35f, 0.35f));
}
void UProject_JSkillButton::Configure(UProject_JPlayerUIComponent *Owner, int32 Index)
{
	if (ScreenOwner == Owner && SlotIndex == Index) return;
	ScreenOwner = Owner;
	SlotIndex = Index;
	// Publish the new binding before returning only the old acquired press.
	ReleasePressedSkill();
}
void UProject_JSkillButton::NativeConstruct()
{
	bAcceptsPress = true;
	Super::NativeConstruct();
}
void UProject_JSkillButton::ReleasePressedSkill()
{
	const TStrongObjectPtr<UProject_JSkillButton> KeepSelf(this);
	const TStrongObjectPtr<UProject_JPlayerUIComponent> Owner(PressOwner.Get());
	const uint64 Receipt = PressReceipt;
	const int32 Index = PressIndex;
	PressReceipt = 0; PressIndex = INDEX_NONE;
	PressOwner.Reset();
	if (Receipt && Owner.IsValid()) Owner->ReleaseSkillReceipt(Index, Receipt);
}
void UProject_JSkillButton::SetIcon(TSoftObjectPtr<UTexture2D> Icon)
{
	const FSoftObjectPath Desired = Icon.ToSoftObjectPath();
	if (!IconRequest.Select(Desired)) { return; }
	if (SkillIcon)
	{
		SkillIcon->SetBrushFromTexture(nullptr);
		SkillIcon->SetVisibility(Desired.IsNull() ? ESlateVisibility::Collapsed : ESlateVisibility::Hidden);
	}
	if (Desired.IsNull()) { return; }
	IconRequest.Load(this, [WeakThis = TWeakObjectPtr<UProject_JSkillButton>(this), Desired](UTexture2D* Texture, uint64 Generation)
	{
		if (auto* Self = WeakThis.Get(); Self && Self->SkillIcon)
		{
			Self->SkillIcon->SetBrushFromTexture(Texture);
			Self->SkillIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
			// Visibility observers can recycle/rebind this widget synchronously.
			if (Self->IconRequest.IsCurrent(Desired, Generation) && Self->bShowingIconFallback && Self->SkillLabel)
			{ Self->SkillLabel->SetText(FText::GetEmpty()); Self->bShowingIconFallback = false; }
		}
	});
}
void UProject_JSkillButton::UpdateLabel(const FText &Text, bool bInAvailable)
{
	bAvailable = bInAvailable;
	if (SkillLabel)
	{
		SkillLabel->SetText(Text);
		SkillLabel->SetColorAndOpacity(FSlateColor(bAvailable ? FLinearColor::White : FLinearColor(0.4f, 0.45f, 0.5f)));
	}
}
FReply UProject_JSkillButton::NativeOnMouseButtonDown(const FGeometry &, const FPointerEvent &Event)
{
	if (!bAcceptsPress || !ScreenOwner.IsValid())
		return FReply::Unhandled();
	if (Event.GetEffectingButton() == EKeys::RightMouseButton && ScreenOwner->CanEditQuickSlots())
	{
		ScreenOwner->ClearQuickSlot(SlotIndex);
		return FReply::Handled();
	}
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (ScreenOwner->CanEditQuickSlots())
			return UWidgetBlueprintLibrary::DetectDragIfPressed(Event, this, EKeys::LeftMouseButton).NativeReply;
		if (bAvailable && !PressReceipt)
		{
			const TStrongObjectPtr<UProject_JSkillButton> KeepSelf(this);
			const TStrongObjectPtr<UProject_JPlayerUIComponent> Owner(ScreenOwner.Get());
			PressOwner = Owner.Get(); PressIndex = SlotIndex;
			Owner->PressSkillWithReceipt(SlotIndex, &PressReceipt);
			if (auto* CurrentOwner = PressOwner.Get(); bAcceptsPress && CurrentOwner && CurrentOwner->IsSkillReceiptCurrent(PressIndex, PressReceipt))
				return FReply::Handled().CaptureMouse(TakeWidget());
			// Activation may have retired the component's receipt without calling
			// back into this widget. Do not capture from that stale local value.
			ReleasePressedSkill();
		}
	}
	return FReply::Handled();
}
void UProject_JSkillButton::NativeOnDragDetected(const FGeometry &, const FPointerEvent &,
												 UDragDropOperation *&Operation)
{
	if (!ScreenOwner.IsValid() || !ScreenOwner->CanEditQuickSlots())
		return;
	const auto Binding = ScreenOwner->GetQuickBinding(SlotIndex);
	if (Binding.Kind == EProject_JQuickSlotKind::Empty)
		return;
	auto *Drag = NewObject<UProject_JQuickSlotDrag>();
	Drag->Owner = ScreenOwner;
	Drag->SourceIndex = SlotIndex;
	Drag->Binding = Binding;
	Drag->Identity = ScreenOwner->GetLayoutIdentity();
	Operation = Drag;
}
bool UProject_JSkillButton::NativeOnDrop(const FGeometry &, const FDragDropEvent &, UDragDropOperation *Operation)
{
	if (!ScreenOwner.IsValid())
		return false;
	if (auto *Drag = Cast<UProject_JQuickSlotDrag>(Operation); Drag && Drag->Owner == ScreenOwner)
		return ScreenOwner->MoveQuickSlot(Drag->SourceIndex, SlotIndex, Drag->Identity, Drag->Binding);
	if (auto *Item = Cast<UProject_JItemDragOperation>(Operation))
		return ScreenOwner->BindItemSlot(SlotIndex, Item->Entry);
	return false;
}
FReply UProject_JSkillButton::NativeOnMouseButtonUp(const FGeometry &Geometry, const FPointerEvent &Event)
{
	if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return Super::NativeOnMouseButtonUp(Geometry, Event);
	ReleasePressedSkill();
	// A release callback may already own a new press/capture.
	return !PressReceipt && HasMouseCapture() ? FReply::Handled().ReleaseMouseCapture() : FReply::Handled();
}
void UProject_JSkillButton::BeginDestroy()
{
	IconRequest.Reset();
	Super::BeginDestroy();
}

void UProject_JSkillButton::NativeDestruct()
{
	bAcceptsPress = false;
	ReleasePressedSkill();
	IconRequest.Reset();
	Super::NativeDestruct();
}
void UProject_JSkillButton::NativeOnMouseCaptureLost(const FCaptureLostEvent &Event)
{
	ReleasePressedSkill();
	Super::NativeOnMouseCaptureLost(Event);
}

