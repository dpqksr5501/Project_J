#include "System/Project_JTargetScoringSubsystem.h"
#include "Tasks/Task.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"

namespace
{
	struct FWork
	{
		explicit FWork(TArray<ProjectJ::TargetScoring::FSnapshot>&& Input) : Snapshots(MoveTemp(Input)) {}
		const TArray<ProjectJ::TargetScoring::FSnapshot> Snapshots;
		std::atomic<bool> Cancelled{false};
	};
	struct FTrackedTask
	{
		TSharedPtr<FWork, ESPMode::ThreadSafe> Work;
		UE::Tasks::TTask<ProjectJ::TargetScoring::FBatchResult> Task;
	};
	// Accessed only on GT. Retains data-only jobs across world teardown, not worlds or callbacks.
	TArray<FTrackedTask> TrackedTasks;
	int64 NextToken = 0;
	bool bShuttingDown = false;
	bool IsLiveOwner(const UObject* Owner)
	{
		if (!IsValid(Owner)) { return false; }
		if (const AActor* Actor = Cast<AActor>(Owner)) { return !Actor->IsActorBeingDestroyed(); }
		if (const UActorComponent* Component = Cast<UActorComponent>(Owner))
		{
			return !Component->IsBeingDestroyed() && IsValid(Component->GetOwner()) && !Component->GetOwner()->IsActorBeingDestroyed();
		}
		return true;
	}
}

struct FProjectJTargetScoringJob
{
	FProject_JGameplayAsyncRequestToken Token;
	TWeakObjectPtr<UObject> Owner;
	uint64 Revision = 0;
	double Submitted = 0.0;
	double Deadline = 0.0;
	EProject_JTargetScoringExecution Mode = EProject_JTargetScoringExecution::Serial;
	UProject_JTargetScoringSubsystem::FBatchCompletion Completion;
	TSharedPtr<FWork, ESPMode::ThreadSafe> Work;
	UE::Tasks::TTask<ProjectJ::TargetScoring::FBatchResult> Task;
	bool bExpired = false;
};

void ProjectJ::TargetScoring::DrainTasksForModuleShutdown()
{
	check(IsInGameThread());
	bShuttingDown = true;
	for (FTrackedTask& Entry : TrackedTasks) { Entry.Work->Cancelled.store(true, std::memory_order_relaxed); }
	// No worker ever depends on GT. Joining here prevents code unload during execution.
	for (FTrackedTask& Entry : TrackedTasks) { Entry.Task.Wait(); }
	TrackedTasks.Empty();
}

bool UProject_JTargetScoringSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE;
}

void UProject_JTargetScoringSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	WorldEpoch = FGuid::NewGuid();
	bAcceptingQueries = true;
	TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &ThisClass::OnWorldTearDown);
}

void UProject_JTargetScoringSubsystem::StopQueries()
{
	check(IsInGameThread());
	bAcceptingQueries = false;
	for (const auto& Job : Jobs)
	{
		Job->Work->Cancelled.store(true, std::memory_order_relaxed);
		Job->Completion = nullptr;
	}
	Jobs.Empty();
	WorldEpoch.Invalidate();
	// Running tasks contain copied values only; map transitions never wait for them.
}

void UProject_JTargetScoringSubsystem::OnWorldTearDown(UWorld* InWorld)
{
	if (InWorld == GetWorld()) { StopQueries(); }
}

void UProject_JTargetScoringSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	StopQueries();
	Super::OnWorldEndPlay(InWorld);
}

void UProject_JTargetScoringSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle);
	StopQueries();
	Super::Deinitialize();
}

