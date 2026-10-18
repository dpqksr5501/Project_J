#pragma once
#include "CoreMinimal.h"
#include "Components/Project_JInventoryComponent.h"
#include "Project_JSourceAuditPhase2UIReceiver.generated.h"

UCLASS(Transient)
class UProject_JSourceAuditPhase2UIReceiver : public UObject
{
    GENERATED_BODY()
public:
    TFunction<void()> OnConsumption;
    bool bConsumedCallback = false;
    int32 Replies = 0;
    EProject_JItemUseResult LastResult = EProject_JItemUseResult::Invalid;
    UFUNCTION() void OnItem(const FProject_JItemInstanceData& Item)
    {
        if (!bConsumedCallback) { bConsumedCallback = true; if (OnConsumption) OnConsumption(); }
    }
    UFUNCTION() void OnReply(FGuid RequestId, EProject_JItemUseResult Result) { ++Replies; LastResult = Result; }
};
