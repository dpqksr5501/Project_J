#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Project_JHandoverSerializable.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UProject_JHandoverSerializable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface to serialize/deserialize critical actor states (e.g., Inventory, GAS Cooldowns)
 * during seamless server-to-server handover (Area of Interest crossing).
 * Pillar 3: Seamless Server Meshing.
 */
class PROJECT_JCHARACTER_API IProject_JHandoverSerializable
{
	GENERATED_BODY()

public:
	/**
	 * Serialize the object's core state into a compact byte array (target < 1KB).
	 * @param OutData The binary payload to be sent to the adjacent server.
	 */
	virtual void SerializeForHandover(TArray<uint8>& OutData) = 0;

	/**
	 * Deserialize the object's core state from the byte array on the new authority server.
	 * @param InData The binary payload received from the previous server.
	 */
	virtual void DeserializeFromHandover(const TArray<uint8>& InData) = 0;
};
