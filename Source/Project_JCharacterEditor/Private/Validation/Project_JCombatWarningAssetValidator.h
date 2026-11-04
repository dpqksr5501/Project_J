#pragma once

#include "EditorValidatorBase.h"
#include "Project_JCombatWarningAssetValidator.generated.h"

/** A gameplay warning must not inherit cosmetic notify delivery filters. */
UCLASS()
class UProject_JCombatWarningAssetValidator : public UEditorValidatorBase
{
	GENERATED_BODY()
protected:
	virtual bool CanValidateAsset_Implementation(const FAssetData& AssetData, UObject* Object, FDataValidationContext& Context) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& AssetData, UObject* Object, FDataValidationContext& Context) override;
};
