#include "System/Project_JNPCDecisionSubsystem.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "Project_JNPCCharacter.h"
#include "Project_JCombatInterface.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

struct FProjectJNPCDecisionAgent
{
	TWeakObjectPtr<UProject_JTargetScoringComponent> Component;
	int32 Team = 0;
	double NextDue = 0.0;
	bool bRegistered = true;
	bool bPending = false;
};

struct FProjectJNPCDecisionReady
{
	TSharedPtr<FProjectJNPCDecisionAgent> Agent;
	FProjectJTargetScoringCompletion Completion;
	double Submitted = 0.0;
};

namespace
{
	bool IsLivingActor(AActor* Actor)
	{
		return IsValid(Actor) && !Actor->IsActorBeingDestroyed()
			&& (!Actor->Implements<UProject_JCombatInterface>() || !IProject_JCombatInterface::Execute_IsDead(Actor));
	}
}

bool UProject_JNPCDecisionSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE;
}

void UProject_JNPCDecisionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Scoring = Collection.InitializeDependency<UProject_JTargetScoringSubsystem>();
	bAccepting = true;
	TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &ThisClass::OnWorldTearDown);
}

bool UProject_JNPCDecisionSubsystem::IsActiveServer() const
{
	return IsInitialized() && bAccepting && GetWorld() && CanScheduleNetworkMode(GetWorld()->GetNetMode());
}

bool UProject_JNPCDecisionSubsystem::RegisterAgent(UProject_JTargetScoringComponent* Component, int32 TeamId)
{
	check(IsInGameThread());
	auto* NPC = IsValid(Component) ? Cast<AProject_JNPCCharacter>(Component->GetOwner()) : nullptr;
	if (!IsActiveServer() || TeamId < 0 || !IsValid(NPC) || !NPC->HasAuthority() || NPC->IsActorBeingDestroyed()
		|| NPC->GetWorld() != GetWorld() || !Component->IsRegistered() || Component->bEndingPlay || Component->IsBeingDestroyed()) { return false; }
	for (const auto& Agent : Agents)
	{
		if (Agent->Component.Get() == Component)
		{
			if (Agent->Team != TeamId) { Agent->Team = TeamId; Component->InvalidateQueryContext(); }
			return true;
		}
	}
	if (Agents.Num() >= MaxAgents) { return false; }
	Component->InvalidateQueryContext();
	const auto Agent = MakeShared<FProjectJNPCDecisionAgent>();
	Agent->Component = Component;
	Agent->Team = TeamId;
	Agent->NextDue = FPlatformTime::Seconds();
	Agents.Add(Agent);
	Component->NPCDecisionSubsystem = this;
	Component->bNPCBatchRegistered = true;
	return true;
}

void UProject_JNPCDecisionSubsystem::UnregisterAgent(UProject_JTargetScoringComponent* Component)
{
	check(IsInGameThread());
	Agents.RemoveAll([Component](const auto& Agent)
	{
		if (Agent->Component.Get() != Component) { return false; }
		Agent->bRegistered = false;
		return true;
	});
	if (IsValid(Component))
	{
		Component->bNPCBatchRegistered = false;
		Component->NPCDecisionSubsystem.Reset();
		Component->InvalidateQueryContext();
	}
	if (Agents.IsEmpty())
	{
		if (auto* Service = Scoring.Get()) { Service->CancelForOwner(this); }
		Ready.Empty(); ActiveBatches.Empty(); OutstandingDecisions = 0; Cursor = 0;
	}
}

bool UProject_JNPCDecisionSubsystem::RegisterTarget(AActor* Target, int32 TeamId)
{
	check(IsInGameThread());
	if (!IsActiveServer() || TeamId < 0 || !IsValid(Target) || !Target->HasAuthority()
		|| Target->IsActorBeingDestroyed() || Target->GetWorld() != GetWorld()) { return false; }
	for (auto& Entry : Targets)
	{
		if (Entry.Actor.Get() == Target)
		{
			Entry.Team = TeamId;
			// A faction change must not leave a cached friendly target selected.
			for (const auto& Agent : Agents)
			{
				if (auto* Component = Agent->Component.Get(); Component && Agent->Team == TeamId && Component->SelectedTarget.Get() == Target)
				{
					Component->InvalidateQueryContext();
				}
			}
			return true;
		}
	}
	Targets.RemoveAll([](const FTarget& Entry) { return !Entry.Actor.IsValid(); });
	if (Targets.Num() >= MaxTargets) { return false; }
	Targets.Add({Target, TeamId});
	return true;
}

