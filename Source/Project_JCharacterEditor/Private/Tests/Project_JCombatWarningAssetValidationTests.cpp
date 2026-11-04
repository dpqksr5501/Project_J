#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Validation/Project_JCombatWarningAssetValidator.h"
#include "Animation/AnimMontage.h"
#include "Animation/Project_JAnimNotifyState_CombatPresentationCue.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCombatWarningAssetValidationTest,
	"ProjectJ.Integrated.GameplayWarningAssetValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCombatWarningAssetValidationTest::RunTest(const FString&)
{
	auto* Montage = NewObject<UAnimMontage>();
	auto* Cue = NewObject<UProject_JAnimNotifyState_CombatPresentationCue>(Montage);
	auto& Event = Montage->Notifies.AddDefaulted_GetRef();
	Event.NotifyStateClass = Cue;
	Event.SetDuration(0.5f);
	auto* Purpose = FindFProperty<FEnumProperty>(Cue->GetClass(), TEXT("Purpose"));
	auto* Tag = FindFProperty<FStructProperty>(Cue->GetClass(), TEXT("CueTag"));
	if (!TestNotNull(TEXT("Authored purpose is reflected"), Purpose) || !TestNotNull(TEXT("Authored tag is reflected"), Tag)) return false;
	Purpose->GetUnderlyingProperty()->SetIntPropertyValue(Purpose->ContainerPtrToValuePtr<void>(Cue), static_cast<uint64>(EProject_JCombatCuePurpose::GameplayWarning));
	// Reuse the runtime test tag; Editor modules must never add native network tags.
	*Tag->ContainerPtrToValuePtr<FGameplayTag>(Cue) = FGameplayTag::RequestGameplayTag(FName(TEXT("ProjectJ.Tests.Presentation.Cue")));
	Event.bCanBeFilteredViaRequest = false;
	auto* Validator = NewObject<UProject_JCombatWarningAssetValidator>();
	const auto Validate = [&]()
	{
		FDataValidationContext Context;
		return Validator->ValidateLoadedAsset(FAssetData(Montage), Montage, Context);
	};
	TestEqual(TEXT("Deterministic server warning is valid"), Validate(), EDataValidationResult::Valid);
	Event.bTriggerOnDedicatedServer = false;
	TestEqual(TEXT("Server-suppressed warning is rejected at its parent asset"), Validate(), EDataValidationResult::Invalid);
	Event.bTriggerOnDedicatedServer = true;
	Event.NotifyTriggerChance = 0.5f;
	TestEqual(TEXT("Probabilistic warning cannot silently disappear"), Validate(), EDataValidationResult::Invalid);
	Event.NotifyTriggerChance = 1.f;
	Event.NotifyFilterType = ENotifyFilterType::LOD;
	TestEqual(TEXT("LOD cannot suppress authority warning production"), Validate(), EDataValidationResult::Invalid);
	Event.NotifyFilterType = ENotifyFilterType::NoFiltering;
	Event.bCanBeFilteredViaRequest = true;
	TestEqual(TEXT("Request-filtered producer is rejected"), Validate(), EDataValidationResult::Invalid);
	Event.bCanBeFilteredViaRequest = false;
	Event.SetDuration(0.f);
	TestEqual(TEXT("Empty warning interval is rejected"), Validate(), EDataValidationResult::Invalid);
	Event.SetDuration(0.5f);
	*Tag->ContainerPtrToValuePtr<FGameplayTag>(Cue) = FGameplayTag();
	TestEqual(TEXT("Warning must resolve a runtime cue key"), Validate(), EDataValidationResult::Invalid);
	Purpose->GetUnderlyingProperty()->SetIntPropertyValue(Purpose->ContainerPtrToValuePtr<void>(Cue), static_cast<uint64>(EProject_JCombatCuePurpose::Cosmetic));
	TestEqual(TEXT("Cosmetic assets are outside this validator's warning contract"), Validate(), EDataValidationResult::NotValidated);
	return true;
}
#endif
