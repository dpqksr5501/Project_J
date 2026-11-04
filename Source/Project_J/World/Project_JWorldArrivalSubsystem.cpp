#include "World/Project_JWorldArrivalSubsystem.h"
#include "World/Project_JWorldStreamingComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/Guid.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/StrongObjectPtr.h"

bool UProject_JWorldArrivalSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type == EWorldType::Game || Type == EWorldType::PIE;
}
void UProject_JWorldArrivalSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Entries.Reserve(MaxPending); bAccepting = true;
    TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &ThisClass::OnTearDown);
}
FName UProject_JWorldArrivalSubsystem::AllocateStreamingSourceIdentity()
{
    check(IsInGameThread());
    if (!bAccepting || !IsInitialized() || !GetWorld() || GetWorld()->bIsTearingDown || LastStreamingSourceNumber == MAX_int32) return NAME_None;
    // One base string per world lifetime; positive internal numbers avoid a new
    // permanent full-path string for every registered provider on outline0 targets.
    if (StreamingSourceNamespace.IsNone())
        StreamingSourceNamespace = FName(*FString::Printf(TEXT("ProjectJWorldPreparation_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    return FName(StreamingSourceNamespace, ++LastStreamingSourceNumber);
}
uint64 UProject_JWorldArrivalSubsystem::Register(UProject_JWorldStreamingComponent* Component, uint64 Request)
{
    check(IsInGameThread());
    if (!bAccepting || !IsInitialized() || !GetWorld() || GetWorld()->bIsTearingDown || !IsValid(Component)
        || Component->IsTemplate() || Component->IsBeingDestroyed() || !Component->IsRegistered() || Component->GetWorld() != GetWorld()
        || Component->StreamingSourceIdentity.IsNone()
        || !Request || Component->ArrivalRequest != Request || Component->ArrivalExecutionToken
        || Entries.Num() >= MaxPending || LastToken == MAX_uint64)
    { ++Stats.Rejected; return 0; }
    const uint64 Token = ++LastToken;
    FEntry Entry; Entry.Component = Component; Entry.Request = Request;
    // Immediate first eligibility; original request age/deadline remain component-owned.
    Entry.NextDue = FPlatformTime::Seconds();
    if (!Head) { Entry.Previous = Entry.Next = Head = Cursor = Token; }
    else
    {
        const uint64 Tail = Entries.FindChecked(Head).Previous;
        Entry.Previous = Tail; Entry.Next = Head;
        Entries.FindChecked(Tail).Next = Token; Entries.FindChecked(Head).Previous = Token;
    }
    Entries.Add(Token, Entry); ++Stats.Registered;
    Stats.Pending = Entries.Num(); Stats.PeakPending = FMath::Max(Stats.PeakPending, Stats.Pending);
    return Token;
}
bool UProject_JWorldArrivalSubsystem::HasPreparationPermit(uint64 Token, const UProject_JWorldStreamingComponent* Component, uint64 Request) const
{
    check(IsInGameThread());
    if (!bAccepting || !IsInitialized() || !GetWorld() || GetWorld()->bIsTearingDown || !IsValid(Component)
        || !Component->IsRegistered() || Component->IsBeingDestroyed() || Component->GetWorld() != GetWorld()
        || !Token || !Request || Component->ArrivalExecutionOwner.Get() != this || Component->ArrivalExecutionToken != Token
        || Component->ArrivalRequest != Request) return false;
    const FEntry* Entry = Entries.Find(Token);
    return Entry && Entry->bPreparationAdmitted && Entry->Request == Request && Entry->Component.Get() == Component;
}
bool UProject_JWorldArrivalSubsystem::TryAdmitPreparation(uint64 Token, UProject_JWorldStreamingComponent* Component, uint64 Request, int32 Limit)
{
    check(IsInGameThread());
    if (!bAccepting || !IsInitialized() || !GetWorld() || GetWorld()->bIsTearingDown || !IsValid(Component)
        || !Component->IsRegistered() || Component->IsBeingDestroyed() || Component->GetWorld() != GetWorld()
        || !Token || !Request || Component->ArrivalExecutionOwner.Get() != this || Component->ArrivalExecutionToken != Token
        || Component->ArrivalRequest != Request) return false;
    auto* Controller = Cast<APlayerController>(Component->GetOwner());
    if (!IsValid(Controller) || !Controller->HasAuthority() || Controller->IsActorBeingDestroyed()) return false;
    FEntry* Entry = Entries.Find(Token);
    if (!Entry || Entry->Request != Request || Entry->Component.Get() != Component) return false;
    if (Entry->bPreparationAdmitted) return true; // Policy shrink never revokes an existing grant.
    if (Component->ArrivalPhase != EProject_JWorldArrivalPhase::Preparing || Limit < 1 || Limit > MaxPending) return false;
    if (Stats.ActivePreparationPermits >= Limit) { ++Stats.PreparationDeferrals; return false; }
    Entry->bPreparationAdmitted = true;
    ++Stats.ActivePreparationPermits; ++Stats.PreparationAdmissions;
    Stats.PeakPreparationPermits = FMath::Max(Stats.PeakPreparationPermits, Stats.ActivePreparationPermits);
    return true;
}
void UProject_JWorldArrivalSubsystem::Remove(uint64 Token)
{
    const FEntry* Found = Entries.Find(Token); if (!Found) return;
    if (Found->bPreparationAdmitted)
    {
        check(Stats.ActivePreparationPermits > 0);
        --Stats.ActivePreparationPermits;
    }
    const uint64 Next = Found->Next, Previous = Found->Previous;
    if (Next == Token) { Head = Cursor = 0; }
    else
    {
        Entries.FindChecked(Previous).Next = Next; Entries.FindChecked(Next).Previous = Previous;
        if (Head == Token) Head = Next;
        if (Cursor == Token) Cursor = Next;
    }
    Entries.Remove(Token); ++Stats.Retired; Stats.Pending = Entries.Num();
}
void UProject_JWorldArrivalSubsystem::Unregister(uint64 Token, UProject_JWorldStreamingComponent* Component, uint64 Request)
{
    check(IsInGameThread());
    const auto* Entry = Entries.Find(Token);
    if (Entry && Entry->Request == Request && Entry->Component == TWeakObjectPtr<UProject_JWorldStreamingComponent>(Component)) Remove(Token);
}
void UProject_JWorldArrivalSubsystem::Tick(float DeltaTime)
{
    check(IsInGameThread());
    if (bTicking || !IsTickable()) return;
    TGuardValue<bool> Guard(bTicking, true);
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    if (Stats.Frame != GFrameCounter)
    {
        Stats.Frame = GFrameCounter; Stats.FrameVisits = Stats.FrameAdvances = 0; Stats.FrameMilliseconds = 0;
        AdmissionFrontier = LastToken;
    }
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_WorldArrivalExecution);
    const double StartedAt = FPlatformTime::Seconds();
    const int32 VisitLimit = FMath::Min(Entries.Num(), MaxFrameVisits - Stats.FrameVisits);
    int32 Visits = 0;
    while (bAccepting && Cursor && Visits < VisitLimit && Stats.FrameAdvances < MaxFrameAdvances)
    {
        const double Now = FPlatformTime::Seconds();
        // One first visit can make progress even after preemption; this remains
        // a soft time budget, with hard frame visit/Advance limits.
        if (!FMath::IsFinite(Now) || (Stats.FrameVisits > 0 && Stats.FrameMilliseconds + (Now-StartedAt)*1000 >= SoftFrameMilliseconds)) break;
        FEntry* Live = Entries.Find(Cursor);
        if (!Live) { Cursor = Head; break; }
        const uint64 Token = Cursor;
        // Snapshot only values/weak identity. No map reference crosses consumer callbacks.
        const FEntry Entry = *Live; Cursor = Entry.Next;
        ++Visits; ++Stats.FrameVisits;
        if (Token > AdmissionFrontier || Entry.VisitedFrame == GFrameCounter) continue;
        Live->VisitedFrame = GFrameCounter;
        if (Now < Entry.NextDue) continue;
        auto* Component = Entry.Component.Get();
        if (!IsValid(Component) || Component->IsBeingDestroyed() || !Component->IsRegistered()
            || Component->GetWorld() != GetWorld() || Component->ArrivalExecutionToken != Token
            || Component->ArrivalExecutionOwner.Get() != this || Component->ArrivalRequest != Entry.Request)
        { Remove(Token); continue; }
        const TStrongObjectPtr<UProject_JWorldStreamingComponent> KeepComponent(Component);
        const double LatenessMs = FMath::Max(0., Now-Entry.NextDue)*1000;
        Stats.MaxDueDelayMilliseconds = FMath::Max(Stats.MaxDueDelayMilliseconds, LatenessMs);
        if (LatenessMs > 0) ++Stats.LateAdvances;
        if (Now >= Component->ArrivalDeadline) ++Stats.DeadlineObservedLate;
        // Charge before callbacks. Cancel/finish/re-register cannot refund this frame.
        Live->NextDue = Now + PollIntervalSeconds;
        ++Stats.FrameAdvances; ++Stats.Advances;
        Component->AdvancePreparedArrival(Now);
        // Never touch the old entry/component generation after the callback.
    }
    Stats.FrameMilliseconds += (FPlatformTime::Seconds()-StartedAt)*1000;
    Stats.PeakFrameMilliseconds = FMath::Max(Stats.PeakFrameMilliseconds, Stats.FrameMilliseconds);
    Stats.PeakFrameVisits = FMath::Max(Stats.PeakFrameVisits, Stats.FrameVisits);
    Stats.PeakFrameAdvances = FMath::Max(Stats.PeakFrameAdvances, Stats.FrameAdvances);
    if (!Entries.IsEmpty() && (Stats.FrameVisits >= MaxFrameVisits || Stats.FrameAdvances >= MaxFrameAdvances
        || Stats.FrameMilliseconds >= SoftFrameMilliseconds)) ++Stats.BudgetStops;
}
void UProject_JWorldArrivalSubsystem::Stop()
{
    check(IsInGameThread());
    bAccepting = false;
    auto Retiring = MoveTemp(Entries); Entries.Reset(); Head = Cursor = 0; Stats.Pending = 0;
    Stats.ActivePreparationPermits = 0; // Logical close, not a native-task/I/O completion fence.
    Stats.Retired += Retiring.Num();
    for (const auto& Pair : Retiring)
    {
        if (auto* Component = Pair.Value.Component.Get(); Component && Component->ArrivalExecutionOwner.Get() == this
            && Component->ArrivalExecutionToken == Pair.Key && Component->ArrivalRequest == Pair.Value.Request)
        {
            Component->ArrivalExecutionToken = 0; Component->ArrivalExecutionOwner.Reset();
            Component->FinishArrival(EProject_JWorldArrivalPhase::Failed, false);
        }
    }
}
void UProject_JWorldArrivalSubsystem::OnTearDown(UWorld* World) { if (World == GetWorld()) Stop(); }
void UProject_JWorldArrivalSubsystem::OnWorldEndPlay(UWorld& World) { Stop(); Super::OnWorldEndPlay(World); }
void UProject_JWorldArrivalSubsystem::Deinitialize()
{
    Stop(); FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle); Super::Deinitialize();
}
bool UProject_JWorldArrivalSubsystem::IsTickable() const
{
    return !IsTemplate() && bAccepting && IsInitialized() && GetWorld() && !GetWorld()->bIsTearingDown && !Entries.IsEmpty();
}
TStatId UProject_JWorldArrivalSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(ProjectJWorldArrivalExecution, STATGROUP_Tickables); }
