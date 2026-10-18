#pragma once
#include "DataValidationCommandlet.h"
#include "Project_JAssetValidationCommandlet.generated.h"

/** Keep the engine's selection/validators; expose their actual completion counts. */
UCLASS()
class UProject_JAssetValidationCommandlet : public UDataValidationCommandlet
{
    GENERATED_BODY()
public:
    UProject_JAssetValidationCommandlet(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}
protected:
    virtual bool ProcessValidationResults(FValidateAssetsResults& Results) override;
};
