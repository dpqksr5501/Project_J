#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Project_JSourceAuditPhase3Receiver.generated.h"
class AProject_JMountCharacter;
UCLASS(Transient)
class UProject_JSourceAuditPhase3Receiver : public UObject
{
    GENERATED_BODY()
public:
    TFunction<void()> OnDismounted;
    UFUNCTION() void MountChanged(AProject_JMountCharacter* Previous, AProject_JMountCharacter* Current)
    { if (!Current && OnDismounted) OnDismounted(); }
};
