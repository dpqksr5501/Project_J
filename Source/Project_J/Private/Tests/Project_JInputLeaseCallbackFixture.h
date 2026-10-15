#pragma once
#include "CoreMinimal.h"
#include "Project_JInputLeaseCallbackFixture.generated.h"

class UProject_JInputLeaseSubsystem;
class UInputMappingContext;

UCLASS(NotBlueprintable, Transient)
class UProject_JInputLeaseCallbackFixture : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY() TObjectPtr<UProject_JInputLeaseSubsystem> Leases;
    TWeakObjectPtr<UObject> Subject;
    int32 Mode = 0;
    bool bObservedLiveSubject = false;
    UFUNCTION() void MappingAdded(const UInputMappingContext* Context);
};
