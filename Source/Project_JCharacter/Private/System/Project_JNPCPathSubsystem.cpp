#include "System/Project_JNPCPathSubsystem.h"
#include "NavigationSystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

struct FProjectJNPCPathEntry
{
	TWeakObjectPtr<UObject> Owner;
	TWeakObjectPtr<APawn> Pawn;
	TWeakObjectPtr<AController> Controller;
	TWeakObjectPtr<UNavigationSystemV1> Navigation;
	FProjectJNPCPathCompletion Result;
	TFunction<void(const FProjectJNPCPathCompletion&)> Callback;
	double Deadline = 0;
	uint32 EngineId = INVALID_NAVQUERYID;
	bool bDispatched = false, bFinished = false, bCancelled = false, bDelivered = false;
};

namespace
{
	bool ValidPosition(const FVector& P)
	{
		return !P.ContainsNaN() && P.GetAbsMax() <= 1.e9;
	}
}

bool UProject_JNPCPathSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE;
}

void UProject_JNPCPathSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bAccepting = true;
	TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &ThisClass::OnTearDown);
}

bool UProject_JNPCPathSubsystem::IsActiveServer() const
{
	return bAccepting && IsInitialized() && GetWorld() && GetWorld()->GetNetMode() != NM_Client;
}

uint64 UProject_JNPCPathSubsystem::Submit(UObject* Owner, APawn* Pawn, const FVector& Goal, uint64 Revision,
	TFunction<void(const FProjectJNPCPathCompletion&)> Callback)
{
	check(IsInGameThread());
	if (!IsActiveServer() || !IsValid(Owner) || Owner->GetWorld() != GetWorld() || !IsValid(Pawn)
		|| Pawn->GetWorld() != GetWorld() || !Pawn->HasAuthority() || Pawn->IsActorBeingDestroyed()
		|| !IsValid(Pawn->GetController()) || !ValidPosition(Goal) || !ValidPosition(Pawn->GetNavAgentLocation())
		|| !Callback || Revision == 0 || Requests.Num() >= MaxRequests
		|| Requests.ContainsByPredicate([Owner](const auto& R) { return R->Owner.Get() == Owner; }))
	{
		++Stats.Rejected; return 0;
	}
	const auto Entry = MakeShared<FProjectJNPCPathEntry>();
	Entry->Owner = Owner; Entry->Pawn = Pawn; Entry->Controller = Pawn->GetController();
	Entry->Result.Token = ++NextToken;
	if (Entry->Result.Token == 0) { Entry->Result.Token = ++NextToken; }
	Entry->Result.IntentRevision = Revision; Entry->Result.Goal = Goal;
	Entry->Deadline = FPlatformTime::Seconds() + RequestLifetimeSeconds;
	Entry->Callback = MoveTemp(Callback);
	Requests.Add(Entry); ++Stats.Accepted;
	return Entry->Result.Token;
}

void UProject_JNPCPathSubsystem::Cancel(uint64 Token)
{
	check(IsInGameThread());
	for (int32 Index = 0; Index < Requests.Num(); ++Index)
	{
		const auto R = Requests[Index];
		if (R->Result.Token != Token || R->bCancelled) { continue; }
		R->bCancelled = true; R->Callback = nullptr; R->Result.Path.Reset(); ++Stats.Cancelled;
		// Aborting an engine-queued query suppresses its completion too. Keep dispatched queries
		// until completion instead, so repeated cancellation cannot evade the in-flight limit.
		if (!R->bDispatched || R->bFinished) { Requests.RemoveAt(Index); }
		return;
	}
}

void UProject_JNPCPathSubsystem::Complete(uint64 Token, uint32 EngineId, ENavigationQueryResult::Type Result, FNavPathSharedPtr Path)
{
	// UE 5.8 dispatches these delegates from UNavigationSystemV1::Tick on GT.
	check(IsInGameThread());
	if (!IsActiveServer()) { return; }
	for (const auto& R : Requests)
	{
		if (R->Result.Token != Token || !R->bDispatched || R->EngineId != EngineId || R->bFinished) { continue; }
		R->bFinished = true;
		if (R->bCancelled || R->bDelivered) { ++Stats.Discarded; return; }
		if (Result == ENavigationQueryResult::Success && Path.IsValid() && Path->IsValid() && !Path->IsPartial())
		{
			// No implicit synchronous re-path: the consumer explicitly requests a new async path.
			Path->EnableRecalculationOnInvalidation(false);
			R->Result.Status = EProjectJNPCPathStatus::Success;
			R->Result.Path = MoveTemp(Path);
		}
		return;
	}
}

