#include "System/Project_JObjectPoolRegistrySubsystem.h"

bool UProject_JObjectPoolRegistrySubsystem::RegisterDefinition(const FProject_JObjectPoolDefinition& Definition)
{
	if (Definition.PoolId.IsNone() || Definition.MaxRetainedCount < Definition.PrewarmCount)
	{
		return false;
	}

	Definitions.Add(Definition.PoolId, Definition);
	return true;
}

bool UProject_JObjectPoolRegistrySubsystem::UnregisterDefinition(FName PoolId)
{
	return !PoolId.IsNone() && Definitions.Remove(PoolId) > 0;
}

const FProject_JObjectPoolDefinition* UProject_JObjectPoolRegistrySubsystem::FindDefinition(FName PoolId) const
{
	return Definitions.Find(PoolId);
}
