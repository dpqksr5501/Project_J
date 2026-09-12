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
}
