#include "MMO/Coordination.h"
#include "Misc/ScopeLock.h"

namespace ProjectJ::MMO
{
EAdmission FAggregateGate::TryAcquire(const TArray<FAggregateKey>& Keys, FGuid& OutTicket)
{
    OutTicket.Invalidate();
    if (Keys.IsEmpty() || Keys.Num() > 64) { return EAdmission::Invalid; }
    TSet<FAggregateKey> Unique;
    for (const auto& Key : Keys) { if (!Key.IsValid() || Unique.Contains(Key)) { return EAdmission::Invalid; } Unique.Add(Key); }
    FScopeLock Lock(&Mutex);
    if (bClosed) { return EAdmission::Closed; }
    if (Active.Num() >= Capacity) { return EAdmission::Capacity; }
    for (const auto& Key : Keys) { if (Owners.Contains(Key)) { return EAdmission::Busy; } }
    OutTicket = FGuid::NewGuid();
    for (const auto& Key : Keys) { Owners.Add(Key, OutTicket); }
    Active.Add(OutTicket, Keys);
    return EAdmission::Accepted;
}
bool FAggregateGate::Release(const FGuid& Ticket)
{
    FScopeLock Lock(&Mutex);
    const auto* Keys = Active.Find(Ticket);
    if (!Keys) { return false; }
    for (const auto& Key : *Keys) { Owners.Remove(Key); }
    Active.Remove(Ticket); return true;
}
void FAggregateGate::Close() { FScopeLock Lock(&Mutex); bClosed = true; }

EAdmission FRequestTracker::Begin(FGuid& OutTicket)
{
    FScopeLock Lock(&Mutex); OutTicket.Invalidate();
    if (bClosed) { return EAdmission::Closed; }
    if (Active.Num() >= Capacity) { return EAdmission::Capacity; }
    OutTicket = FGuid::NewGuid(); Active.Add(OutTicket); return EAdmission::Accepted;
}
bool FRequestTracker::Complete(const FGuid& Ticket)
{
    FScopeLock Lock(&Mutex);
    return !bClosed && Active.Remove(Ticket) == 1;
}
void FRequestTracker::Close() { FScopeLock Lock(&Mutex); bClosed = true; Active.Reset(); }
int32 FRequestTracker::Num() const { FScopeLock Lock(&Mutex); return Active.Num(); }

bool FServerSessionGate::SetCapacity(int32 InCapacity)
{
    FScopeLock Lock(&Mutex);
    if (bClosed || !Accounts.IsEmpty() || InCapacity < 1) return false;
    Capacity = InCapacity;
    return true;
}

EAdmission FServerSessionGate::Begin(const FGuid& AccountId, const FGuid& CharacterId, FServerSessionAttempt& OutAttempt)
{
    OutAttempt = {};
    if (!AccountId.IsValid() || !CharacterId.IsValid()) return EAdmission::Invalid;
    FScopeLock Lock(&Mutex);
    if (bClosed) return EAdmission::Closed;
    if (NextSerial == MAX_uint64 || (!Accounts.Contains(AccountId) && Accounts.Num() >= Capacity)) return EAdmission::Capacity;
    OutAttempt = {AccountId, CharacterId, FGuid::NewGuid(), ++NextSerial};
    Accounts.FindOrAdd(AccountId).Pending = OutAttempt;
    return EAdmission::Accepted;
}

EAdmission FServerSessionGate::Accept(const FServerSessionAttempt& Attempt, FServerSessionLease& OutLease, FServerSessionLease& OutReplaced)
{
    OutLease = {}; OutReplaced = {};
    FScopeLock Lock(&Mutex);
    if (bClosed) return EAdmission::Closed;
    auto* Slot = Accounts.Find(Attempt.AccountId);
    if (!Attempt.IsValid() || !Slot || !(Slot->Pending == Attempt)) return EAdmission::Invalid;
    if (const auto* Account = CharacterAccounts.Find(Attempt.CharacterId); Account && *Account != Attempt.AccountId) return EAdmission::Busy;
    OutReplaced = Slot->Current;
    if (OutReplaced.IsValid()) CharacterAccounts.Remove(OutReplaced.CharacterId);
    OutLease = {Attempt.AccountId, Attempt.CharacterId, Attempt.Ticket};
    Slot->Current = OutLease;
    Slot->Pending = {};
    CharacterAccounts.Add(OutLease.CharacterId, OutLease.AccountId);
    return EAdmission::Accepted;
}

bool FServerSessionGate::Cancel(const FServerSessionAttempt& Attempt)
{
    FScopeLock Lock(&Mutex);
    auto* Slot = Accounts.Find(Attempt.AccountId);
    if (bClosed || !Attempt.IsValid() || !Slot || !(Slot->Pending == Attempt)) return false;
    Slot->Pending = {};
    if (!Slot->Current.IsValid()) Accounts.Remove(Attempt.AccountId);
    return true;
}

bool FServerSessionGate::Release(const FServerSessionLease& Lease)
{
    FScopeLock Lock(&Mutex);
    auto* Slot = Accounts.Find(Lease.AccountId);
    if (bClosed || !Lease.IsValid() || !Slot || !(Slot->Current == Lease)) return false;
    CharacterAccounts.Remove(Lease.CharacterId);
    Slot->Current = {};
    if (!Slot->Pending.IsValid()) Accounts.Remove(Lease.AccountId);
    return true;
}

bool FServerSessionGate::IsCurrent(const FServerSessionLease& Lease) const
{
    FScopeLock Lock(&Mutex);
    const auto* Slot = Accounts.Find(Lease.AccountId);
    return !bClosed && Lease.IsValid() && Slot && Slot->Current == Lease;
}

void FServerSessionGate::Close()
{
    FScopeLock Lock(&Mutex);
    bClosed = true; Accounts.Reset(); CharacterAccounts.Reset();
}
}
