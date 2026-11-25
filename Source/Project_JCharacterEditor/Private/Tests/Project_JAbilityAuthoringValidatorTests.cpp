#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Validation/Project_JAbilityAuthoringAssetValidator.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Abilities/GameplayAbility.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAbilityAuthoringValidatorTest,
	"ProjectJ.Integrated.AbilityAuthoringValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAbilityAuthoringValidatorTest::RunTest(const FString&)
{
	auto* Validator = NewObject<UProject_JAbilityAuthoringAssetValidator>();

	UBlueprint* ProjectAbilityBP = FKismetEditorUtilities::CreateBlueprint(
		UProject_JGameplayAbility_Melee::StaticClass(), GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_TestProjectAbility")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!TestNotNull(TEXT("Create transient UProject_JGameplayAbility blueprint"), ProjectAbilityBP) ||
		!TestNotNull(TEXT("Compiled GeneratedClass exists"), ProjectAbilityBP->GeneratedClass.Get()))
	{
		return false;
	}

	auto Validate = [&](UObject* Asset) {
		FDataValidationContext Context;
		return Validator->ValidateLoadedAsset(FAssetData(Asset), Asset, Context);
	};

	TestEqual(TEXT("Instanced UProject_JGameplayAbility blueprint passes validation"),
		Validate(ProjectAbilityBP), EDataValidationResult::Valid);

	UObject* DefaultAbility = ProjectAbilityBP->GeneratedClass->GetDefaultObject();
	FByteProperty* InstancingProp = FindFProperty<FByteProperty>(UGameplayAbility::StaticClass(), TEXT("InstancingPolicy"));
	if (!TestNotNull(TEXT("DefaultAbility exists"), DefaultAbility) ||
		!TestNotNull(TEXT("InstancingPolicy property reflected"), InstancingProp))
	{
		return false;
	}
	// Value 0 corresponds to legacy NonInstanced; write via raw byte to avoid UE 5.8 deprecation warnings.
	InstancingProp->SetPropertyValue_InContainer(DefaultAbility, static_cast<uint8>(0));
	TestEqual(TEXT("NonInstanced UProject_JGameplayAbility blueprint fails validation"),
		Validate(ProjectAbilityBP), EDataValidationResult::Invalid);

	InstancingProp->SetPropertyValue_InContainer(DefaultAbility, static_cast<uint8>(EGameplayAbilityInstancingPolicy::InstancedPerExecution));
	TestEqual(TEXT("InstancedPerExecution UProject_JGameplayAbility blueprint passes validation"),
		Validate(ProjectAbilityBP), EDataValidationResult::Valid);

	UBlueprint* PlainAbilityBP = FKismetEditorUtilities::CreateBlueprint(
		UGameplayAbility::StaticClass(), GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_TestPlainAbility")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (TestNotNull(TEXT("Create transient plain UGameplayAbility blueprint"), PlainAbilityBP))
	{
		TestEqual(TEXT("Non-Project_JGameplayAbility blueprints are outside this validator scope"),
			Validate(PlainAbilityBP), EDataValidationResult::NotValidated);
	}
	return true;
}
#endif
