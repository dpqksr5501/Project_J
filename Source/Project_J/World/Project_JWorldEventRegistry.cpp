#include "Game/Project_JPlayerState.h"
#include "World/Project_JWorldEventRegistry.h"
#include "World/Project_JWorldEncounter.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/ActorComponent.h"

static FAutoConsoleCommandWithWorld GWorldEventStats(TEXT("ProjectJ.WorldEvents.Stats"),
    TEXT("Read authority-local event scheduler pressure, preparation slots and observer overflow."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        if (const auto* Registry = World ? World->GetSubsystem<UProject_JWorldEventRegistry>() : nullptr)
        {
            const auto& S = Registry->GetStats();
            UE_LOG(LogTemp, Display, TEXT("WorldEvents updates=%llu spawnApplications=%llu visits=%d tickUpdates=%d spawns=%d prep=%d/%d peak=%d observers=%d overflow=%llu deferrals=%llu expired=%llu tickMs=%.3f maxLateness=%.3f"),
                S.Updates, S.SpawnApplications, S.LastTickVisits, S.LastTickUpdates, S.LastTickSpawns, S.Preparations,
                Registry->MaxPreparations, S.PeakPreparations, S.Observers, S.ObserverOverflows,
                S.PreparationDeferrals, S.ExpiredPreparations, S.LastTickMilliseconds, S.MaxUpdateLatenessSeconds);
        }
    }));

