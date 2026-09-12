#pragma once
#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

namespace ProjectJ::MMO
{
// Namespace prevents a character ID and guild ID from sharing a lock by accident.
struct FAggregateKey
{
    FName Kind;
    FGuid Id;
    bool IsValid() const { return !Kind.IsNone() && Id.IsValid(); }
    bool operator==(const FAggregateKey& Other) const { return Kind == Other.Kind && Id == Other.Id; }
    friend uint32 GetTypeHash(const FAggregateKey& Key) { return HashCombine(GetTypeHash(Key.Kind), GetTypeHash(Key.Id)); }
};

enum class EAdmission : uint8 { Accepted, Invalid, Busy, Capacity, Closed };

/** Nonblocking, process-local reservations. Independent owners may run in parallel.
 * No callbacks, tasks, UObject access or I/O under the lock. Not a distributed lock.
 * Keep a reservation until work actually completes, including cancellation acknowledgement.
 */
class PROJECT_JMMO_API FAggregateGate
{
public:
    explicit FAggregateGate(int32 InCapacity = 128) : Capacity(FMath::Max(1, InCapacity)) {}
    EAdmission TryAcquire(const TArray<FAggregateKey>& Keys, FGuid& OutTicket);
    bool Release(const FGuid& Ticket);
    void Close(); // Stops admission; does not unlock work that is still running.
private:
    FCriticalSection Mutex;
    const int32 Capacity;
    bool bClosed = false;
    TMap<FAggregateKey, FGuid> Owners;
    TMap<FGuid, TArray<FAggregateKey>> Active;
};

/** Bounded lifetime/at-most-once completion gate for asynchronous adapters.
 * A closed instance cannot be reopened. New sessions use a new tracker.
 */
class PROJECT_JMMO_API FRequestTracker
{
public:
    explicit FRequestTracker(int32 InCapacity = 128) : Capacity(FMath::Max(1, InCapacity)) {}
    EAdmission Begin(FGuid& OutTicket);
    bool Complete(const FGuid& Ticket);
    void Close();
    int32 Num() const;
private:
    mutable FCriticalSection Mutex;
    const int32 Capacity;
    bool bClosed = false;
    TSet<FGuid> Active;
};
}
