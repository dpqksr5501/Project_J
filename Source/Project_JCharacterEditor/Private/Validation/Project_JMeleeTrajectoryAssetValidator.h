#pragma once
#include "EditorValidatorBase.h"
#include "Project_JMeleeTrajectoryAssetValidator.generated.h"

/** Enabled gameplay paths retain their exact offline source and sampling policy. */
UCLASS()
class UProject_JMeleeTrajectoryAssetValidator : public UEditorValidatorBase
{
    GENERATED_BODY()
protected:
    virtual bool CanValidateAsset_Implementation(const FAssetData& AssetData, UObject* Object, FDataValidationContext& Context) const override;
    virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& AssetData, UObject* Object, FDataValidationContext& Context) override;
};
