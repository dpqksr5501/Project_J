#pragma once

#include "IpNetDriver.h"
#include "Project_JIpNetDriver.generated.h"

/** Applies the installed replication system's capacity before socket admission allocates an ID. */
UCLASS(Transient, Config=Engine)
class UProject_JIpNetDriver : public UIpNetDriver
{
	GENERATED_BODY()

public:
	virtual void TickDispatch(float DeltaSeconds) override;
	int32 GetIncomingCapacity() const;
};
