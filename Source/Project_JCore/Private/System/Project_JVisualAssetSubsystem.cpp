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
	TWeakObjectPtr<UObject> Owner;
	TSharedPtr<FProjectJVisualAssetGroup> Group;
	TFunction<void(UObject*)> Apply;
	bool bDelivered = false;
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
	auto* Found = Groups.FindByPredicate([&](const auto& G) { return G->Path == Path; });
	TSharedPtr<FProjectJVisualAssetGroup> Group = Found ? *Found : nullptr;
	if (!Group)
	{
		if (Groups.Num() >= MaxGroups) { ++Stats.Rejected; return 0; }
		Group = MakeShared<FProjectJVisualAssetGroup>(); Group->Path = MoveTemp(Path); Groups.Add(Group);
	}
	auto Lease = MakeShared<FProjectJVisualAssetLease>();
	Lease->Token = ++NextToken; if (!Lease->Token) { Lease->Token = ++NextToken; }
	Lease->Owner = Owner; Lease->Group = Group; Lease->Apply = MoveTemp(Apply); ++Group->Consumers;
	Leases.Add(Lease); ++Stats.Accepted;
	return Lease->Token;
}
void UProject_JVisualAssetSubsystem::Release(uint64 Token)
{
	check(IsInGameThread());
	const int32 Index = Leases.IndexOfByPredicate([Token](const auto& L) { return L->Token == Token; });
	if (Index == INDEX_NONE) { return; }
	const auto Lease = Leases[Index]; Lease->Apply = nullptr;
	Leases.RemoveAt(Index);
	const auto Group = Lease->Group;
	if (--Group->Consumers == 0)
	{
		// Preserve an in-flight reservation until completion. Otherwise cancellation churn
		// could launch new package work faster than the admission limit accounts for.
		if (!Group->bStarted || Group->bReady)
		{
			if (Group->Handle) { Group->Handle->CancelHandle(); Group->Handle.Reset(); }
			Groups.RemoveSingle(Group);
		}
	}
}
void UProject_JVisualAssetSubsystem::Tick(float DeltaTime)
{
	check(IsInGameThread());
	if (!bAccepting) { return; }
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_VisualAssets_LoadAndApply);
	const double Started = FPlatformTime::Seconds();
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
		G->Handle = UProject_JAssetManager::Get().GetStreamableManager().RequestAsyncLoad(G->Path,
			FStreamableDelegate::CreateLambda([WeakGroup]()
			{
				check(IsInGameThread());
				if (const auto Group = WeakGroup.Pin()) { Group->bReady = true; }
			}));
		if (!G->Handle) { G->bReady = true; }
	}
	Stats.LastTickApplications = 0;
	const int32 VisitLimit = FMath::Min(Leases.Num(), MaxLeaseVisitsPerTick);
	for (int32 Visit = 0; bAccepting && Visit < VisitLimit && !Leases.IsEmpty() && WithinBudget(); ++Visit)
	{
		Cursor %= Leases.Num();
		const auto Lease = Leases[Cursor];
		Cursor = (Cursor + 1) % Leases.Num();
		if (!IsVisualOwnerAlive(Lease->Owner.Get())) { Release(Lease->Token); continue; }
		if (Lease->bDelivered || !Lease->Group->bReady) { continue; }
		if (Stats.LastTickApplications >= MaxApplicationsPerTick) { break; }
		Lease->bDelivered = true;
		const auto Group = Lease->Group; // pins the handle even if Apply releases the token.
		UObject* Asset = Group->Handle ? Group->Handle->GetLoadedAsset() : nullptr;
		TStrongObjectPtr<UObject> PinnedAsset(Asset);
		if (!Asset) { ++Stats.Failed; }
		auto Apply = MoveTemp(Lease->Apply);
		++Stats.LastTickApplications; ++Stats.Delivered;
		if (Apply) { Apply(Asset); }
		// Failed leases never pin a failed group; callers may explicitly retry.
		if (!Asset) { Release(Lease->Token); }
	}
	Stats.LastTickMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.0;
}
void UProject_JVisualAssetSubsystem::Stop()
{
	check(IsInGameThread()); bAccepting = false;
	for (const auto& Lease : Leases) { Lease->Apply = nullptr; }
	for (const auto& Group : Groups) { if (Group->Handle) { Group->Handle->CancelHandle(); } }
	Leases.Empty(); Groups.Empty(); Cursor = 0;
}
void UProject_JVisualAssetSubsystem::OnTearDown(UWorld* World) { if (World == GetWorld()) { Stop(); } }
void UProject_JVisualAssetSubsystem::OnWorldEndPlay(UWorld& World) { Stop(); Super::OnWorldEndPlay(World); }
void UProject_JVisualAssetSubsystem::Deinitialize() { Stop(); FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle); Super::Deinitialize(); }
bool UProject_JVisualAssetSubsystem::IsTickable() const { return !IsTemplate() && bAccepting && !Groups.IsEmpty(); }
TStatId UProject_JVisualAssetSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(ProjectJVisualAssets, STATGROUP_Tickables); }
