#pragma once

#include "CoreMinimal.h"
#include "Project_JEquipmentTypes.generated.h"

UENUM(BlueprintType)
enum class EProject_JEquipmentSlot : uint8
{
	None UMETA(DisplayName = "None"),
	Weapon UMETA(DisplayName = "Weapon"),
	Head UMETA(DisplayName = "Head"),
	Chest UMETA(DisplayName = "Chest"),
	Hands UMETA(DisplayName = "Hands"),
	Legs UMETA(DisplayName = "Legs"),
	Feet UMETA(DisplayName = "Feet"),
	Accessory UMETA(DisplayName = "Accessory"),
	Back UMETA(DisplayName = "Back"),
	Mount UMETA(DisplayName = "Mount")
};
