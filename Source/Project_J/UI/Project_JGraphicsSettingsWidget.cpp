#include "UI/Project_JGraphicsSettingsWidget.h"

#include "UI/Project_JGraphicsSettings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameUserSettings.h"

#define LOCTEXT_NAMESPACE "ProjectJGraphics"

namespace
{
struct FQualitySetting
{
	FText Label;
	int32 (UGameUserSettings::*Read)() const;
	void (UGameUserSettings::*Write)(int32);
};

const FQualitySetting QualitySettings[] = {
	{LOCTEXT("Shadows", "그림자"), &UGameUserSettings::GetShadowQuality, &UGameUserSettings::SetShadowQuality},
	{LOCTEXT("Reflections", "반사"), &UGameUserSettings::GetReflectionQuality, &UGameUserSettings::SetReflectionQuality},
	{LOCTEXT("Lighting", "간접 조명"), &UGameUserSettings::GetGlobalIlluminationQuality, &UGameUserSettings::SetGlobalIlluminationQuality},
	{LOCTEXT("Textures", "텍스처"), &UGameUserSettings::GetTextureQuality, &UGameUserSettings::SetTextureQuality},
	{LOCTEXT("AA", "안티앨리어싱"), &UGameUserSettings::GetAntiAliasingQuality, &UGameUserSettings::SetAntiAliasingQuality},
	{LOCTEXT("Effects", "효과 및 원격 망토"), &UGameUserSettings::GetVisualEffectQuality, &UGameUserSettings::SetVisualEffectQuality},
	{LOCTEXT("View", "표시 거리"), &UGameUserSettings::GetViewDistanceQuality, &UGameUserSettings::SetViewDistanceQuality},
	{LOCTEXT("Foliage", "초목"), &UGameUserSettings::GetFoliageQuality, &UGameUserSettings::SetFoliageQuality},
	{LOCTEXT("Landscape", "지형"), &UGameUserSettings::GetLandscapeQuality, &UGameUserSettings::SetLandscapeQuality},
	{LOCTEXT("Post", "후처리"), &UGameUserSettings::GetPostProcessingQuality, &UGameUserSettings::SetPostProcessingQuality},
	{LOCTEXT("Shading", "셰이딩"), &UGameUserSettings::GetShadingQuality, &UGameUserSettings::SetShadingQuality},
};

void AddQualityOptions(UComboBoxString& Combo)
{
	for (const FText& Label : {LOCTEXT("Low", "낮음"), LOCTEXT("Medium", "중간"), LOCTEXT("High", "높음"),
		LOCTEXT("Epic", "매우 높음"), LOCTEXT("Cinematic", "시네마틱")})
	{
		Combo.AddOption(Label.ToString());
	}
}

void PopulateNumbers(UComboBoxString& Combo, TArray<float>& Choices, float Current, bool bPercent)
{
	Combo.ClearOptions();
	if (!Choices.ContainsByPredicate([Current](float Value) { return FMath::IsNearlyEqual(Value, Current, .001f); }))
	{
		Choices.Add(Current);
		Choices.Sort();
	}
	for (const float Value : Choices)
	{
		FNumberFormattingOptions Format;
		Format.SetUseGrouping(false).SetMaximumFractionalDigits(2);
		const FText Number = FText::AsNumber(Value, &Format);
		const FText Label = bPercent ? FText::Format(LOCTEXT("Percent", "{0}%"), Number) :
			(Value == 0.f ? LOCTEXT("Unlimited", "제한 없음") : FText::Format(LOCTEXT("FPS", "{0} FPS"), Number));
		Combo.AddOption(Label.ToString());
	}
	Combo.SetSelectedIndex(Choices.IndexOfByPredicate([Current](float Value) { return FMath::IsNearlyEqual(Value, Current, .001f); }));
}
}

