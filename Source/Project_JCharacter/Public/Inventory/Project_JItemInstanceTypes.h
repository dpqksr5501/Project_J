#pragma once

#include "CoreMinimal.h"
#include "Project_JItemInstanceTypes.generated.h"

class UProject_JEquipmentItemDefinition;

USTRUCT(BlueprintType)
struct FProject_JItemInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	FGuid InstanceId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<UProject_JEquipmentItemDefinition> ItemDef = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "1"))
	int32 ItemLevel = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (ClampMin = "1"))
	int32 StackCount = 1;

	bool IsValid() const
	{
		return ItemDef != nullptr;
	}
};
