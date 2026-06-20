#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Project_JItemDefinition.generated.h"

class UTexture2D;

/**
 * Base abstract Primary Data Asset representing any kind of item in Project J.
 * Derived classes define specific types of items (e.g., Equipment, Consumable).
 */
UCLASS(BlueprintType, Abstract, Const)
class PROJECT_JCHARACTER_API UProject_JItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// Unique identifier for items of this type
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	FName ItemId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	FText ItemName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item", meta = (MultiLine = true))
	FText ItemDescription;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item", meta = (ClampMin = "1"))
	int32 MaxStackCount = 1;
};