TSharedRef<SWidget> UProject_JGraphicsSettingsWidget::RebuildWidget()
{
	if (!WidgetTree->RootWidget)
	{
		auto* Panel = WidgetTree->ConstructWidget<UVerticalBox>();
		WidgetTree->RootWidget = Panel;
		auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
		Panel->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		auto* Rows = WidgetTree->ConstructWidget<UVerticalBox>();
		Scroll->AddChild(Rows);

		const auto Label = [this](const FText& Value, int32 Size)
		{
			auto* Text = WidgetTree->ConstructWidget<UTextBlock>();
			Text->SetText(Value);
			auto Font = Text->GetFont();
			Font.Size = Size;
			Text->SetFont(Font);
			return Text;
		};
		const auto Section = [&Label, Rows](const FText& Title)
		{
			Rows->AddChildToVerticalBox(Label(Title, 15))->SetPadding(FMargin(0, 8, 0, 4));
		};
		const auto Row = [this, &Label, Rows](const FText& Title, UWidget* Field)
		{
			auto* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
			Rows->AddChildToVerticalBox(Line)->SetPadding(FMargin(0, 2));
			auto* Caption = Line->AddChildToHorizontalBox(Label(Title, 12));
			Caption->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			Caption->SetVerticalAlignment(VAlign_Center);
			auto* Value = Line->AddChildToHorizontalBox(Field);
			Value->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			Value->SetPadding(FMargin(8, 0, 0, 0));
		};

		Section(LOCTEXT("Display", "화면"));
		RenderScale = WidgetTree->ConstructWidget<UComboBoxString>();
		Row(LOCTEXT("RenderScale", "렌더 해상도 비율"), RenderScale);
		FrameLimit = WidgetTree->ConstructWidget<UComboBoxString>();
		Row(LOCTEXT("FrameLimit", "최대 프레임"), FrameLimit);
		VSync = WidgetTree->ConstructWidget<UCheckBox>();
		Row(LOCTEXT("VSync", "수직 동기화"), VSync);

		Section(LOCTEXT("GraphicsQuality", "그래픽 품질"));
		Preset = WidgetTree->ConstructWidget<UComboBoxString>();
		AddQualityOptions(*Preset);
		Row(LOCTEXT("Preset", "품질 일괄 선택"), Preset);
		Preset->OnSelectionChanged.AddDynamic(this, &ThisClass::SelectPreset);
		for (const auto& Setting : QualitySettings)
		{
			auto* Combo = WidgetTree->ConstructWidget<UComboBoxString>();
			AddQualityOptions(*Combo);
			Combo->OnSelectionChanged.AddDynamic(this, &ThisClass::SelectQuality);
			QualityRows.Add(Combo);
			Row(Setting.Label, Combo);
		}

		Section(LOCTEXT("Crowd", "다른 플레이어"));
		OtherEffectOpacity = WidgetTree->ConstructWidget<UComboBoxString>();
		Row(LOCTEXT("Opacity", "이펙트 불투명도"), OtherEffectOpacity);
		OptionalEffects = WidgetTree->ConstructWidget<UCheckBox>();
		Row(LOCTEXT("OptionalEffects", "장식 이펙트 표시"), OptionalEffects);

		auto* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
		Panel->AddChildToVerticalBox(Actions)->SetPadding(FMargin(0, 8, 0, 3));
		auto* ApplyButton = WidgetTree->ConstructWidget<UButton>();
		ApplyButton->SetContent(Label(LOCTEXT("Apply", "적용"), 13));
		ApplyButton->OnClicked.AddDynamic(this, &ThisClass::Apply);
		Actions->AddChildToHorizontalBox(ApplyButton)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		auto* RevertButton = WidgetTree->ConstructWidget<UButton>();
		RevertButton->SetContent(Label(LOCTEXT("Revert", "변경 취소"), 13));
		RevertButton->OnClicked.AddDynamic(this, &ThisClass::Revert);
		auto* RevertSlot = Actions->AddChildToHorizontalBox(RevertButton);
		RevertSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		RevertSlot->SetPadding(FMargin(8, 0, 0, 0));
		Status = Label(FText::GetEmpty(), 11);
		Status->SetAutoWrapText(true);
		Panel->AddChildToVerticalBox(Status);
		ReloadAppliedSettings();
	}
	return Super::RebuildWidget();
}

void UProject_JGraphicsSettingsWidget::ReloadAppliedSettings()
{
	const auto* Settings = UGameUserSettings::GetGameUserSettings();
	const auto* Crowd = UGameInstance::GetSubsystem<UProject_JGraphicsSettings>(GetGameInstance());
	if (!Settings || !Crowd || !Preset) return;
	for (int32 Index = 0; Index < QualityRows.Num(); ++Index)
	{
		QualityRows[Index]->SetSelectedIndex((Settings->*QualitySettings[Index].Read)());
	}
	SyncPresetSelection();
	float Normalized, Scale, MinScale, MaxScale;
	Settings->GetResolutionScaleInformationEx(Normalized, Scale, MinScale, MaxScale);
	RenderFactors.Reset();
	for (const float Value : {50.f, 60.f, 70.f, 80.f, 90.f, 100.f})
		if (Value >= MinScale && Value <= MaxScale) RenderFactors.Add(Value);
	PopulateNumbers(*RenderScale, RenderFactors, Scale, true);
	FrameLimits = {0.f, 30.f, 60.f, 90.f, 120.f, 144.f, 165.f, 240.f};
	PopulateNumbers(*FrameLimit, FrameLimits, Settings->GetFrameRateLimit(), false);
	LoadedOtherEffectOpacity = Crowd->GetEffectiveOtherPlayerEffectOpacity();
	bLoadedOptionalEffects = Crowd->GetEffectiveShowOtherPlayerOptionalEffects();
	bPresetSelectedSinceReload = false;
	Opacities = {0.f, 25.f, 50.f, 75.f, 100.f};
	PopulateNumbers(*OtherEffectOpacity, Opacities, 100.f * LoadedOtherEffectOpacity, true);
	VSync->SetIsChecked(Settings->IsVSyncEnabled());
	OptionalEffects->SetIsChecked(bLoadedOptionalEffects);
	Status->SetText(FText::GetEmpty());
}

