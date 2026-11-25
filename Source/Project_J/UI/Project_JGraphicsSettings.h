#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JGraphicsSettings.generated.h"

/** Project-specific client presentation preferences in GameUserSettings.ini. */
UCLASS(Config=GameUserSettings)
class PROJECT_J_API UProject_JGraphicsSettings : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	void ApplyCurrentPolicy();
	void ApplyAndSave();
	void ResetOtherPlayerEffectsToPreset();
	void SyncDisplayValuesFromActiveCVars();

	float GetEffectiveOtherPlayerEffectOpacity() const;
	bool GetEffectiveShowOtherPlayerOptionalEffects() const;

	UPROPERTY(Config)
	bool bUseCustomOtherPlayerEffects = false;

	UPROPERTY(Config)
	float OtherPlayerEffectOpacity = 1.f;

	UPROPERTY(Config)
	bool bShowOtherPlayerOptionalEffects = true;

private:
	void Apply();
};
