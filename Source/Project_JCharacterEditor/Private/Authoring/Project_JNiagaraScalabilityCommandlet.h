#pragma once
#include "Commandlets/Commandlet.h"
#include "Project_JNiagaraScalabilityCommandlet.generated.h"

/** Explicit owned-loop scalability authoring. Default is read-only; only -Apply saves the three recipe packages. */
UCLASS()
class UProject_JNiagaraScalabilityCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UProject_JNiagaraScalabilityCommandlet();
    virtual int32 Main(const FString& Params) override;
};
