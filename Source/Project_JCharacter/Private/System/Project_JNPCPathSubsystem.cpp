#include "System/Project_JNPCPathSubsystem.h"
#include "NavigationSystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CountersTrace.h"

TRACE_DECLARE_INT_COUNTER(NPCPathQueued, TEXT("ProjectJ/NPCPath/Queued"));
TRACE_DECLARE_INT_COUNTER(NPCPathInFlight, TEXT("ProjectJ/NPCPath/InFlight"));
TRACE_DECLARE_INT_COUNTER(NPCPathTombstones, TEXT("ProjectJ/NPCPath/Tombstones"));
TRACE_DECLARE_INT_COUNTER(NPCPathAccepted, TEXT("ProjectJ/NPCPath/Accepted"));
TRACE_DECLARE_INT_COUNTER(NPCPathRejected, TEXT("ProjectJ/NPCPath/Rejected"));
TRACE_DECLARE_INT_COUNTER(NPCPathDelivered, TEXT("ProjectJ/NPCPath/Delivered"));
TRACE_DECLARE_INT_COUNTER(NPCPathCancelled, TEXT("ProjectJ/NPCPath/Cancelled"));
TRACE_DECLARE_INT_COUNTER(NPCPathExpired, TEXT("ProjectJ/NPCPath/Expired"));
TRACE_DECLARE_INT_COUNTER(NPCPathDiscarded, TEXT("ProjectJ/NPCPath/Discarded"));
TRACE_DECLARE_INT_COUNTER(NPCPathPeakInFlight, TEXT("ProjectJ/NPCPath/PeakInFlight"));
TRACE_DECLARE_INT_COUNTER(NPCPathPeakQueued, TEXT("ProjectJ/NPCPath/PeakQueued"));
TRACE_DECLARE_INT_COUNTER(NPCPathIgnoredAfterStop, TEXT("ProjectJ/NPCPath/IgnoredAfterStop"));
TRACE_DECLARE_FLOAT_COUNTER(NPCPathQueueMs, TEXT("ProjectJ/NPCPath/LastQueueMs"));
TRACE_DECLARE_FLOAT_COUNTER(NPCPathEngineMs, TEXT("ProjectJ/NPCPath/LastEngineObservedMs"));
TRACE_DECLARE_FLOAT_COUNTER(NPCPathDeliveryMs, TEXT("ProjectJ/NPCPath/LastDeliveryMs"));
TRACE_DECLARE_FLOAT_COUNTER(NPCPathPolicyMs, TEXT("ProjectJ/NPCPath/PolicyMs"));

struct FProjectJNPCPathEntry
{
	TWeakObjectPtr<UObject> Owner;
	TWeakObjectPtr<APawn> Pawn;
	TWeakObjectPtr<AController> Controller;
	TWeakObjectPtr<UNavigationSystemV1> Navigation;
	FProjectJNPCPathCompletion Result;
	TFunction<void(const FProjectJNPCPathCompletion&)> Callback;
	double Deadline = 0;
	double Submitted = 0;
	double DispatchedAt = 0, FinishedAt = 0;
#if WITH_DEV_AUTOMATION_TESTS
	int32 CapturePhase = INDEX_NONE;
	uint32 CapturedPawnId = 0;
#endif
	EProjectJNPCPathPriority Priority = EProjectJNPCPathPriority::Normal;
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
	return bAccepting && IsInitialized() && GetWorld() && !GetWorld()->bIsTearingDown && GetWorld()->GetNetMode() != NM_Client;
}

uint64 UProject_JNPCPathSubsystem::Submit(UObject* Owner, APawn* Pawn, const FVector& Goal, uint64 Revision,
	TFunction<void(const FProjectJNPCPathCompletion&)> Callback, EProjectJNPCPathPriority Priority)
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
#if WITH_DEV_AUTOMATION_TESTS
	Entry->CapturePhase = CapturePhase; Entry->CapturedPawnId = Pawn->GetUniqueID();
