#include "System/Project_JVisualAssetSubsystem.h"
#include "System/Project_JAssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/StrongObjectPtr.h"

struct FProjectJVisualAssetGroup
{
	FSoftObjectPath Path;
	TSharedPtr<FStreamableHandle> Handle;
	int32 Consumers = 0;
	bool bStarted = false, bReady = false;
};
struct FProjectJVisualAssetLease
{
	uint64 Token = 0;
	uint64 PreviousToken = 0, NextToken = 0;
	TWeakObjectPtr<UObject> Owner;
	TSharedPtr<FProjectJVisualAssetGroup> Group;
	TFunction<void(UObject*)> Apply;
	bool bDelivered = false;
	double Deadline = 0;
};
namespace
{
	bool IsVisualOwnerAlive(UObject* Owner)
	{
		if (!IsValid(Owner)) { return false; }
		if (const auto* Component = Cast<UActorComponent>(Owner))
		{
			return !Component->IsBeingDestroyed() && IsValid(Component->GetOwner()) && !Component->GetOwner()->IsActorBeingDestroyed();
		}
		if (const auto* Actor = Cast<AActor>(Owner)) { return !Actor->IsActorBeingDestroyed(); }
		return true;
	}
}

bool UProject_JVisualAssetSubsystem::DoesSupportWorldType(EWorldType::Type Type) const { return Type == EWorldType::Game || Type == EWorldType::PIE; }
void UProject_JVisualAssetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection); bAccepting = true;
	TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &ThisClass::OnTearDown);
}
uint64 UProject_JVisualAssetSubsystem::Request(UObject* Owner, FSoftObjectPath Path, TFunction<void(UObject*)> Apply)
{
	check(IsInGameThread());
	if (!bAccepting || !IsVisualOwnerAlive(Owner) || !GetWorld() || GetWorld()->bIsTearingDown
		|| Owner->GetWorld() != GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer
		|| !Path.IsValid() || Leases.Num() >= MaxLeases) { ++Stats.Rejected; return 0; }
	const double Now = FPlatformTime::Seconds();
	// A healthy arrival must not sweep unrelated failures for every visual lease.
	// Expire the requested path here; reclaim other expired entries only when a new failure needs room.
	if (const double* Until = FailedUntil.Find(Path))
	{
		if (*Until > Now) { ++Stats.Rejected; ++Stats.BackoffRejected; return 0; }
		FailedUntil.Remove(Path);
	}
	const int32 OwnerLeases = OwnerLeaseCounts.FindRef(Owner);
	if (OwnerLeases >= MaxLeasesPerOwner) { ++Stats.Rejected; return 0; }
	auto* Found = Groups.FindByPredicate([&](const auto& G) { return G->Path == Path; });
	TSharedPtr<FProjectJVisualAssetGroup> Group = Found ? *Found : nullptr;
	if (!Group)
	{
		if (Groups.Num() >= MaxGroups) { ++Stats.Rejected; return 0; }
		Group = MakeShared<FProjectJVisualAssetGroup>(); Group->Path = MoveTemp(Path); Groups.Add(Group);
	}
	auto Lease = MakeShared<FProjectJVisualAssetLease>();
	// Zero means rejected. A counter wrap must not alias a still-live lease.
	do { ++NextToken; } while (!NextToken || LeaseSlots.Contains(NextToken));
	Lease->Token = NextToken;
	Lease->Owner = Owner; Lease->Group = Group; Lease->Apply = MoveTemp(Apply); ++Group->Consumers;
	Lease->Deadline = Now + RequestLifetimeSeconds;
	if (!LeaseCursorToken)
	{
		LeaseCursorToken = Lease->Token;
		Lease->PreviousToken = Lease->NextToken = Lease->Token;
	}
	else
	{
		// New arrivals join behind every current consumer, including across a
		// wrap. A continuous arrival burst cannot extend the cursor's frontier.
		const auto Next = Leases[LeaseSlots.FindChecked(LeaseCursorToken)];
		const auto Tail = Leases[LeaseSlots.FindChecked(Next->PreviousToken)];
		Lease->PreviousToken = Tail->Token; Lease->NextToken = Next->Token;
		Tail->NextToken = Lease->Token; Next->PreviousToken = Lease->Token;
	}
	LeaseSlots.Add(Lease->Token, Leases.Add(Lease));
	++OwnerLeaseCounts.FindOrAdd(Lease->Owner); ++Stats.Accepted;
	++PendingLeases;
	return Lease->Token;
}
void UProject_JVisualAssetSubsystem::Release(uint64 Token)
{
	check(IsInGameThread());
	const int32* Slot = LeaseSlots.Find(Token);
	if (!Slot) { return; }
	const int32 Index = *Slot;
	if (!ensureMsgf(Leases.IsValidIndex(Index) && Leases[Index]->Token == Token,
		TEXT("Visual asset lease index must resolve the current token"))) { return; }
	const auto Lease = Leases[Index]; Lease->Apply = nullptr;
	// Order is independent of physical storage. A swap cannot move an unvisited
	// consumer behind the cursor during repeated cosmetic replacement/cancellation.
	if (Leases.Num() == 1) { LeaseCursorToken = 0; }
	else
	{
		Leases[LeaseSlots.FindChecked(Lease->PreviousToken)]->NextToken = Lease->NextToken;
		Leases[LeaseSlots.FindChecked(Lease->NextToken)]->PreviousToken = Lease->PreviousToken;
		if (LeaseCursorToken == Token) LeaseCursorToken = Lease->NextToken;
	}
	if (!Lease->bDelivered) { --PendingLeases; }
	if (int32* Count = OwnerLeaseCounts.Find(Lease->Owner))
	{
		if (--*Count == 0) { OwnerLeaseCounts.Remove(Lease->Owner); }
	}
	LeaseSlots.Remove(Token);
	Leases.RemoveAtSwap(Index, 1, EAllowShrinking::No);
	if (Leases.IsValidIndex(Index)) { LeaseSlots.FindChecked(Leases[Index]->Token) = Index; }
	checkSlow(LeaseSlots.Num() == Leases.Num());
	const auto Group = Lease->Group;
	if (--Group->Consumers == 0)
	{
		// Preserve an in-flight reservation until completion. Otherwise cancellation churn
		// could launch new package work faster than the admission limit accounts for.
		if (!Group->bStarted || Group->bReady)
		{
			if (Group->Handle) { Group->Handle->CancelHandle(); Group->Handle.Reset(); }
			Groups.RemoveSingle(Group);
			if (Groups.IsEmpty()) Stats.InFlight = 0;
		}
	}
}
void UProject_JVisualAssetSubsystem::Tick(float DeltaTime)
{
	check(IsInGameThread());
	if (!IsTickable() || bTicking || !GetWorld() || GetWorld()->bIsTearingDown) { return; }
	TStrongObjectPtr<ThisClass> PinnedThis(this);
	TGuardValue<bool> TickGuard(bTicking, true);
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_VisualAssets_LoadAndApply);
	const double Started = FPlatformTime::Seconds();
	++Stats.ExecutedTicks;
	Stats.LastTickLeaseVisits = 0;
	const bool bIdleSweep = PendingLeases == 0;
	if (bIdleSweep && IdleSweepRemaining == 0) IdleSweepRemaining = Leases.Num();
	else if (!bIdleSweep) IdleSweepRemaining = 0;
	bGroupCleanupNeeded = false;
	const auto WithinBudget = [&]() { return (FPlatformTime::Seconds() - Started) * 1000.0 < ApplyBudgetMilliseconds; };
	Groups.RemoveAll([](const auto& G)
	{
		if (G->Consumers == 0 && G->bReady)
		{
			if (G->Handle) { G->Handle->CancelHandle(); }
			return true;
		}
		return false;
	});
	int32 InFlight = 0, Starts = 0;
	for (const auto& G : Groups) { InFlight += G->bStarted && !G->bReady ? 1 : 0; }
	for (const auto& G : Groups)
	{
		if (G->bStarted) { continue; }
		if (InFlight >= MaxLoadsInFlight || Starts >= MaxStartsPerTick || !WithinBudget()) { break; }
		G->bStarted = true; ++InFlight; ++Starts; ++Stats.Loads;
		TWeakPtr<FProjectJVisualAssetGroup> WeakGroup(G);
		TWeakObjectPtr<ThisClass> WeakThis(this);
		G->Handle = UProject_JAssetManager::Get().GetStreamableManager().RequestAsyncLoad(G->Path,
			FStreamableDelegate::CreateLambda([WeakGroup, WeakThis]()
			{
				check(IsInGameThread());
				if (const auto Group = WeakGroup.Pin()) { Group->bReady = true; }
				if (auto* Self = WeakThis.Get(); Self && Self->bAccepting) Self->bGroupCleanupNeeded = true;
			}));
		if (!G->Handle) { G->bReady = true; }
	}
	Stats.InFlight = InFlight; Stats.PeakInFlight = FMath::Max(Stats.PeakInFlight, InFlight);
	Stats.LastTickApplications = 0;
	const int32 VisitLimit = FMath::Min(Leases.Num(), MaxLeaseVisitsPerTick);
	for (int32 Visit = 0; bAccepting && Visit < VisitLimit && !Leases.IsEmpty()
		&& Stats.LastTickApplications < MaxApplicationsPerTick && WithinBudget(); ++Visit)
	{
		const auto Lease = Leases[LeaseSlots.FindChecked(LeaseCursorToken)];
		LeaseCursorToken = Lease->NextToken;
		++Stats.LastTickLeaseVisits;
		if (bIdleSweep) IdleSweepRemaining = FMath::Max(0, IdleSweepRemaining - 1);
		if (!IsVisualOwnerAlive(Lease->Owner.Get())) { Release(Lease->Token); continue; }
		const bool bExpired = !Lease->Group->bReady && FPlatformTime::Seconds() >= Lease->Deadline;
		if (Lease->bDelivered || (!Lease->Group->bReady && !bExpired)) { continue; }
		Lease->bDelivered = true;
		--PendingLeases;
		const auto Group = Lease->Group; // pins the handle even if Apply releases the token.
		UObject* Asset = !bExpired && Group->Handle ? Group->Handle->GetLoadedAsset() : nullptr;
		TStrongObjectPtr<UObject> PinnedAsset(Asset);
		if (!Asset)
		{
			++Stats.Failed; Stats.TimedOut += bExpired ? 1 : 0;
			// Bound the negative cache; record before callbacks can request the same missing asset again.
			const double FailureNow = FPlatformTime::Seconds();
			if (FailedUntil.Num() >= MaxGroups && !FailedUntil.Contains(Group->Path))
			{
				for (auto It = FailedUntil.CreateIterator(); It; ++It)
				{
					if (It.Value() <= FailureNow) { It.RemoveCurrent(); }
				}
			}
			if (FailedUntil.Num() < MaxGroups || FailedUntil.Contains(Group->Path))
			{ FailedUntil.Add(Group->Path, FailureNow + FailureBackoffSeconds); }
		}
		auto Apply = MoveTemp(Lease->Apply);
		TStrongObjectPtr<UObject> PinnedOwner(Lease->Owner.Get());
		++Stats.LastTickApplications; ++Stats.Delivered;
		if (Apply) { Apply(Asset); }
		// Failed leases never pin a failed group; callers may explicitly retry.
		if (!Asset) { Release(Lease->Token); }
	}
	if (PendingLeases == 0 && IdleSweepRemaining == 0) NextIdleSweep = FPlatformTime::Seconds() + IdleSweepIntervalSeconds;
	check(PendingLeases >= 0 && PendingLeases <= Leases.Num());
	Stats.LastTickMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.0;
}
void UProject_JVisualAssetSubsystem::Stop()
{
	check(IsInGameThread()); bAccepting = false;
	for (const auto& Lease : Leases) { Lease->Apply = nullptr; }
	for (const auto& Group : Groups) { if (Group->Handle) { Group->Handle->CancelHandle(); } }
	Leases.Empty(); LeaseSlots.Empty(); OwnerLeaseCounts.Empty(); Groups.Empty(); FailedUntil.Empty();
	LeaseCursorToken = 0; Stats.InFlight = 0;
	PendingLeases = IdleSweepRemaining = 0; NextIdleSweep = 0; bGroupCleanupNeeded = false;
}
void UProject_JVisualAssetSubsystem::OnTearDown(UWorld* World) { if (World == GetWorld()) { Stop(); } }
void UProject_JVisualAssetSubsystem::OnWorldEndPlay(UWorld& World) { Stop(); Super::OnWorldEndPlay(World); }
void UProject_JVisualAssetSubsystem::Deinitialize() { Stop(); FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle); Super::Deinitialize(); }
bool UProject_JVisualAssetSubsystem::IsTickable() const
{
	return !IsTemplate() && bAccepting && !Groups.IsEmpty() && (PendingLeases > 0 || bGroupCleanupNeeded ||
		IdleSweepRemaining > 0 || (!Leases.IsEmpty() && FPlatformTime::Seconds() >= NextIdleSweep));
}
TStatId UProject_JVisualAssetSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(ProjectJVisualAssets, STATGROUP_Tickables); }

#if WITH_DEV_AUTOMATION_TESTS
void UProject_JVisualAssetSubsystem::ExpireForTest(uint64 Token)
{
	for (const auto& Lease : Leases) { if (Lease->Token == Token) { Lease->Deadline = -1; Lease->Group->bStarted = true; } }
}
#endif
