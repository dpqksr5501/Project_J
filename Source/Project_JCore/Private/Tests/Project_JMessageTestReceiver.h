#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Project_JMessageTestReceiver.generated.h"
class UProject_JMessageSubsystem;

// Private fixture; no runtime registration or ticks.
UCLASS()
class UProject_JMessageTestReceiver : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY() TObjectPtr<UProject_JMessageSubsystem> Router;
    FGameplayTag OtherChannel;
    int32 FirstCount = 0, SecondCount = 0;
    UFUNCTION() void ChangeSubscriptions(FGameplayTag Channel, UObject* Payload);
    UFUNCTION() void Count(FGameplayTag Channel, UObject* Payload);
};