void UProject_JGraphicsSettingsWidget::SelectPreset(FString, ESelectInfo::Type Selection)
{
	if (Selection == ESelectInfo::Direct) return;
	const int32 Index = Preset->GetSelectedIndex();
	if (Index == INDEX_NONE) return;
	bPresetSelectedSinceReload = true;
	for (UComboBoxString* Row : QualityRows) Row->SetSelectedIndex(Index);
}

void UProject_JGraphicsSettingsWidget::SelectQuality(FString, ESelectInfo::Type Selection)
{
	if (Selection != ESelectInfo::Direct) SyncPresetSelection();
}

void UProject_JGraphicsSettingsWidget::SyncPresetSelection()
{
	if (QualityRows.IsEmpty()) return;
	const int32 Index = QualityRows[0]->GetSelectedIndex();
	if (Index != INDEX_NONE && !QualityRows.ContainsByPredicate([Index](const auto& Row) { return Row->GetSelectedIndex() != Index; }))
	{
		Preset->SetSelectedIndex(Index);
	}
	else
	{
		Preset->ClearSelection();
	}
}

void UProject_JGraphicsSettingsWidget::Apply()
{
	auto* Settings = UGameUserSettings::GetGameUserSettings();
	auto* Crowd = UGameInstance::GetSubsystem<UProject_JGraphicsSettings>(GetGameInstance());
	if (!Settings || !Crowd || !Preset || !GetOwningLocalPlayer()) return;
	for (int32 Index = 0; Index < QualityRows.Num(); ++Index)
	{
		const int32 Selected = QualityRows[Index]->GetSelectedIndex();
		if (Selected != INDEX_NONE) (Settings->*QualitySettings[Index].Write)(Selected);
	}
	if (const int32 Selected = RenderScale->GetSelectedIndex(); RenderFactors.IsValidIndex(Selected))
		Settings->SetResolutionScaleValueEx(RenderFactors[Selected]);
	if (const int32 Selected = FrameLimit->GetSelectedIndex(); FrameLimits.IsValidIndex(Selected))
		Settings->SetFrameRateLimit(FrameLimits[Selected]);
	const float SelectedOpacity = (OtherEffectOpacity->GetSelectedIndex() != INDEX_NONE && Opacities.IsValidIndex(OtherEffectOpacity->GetSelectedIndex()))
		? (Opacities[OtherEffectOpacity->GetSelectedIndex()] / 100.f)
		: LoadedOtherEffectOpacity;
	const bool bSelectedOptional = OptionalEffects->IsChecked();
	const bool bCrowdEditedByUser = !FMath::IsNearlyEqual(SelectedOpacity, LoadedOtherEffectOpacity, 0.001f)
		|| bSelectedOptional != bLoadedOptionalEffects;
	Settings->SetVSyncEnabled(VSync->IsChecked());
	if (bCrowdEditedByUser)
	{
		Crowd->bUseCustomOtherPlayerEffects = true;
		Crowd->OtherPlayerEffectOpacity = SelectedOpacity;
		Crowd->bShowOtherPlayerOptionalEffects = bSelectedOptional;
	}
	else if (bPresetSelectedSinceReload)
	{
		// Switching overall graphics preset without editing crowd sliders follows DefaultScalability.ini.
		Crowd->bUseCustomOtherPlayerEffects = false;
	}
	Settings->ApplyNonResolutionSettings();
	Settings->SaveSettings();
	Crowd->ApplyAndSave();
	ReloadAppliedSettings();
	Status->SetText(LOCTEXT("Applied", "설정을 적용했습니다."));
}

void UProject_JGraphicsSettingsWidget::Revert()
{
	ReloadAppliedSettings();
}

#undef LOCTEXT_NAMESPACE