FProject_JGameplayAsyncRequestToken UProject_JTargetScoringSubsystem::Submit(UObject* Owner,
	ProjectJ::TargetScoring::FSnapshot Snapshot, EProject_JTargetScoringExecution Mode, uint64 Revision,
	FCompletion Completion, double TimeoutSeconds)
{
	check(IsInGameThread());
	if (!Completion) { return {}; }
	TArray<ProjectJ::TargetScoring::FSnapshot> Snapshots;
	Snapshots.Add(MoveTemp(Snapshot));
	return SubmitBatch(Owner, MoveTemp(Snapshots), Mode, Revision,
		[Callback = MoveTemp(Completion)](const FProjectJTargetScoringBatchCompletion& Batch)
		{
			FProjectJTargetScoringCompletion Single;
			Single.Token = Batch.Token;
			Single.WorldEpoch = Batch.WorldEpoch;
			Single.ContextRevision = Batch.ContextRevision;
			Single.DeliveryMilliseconds = Batch.DeliveryMilliseconds;
			if (Batch.Result.Results.Num() == 1) { Single.Result = Batch.Result.Results[0]; }
			Callback(Single);
		}, TimeoutSeconds);
}

FProject_JGameplayAsyncRequestToken UProject_JTargetScoringSubsystem::SubmitBatch(UObject* Owner,
	TArray<ProjectJ::TargetScoring::FSnapshot> Snapshots, EProject_JTargetScoringExecution Mode, uint64 Revision,
	FBatchCompletion Completion, double TimeoutSeconds)
{
	check(IsInGameThread());
	if (!IsInitialized() || !bAcceptingQueries || bShuttingDown || !IsLiveOwner(Owner) || Owner->GetWorld() != GetWorld() || !Completion
		|| Snapshots.IsEmpty() || Snapshots.Num() > ProjectJ::TargetScoring::MaxQueriesPerBatch || Jobs.Num() >= MaxPendingRequests
		|| !FMath::IsFinite(TimeoutSeconds) || TimeoutSeconds <= 0.0 || TimeoutSeconds > 30.0
		|| static_cast<uint8>(Mode) > static_cast<uint8>(EProject_JTargetScoringExecution::TaskParallelFor)
		|| NextToken == MAX_int64)
	{
		return {};
	}
	int32 TotalCandidates = 0;
	for (const auto& Snapshot : Snapshots)
	{
		if (Snapshot.Candidates.Num() > ProjectJ::TargetScoring::MaxCandidates - TotalCandidates) { return {}; }
		TotalCandidates += Snapshot.Candidates.Num();
	}
	const auto Job = MakeShared<FProjectJTargetScoringJob>();
	Job->Token.Value = ++NextToken;
	Job->Owner = Owner;
	Job->Revision = Revision;
	Job->Submitted = FPlatformTime::Seconds();
	Job->Deadline = Job->Submitted + TimeoutSeconds;
	Job->Mode = Mode;
	Job->Completion = MoveTemp(Completion);
	Job->Work = MakeShared<FWork, ESPMode::ThreadSafe>(MoveTemp(Snapshots));
	Jobs.Add(Job);
	return Job->Token;
}

void UProject_JTargetScoringSubsystem::Cancel(FProject_JGameplayAsyncRequestToken Token)
{
	check(IsInGameThread());
	for (int32 Index = Jobs.Num() - 1; Index >= 0; --Index)
	{
		const auto& Job = Jobs[Index];
		if (Job->Token.Value != Token.Value) { continue; }
		Job->Completion = nullptr;
		Job->Work->Cancelled.store(true, std::memory_order_relaxed);
		if (!Job->Task.IsValid()) { Jobs.RemoveAt(Index); }
		return;
	}
}

void UProject_JTargetScoringSubsystem::CancelForOwner(const UObject* Owner)
{
	check(IsInGameThread());
	TArray<FProject_JGameplayAsyncRequestToken> Tokens;
	for (const auto& Job : Jobs) { if (Job->Owner.Get() == Owner) { Tokens.Add(Job->Token); } }
	for (const auto Token : Tokens) { Cancel(Token); }
}

bool UProject_JTargetScoringSubsystem::IsPending(FProject_JGameplayAsyncRequestToken Token) const
{
	check(IsInGameThread());
	return Jobs.ContainsByPredicate([Token](const auto& Job) { return Job->Token.Value == Token.Value && !!Job->Completion; });
}

