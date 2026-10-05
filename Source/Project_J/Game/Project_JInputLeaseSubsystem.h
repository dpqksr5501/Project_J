#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Project_JInputLeaseSubsystem.generated.h"

class UInputMappingContext;

/** Controller replacement shares contexts by owner. Pre-existing mappings remain externally owned. */
UCLASS()
class PROJECT_J_API UProject_JInputLeaseSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()
public:
	bool Acquire(UObject* Owner, UInputMappingContext* Context);
	void Release(UObject* Owner);
	virtual void Deinitialize() override;
private:
	struct FLease { TSet<TWeakObjectPtr<UObject>> Owners; bool bAddedMapping = false; };
	TMap<TWeakObjectPtr<UInputMappingContext>, FLease> Leases;
	bool bAccepting = true;
	void PruneOwners();
};