#endif
	Entry->Result.Token = ++NextToken;
	if (Entry->Result.Token == 0) { Entry->Result.Token = ++NextToken; }
	Entry->Result.IntentRevision = Revision; Entry->Result.Goal = Goal;
	Entry->Submitted = FPlatformTime::Seconds(); Entry->Priority = Priority;
	Entry->Deadline = Entry->Submitted + RequestLifetimeSeconds;
	Entry->Callback = MoveTemp(Callback);
	Requests.Add(Entry); ++Stats.Accepted;
	int32 QueuedAtAdmission = 0;
	for (const auto& R : Requests) { QueuedAtAdmission += !R->bDispatched && !R->bFinished && !R->bCancelled ? 1 : 0; }
	Stats.PeakQueued = FMath::Max(Stats.PeakQueued, QueuedAtAdmission);
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
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NPCPath_EngineCompletion);
	if (!IsActiveServer()) { ++Stats.IgnoredAfterStop; TRACE_COUNTER_SET(NPCPathIgnoredAfterStop, Stats.IgnoredAfterStop); return; }
	for (const auto& R : Requests)
	{
		if (R->Result.Token != Token || !R->bDispatched || R->EngineId != EngineId || R->bFinished) { continue; }
		R->bFinished = true;
		R->FinishedAt = FPlatformTime::Seconds();
		R->Result.EngineMilliseconds = (R->FinishedAt - R->DispatchedAt) * 1000.0;
		++Stats.EngineCompleted; Stats.EngineMilliseconds += R->Result.EngineMilliseconds;
		TRACE_COUNTER_SET(NPCPathEngineMs, R->Result.EngineMilliseconds);
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
	if (!IsActiveServer() || bTicking) { return; }
	TGuardValue<bool> TickGuard(bTicking, true);
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NPCPath_AdmissionAndDelivery);
	++Stats.TickSequence;
	Stats.LastTickDispatches = Stats.LastTickDeliveries = 0;
	const double Now = FPlatformTime::Seconds();
	// Snapshot permits callbacks to cancel/submit/tear down without invalidating iteration.
	auto Snapshot = Requests;
	// A finite urgency credit lets old normal requests overtake newly arriving urgent ones.
	Snapshot.StableSort([](const auto& A, const auto& B)
	{
		return A->Submitted - (A->Priority == EProjectJNPCPathPriority::Urgent ? UrgentBoostSeconds : 0)
			< B->Submitted - (B->Priority == EProjectJNPCPathPriority::Urgent ? UrgentBoostSeconds : 0);
	});
	int32 InFlight = 0;
	for (const auto& R : Snapshot) { InFlight += R->bDispatched && !R->bFinished ? 1 : 0; }
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
			const double DeliveredAt = FPlatformTime::Seconds();
			R->Result.TotalMilliseconds = (DeliveredAt - R->Submitted) * 1000.0;
			R->Result.DeliveryMilliseconds = R->FinishedAt > 0 ? (DeliveredAt - R->FinishedAt) * 1000.0 : 0;
			Stats.DeliveryMilliseconds += R->Result.DeliveryMilliseconds;
			TRACE_COUNTER_SET(NPCPathDeliveryMs, R->Result.DeliveryMilliseconds);
			Stats.Succeeded += R->Result.Status == EProjectJNPCPathStatus::Success ? 1 : 0;
			Stats.Failed += R->Result.Status == EProjectJNPCPathStatus::Failed ? 1 : 0;
			Stats.MaxDeliveryMilliseconds = FMath::Max(Stats.MaxDeliveryMilliseconds, R->Result.TotalMilliseconds);
#if WITH_DEV_AUTOMATION_TESTS
			if (R->CapturePhase != INDEX_NONE)
			{
				if (LatencySamples.Num() < 1024)
				{
					LatencySamples.Add({R->Result.Token, R->CapturedPawnId, R->CapturePhase, R->Result.Status,
						R->Result.QueueMilliseconds, R->Result.EngineMilliseconds, R->Result.DeliveryMilliseconds, R->Result.TotalMilliseconds});
				}
				else { ++DroppedLatencySamples; }
			}