bool UProject_JTargetScoringSubsystem::IsTickable() const { return IsInitialized() && !Jobs.IsEmpty(); }
TStatId UProject_JTargetScoringSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(ProjectJ_TargetScoring, STATGROUP_Tickables); }

void UProject_JTargetScoringSubsystem::Tick(float DeltaTime)
{
	check(IsInGameThread());
	if (!IsInitialized() || !bAcceptingQueries) { return; }
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_TargetScoring_DispatchAndApply);
	TrackedTasks.RemoveAll([](const FTrackedTask& Entry) { return Entry.Task.IsCompleted(); });
	struct FReady { TSharedPtr<FProjectJTargetScoringJob> Job; FProjectJTargetScoringBatchCompletion Completion; };
	TArray<FReady, TInlineAllocator<MaxCompletionsPerTick>> Ready;
	for (int32 Index = 0; Index < Jobs.Num() && Ready.Num() < MaxCompletionsPerTick;)
	{
		const auto Job = Jobs[Index];
		if (!IsLiveOwner(Job->Owner.Get())) { Job->Completion = nullptr; Job->Work->Cancelled.store(true, std::memory_order_relaxed); }
		if (FPlatformTime::Seconds() >= Job->Deadline)
		{
			Job->bExpired = true;
			Job->Work->Cancelled.store(true, std::memory_order_relaxed);
		}
		ProjectJ::TargetScoring::FBatchResult Result;
		if (Job->Task.IsValid())
		{
			if (!Job->Task.IsCompleted()) { ++Index; continue; }
			// GetResult contains Wait internally; only call after completion is published.
			Result = Job->Task.GetResult();
		}
		else if (Job->Work->Cancelled.load(std::memory_order_relaxed))
		{
			Result.Results.SetNum(Job->Work->Snapshots.Num());
			for (auto& Entry : Result.Results) { Entry.Status = ProjectJ::TargetScoring::EStatus::Cancelled; }
		}
		else if (Job->Mode == EProject_JTargetScoringExecution::Serial)
		{
			Result = ProjectJ::TargetScoring::EvaluateBatch(Job->Work->Snapshots, false, Job->Work->Cancelled);
		}
		else
		{
			if (TrackedTasks.Num() >= MaxGlobalWorkerTasks) { ++Index; continue; }
			// The lambda deliberately captures no Job, delegate, weak object, subsystem, or world.
			const auto Work = Job->Work;
			const bool bParallel = Job->Mode == EProject_JTargetScoringExecution::TaskParallelFor;
			Job->Task = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Work, bParallel]()
			{
				return ProjectJ::TargetScoring::EvaluateBatch(Work->Snapshots, bParallel, Work->Cancelled);
			});
			TrackedTasks.Add({Work, Job->Task});
			++LaunchedTaskCount;
			++Index;
			continue;
		}
		if (Job->bExpired)
		{
			for (auto& Entry : Result.Results) { Entry.Status = ProjectJ::TargetScoring::EStatus::Expired; }
		}
		FProjectJTargetScoringBatchCompletion Completion;
		Completion.Token = Job->Token;
		Completion.WorldEpoch = WorldEpoch;
		Completion.ContextRevision = Job->Revision;
		Completion.Result = MoveTemp(Result);
		Completion.DeliveryMilliseconds = (FPlatformTime::Seconds() - Job->Submitted) * 1000.0;
		Ready.Add({Job, Completion});
		++Index;
	}
	// Remove before invoking user code: callbacks may submit/cancel work or destroy this world.
	for (FReady& Entry : Ready)
	{
		const bool bStillPending = Jobs.RemoveSingle(Entry.Job) != 0;
		if (bStillPending && IsInitialized() && WorldEpoch == Entry.Completion.WorldEpoch && IsLiveOwner(Entry.Job->Owner.Get()) && Entry.Job->Completion)
		{
			Entry.Job->Completion(Entry.Completion);
		}
	}
}
