#include "UI/Project_JMinimapWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "Engine/AssetManager.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Kismet/GameplayStatics.h"

bool UProject_JMapDefinition::WorldToUV(FVector Position, FVector2D Center, FVector2D HalfExtent, FVector2D &OutUV)
{
	if (Position.ContainsNaN() || Center.ContainsNaN() || HalfExtent.ContainsNaN() || HalfExtent.X <= 0 ||
		HalfExtent.Y <= 0)
		return false;
	OutUV = FVector2D(0.5 + (Position.Y - Center.Y) / (2 * HalfExtent.Y),
					  0.5 - (Position.X - Center.X) / (2 * HalfExtent.X));
	return true;
}
TSharedRef<SWidget> UProject_JMinimapWidget::RebuildWidget()
{
	if (!WidgetTree->RootWidget)
	{
		auto *Size = WidgetTree->ConstructWidget<USizeBox>();
		Size->SetWidthOverride(168);
		Size->SetHeightOverride(168);
		Size->SetVisibility(ESlateVisibility::Visible);
		WidgetTree->RootWidget = Size;
	}
	// Both UserWidget and SizeBox default to SelfHitTestInvisible. A painted map has no child to hit.
	SetVisibility(ESlateVisibility::Visible);
	SetClipping(EWidgetClipping::ClipToBounds);
	return Super::RebuildWidget();
}
void UProject_JMinimapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (!Definition)
		Definition = LoadObject<UProject_JMapDefinition>(nullptr, TEXT("/Game/UI/DA_ProjectJMap.DA_ProjectJMap"));
	if (Definition && !Definition->LevelName.IsNone() &&
		Definition->LevelName != FName(*UGameplayStatics::GetCurrentLevelName(this, true)))
		Definition = nullptr;
	if (Definition && !Definition->Texture.IsNull())
	{
		const auto Path = Definition->Texture.ToSoftObjectPath();
		LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
			Path,
			[WeakThis = TWeakObjectPtr<UProject_JMinimapWidget>(this), Path]()
			{
				if (auto *Self = WeakThis.Get())
				{
					Self->MapTexture = Cast<UTexture2D>(Path.ResolveObject());
					Self->TextureBrush.SetResourceObject(Self->MapTexture);
				}
			});
	}
	Sample();
	GetWorld()->GetTimerManager().SetTimer(SampleTimer, this, &ThisClass::Sample, 0.2f, true);
	SetToolTipText(NSLOCTEXT("ProjectJUI", "MapTooltip", "휠: 확대/축소 · N: 북쪽 · 노란 점: 추적 목표"));
}
void UProject_JMinimapWidget::NativeDestruct()
{
	GetWorld()->GetTimerManager().ClearTimer(SampleTimer);
	if (LoadHandle)
		LoadHandle->CancelHandle();
	LoadHandle.Reset();
	Super::NativeDestruct();
}
void UProject_JMinimapWidget::Sample()
{
	if (!IsVisible())
		return;
	const auto *PC = GetOwningPlayer();
	const APawn *Pawn = PC ? PC->GetPawn() : nullptr;
	bHasPlayer = Pawn != nullptr;
	if (Pawn)
	{
		PlayerPosition = Pawn->GetActorLocation();
		PlayerYaw = Pawn->GetActorRotation().Yaw;
	}
}
void UProject_JMinimapWidget::SetQuestMarkers(TArray<FProject_JMapMarker> InMarkers)
{
	QuestMarkers = MoveTemp(InMarkers);
}
void UProject_JMinimapWidget::SetZoom(float Value)
{
	if (FMath::IsFinite(Value))
		Zoom = FMath::Clamp(Value, 1.f, 8.f);
}
FReply UProject_JMinimapWidget::NativeOnMouseWheel(const FGeometry &, const FPointerEvent &Event)
{
	SetZoom(Zoom * (Event.GetWheelDelta() > 0 ? 1.25f : 0.8f));
	return FReply::Handled();
}
int32 UProject_JMinimapWidget::NativePaint(const FPaintArgs &Args, const FGeometry &Geometry, const FSlateRect &Culling,
										   FSlateWindowElementList &Out, int32 Layer, const FWidgetStyle &Style,
										   bool bEnabled) const
{
	Layer = Super::NativePaint(Args, Geometry, Culling, Out, Layer, Style, bEnabled);
	const FVector2D Size = Geometry.GetLocalSize();
	const auto *White = FCoreStyle::Get().GetBrush("WhiteBrush");
	FSlateDrawElement::MakeBox(Out, ++Layer, Geometry.ToPaintGeometry(), White, ESlateDrawEffect::None,
							   FLinearColor(0.02f, 0.035f, 0.045f, 0.9f));
	const FVector2D Center = Definition ? Definition->Center : FVector2D::ZeroVector;
	const FVector2D Extent = Definition ? Definition->HalfExtent : FVector2D(5000, 5000);
	FVector2D PlayerUV;
	if (!UProject_JMapDefinition::WorldToUV(PlayerPosition, Center, Extent, PlayerUV))
		return Layer;
	const FVector2D ViewCenter(FMath::Clamp(PlayerUV.X, 0.5 / Zoom, 1. - 0.5 / Zoom),
							   FMath::Clamp(PlayerUV.Y, 0.5 / Zoom, 1. - 0.5 / Zoom));
	if (MapTexture)
	{
		FSlateBrush Brush = TextureBrush;
		const FVector2D Half(0.5 / Zoom, 0.5 / Zoom);
		Brush.SetUVRegion(FBox2f(FVector2f(ViewCenter - Half), FVector2f(ViewCenter + Half)));
		FSlateDrawElement::MakeBox(Out, ++Layer, Geometry.ToPaintGeometry(), &Brush);
	}
	else
	{
		for (int32 I = 1; I < 4; ++I)
		{
			const float T = I / 4.f;
			FSlateDrawElement::MakeLines(Out, Layer + 1, Geometry.ToPaintGeometry(),
										 {FVector2f(Size.X * T, 0), FVector2f(Size.X * T, Size.Y)},
										 ESlateDrawEffect::None, FLinearColor(0.15f, 0.22f, 0.25f), true);
			FSlateDrawElement::MakeLines(Out, Layer + 1, Geometry.ToPaintGeometry(),
										 {FVector2f(0, Size.Y * T), FVector2f(Size.X, Size.Y * T)},
										 ESlateDrawEffect::None, FLinearColor(0.15f, 0.22f, 0.25f), true);
		}
		++Layer;
	}
	const auto PositionOnMap = [&](FVector Position)
	{
		FVector2D UV;
		UProject_JMapDefinition::WorldToUV(Position, Center, Extent, UV);
		return ((UV - ViewCenter) * Zoom + FVector2D(0.5, 0.5)) * Size;
	};
	if (Definition && !MapTexture)
		for (const auto &Region : Definition->Regions)
		{
			const FVector2D A = PositionOnMap(FVector(Region.Min.X, Region.Min.Y, 0)),
							B = PositionOnMap(FVector(Region.Max.X, Region.Max.Y, 0));
			const FVector2D Min(FMath::Clamp(FMath::Min(A.X, B.X), 0., Size.X),
								FMath::Clamp(FMath::Min(A.Y, B.Y), 0., Size.Y)),
				Max(FMath::Clamp(FMath::Max(A.X, B.X), 0., Size.X), FMath::Clamp(FMath::Max(A.Y, B.Y), 0., Size.Y));
			if (Max.X > Min.X && Max.Y > Min.Y)
				FSlateDrawElement::MakeBox(
					Out, Layer + 1,
					Geometry.ToPaintGeometry(FVector2f(Max - Min), FSlateLayoutTransform(FVector2f(Min))), White,
					ESlateDrawEffect::None, FLinearColor(0.14f, 0.22f, 0.25f, 0.85f));
		}
	++Layer;
	auto DrawMarker = [&](const FProject_JMapMarker &Marker)
	{
		const FVector2D P = PositionOnMap(Marker.Position);
		if (!Marker.Position.ContainsNaN() && P.X >= 4 && P.Y >= 4 && P.X <= Size.X - 4 && P.Y <= Size.Y - 4)
			FSlateDrawElement::MakeBox(
				Out, Layer + 1,
				Geometry.ToPaintGeometry(FVector2f(6, 6), FSlateLayoutTransform(FVector2f(P - FVector2D(3, 3)))), White,
				ESlateDrawEffect::None, Marker.Color);
	};
	if (Definition)
		for (const auto &Marker : Definition->Markers)
			DrawMarker(Marker);
	for (const auto &Marker : QuestMarkers)
		DrawMarker(Marker);
	++Layer;
	if (bHasPlayer)
	{
		const FVector2D P = PositionOnMap(PlayerPosition);
		const double Angle = FMath::DegreesToRadians(PlayerYaw);
		const FVector2D Direction(FMath::Sin(Angle), -FMath::Cos(Angle));
		const FVector2D Side(Direction.Y, -Direction.X);
		TArray<FVector2f> Points{FVector2f(P + Direction * 8), FVector2f(P - Direction * 5 + Side * 5),
								 FVector2f(P - Direction * 5 - Side * 5), FVector2f(P + Direction * 8)};
		FSlateDrawElement::MakeLines(Out, ++Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None,
									 FLinearColor::White, true, 2);
	}
	FSlateDrawElement::MakeText(
		Out, ++Layer, Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(5, 3))), TEXT("N"),
		FCoreStyle::GetDefaultFontStyle("Regular", 11), ESlateDrawEffect::None, FLinearColor::White);
	if (Definition)
		FSlateDrawElement::MakeText(Out, ++Layer,
									Geometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(22, 3))),
									Definition->MapName, FCoreStyle::GetDefaultFontStyle("Regular", 10),
									ESlateDrawEffect::None, FLinearColor::White);
	return Layer;
}
