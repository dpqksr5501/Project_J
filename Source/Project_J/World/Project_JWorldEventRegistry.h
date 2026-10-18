#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Project_JWorldEventRegistry.generated.h"

/** Authority-local uniqueness, not a second event state store or persistent service. */
UCLASS()
class PROJECT_J_API UProject_JWorldEventRegistry : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    bool Register(FName Id, AActor* Owner);
    void Unregister(FName Id, AActor* Owner);
    virtual void Deinitialize() override;
private:
    TMap<FName,TWeakObjectPtr<AActor>> Owners;
};
