#pragma once
#include "EditorValidatorBase.h"
#include "Project_JWeaponMotionAssetValidator.generated.h"

/** Engine animation assets own notify subobjects; validate their runtime numeric contract at the asset boundary. */
UCLASS()
class UProject_JWeaponMotionAssetValidator : public UEditorValidatorBase
{
	GENERATED_BODY()
protected:
	virtual bool CanValidateAsset_Implementation(const FAssetData& AssetData, UObject* Object, FDataValidationContext& Context) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& AssetData, UObject* Object, FDataValidationContext& Context) override;
};