#endif
			if (Callback) { TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NPCPath_ConsumerDelivery); Callback(R->Result); }
			continue;
		}
		if (R->bDispatched || Stats.LastTickDispatches >= MaxDispatchesPerTick || InFlight >= MaxInFlight) { continue; }
		++Stats.LastTickDispatches;
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NPCPath_Dispatch);
		R->DispatchedAt = FPlatformTime::Seconds();
		R->Result.QueueMilliseconds = (R->DispatchedAt - R->Submitted) * 1000.0;
		TRACE_COUNTER_SET(NPCPathQueueMs, R->Result.QueueMilliseconds);
		auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		const FVector Start = Pawn->GetNavAgentLocation();
		const ANavigationData* Data = Nav && ValidPosition(Start) ? Nav->GetNavDataForProps(Pawn->GetNavAgentPropertiesRef(), Start) : nullptr;
		if (!Data) { R->bFinished = true; R->FinishedAt = FPlatformTime::Seconds(); continue; }
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
		if (R->bDispatched) { ++Stats.Dispatched; ++InFlight; Stats.QueueMilliseconds += R->Result.QueueMilliseconds; }
		else { R->FinishedAt = FPlatformTime::Seconds(); }
	}
	Stats.InFlight = InFlight; Stats.PeakInFlight = FMath::Max(Stats.PeakInFlight, InFlight);
	Stats.Queued = Stats.Tombstones = 0;
	for (const auto& R : Requests)
	{
		Stats.Queued += !R->bDispatched && !R->bFinished && !R->bCancelled ? 1 : 0;
		Stats.Tombstones += R->bDispatched && !R->bFinished && (R->bCancelled || R->bDelivered) ? 1 : 0;
	}
	Stats.PeakQueued = FMath::Max(Stats.PeakQueued, Stats.Queued);
	Stats.LastTickMilliseconds = (FPlatformTime::Seconds() - Now) * 1000.0;
	// These counters describe the last serviced world, never a sum across PIE worlds.
	TRACE_COUNTER_SET(NPCPathQueued, Stats.Queued);
	TRACE_COUNTER_SET(NPCPathInFlight, Stats.InFlight);
	TRACE_COUNTER_SET(NPCPathTombstones, Stats.Tombstones);
	TRACE_COUNTER_SET(NPCPathAccepted, Stats.Accepted);
	TRACE_COUNTER_SET(NPCPathRejected, Stats.Rejected);
	TRACE_COUNTER_SET(NPCPathDelivered, Stats.Delivered);
	TRACE_COUNTER_SET(NPCPathCancelled, Stats.Cancelled);
	TRACE_COUNTER_SET(NPCPathExpired, Stats.Expired);
	TRACE_COUNTER_SET(NPCPathDiscarded, Stats.Discarded);
	TRACE_COUNTER_SET(NPCPathPeakInFlight, Stats.PeakInFlight);
	TRACE_COUNTER_SET(NPCPathPeakQueued, Stats.PeakQueued);
	TRACE_COUNTER_SET(NPCPathPolicyMs, Stats.LastTickMilliseconds);
}

void UProject_JNPCPathSubsystem::Stop()
{
	check(IsInGameThread());
	bAccepting = false;
#if WITH_DEV_AUTOMATION_TESTS
	CapturePhase = INDEX_NONE;
#endif
	Stats.StoppedRequests += Requests.Num();
	for (const auto& R : Requests)
	{
		R->Callback = nullptr;
		if (R->bDispatched && !R->bFinished)
		{
			if (auto* Nav = R->Navigation.Get()) { Nav->AbortAsyncFindPathRequest(R->EngineId); }
		}
	}
	Requests.Empty();
	Stats.InFlight = Stats.Queued = Stats.Tombstones = 0;
	TRACE_COUNTER_SET(NPCPathQueued, 0); TRACE_COUNTER_SET(NPCPathInFlight, 0); TRACE_COUNTER_SET(NPCPathTombstones, 0);
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
void UProject_JNPCPathSubsystem::AgeForTest(uint64 Token, double Seconds)
{
	for (const auto& R : Requests) { if (R->Result.Token == Token) { R->Submitted -= Seconds; } }
}
void UProject_JNPCPathSubsystem::SimulateDispatchForTest(uint64 Token, bool bExpired)
{
	check(IsInGameThread());
	for (const auto& R : Requests)
	{
		if (R->Result.Token != Token) { continue; }
		R->bDispatched = true; R->EngineId = 1; R->DispatchedAt = FPlatformTime::Seconds();
		if (bExpired) { R->Deadline = -1; }
	}
}
#endif