void UProject_JNPCPathSubsystem::Tick(float DeltaTime)
{
	check(IsInGameThread());
	if (!IsActiveServer()) { return; }
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NPCPath_AdmissionAndDelivery);
	Stats.LastTickDispatches = Stats.LastTickDeliveries = 0;
	const double Now = FPlatformTime::Seconds();
	// Snapshot permits callbacks to cancel/submit/tear down without invalidating iteration.
	const auto Snapshot = Requests;
	for (const auto& R : Snapshot)
	{
		if ((FPlatformTime::Seconds() - Now) * 1000.0 >= GameThreadBudgetMilliseconds) { break; }
		if (!IsActiveServer()) { return; }
		if (!Requests.Contains(R)) { continue; }
		auto* Pawn = R->Pawn.Get();
		if (!R->Owner.IsValid() || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !Pawn->HasAuthority()
			|| !R->Controller.IsValid() || Pawn->GetController() != R->Controller.Get()) { Cancel(R->Result.Token); }
		if (R->bCancelled || R->bDelivered)
		{
			if (!R->bDispatched || R->bFinished) { Requests.RemoveSingle(R); }
			continue;
		}
		if (Now >= R->Deadline || R->bFinished)
		{
			if (Stats.LastTickDeliveries >= MaxDeliveriesPerTick) { continue; }
			if (Now >= R->Deadline)
			{
				R->Result.Status = EProjectJNPCPathStatus::Expired; R->Result.Path.Reset(); ++Stats.Expired;
			}
			R->bDelivered = true;
			if (!R->bDispatched || R->bFinished) { Requests.RemoveSingle(R); }
			auto Callback = MoveTemp(R->Callback);
			++Stats.LastTickDeliveries; ++Stats.Delivered;
			if (Callback) { Callback(R->Result); }
			continue;
		}
		if (R->bDispatched || Stats.LastTickDispatches >= MaxDispatchesPerTick) { continue; }
		++Stats.LastTickDispatches;
		auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		const FVector Start = Pawn->GetNavAgentLocation();
		const ANavigationData* Data = Nav && ValidPosition(Start) ? Nav->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef(), Start) : nullptr;
		if (!Data) { R->bFinished = true; continue; }
		R->Navigation = Nav; R->Result.Start = Start;
		FPathFindingQuery Query(Pawn, *Data, Start, R->Result.Goal, Data->GetDefaultQueryFilter());
		Query.SetAllowPartialPaths(false);
		const TWeakObjectPtr<ThisClass> WeakThis(this);
		const uint64 Token = R->Result.Token;
		R->EngineId = Nav->FindPathAsync(Pawn->GetNavAgentPropertiesRef(), Query,
			FNavPathQueryDelegate::CreateLambda([WeakThis, Token](uint32 Id, ENavigationQueryResult::Type Result, FNavPathSharedPtr Path)
			{
				if (auto* Self = WeakThis.Get()) { Self->Complete(Token, Id, Result, MoveTemp(Path)); }
			}));
		R->bDispatched = R->EngineId != INVALID_NAVQUERYID;
		R->bFinished = !R->bDispatched;
		if (R->bDispatched) { ++Stats.Dispatched; }
	}
}

void UProject_JNPCPathSubsystem::Stop()
{
	check(IsInGameThread());
	bAccepting = false;
	for (const auto& R : Requests)
	{
		R->Callback = nullptr;
		if (R->bDispatched && !R->bFinished)
		{
			if (auto* Nav = R->Navigation.Get()) { Nav->AbortAsyncFindPathRequest(R->EngineId); }
		}
	}
	Requests.Empty();
}
void UProject_JNPCPathSubsystem::OnTearDown(UWorld* World) { if (World == GetWorld()) { Stop(); } }
void UProject_JNPCPathSubsystem::OnWorldEndPlay(UWorld& World) { Stop(); Super::OnWorldEndPlay(World); }
void UProject_JNPCPathSubsystem::Deinitialize()
{
	Stop(); FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle); Super::Deinitialize();
}
bool UProject_JNPCPathSubsystem::IsTickable() const { return !IsTemplate() && IsActiveServer() && !Requests.IsEmpty(); }
TStatId UProject_JNPCPathSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(ProjectJNPCPathSubsystem, STATGROUP_Tickables); }

#if WITH_DEV_AUTOMATION_TESTS
void UProject_JNPCPathSubsystem::SimulateDispatchForTest(uint64 Token, bool bExpired)
{
	check(IsInGameThread());
	for (const auto& R : Requests)
	{
		if (R->Result.Token != Token) { continue; }
		R->bDispatched = true; R->EngineId = 1;
		if (bExpired) { R->Deadline = -1; }
	}
}
#endif
