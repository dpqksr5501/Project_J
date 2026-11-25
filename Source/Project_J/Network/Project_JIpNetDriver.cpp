#include "Network/Project_JIpNetDriver.h"

#include "Engine/NetworkDelegates.h"
#include "Misc/ScopeExit.h"
#if UE_WITH_IRIS
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#endif

namespace
{
class FIncomingCapacityNotify final : public FNetworkNotify
{
public:
	FIncomingCapacityNotify(UProject_JIpNetDriver& InDriver, FNetworkNotify& InNext, int32 InCapacity)
		: Driver(InDriver), Next(InNext), Capacity(InCapacity) {}

	EAcceptConnection::Type NotifyAcceptingConnection() override
	{
		return Driver.ClientConnections.Num() < Capacity ? Next.NotifyAcceptingConnection() : EAcceptConnection::Ignore;
	}
	void NotifyAcceptedConnection(UNetConnection* Connection) override { Next.NotifyAcceptedConnection(Connection); }
	bool NotifyAcceptingChannel(UChannel* Channel) override { return Next.NotifyAcceptingChannel(Channel); }
	void NotifyControlMessage(UNetConnection* Connection, uint8 Type, FInBunch& Data) override
	{
		Next.NotifyControlMessage(Connection, Type, Data);
	}

private:
	UProject_JIpNetDriver& Driver;
	FNetworkNotify& Next;
	int32 Capacity;
};
}

int32 UProject_JIpNetDriver::GetIncomingCapacity() const
{
#if UE_WITH_IRIS
	if (const auto* Replication = GetReplicationSystem())
		return FMath::Max(0, static_cast<int32>(Replication->GetMaxConnectionCount()) - 1); // Connection ID zero is reserved.
#endif
	return MAX_int32;
}

void UProject_JIpNetDriver::TickDispatch(float DeltaSeconds)
{
	const int32 Capacity = GetIncomingCapacity();
	if (!Notify || ServerConnection || Capacity == MAX_int32)
	{
		Super::TickDispatch(DeltaSeconds);
		return;
	}
	FNetworkNotify* Original = Notify;
	FIncomingCapacityNotify Capped(*this, *Original, Capacity);
	Notify = &Capped;
	ON_SCOPE_EXIT { if (Notify == &Capped) Notify = Original; };
	Super::TickDispatch(DeltaSeconds);
}
