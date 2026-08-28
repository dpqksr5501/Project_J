#pragma once

#include "CoreMinimal.h"
#include "Optimization/Project_JObjectPoolTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JObjectPoolRegistrySubsystem.generated.h"

/**
 * Project-wide registry for future pools. This is deliberately not a pool
 * implementation: it stores policy definitions only, so adding it does not alter
 * spawn/despawn behaviour or network ownership.
 */
UCLASS()
class PROJECT_JCORE_API UProject_JObjectPoolRegistrySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	bool RegisterDefinition(const FProject_JObjectPoolDefinition& Definition);
	bool UnregisterDefinition(FName PoolId);
	const FProject_JObjectPoolDefinition* FindDefinition(FName PoolId) const;
	const TMap<FName, FProject_JObjectPoolDefinition>& GetDefinitions() const { return Definitions; }

private:
	TMap<FName, FProject_JObjectPoolDefinition> Definitions;
};
