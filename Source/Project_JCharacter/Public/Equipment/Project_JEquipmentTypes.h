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

UENUM(BlueprintType)
enum class EProject_JEquipmentOperationFailure : uint8
{
	None,
	NotAuthority,
	InvalidRequest,
	InventoryUnavailable,
	ItemNotOwned,
	ItemLocked,
	InvalidDefinition,
	InvalidSlot,
	AlreadyEquipped,
	InventoryLockFailed
};

USTRUCT(BlueprintType)
struct FProject_JEquipmentOperationResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment")
	bool bSucceeded = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment")
	EProject_JEquipmentOperationFailure Failure = EProject_JEquipmentOperationFailure::InvalidRequest;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment")
	FGuid ItemInstanceId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equipment")
	EProject_JEquipmentSlot Slot = EProject_JEquipmentSlot::None;

	static FProject_JEquipmentOperationResult Success(FGuid InstanceId, EProject_JEquipmentSlot InSlot)
	{
		FProject_JEquipmentOperationResult Result;
		Result.bSucceeded = true;
		Result.Failure = EProject_JEquipmentOperationFailure::None;
		Result.ItemInstanceId = InstanceId;
		Result.Slot = InSlot;
		return Result;
	}

	static FProject_JEquipmentOperationResult FailureResult(
		EProject_JEquipmentOperationFailure FailureReason,
		FGuid InstanceId = FGuid(),
		EProject_JEquipmentSlot InSlot = EProject_JEquipmentSlot::None)
	{
		FProject_JEquipmentOperationResult Result;
		Result.Failure = FailureReason;
		Result.ItemInstanceId = InstanceId;
		Result.Slot = InSlot;
		return Result;
	}
};
