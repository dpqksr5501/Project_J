#include "UI/Project_JHUDWindow.h"
#include "UI/Project_JPlayerHUDWidget.h"
#include "UI/Project_JUILayoutSettings.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/LocalPlayer.h"
#include "Styling/CoreStyle.h"

UProject_JUICommandButton::UProject_JUICommandButton()
{
	InitIsFocusable(true);
}
void UProject_JUICommandButton::InitializeCommand(UProject_JPlayerHUDWidget *InHUD, FName InCommand, const FText &Label)
{
	HUD = InHUD;
	Command = InCommand;
	OnClicked.AddUniqueDynamic(this, &ThisClass::Execute);
	auto *Text = NewObject<UTextBlock>(this);
	Text->SetText(Label);
	auto Font = Text->GetFont();
	Font.Size = 12;
	Text->SetFont(Font);
	SetContent(Text);
	FButtonStyle Style = GetStyle();
	Style.Normal = *FCoreStyle::Get().GetBrush("WhiteBrush");
	Style.Hovered = Style.Normal;
	Style.Pressed = Style.Normal;
	Style.Normal.TintColor = FSlateColor(FLinearColor(0.08f, 0.12f, 0.16f, 0.85f));
	Style.Hovered.TintColor = FSlateColor(FLinearColor(0.15f, 0.23f, 0.29f, 0.95f));
	Style.Pressed.TintColor = FSlateColor(FLinearColor(0.05f, 0.1f, 0.13f));
	Style.NormalPadding = FMargin(7, 4);
	Style.PressedPadding = FMargin(7, 4);
	SetStyle(Style);
}
void UProject_JUICommandButton::Execute()
{
	if (HUD.IsValid())
		HUD->ExecuteCommand(Command);
}
void UProject_JHUDWindow::InitializeWindow(UProject_JPlayerHUDWidget *InHUD, FName InId, const FText &InTitle)
{
	HUD = InHUD;
	WindowId = InId;
	Title = InTitle;
	NormalizedPosition = InId == TEXT("Equipment") ? FVector2D(0.22, 0.25)
						 : InId == TEXT("Bag")	   ? FVector2D(0.65, 0.25)
												   : FVector2D(0.4, 0.25);
}
TSharedRef<SWidget> UProject_JHUDWindow::RebuildWidget()
{
	// A designer skin must supply BodyBox; malformed skins fall back to the usable native window.
	if (!WidgetTree->RootWidget || !BodyBox)
	{
		auto *Frame = WidgetTree->ConstructWidget<UBorder>();
		Frame->SetBrushColor(HUD.IsValid() && HUD->HUDStyle ? HUD->HUDStyle->PanelColor
															: FLinearColor(0.018f, 0.027f, 0.038f, 0.94f));
		Frame->SetPadding(FMargin(10));
		WidgetTree->RootWidget = Frame;
		auto *Column = WidgetTree->ConstructWidget<UVerticalBox>();
		Frame->SetContent(Column);
		auto *Header = WidgetTree->ConstructWidget<UHorizontalBox>();
		Column->AddChildToVerticalBox(Header)->SetPadding(FMargin(0, 0, 0, 8));
		TitleLabel = WidgetTree->ConstructWidget<UTextBlock>();
		TitleLabel->SetText(Title);
		auto Font = TitleLabel->GetFont();
		Font.Size = 14;
		TitleLabel->SetFont(Font);
		Header->AddChildToHorizontalBox(TitleLabel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		auto *Close = WidgetTree->ConstructWidget<UProject_JUICommandButton>();
		Close->InitializeCommand(HUD.Get(), FName(*(TEXT("Close.") + WindowId.ToString())),
								 FText::FromString(TEXT("×")));
		Header->AddChildToHorizontalBox(Close);
		BodyBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BodyBox"));
		Column->AddChildToVerticalBox(BodyBox)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	if (TitleLabel)
		TitleLabel->SetText(Title);
	return Super::RebuildWidget();
}
void UProject_JHUDWindow::RequestClose()
{
	if (HUD.IsValid())
		HUD->CloseWindow(WindowId);
}
void UProject_JHUDWindow::SetIdentity(const FString &Key)
{
	const FString NewKey = Key.IsEmpty() ? FString() : Key + TEXT("/Window/") + WindowId.ToString();
	if (LayoutKey == NewKey)
		return;
	LayoutKey = NewKey;
	NormalizedPosition = WindowId == TEXT("Bag")		 ? FVector2D(0.65, 0.25)
						 : WindowId == TEXT("Equipment") ? FVector2D(0.22, 0.25)
														 : FVector2D(0.4, 0.25);
	if (auto *LP = GetOwningLocalPlayer())
		LP->GetSubsystem<UProject_JUILayoutSettings>()->Read(LayoutKey, NormalizedPosition);
}
void UProject_JHUDWindow::ApplyLayout(FVector2D Extent)
{
	if (Extent.X <= 0 || Extent.Y <= 0)
		return;
	if (auto *CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		const FVector2D Size(FMath::Min(PreferredSize.X, Extent.X), FMath::Min(PreferredSize.Y, Extent.Y));
		CanvasSlot->SetSize(Size);
		CanvasSlot->SetPosition((Extent - Size) * NormalizedPosition);
	}
}
FReply UProject_JHUDWindow::NativeOnPreviewMouseButtonDown(const FGeometry &Geometry, const FPointerEvent &Event)
{
	if (HUD.IsValid())
		HUD->RaiseWindow(WindowId);
	return Super::NativeOnPreviewMouseButtonDown(Geometry, Event);
}
FReply UProject_JHUDWindow::NativeOnMouseButtonDown(const FGeometry &Geometry, const FPointerEvent &Event)
{
	if (TitleLabel && Event.GetEffectingButton() == EKeys::LeftMouseButton &&
		TitleLabel->GetCachedGeometry().IsUnderLocation(Event.GetScreenSpacePosition()))
	{
		DragOffset = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
		return UWidgetBlueprintLibrary::DetectDragIfPressed(Event, this, EKeys::LeftMouseButton).NativeReply;
	}
	return Super::NativeOnMouseButtonDown(Geometry, Event);
}
void UProject_JHUDWindow::NativeOnDragDetected(const FGeometry &, const FPointerEvent &, UDragDropOperation *&Operation)
{
	Operation = NewObject<UDragDropOperation>();
	Operation->Payload = this;
}
bool UProject_JHUDWindow::FinishMove(const FPointerEvent &Event)
{
	auto *CanvasSlot = Cast<UCanvasPanelSlot>(Slot);
	const auto *Parent = GetParent();
	if (!CanvasSlot || !Parent)
		return false;
	const FVector2D Extent = Parent->GetCachedGeometry().GetLocalSize();
	const FVector2D Available = Extent - CanvasSlot->GetSize();
	const FVector2D Position = Parent->GetCachedGeometry().AbsoluteToLocal(Event.GetScreenSpacePosition()) - DragOffset;
	NormalizedPosition = FVector2D(Available.X > 0 ? FMath::Clamp(Position.X / Available.X, 0., 1.) : 0,
								   Available.Y > 0 ? FMath::Clamp(Position.Y / Available.Y, 0., 1.) : 0);
	ApplyLayout(Extent);
	if (auto *LP = GetOwningLocalPlayer())
		LP->GetSubsystem<UProject_JUILayoutSettings>()->Write(LayoutKey, NormalizedPosition);
	return true;
}
