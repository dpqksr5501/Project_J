#include "UI/Project_JGraphicsSettings.h"

#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"

bool UProject_JGraphicsSettings::ShouldCreateSubsystem(UObject* Outer) const
{
	return FApp::CanEverRender() && Super::ShouldCreateSubsystem(Outer);
}

void UProject_JGraphicsSettings::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadConfig(GetClass(), *GGameUserSettingsIni);

	// Backward compatibility: existing saved profiles may store explicit opacity/optional values
	// without bUseCustomOtherPlayerEffects. Fresh profiles inherit DefaultScalability.ini presets.
	if (!bUseCustomOtherPlayerEffects && GConfig)
	{
		const FString Section = GetClass()->GetPathName();
		bool bExplicitFlagValue = false;
		const bool bHasExplicitFlag = GConfig->GetBool(*Section, TEXT("bUseCustomOtherPlayerEffects"), bExplicitFlagValue, GGameUserSettingsIni);
		if (!bHasExplicitFlag)
		{
			float DummyOpacity = 0.f;
			bool bDummyOptional = false;
			if (GConfig->GetFloat(*Section, TEXT("OtherPlayerEffectOpacity"), DummyOpacity, GGameUserSettingsIni)
				|| GConfig->GetBool(*Section, TEXT("bShowOtherPlayerOptionalEffects"), bDummyOptional, GGameUserSettingsIni))
			{
				bUseCustomOtherPlayerEffects = true;
			}
		}
	}

	ApplyCurrentPolicy();
}

void UProject_JGraphicsSettings::ApplyCurrentPolicy()
{
	if (bUseCustomOtherPlayerEffects)
	{
		Apply();
	}
	else
	{
		// Release any prior GameSetting override so DefaultScalability.ini [EffectsQuality@*] takes effect.
		auto& Console = IConsoleManager::Get();
		if (auto* Opacity = Console.FindConsoleVariable(TEXT("ProjectJ.Combat.Presentation.OtherPlayerOpacityScale")))
			Opacity->Unset(ECVF_SetByGameSetting);
		if (auto* Optional = Console.FindConsoleVariable(TEXT("ProjectJ.Combat.Presentation.OtherPlayerOptionalEffects")))
			Optional->Unset(ECVF_SetByGameSetting);
		SyncDisplayValuesFromActiveCVars();
	}
}

void UProject_JGraphicsSettings::ApplyAndSave()
{
	ApplyCurrentPolicy();
	SaveConfig(CPF_Config, *GGameUserSettingsIni);
}

void UProject_JGraphicsSettings::ResetOtherPlayerEffectsToPreset()
{
	bUseCustomOtherPlayerEffects = false;
	ApplyAndSave();
}

void UProject_JGraphicsSettings::SyncDisplayValuesFromActiveCVars()
{
	OtherPlayerEffectOpacity = GetEffectiveOtherPlayerEffectOpacity();
	bShowOtherPlayerOptionalEffects = GetEffectiveShowOtherPlayerOptionalEffects();
}

float UProject_JGraphicsSettings::GetEffectiveOtherPlayerEffectOpacity() const
{
	if (const auto* Opacity = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Combat.Presentation.OtherPlayerOpacityScale")))
	{
		const float Value = Opacity->GetFloat();
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, 1.f) : 1.f;
	}
	return FMath::IsFinite(OtherPlayerEffectOpacity) ? FMath::Clamp(OtherPlayerEffectOpacity, 0.f, 1.f) : 1.f;
}

bool UProject_JGraphicsSettings::GetEffectiveShowOtherPlayerOptionalEffects() const
{
	if (const auto* Optional = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Combat.Presentation.OtherPlayerOptionalEffects")))
	{
		return Optional->GetInt() != 0;
	}
	return bShowOtherPlayerOptionalEffects;
}

void UProject_JGraphicsSettings::Apply()
{
	OtherPlayerEffectOpacity = FMath::IsFinite(OtherPlayerEffectOpacity)
		? FMath::Clamp(OtherPlayerEffectOpacity, 0.f, 1.f) : 1.f;
	auto& Console = IConsoleManager::Get();
	if (auto* Opacity = Console.FindConsoleVariable(TEXT("ProjectJ.Combat.Presentation.OtherPlayerOpacityScale")))
		Opacity->Set(OtherPlayerEffectOpacity, ECVF_SetByGameSetting);
	if (auto* Optional = Console.FindConsoleVariable(TEXT("ProjectJ.Combat.Presentation.OtherPlayerOptionalEffects")))
		Optional->Set(bShowOtherPlayerOptionalEffects ? 1 : 0, ECVF_SetByGameSetting);
}
