#pragma once

#include "CoreMinimal.h"
#include "Inventory/Project_JItemDefinition.h"
#include "Project_JMountItemDefinition.generated.h"

class AProject_JMountCharacter;

/** Inventory item that may spawn one specific mount class for its owner. */
UCLASS(BlueprintType, Const)
class PROJECT_JCHARACTER_API UProject_JMountItemDefinition : public UProject_JItemDefinition
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount")
	TSoftClassPtr<AProject_JMountCharacter> MountClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount", meta = (ClampMin = "0.0"))
	float SpawnDistance = 180.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mount")
	bool bAutoMountAfterSpawn = true;
};
