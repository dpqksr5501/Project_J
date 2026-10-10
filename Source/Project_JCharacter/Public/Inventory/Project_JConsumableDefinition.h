#pragma once
#include "Inventory/Project_JItemDefinition.h"
#include "Project_JConsumableDefinition.generated.h"

/** Server-authored restorative item. The client submits only an owned instance ID. */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JConsumableDefinition : public UProject_JItemDefinition
{
 GENERATED_BODY()
public:
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) float RestoreHealth = 0;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) float RestoreMana = 0;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0.1")) float CooldownSeconds = 10;
};