void UProject_JNPCDecisionSubsystem::UnregisterTarget(AActor* Target)
{
	check(IsInGameThread());
	Targets.RemoveAll([Target](const FTarget& Entry) { return Entry.Actor.Get() == Target; });
	for (const auto& Agent : Agents)
	{
		if (auto* Component = Agent->Component.Get(); Component && Component->SelectedTarget.Get() == Target) { Component->InvalidateQueryContext(); }
	}
}

bool UProject_JNPCDecisionSubsystem::IsEligibleTarget(AActor* Target, int32 Team) const
{
	return IsLivingActor(Target) && Target->GetWorld() == GetWorld()
		&& Targets.ContainsByPredicate([Target, Team](const FTarget& Entry) { return Entry.Actor.Get() == Target && Entry.Team != Team; });
}

void UProject_JNPCDecisionSubsystem::StopScheduler()
{
	check(IsInGameThread());
	bAccepting = false;
	if (auto* Service = Scoring.Get()) { Service->CancelForOwner(this); }
	const auto PreviousAgents = MoveTemp(Agents);
	Ready.Empty(); ActiveBatches.Empty(); Targets.Empty(); OutstandingDecisions = 0;
	for (const auto& Agent : PreviousAgents)
	{
		Agent->bRegistered = false;
		if (auto* Component = Agent->Component.Get())
		{
			Component->bNPCBatchRegistered = false;
			Component->NPCDecisionSubsystem.Reset();
			Component->InvalidateQueryContext();
		}
	}
}

void UProject_JNPCDecisionSubsystem::OnWorldTearDown(UWorld* World) { if (World == GetWorld()) { StopScheduler(); } }
void UProject_JNPCDecisionSubsystem::OnWorldEndPlay(UWorld& World) { StopScheduler(); Super::OnWorldEndPlay(World); }
void UProject_JNPCDecisionSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle);
	StopScheduler();
	Super::Deinitialize();
}
bool UProject_JNPCDecisionSubsystem::IsTickable() const { return IsActiveServer() && (!Agents.IsEmpty() || OutstandingDecisions > 0); }
TStatId UProject_JNPCDecisionSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(ProjectJ_NPCDecision, STATGROUP_Tickables); }

void UProject_JNPCDecisionSubsystem::QueueResults(const FProjectJTargetScoringBatchCompletion& Batch,
	const TArray<TSharedPtr<FProjectJNPCDecisionAgent>>& BatchAgents, const TArray<uint64>& Revisions, double Submitted)
{
	check(IsInGameThread());
	if (!IsActiveServer()) { return; }
	ActiveBatches.RemoveAll([&](const auto Token) { return Token.Value == Batch.Token.Value; });
	// Capacity was reserved before dispatch, including both in-flight and ready decisions.
	check(Ready.Num() + BatchAgents.Num() <= MaxOutstandingDecisions);
	for (int32 Index = 0; Index < BatchAgents.Num(); ++Index)
	{
		const auto Entry = MakeShared<FProjectJNPCDecisionReady>();
		Entry->Agent = BatchAgents[Index];
		Entry->Submitted = Submitted;
		Entry->Completion.Token = Batch.Token;
		Entry->Completion.WorldEpoch = Batch.WorldEpoch;
		Entry->Completion.ContextRevision = Revisions[Index];
		Entry->Completion.DeliveryMilliseconds = Batch.DeliveryMilliseconds;
		if (Batch.Result.Results.IsValidIndex(Index)) { Entry->Completion.Result = Batch.Result.Results[Index]; }
		Ready.Add(Entry);
	}
}

