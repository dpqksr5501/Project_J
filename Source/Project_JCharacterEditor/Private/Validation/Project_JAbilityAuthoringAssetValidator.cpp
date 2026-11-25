#include "Validation/Project_JAbilityAuthoringAssetValidator.h"
#include "Engine/Blueprint.h"
#include "Misc/DataValidation.h"
#include "Project_JGameplayAbility.h"
#include "UObject/UnrealType.h"

bool UProject_JAbilityAuthoringAssetValidator::CanValidateAsset_Implementation(const FAssetData&, UObject* Object, FDataValidationContext&) const
{
	const UBlueprint* Blueprint = Cast<UBlueprint>(Object);
	if (!Blueprint || !Blueprint->GeneratedClass) return false;
	const UClass* GeneratedClass = Blueprint->GeneratedClass;
	if (GeneratedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) return false;
	return GeneratedClass->IsChildOf(UProject_JGameplayAbility::StaticClass());
}

EDataValidationResult UProject_JAbilityAuthoringAssetValidator::ValidateLoadedAsset_Implementation(const FAssetData&, UObject* Object, FDataValidationContext&)
{
	const UBlueprint* Blueprint = CastChecked<UBlueprint>(Object);
	const UClass* GeneratedClass = Blueprint->GeneratedClass;
	const UProject_JGameplayAbility* DefaultAbility = GeneratedClass ? Cast<UProject_JGameplayAbility>(GeneratedClass->GetDefaultObject(false)) : nullptr;
	if (!DefaultAbility)
	{
		AssetFails(Object, NSLOCTEXT("ProjectJAbilityAuthoringValidator", "MissingCDO",
			"Project_JGameplayAbility blueprint has no valid generated class default object. Compile and save the blueprint."));
		return EDataValidationResult::Invalid;
	}

	// Read the authored InstancingPolicy directly so legacy NonInstanced assets are caught
	// even when UE 5.8 GetInstancingPolicy() coerces NonInstanced at runtime.
	uint8 AuthoredPolicy = static_cast<uint8>(DefaultAbility->GetInstancingPolicy());
	if (const FByteProperty* InstancingProp = FindFProperty<FByteProperty>(UGameplayAbility::StaticClass(), TEXT("InstancingPolicy")))
	{
		AuthoredPolicy = InstancingProp->GetPropertyValue_InContainer(DefaultAbility);
	}
	if (AuthoredPolicy != static_cast<uint8>(EGameplayAbilityInstancingPolicy::InstancedPerActor) &&
		AuthoredPolicy != static_cast<uint8>(EGameplayAbilityInstancingPolicy::InstancedPerExecution))
	{
		AssetFails(Object, NSLOCTEXT("ProjectJAbilityAuthoringValidator", "NonInstanced",
			"UProject_JGameplayAbility blueprints must author InstancedPerActor or InstancedPerExecution so native client execution payload admission and active-handle tracking remain valid."));
		return EDataValidationResult::Invalid;
	}

	AssetPasses(Object);
	return EDataValidationResult::Valid;
}
