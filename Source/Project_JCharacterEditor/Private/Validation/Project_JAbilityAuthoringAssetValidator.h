#pragma once

#include "EditorValidatorBase.h"
#include "Project_JAbilityAuthoringAssetValidator.generated.h"

/**
 * Ensures Blueprint subclasses of UProject_JGameplayAbility keep an instanced
 * policy so UProject_JAbilitySystemComponent client execution payload admission
 * and handle ownership never break silently at runtime.
 */
UCLASS()
class UProject_JAbilityAuthoringAssetValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

protected:
	virtual bool CanValidateAsset_Implementation(const FAssetData& AssetData, UObject* Object, FDataValidationContext& Context) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& AssetData, UObject* Object, FDataValidationContext& Context) override;
};