void UProject_JNPCDecisionSubsystem::Tick(float DeltaTime)
{
	check(IsInGameThread());
	if (!IsActiveServer() || !Scoring.IsValid()) { return; }
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NPCDecision_CollectAndApply);
	const double Started = FPlatformTime::Seconds();
	Stats.LastTickAgentVisits = Stats.LastTickCandidateVisits = Stats.LastTickResults = 0;
	const auto WithinBudget = [&]() { return (FPlatformTime::Seconds() - Started) * 1000.0 < GameThreadBudgetMilliseconds; };
	while (!Ready.IsEmpty() && Stats.LastTickResults < MaxResultsPerTick && WithinBudget())
	{
		const auto Entry = Ready[0];
		Ready.RemoveAt(0);
		--OutstandingDecisions;
		++Stats.LastTickResults;
		Entry->Agent->bPending = false;
		auto* Component = Entry->Agent->Component.Get();
		AActor* Owner = Component ? Component->GetOwner() : nullptr;
		const bool bCurrent = Component && Entry->Agent->bRegistered && Component->bNPCBatchRegistered
			&& Component->PendingToken.Value == Entry->Completion.Token.Value && Component->ContextRevision == Entry->Completion.ContextRevision;
		if (!bCurrent) { ++Stats.DiscardedDecisions; continue; }
		const int32 BestId = Entry->Completion.Result.BestId;
		AActor* Target = Component->CandidateActors.IsValidIndex(BestId) ? Component->CandidateActors[BestId].Get() : nullptr;
		if (!IsLivingActor(Owner) || !Owner->HasAuthority() || FPlatformTime::Seconds() - Entry->Submitted > MaxResultAgeSeconds
			|| Entry->Completion.Result.Status != ProjectJ::TargetScoring::EStatus::Completed
			|| (BestId != INDEX_NONE && !IsEligibleTarget(Target, Entry->Agent->Team)))
		{
			Component->InvalidateQueryContext();
			++Stats.DiscardedDecisions;
			continue;
		}
		++Stats.AppliedDecisions;
		Component->ApplyResult(Entry->Completion); // External callbacks only after queue/counters are updated.
		if (!IsActiveServer()) { return; }
	}

	TArray<ProjectJ::TargetScoring::FSnapshot> Snapshots;
	TArray<TSharedPtr<FProjectJNPCDecisionAgent>> BatchAgents;
	TArray<uint64> Revisions;
	const int32 VisitLimit = FMath::Min(Agents.Num(), MaxAgentVisitsPerTick);
	for (int32 Visit = 0; Visit < VisitLimit && !Agents.IsEmpty() && WithinBudget()
		&& BatchAgents.Num() < MaxQueriesPerDispatch && OutstandingDecisions + BatchAgents.Num() < MaxOutstandingDecisions; ++Visit)
	{
		Cursor %= Agents.Num();
		const auto Agent = Agents[Cursor];
		Cursor = (Cursor + 1) % Agents.Num();
		++Stats.LastTickAgentVisits;
		auto* Component = Agent->Component.Get();
		auto* NPC = Component ? Cast<AProject_JNPCCharacter>(Component->GetOwner()) : nullptr;
		if (!Component || Component->IsBeingDestroyed() || !IsValid(NPC) || NPC->IsActorBeingDestroyed() || !NPC->HasAuthority())
		{
			Agent->bRegistered = false;
			Agents.RemoveSingle(Agent);
			continue;
		}
		const double Now = FPlatformTime::Seconds();
		if (Agent->bPending || Now < Agent->NextDue) { continue; }
		const float Recommended = NPC->GetRecommendedAIUpdateInterval();
		const double Interval = FMath::IsFinite(Recommended) ? FMath::Clamp(double(Recommended), 0.05, 2.0) : 1.0;
		Agent->NextDue = Now + Interval; // No catch-up burst after stalls; round-robin prevents a fixed-index bias.
		if (!IsLivingActor(NPC)) { Component->InvalidateQueryContext(); continue; }
		TArray<AActor*> Candidates;
		for (const FTarget& Entry : Targets)
		{
			++Stats.LastTickCandidateVisits;
			AActor* Target = Entry.Actor.Get();
			if (Entry.Team != Agent->Team && Target != NPC && IsLivingActor(Target) && Target->GetWorld() == GetWorld()
				&& FVector::DistSquared(Target->GetActorLocation(), NPC->GetActorLocation()) <= FMath::Square(Component->Range))
			{
				Candidates.Add(Target);
			}
		}
		ProjectJ::TargetScoring::FSnapshot Snapshot;
		if (!Component->PrepareSnapshot(Candidates, Snapshot)) { continue; }
		Snapshots.Add(MoveTemp(Snapshot));
		BatchAgents.Add(Agent);
		Revisions.Add(Component->ContextRevision);
	}
	if (!Snapshots.IsEmpty())
	{
		const TWeakObjectPtr<ThisClass> WeakThis(this);
		const double Submitted = FPlatformTime::Seconds();
		const auto Token = Scoring->SubmitBatch(this, MoveTemp(Snapshots), EProject_JTargetScoringExecution::TaskParallelFor, 0,
			[WeakThis, BatchAgents, Revisions, Submitted](const FProjectJTargetScoringBatchCompletion& Completion)
			{
				if (auto* Self = WeakThis.Get()) { Self->QueueResults(Completion, BatchAgents, Revisions, Submitted); }
			});
		if (Token.IsValid())
		{
			ActiveBatches.Add(Token);
			OutstandingDecisions += BatchAgents.Num();
			++Stats.SubmittedBatches;
			Stats.SubmittedDecisions += BatchAgents.Num();
			for (const auto& Agent : BatchAgents)
			{
				Agent->bPending = true;
				if (auto* Component = Agent->Component.Get()) { Component->PendingToken = Token; Component->bSharedBatchRequest = true; }
			}
		}
		else
		{
			++Stats.RejectedBatches;
			for (const auto& Agent : BatchAgents) { if (auto* Component = Agent->Component.Get()) { Component->InvalidateQueryContext(); } }
		}
	}
	Stats.LastTickGameThreadMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.0;
}