bool UProject_JWorldEventRegistry::DoesSupportWorldType(EWorldType::Type Type) const
{ return Type == EWorldType::Game || Type == EWorldType::PIE; }
void UProject_JWorldEventRegistry::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bAccepting = true;
    InvalidateEventDeadline(); NextPreparationWake = 0;
    TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &ThisClass::OnTearDown);
}
bool UProject_JWorldEventRegistry::Register(FName Id, AActor* Owner)
{
    check(IsInGameThread());
    if (!bAccepting || !GetWorld() || GetWorld()->bIsTearingDown || Id.IsNone() ||
        !IsValid(Owner) || Owner->IsActorBeingDestroyed() || Owner->GetWorld() != GetWorld() || !Owner->HasAuthority())
    { ++Stats.RejectedRegistrations; return false; }
    const auto* Encounter = Cast<AProject_JWorldEncounter>(Owner);
    const bool bRunnable = Encounter && Encounter->Phase != EProject_JEncounterPhase::Disabled && !Encounter->NPCClass.IsNull();
    if (const auto* Existing = Owners.Find(Id))
    {
        // Re-registering the same authority owner can reconcile explicit phase/config repair
        // without giving up its ID or creating duplicate round-robin slots.
        if (Existing->Owner.Get() == Owner)
        {
            auto& Entry = Owners.FindChecked(Id);
            if (bRunnable) AddVisit(Id, Entry);
            else RemoveVisit(Entry);
            // Explicit owner re-registration also reconciles changed schedule data.
            Owners.FindChecked(Id).NextUpdate = GetWorld()->GetTimeSeconds();
            InvalidateEventDeadline();
            return true;
        }
        if (Existing->Owner.IsValid()) { ++Stats.RejectedRegistrations; return false; }
        Unregister(Id, nullptr);
    }
    if (Owners.Num() >= MaxEvents) { ++Stats.RejectedRegistrations; return false; }
    auto& Entry = Owners.Add(Id);
    Entry.Owner = Owner;
    Entry.NextUpdate = GetWorld()->GetTimeSeconds() + double(GetTypeHash(Id) % 1000) / 1000.0;
    if (bRunnable) AddVisit(Id, Entry);
    InvalidateEventDeadline();
    return true;
}
void UProject_JWorldEventRegistry::Unregister(FName Id, AActor* Owner)
{
    check(IsInGameThread());
    const auto* Existing = Owners.Find(Id);
    if (!Existing || Existing->Owner.Get() != Owner) return;
    RemoveVisit(Owners.FindChecked(Id));
    Owners.Remove(Id);
    InvalidateEventDeadline();
}
void UProject_JWorldEventRegistry::AddVisit(FName Id, FProject_JWorldEventEntry& Entry)
{
    if (Entry.VisitIndex != INDEX_NONE) return;
    if (VisitOrder.IsEmpty())
    {
        Entry.PreviousVisit = Entry.NextVisit = Id;
        Entry.VisitIndex = VisitOrder.Add(Id); Cursor = 0;
        return;
    }
    // New work follows every survivor ahead of the current cursor. Storage
    // admission/removal remains O(1); physical swap order never becomes priority.
    const FName Next = VisitOrder[Cursor];
    const FName Previous = Owners.FindChecked(Next).PreviousVisit;
    Entry.PreviousVisit = Previous; Entry.NextVisit = Next;
    Owners.FindChecked(Previous).NextVisit = Id;
    Owners.FindChecked(Next).PreviousVisit = Id;
    Entry.VisitIndex = VisitOrder.Add(Id);
}
void UProject_JWorldEventRegistry::RemoveVisit(FProject_JWorldEventEntry& Entry)
{
    const int32 Index = Entry.VisitIndex;
    if (Index == INDEX_NONE) return;
    checkSlow(VisitOrder.IsValidIndex(Index));
    const FName Next = VisitOrder[Cursor] == VisitOrder[Index] ? Entry.NextVisit : VisitOrder[Cursor];
    if (VisitOrder.Num() > 1)
    {
        Owners.FindChecked(Entry.PreviousVisit).NextVisit = Entry.NextVisit;
        Owners.FindChecked(Entry.NextVisit).PreviousVisit = Entry.PreviousVisit;
    }
    Entry.VisitIndex = INDEX_NONE; Entry.PreviousVisit = Entry.NextVisit = NAME_None;
    VisitOrder.RemoveAtSwap(Index, 1, EAllowShrinking::No);
    if (VisitOrder.IsValidIndex(Index)) Owners.FindChecked(VisitOrder[Index]).VisitIndex = Index;
    Cursor = VisitOrder.IsEmpty() ? 0 : Owners.FindChecked(Next).VisitIndex;
}
bool UProject_JWorldEventRegistry::IsCurrent(const AProject_JWorldEncounter* Owner) const
{
	if (!IsValid(Owner) || Owner->IsActorBeingDestroyed() || Owner->Phase == EProject_JEncounterPhase::Disabled) return false;
    const auto* Entry = Owner ? Owners.Find(Owner->EncounterId) : nullptr;
    return bAccepting && IsValid(Owner) && !Owner->IsActorBeingDestroyed() && GetWorld() &&
        !GetWorld()->bIsTearingDown && Owner->HasAuthority() && Entry && Entry->Owner.Get() == Owner;
}
bool UProject_JWorldEventRegistry::HasNearbyPlayer(const FVector& Origin, double Radius)
{
    bool bFound = false;
    return TryQueryNearbyPlayer(Origin, Radius, bFound) && bFound;
}
bool UProject_JWorldEventRegistry::TryQueryNearbyPlayer(const FVector& Origin, double Radius, bool& bFound)
{
    check(IsInGameThread());
    bFound = false;
    if (!bAccepting || !GetWorld() || GetWorld()->bIsTearingDown) return false;
    const double Now = FPlatformTime::Seconds();
    if (Now >= NextObserverSnapshot)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_WorldEventObservers);
        NextObserverSnapshot = Now + 0.5;
        TArray<ProjectJ::TargetScoring::FSpatialTarget> Values;
        bCompleteObservers = true;
        int32 Visits = 0;
        for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            if (++Visits > ProjectJ::TargetScoring::FTargetSpatialSnapshot::MaxEntries)
            { bCompleteObservers = false; ++Stats.ObserverOverflows; break; }
            const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
            if (IsValid(Pawn) && !Pawn->IsActorBeingDestroyed() && !Pawn->GetActorLocation().ContainsNaN())
                Values.Add({Values.Num(), Pawn->GetActorLocation(), 1});
        }
        Stats.Observers = Values.Num(); ++Stats.SnapshotBuilds;
        // Event activation spans kilometres; NPC combat queries keep their existing 10m cells.
        bCompleteObservers = Players.Build(MoveTemp(Values), 100000.0) && bCompleteObservers;
    }
    if (!bCompleteObservers) return false;
    bool bResult = false;
    ProjectJ::TargetScoring::FSpatialQueryStats QueryStats;
    if (!Players.HasAny(Origin, Radius, 0, bResult, QueryStats)) return false;
    bFound = bResult;
    return true;
}
bool UProject_JWorldEventRegistry::IsPreparationCurrent(UObject* Owner) const
{
    if (auto* Encounter = Cast<AProject_JWorldEncounter>(Owner)) return IsCurrent(Encounter);
    const auto* Component = Cast<UActorComponent>(Owner);
    const auto* Actor = Component ? Component->GetOwner() : nullptr;
    return bAccepting && GetWorld() && !GetWorld()->bIsTearingDown && GetWorld()->GetNetMode() != NM_Client
        && IsValid(Component) && !Component->IsBeingDestroyed() && Component->IsRegistered() && Component->GetWorld() == GetWorld()
        && IsValid(Actor) && !Actor->IsActorBeingDestroyed() && Actor->HasAuthority();
}
uint64 UProject_JWorldEventRegistry::RequestPreparation(AProject_JWorldEncounter* Owner, const FSoftObjectPath& ClassPath)
{
    if (!IsValid(Owner) || !Owner->HasValidCombatConfiguration()) return 0;
    const TWeakObjectPtr<AProject_JWorldEncounter> WeakOwner(Owner);
    const FSoftObjectPath DefinitionPath = Owner->CombatDefinition.ToSoftObjectPath();
    const int32 Team = Owner->CombatTeamId;
    return BeginPreparation(Owner, ClassPath, [WeakOwner, ClassPath, DefinitionPath, Team](uint64 Token, bool bLoadCompleted)
    {
        if (auto* Encounter = WeakOwner.Get(); Encounter && Encounter->PreparationToken == Token &&
            Encounter->Phase == EProject_JEncounterPhase::Preparing)
        {
            if (Encounter->NPCClass.ToSoftObjectPath() != ClassPath || Encounter->CombatDefinition.ToSoftObjectPath() != DefinitionPath
                || (!DefinitionPath.IsNull() && Encounter->CombatTeamId != Team)) { Encounter->EnterCooldown(false); return; }
            if (bLoadCompleted) Encounter->CompletePreparation(Token); else Encounter->EnterCooldown(false);
        }
    }, DefinitionPath);
}
uint64 UProject_JWorldEventRegistry::RequestComponentPreparation(UActorComponent* Owner, const FSoftObjectPath& ClassPath, TFunction<void(uint64, bool)> Completion)
{ return Completion ? BeginPreparation(Owner, ClassPath, MoveTemp(Completion)) : 0; }
uint64 UProject_JWorldEventRegistry::BeginPreparation(UObject* Owner, const FSoftObjectPath& ClassPath, TFunction<void(uint64, bool)> Completion,
    const FSoftObjectPath& AdditionalPath)
{
    check(IsInGameThread());
    if (!IsPreparationCurrent(Owner) || !ClassPath.IsValid() || (!AdditionalPath.IsNull() && !AdditionalPath.IsValid())) return 0;
    for (const auto& Pair : Preparations)
        if (Pair.Value.Owner.Get() == Owner) return 0;
    if (Preparations.Num() >= MaxPreparations) { ++Stats.PreparationDeferrals; return 0; }
    uint64 Token = ++NextPreparationToken;
    if (!Token) Token = ++NextPreparationToken;
    auto& Entry = Preparations.Add(Token);
    Entry.Owner = Owner; Entry.Completion = MoveTemp(Completion);
    const double Now = FPlatformTime::Seconds();
    Entry.Deadline = Now + PreparationTimeoutSeconds;
    // Pending encounter demand reuses the existing one-second monotonic wake;
    // component-owned creation keeps its original completion/timeout policy.
    const double Wake = Cast<AProject_JWorldEncounter>(Owner) ? FMath::Min(Entry.Deadline, Now + UpdateIntervalSeconds) : Entry.Deadline;
    NextPreparationWake = FMath::Min(NextPreparationWake, Wake);
    TWeakObjectPtr<UProject_JWorldEventRegistry> WeakThis(this);
    // One physical preparation reservation/handle owns both dependencies, including already-resident assets.
    // Component callers retain the existing single-class request and cancellation semantics.
    TArray<FSoftObjectPath> Paths;
    Paths.Reserve(AdditionalPath.IsNull() ? 1 : 2); Paths.Add(ClassPath);
    if (!AdditionalPath.IsNull()) Paths.AddUnique(AdditionalPath);
    auto Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(MoveTemp(Paths),
        FStreamableDelegate::CreateLambda([WeakThis, Token]
        {
            if (auto* Self = WeakThis.Get(); Self && Self->bAccepting)
                if (auto* Ready = Self->Preparations.Find(Token))
                { Ready->bReady = true; Self->NextPreparationWake = 0; }
        }));
    if (!Handle) { Preparations.Remove(Token); return 0; }
    Preparations.FindChecked(Token).Handle = MoveTemp(Handle);
    Stats.Preparations = Preparations.Num();
    Stats.PeakPreparations = FMath::Max(Stats.PeakPreparations, Stats.Preparations);
    return Token;
}
void UProject_JWorldEventRegistry::ReleasePreparation(uint64 Token, UObject* Owner)
{
    check(IsInGameThread());
    auto* Entry = Preparations.Find(Token);
    if (!Entry || Entry->Owner.Get() != Owner) return;
    Entry->Owner.Reset(); Entry->Completion = {};
    // Cancellation retires the consumer, not the engine's physical package work.
    if (Entry->bReady) Preparations.Remove(Token);
    InvalidateEventDeadline(); // The released owner may leave Preparing.
    NextPreparationWake = 0; // Recompute at most eight physical reservations on the next apply.
    Stats.Preparations = Preparations.Num();
}
void UProject_JWorldEventRegistry::Tick(float)
{
    if (!IsTickable() || bTicking) return;
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_WorldEvents);
    const TStrongObjectPtr<UProject_JWorldEventRegistry> KeepSelf(this);
    TGuardValue<bool> TickGuard(bTicking, true);
    const double Started = FPlatformTime::Seconds();
    ON_SCOPE_EXIT { Stats.LastTickMilliseconds = (FPlatformTime::Seconds() - Started) * 1000; Stats.Preparations = Preparations.Num(); };
    const auto WithinBudget = [&] { return (FPlatformTime::Seconds() - Started) * 1000 < UpdateBudgetMilliseconds; };
    Stats.LastTickVisits = Stats.LastTickUpdates = Stats.LastTickSpawns = 0;
    const double EventNow = GetWorld()->GetTimeSeconds();
    if (!FMath::IsFinite(EventNow)) { InvalidateEventDeadline(); return; }
    if (EventNow < LastEventClock || !FMath::IsFinite(NextEventWake)) InvalidateEventDeadline();
    LastEventClock = EventNow;
    const FDateTime ScheduleNow = FDateTime::UtcNow();
    TArray<uint64> Tokens;
    Preparations.GetKeys(Tokens);
    Tokens.Sort(); // Oldest ready work wins even while newer events keep arriving.
    TArray<uint64, TInlineAllocator<MaxPreparations>> UnknownDemand;
    for (uint64 Token : Tokens)
    {
        if (!bAccepting || !WithinBudget()) break;
        UObject* Owner = nullptr;
        bool bReady = false;
        double Deadline = 0;
        {
            const auto* Entry = Preparations.Find(Token);
            if (!Entry) continue;
            Owner = Entry->Owner.Get(); bReady = Entry->bReady; Deadline = Entry->Deadline;
        }
        if (!IsPreparationCurrent(Owner))
        {
            if (bReady) Preparations.Remove(Token);
            else { auto& Cancelled = Preparations.FindChecked(Token); Cancelled.Owner.Reset(); Cancelled.Completion = {}; }
            continue;
        }
        const TStrongObjectPtr<UObject> KeepOwner(Owner);
        bool bDemandKnown = true;
        if (auto* Encounter = Cast<AProject_JWorldEncounter>(Owner))
        {
            if (Encounter->Phase != EProject_JEncounterPhase::Preparing || Encounter->PreparationToken != Token)
            { ReleasePreparation(Token, Owner); continue; }
            const bool bDemand = Encounter->HasActivationDemand(bDemandKnown);
            // No map entry reference crosses an encounter transition. Release
            // retires this consumer while unfinished physical loads keep a slot.
            if (bDemandKnown && !bDemand)
            { Encounter->EnterCooldown(false); continue; }
        }
        if ((!bReady || !bDemandKnown) && FPlatformTime::Seconds() >= Deadline)
        {
            ++Stats.ExpiredPreparations;
            TFunction<void(uint64, bool)> Completion;
            TSharedPtr<FStreamableHandle> CompletedHandle;
            {
                auto& Expired = Preparations.FindChecked(Token);
                Completion = MoveTemp(Expired.Completion);
                if (bReady)
                {
                    CompletedHandle = MoveTemp(Expired.Handle);
                    Preparations.Remove(Token);
                }
                else { Expired.Owner.Reset(); }
            }
            InvalidateEventDeadline();
            if (Completion) Completion(Token, false);
        }
        else if (!bDemandKnown)
        {
            // Ready-but-unknown is not a successful spawn, nor immediate
            // per-frame polling. Only visited unknown tokens get the slow wake.
            UnknownDemand.Add(Token);
        }
        else if (bReady && Stats.LastTickSpawns < MaxSpawnsPerTick)
        {
            ++Stats.LastTickSpawns; ++Stats.SpawnApplications;
            auto Completed = MoveTemp(Preparations.FindChecked(Token));
            Preparations.Remove(Token); // Detach before callbacks; keep the complete dependency group alive through spawn.
            InvalidateEventDeadline();
            if (Completed.Completion) Completed.Completion(Token, true);
            // No array/map reference crosses a callback. Physical completion released this reservation.
        }
    }
    // Preparation completion/timeout uses monotonic time, independently of the
    // encounter's game-time cadence. Cancelled physical loads wake by callback.
    NextPreparationWake = TNumericLimits<double>::Max();
    const double PreparationNow = FPlatformTime::Seconds();
    for (const auto& Pair : Preparations)
    {
        const auto* Encounter = Cast<AProject_JWorldEncounter>(Pair.Value.Owner.Get());
        const bool bPendingEncounter = IsPreparationCurrent(Pair.Value.Owner.Get()) && Encounter &&
            Encounter->Phase == EProject_JEncounterPhase::Preparing && Encounter->PreparationToken == Pair.Key;
        if (Pair.Value.bReady)
        {
            if (bPendingEncounter && UnknownDemand.Contains(Pair.Key))
                NextPreparationWake = FMath::Min(NextPreparationWake, FMath::Min(Pair.Value.Deadline, PreparationNow + UpdateIntervalSeconds));
            else { NextPreparationWake = 0; break; }
        }
        else if (IsPreparationCurrent(Pair.Value.Owner.Get()))
        {
            const double Wake = bPendingEncounter ? FMath::Min(Pair.Value.Deadline, PreparationNow + UpdateIntervalSeconds) : Pair.Value.Deadline;
            NextPreparationWake = FMath::Min(NextPreparationWake, Wake);
        }
    }
    const uint64 Revision = ScheduleRevision;
    if (DeadlineVisitsRemaining == 0)
    {
        DeadlineVisitsRemaining = VisitOrder.Num();
        CandidateEventWake = TNumericLimits<double>::Max();
    }
    const int32 Visits = FMath::Min3(VisitOrder.Num(), MaxVisitsPerTick, DeadlineVisitsRemaining);
    for (int32 Visit = 0; Visit < Visits && bAccepting && !VisitOrder.IsEmpty() && ScheduleRevision == Revision &&
        Stats.LastTickUpdates < MaxUpdatesPerTick && WithinBudget(); ++Visit)
    {
        Cursor %= VisitOrder.Num();
        const FName Id = VisitOrder[Cursor];
        // Advance before extensible encounter callbacks; unregister can then
        // preserve or replace exactly that next identity without reordering peers.
        Cursor = Owners.FindChecked(Owners.FindChecked(Id).NextVisit).VisitIndex;
        ++Stats.LastTickVisits;
        --DeadlineVisitsRemaining;
        auto* Entry = Owners.Find(Id);
        if (!Entry) continue;
        AActor* Actor = Entry->Owner.Get();
        if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()) { Unregister(Id, Actor); continue; }
        auto* Encounter = Cast<AProject_JWorldEncounter>(Actor);
        const double Now = GetWorld()->GetTimeSeconds();
        if (!Encounter || Encounter->Phase == EProject_JEncounterPhase::Disabled || Encounter->Phase == EProject_JEncounterPhase::Preparing) continue;
        // Closed schedules share the next whole-second heartbeat, including
        // entries whose initial hash stagger has not yet become due. A sweep
        // spanning frames can cross one heartbeat, then converge on the next.
        if (Encounter->Phase == EProject_JEncounterPhase::Dormant && Encounter->bUseSchedule &&
            !Encounter->IsScheduleOpen(ScheduleNow))
        {
            Entry->NextUpdate = FMath::FloorToDouble(Now) + UpdateIntervalSeconds;
            CandidateEventWake = FMath::Min(CandidateEventWake, Entry->NextUpdate);
            continue;
        }
        if (!FMath::IsFinite(Entry->NextUpdate) || Entry->NextUpdate > Now + UpdateIntervalSeconds)
            Entry->NextUpdate = Now;
        if (Now < Entry->NextUpdate)
        { CandidateEventWake = FMath::Min(CandidateEventWake, Entry->NextUpdate); continue; }
        const double Lateness = Now - Entry->NextUpdate;
        Entry->NextUpdate = Now + UpdateIntervalSeconds;
        CandidateEventWake = FMath::Min(CandidateEventWake, Entry->NextUpdate);
        Stats.MaxUpdateLatenessSeconds = FMath::Max(Stats.MaxUpdateLatenessSeconds, Lateness);
        ++Stats.LastTickUpdates; ++Stats.Updates;
        const TStrongObjectPtr<AProject_JWorldEncounter> KeepOwner(Encounter);
        Encounter->UpdateEncounter();
    }
    if (ScheduleRevision == Revision && DeadlineVisitsRemaining == 0)
    {
        // Never cache a distant UTC start/end as a long game-time sleep: the
        // existing one-second cadence rechecks forward/backward UTC changes.
        NextEventWake = CandidateEventWake == TNumericLimits<double>::Max() ? CandidateEventWake
            : FMath::Min(CandidateEventWake, GetWorld()->GetTimeSeconds() + UpdateIntervalSeconds);
    }
    else { NextEventWake = -TNumericLimits<double>::Max(); }
}
void UProject_JWorldEventRegistry::InvalidateEventDeadline()
{
    ++ScheduleRevision; DeadlineVisitsRemaining = 0;
    CandidateEventWake = TNumericLimits<double>::Max(); NextEventWake = -TNumericLimits<double>::Max();
}
bool UProject_JWorldEventRegistry::IsTickable() const
{
    if (IsTemplate() || !bAccepting || !GetWorld() || GetWorld()->bIsTearingDown || GetWorld()->GetNetMode() == NM_Client) return false;
    const double Now = GetWorld()->GetTimeSeconds();
    // Max is the intentional no-event sentinel (e.g. all owners Preparing).
    // A clock rewind must not turn that sentinel into per-frame busy work.
    const bool bEventsDue = !VisitOrder.IsEmpty() && NextEventWake != TNumericLimits<double>::Max() &&
        (!FMath::IsFinite(Now) || !FMath::IsFinite(NextEventWake) || Now < LastEventClock || Now >= NextEventWake);
    return bEventsDue || (!Preparations.IsEmpty() && FPlatformTime::Seconds() >= NextPreparationWake);
}
TStatId UProject_JWorldEventRegistry::GetStatId() const
{ RETURN_QUICK_DECLARE_CYCLE_STAT(UProject_JWorldEventRegistry, STATGROUP_Tickables); }
void UProject_JWorldEventRegistry::OnTearDown(UWorld* World) { if (World == GetWorld()) Stop(); }
void UProject_JWorldEventRegistry::OnWorldEndPlay(UWorld& World) { if (&World == GetWorld()) Stop(); Super::OnWorldEndPlay(World); }
void UProject_JWorldEventRegistry::Stop()
{
    bAccepting = false;
    InvalidateEventDeadline(); NextPreparationWake = TNumericLimits<double>::Max();
    Owners.Reset(); VisitOrder.Reset();
    for (auto& Pair : Preparations) if (Pair.Value.Handle) Pair.Value.Handle->CancelHandle();
    Preparations.Reset();
    Players.Build({});
    Stats.Preparations = 0;
}
void UProject_JWorldEventRegistry::Deinitialize()
{
    Stop();
    FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle);
    Super::Deinitialize();
}

void UProject_JWorldEventRegistry::NotifyTrustedProfileReady(AProject_JPlayerState* Player)
{
    if (!bAccepting || !IsValid(Player) || Player->GetWorld() != GetWorld() || !Player->CanAcceptSessionCommand()) return;
    TArray<TWeakObjectPtr<AActor>> Snapshot;
    Snapshot.Reserve(Owners.Num());
    for (const auto& Entry : Owners) Snapshot.Add(Entry.Value.Owner);
    for (const auto& Weak : Snapshot)
    {
        auto* Encounter = Cast<AProject_JWorldEncounter>(Weak.Get());
        if (IsValid(Encounter) && IsCurrent(Encounter)) Encounter->TryClaimDevelopmentRewards(Player);
    }
}
