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

/** Server-issued before asynchronous credential/character-ownership verification.
 * Values are process-local capabilities, never client-authentication claims.
 */
struct FServerSessionAttempt
{
    FGuid AccountId, CharacterId, Ticket;
    uint64 Serial = 0;
    bool IsValid() const { return AccountId.IsValid() && CharacterId.IsValid() && Ticket.IsValid() && Serial != 0; }
    bool operator==(const FServerSessionAttempt& Other) const
    { return AccountId == Other.AccountId && CharacterId == Other.CharacterId && Ticket == Other.Ticket && Serial == Other.Serial; }
};

/** Runtime writer identity, independent from the current Avatar and persisted data. */
struct FServerSessionLease
{
    FGuid AccountId, CharacterId, Epoch;
    bool IsValid() const { return AccountId.IsValid() && CharacterId.IsValid() && Epoch.IsValid(); }
    bool operator==(const FServerSessionLease& Other) const
    { return AccountId == Other.AccountId && CharacterId == Other.CharacterId && Epoch == Other.Epoch; }
};

/** One accepted character per account and one writer per character on this host.
 * Begin replaces only the pending attempt, preserving any accepted lease until
 * verified acceptance. An older completion cannot replace a newer Begin.
 * Callers own UObject cleanup; no callbacks, tasks or I/O run under this lock.
 * This is not credential verification or a distributed session repository.
 */
class PROJECT_JMMO_API FServerSessionGate
{
public:
    explicit FServerSessionGate(int32 InCapacity = 128) : Capacity(FMath::Max(1, InCapacity)) {}
    /** Startup configuration only: refuses a populated or closed gate. No allocation or eviction. */
    bool SetCapacity(int32 InCapacity);
    EAdmission Begin(const FGuid& AccountId, const FGuid& CharacterId, FServerSessionAttempt& OutAttempt);
    EAdmission Accept(const FServerSessionAttempt& Attempt, FServerSessionLease& OutLease, FServerSessionLease& OutReplaced);
    bool Cancel(const FServerSessionAttempt& Attempt);
    bool Release(const FServerSessionLease& Lease);
    bool IsCurrent(const FServerSessionLease& Lease) const;
    void Close();
private:
    struct FAccountSlot { FServerSessionAttempt Pending; FServerSessionLease Current; };
    mutable FCriticalSection Mutex;
    int32 Capacity;
    bool bClosed = false;
    uint64 NextSerial = 0;
    TMap<FGuid, FAccountSlot> Accounts;
    TMap<FGuid, FGuid> CharacterAccounts;
};
}
