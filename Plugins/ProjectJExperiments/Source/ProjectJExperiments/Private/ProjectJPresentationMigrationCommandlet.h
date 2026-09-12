#pragma once
#include "Commandlets/Commandlet.h"
#include "ProjectJPresentationMigrationCommandlet.generated.h"

/** Explicit authoring operation; never invoked by automation tests or startup. */
UCLASS()
class UProjectJPresentationMigrationCommandlet final : public UCommandlet
{
    GENERATED_BODY()
public:
    virtual int32 Main(const FString& Params) override;
};
