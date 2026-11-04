#pragma once
#include "DataValidationCommandlet.h"
#include "UObject/SoftObjectPath.h"
#include "Project_JAssetValidationCommandlet.generated.h"

/** Keep the engine's selection/validators; expose their actual completion counts. */
UCLASS()
class UProject_JAssetValidationCommandlet : public UDataValidationCommandlet
{
    GENERATED_BODY()
public:
    UProject_JAssetValidationCommandlet(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}
    virtual int32 Main(const FString& FullCommandLine) override;
protected:
    virtual bool GetAssetsToValidate(IAssetRegistry& AssetRegistry, TArray<FAssetData>& OutAssetDataList) override;
    virtual bool ProcessValidationResults(FValidateAssetsResults& Results) override;
private:
    bool bExplicitSelection = false;
    TArray<FSoftObjectPath> ExplicitAssetPaths;
};
