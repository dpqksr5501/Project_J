#pragma once
#include "Commandlets/Commandlet.h"
#include "Project_JMeshLODCommandlet.generated.h"

/** Explicit bounded asset list. Apply is isolated from live editor sessions and commits only validated staged packages. */
UCLASS()
class UProject_JMeshLODCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UProject_JMeshLODCommandlet();
    virtual int32 Main(const FString& Params) override;
};
