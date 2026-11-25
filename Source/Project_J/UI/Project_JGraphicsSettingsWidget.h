#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "Project_JGraphicsSettingsWidget.generated.h"

class UCheckBox;
class UTextBlock;

/** Stages changes until Apply. Uses the game's existing HUD window for input and lifetime. */
UCLASS()
class PROJECT_J_API UProject_JGraphicsSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void ReloadAppliedSettings();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	friend class UProject_JPlayerUIComponent;

	UFUNCTION() void Apply();
	UFUNCTION() void Revert();
	UFUNCTION() void SelectPreset(FString Value, ESelectInfo::Type Selection);
	UFUNCTION() void SelectQuality(FString Value, ESelectInfo::Type Selection);
	void SyncPresetSelection();

	UPROPERTY() TArray<TObjectPtr<UComboBoxString>> QualityRows;
	UPROPERTY() TObjectPtr<UComboBoxString> Preset;
	UPROPERTY() TObjectPtr<UComboBoxString> RenderScale;
	UPROPERTY() TObjectPtr<UComboBoxString> FrameLimit;
	UPROPERTY() TObjectPtr<UComboBoxString> OtherEffectOpacity;
	UPROPERTY() TObjectPtr<UCheckBox> VSync;
	UPROPERTY() TObjectPtr<UCheckBox> OptionalEffects;
	UPROPERTY() TObjectPtr<UTextBlock> Status;
	TArray<float> RenderFactors;
	TArray<float> FrameLimits;
	TArray<float> Opacities;
	float LoadedOtherEffectOpacity = 1.f;
	bool bLoadedOptionalEffects = true;
	bool bPresetSelectedSinceReload = false;
};
