#pragma once
#include "EditorValidatorBase.h"
#include "Project_JMeshLODAssetValidator.generated.h"
UCLASS()
class UProject_JMeshLODAssetValidator : public UEditorValidatorBase
{
    GENERATED_BODY()
protected:
    virtual bool CanValidateAsset_Implementation(const FAssetData&,UObject*,FDataValidationContext&) const override;
    virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData&,UObject*,FDataValidationContext&) override;
};
