#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Project_JSocialStateInterface.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UProject_JSocialStateInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface to query social relations (party, guild membership)
 * used by replication policy filters.
 */
class PROJECT_JCORE_API IProject_JSocialStateInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "MMO|Social")
	FName GetPartyId() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "MMO|Social")
	FName GetGuildId() const;
};
