#include "System/Project_JNPCDecisionSubsystem.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Project_JNPCCharacter.h"
#include "Project_JCombatInterface.h"
#include "Optimization/Project_JTargetSpatialSnapshot.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

struct FProjectJNPCDecisionAgent
{
	TWeakObjectPtr<UProject_JTargetScoringComponent> Component;
	int32 Team = 0;
	double NextDue = 0.0;
	double LastScheduled = -1.0;
	double Interval = 0.05;
	EProject_JNPCUpdateBudgetTier DecisionTier = EProject_JNPCUpdateBudgetTier::Near;
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
	return IsInitialized() && bAccepting && GetWorld() && !GetWorld()->bIsTearingDown && CanScheduleNetworkMode(GetWorld()->GetNetMode());
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
			if (Agent->Team != TeamId) { Agent->Team = TeamId; Component->NPCDecisionTeam = TeamId; Component->InvalidateQueryContext(); }
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
	Component->NPCDecisionTeam = TeamId;
	return true;
}

bool UProject_JNPCDecisionSubsystem::RegisterAction(UProject_JNPCActionComponent* Component)
{
	check(IsInGameThread());
	if (!IsActiveServer() || !IsValid(Component) || Component->GetWorld() != GetWorld()
		|| !Component->IsRegistered() || Component->IsBeingDestroyed()) { return false; }
	if (Actions.ContainsByPredicate([Component](const auto& E) { return E.Component.Get() == Component; })) { return true; }
	if (Actions.Num() >= MaxAgents) { return false; }
	// Spread the first update; round-robin preserves fairness when the soft budget is exhausted.
	Actions.Add({Component, FPlatformTime::Seconds() + (Actions.Num() % 10) * 0.01});
	return true;
}

void UProject_JNPCDecisionSubsystem::UnregisterAction(UProject_JNPCActionComponent* Component)
{
	check(IsInGameThread());
	Actions.RemoveAll([Component](const auto& E) { return E.Component.Get() == Component; });
}

void UProject_JNPCDecisionSubsystem::UpdateActions()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NPCAction_Budget);
	const double Started = FPlatformTime::Seconds();
	Stats.LastTickActionVisits = Stats.LastTickActionUpdates = 0;
	const int32 Limit = FMath::Min(Actions.Num(), MaxActionVisitsPerTick);
	for (int32 Visit = 0; Visit < Limit && !Actions.IsEmpty() && IsActiveServer()
		&& Stats.LastTickActionUpdates < MaxActionUpdatesPerTick
		&& (FPlatformTime::Seconds() - Started) * 1000.0 < ActionBudgetMilliseconds; ++Visit)
	{
		ActionCursor %= Actions.Num();
		auto& Entry = Actions[ActionCursor];
		auto* Component = Entry.Component.Get();
		++Stats.LastTickActionVisits;
		if (!IsValid(Component) || Component->IsBeingDestroyed())
		{
			Actions.RemoveAt(ActionCursor); continue;
		}
		const double Now = FPlatformTime::Seconds();
		const bool bDue = Now >= Entry.NextDue;
		if (bDue)
		{
			Stats.MaxActionLatenessMilliseconds = FMath::Max(Stats.MaxActionLatenessMilliseconds, (Now - Entry.NextDue) * 1000.0);
			Entry.NextDue = Now + 0.1;
		}
		ActionCursor = (ActionCursor + 1) % Actions.Num();
		if (bDue) { ++Stats.LastTickActionUpdates; Component->UpdateAction(); }
		// No array reference is used after a callback (which may unregister or end the world).
	}
	Stats.LastTickActionMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.0;
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
		Component->NPCDecisionTeam = INDEX_NONE;
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
			if (Entry.Team != TeamId) { ++TargetRegistryRevision; }
			Entry.Team = TeamId;
			// A faction change must not leave a cached friendly target selected.
			const auto AgentSnapshot = Agents;
			for (const auto& Agent : AgentSnapshot)
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
	if (Targets.Num() >= MaxTargets) { ++Stats.RejectedTargets; return false; }
	Targets.Add({Target, TeamId});
	++TargetRegistryRevision;
	return true;
}

bool UProject_JNPCDecisionSubsystem::CanActOnTarget(const UProject_JTargetScoringComponent* Component, AActor* Target) const
{
	check(IsInGameThread());
	if (!IsActiveServer() || !IsValid(Component) || !Component->bNPCBatchRegistered
		|| Component->NPCDecisionSubsystem.Get() != this || Component->NPCDecisionTeam < 0
		|| !IsLivingActor(Component->GetOwner()) || !Component->GetOwner()->HasAuthority()) { return false; }
	return IsEligibleTarget(Target, Component->NPCDecisionTeam);
}

void UProject_JNPCDecisionSubsystem::UnregisterTarget(AActor* Target)
{
	check(IsInGameThread());
	if (Targets.RemoveAll([Target](const FTarget& Entry) { return Entry.Actor.Get() == Target; }) > 0) { ++TargetRegistryRevision; }
	const auto AgentSnapshot = Agents;
	for (const auto& Agent : AgentSnapshot)
	{
		if (auto* Component = Agent->Component.Get(); Component && Component->SelectedTarget.Get() == Target) { Component->InvalidateQueryContext(); }
	}
}

bool UProject_JNPCDecisionSubsystem::IsEligibleTarget(AActor* Target, int32 Team) const
{
	return IsLivingActor(Target) && Target->GetWorld() == GetWorld()
		&& Targets.ContainsByPredicate([Target, Team](const FTarget& Entry) { return Entry.Actor.Get() == Target && Entry.Team != Team; });
}

bool UProject_JNPCDecisionSubsystem::RegisterObserver(AActor* Observer)
{
	check(IsInGameThread());
	if (!IsActiveServer() || !IsValid(Observer) || Observer->IsActorBeingDestroyed()
		|| !Observer->HasAuthority() || Observer->GetWorld() != GetWorld()) { return false; }
	Observers.RemoveAll([](const auto& Entry) { return !Entry.IsValid(); });
	if (Observers.Contains(Observer)) { return true; }
	if (Observers.Num() >= MaxObservers) { ++Stats.RejectedObservers; return false; }
	Observers.Add(Observer);
	return true;
}

void UProject_JNPCDecisionSubsystem::UnregisterObserver(AActor* Observer)
{
	check(IsInGameThread());
	Observers.Remove(Observer);
}

bool UProject_JNPCDecisionSubsystem::GetAgentDecisionBudget(const UProject_JTargetScoringComponent* Component,
	EProject_JNPCUpdateBudgetTier& OutTier, double& OutInterval) const
{
	check(IsInGameThread());
	for (const auto& Agent : Agents)
	{
		if (Agent->Component.Get() == Component) { OutTier = Agent->DecisionTier; OutInterval = Agent->Interval; return true; }
	}
	return false;
}

bool UProject_JNPCDecisionSubsystem::CaptureObserverPositions(TArray<FVector>& Positions)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NPCDecision_Observers);
	Positions.Reset();
	TSet<TWeakObjectPtr<AActor>> Seen;
	bool bComplete = true;
	const auto Capture = [&](AActor* Actor)
	{
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Seen.Contains(Actor)) { return; }
		Seen.Add(Actor);
		if (Positions.Num() >= MaxObservers || Actor->GetWorld() != GetWorld() || !Actor->HasAuthority()) { bComplete = false; return; }
		const FVector Position = Actor->GetActorLocation();
		if (Position.ContainsNaN()) { bComplete = false; return; }
		Positions.Add(Position);
	};
	Observers.RemoveAll([](const auto& Entry) { return !Entry.IsValid(); });
	for (const auto& Observer : Observers) { Capture(Observer.Get()); }
	int32 ControllerVisits = 0;
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (++ControllerVisits > MaxObservers) { bComplete = false; break; }
		if (const APlayerController* Controller = It->Get()) { Capture(Controller->GetPawnOrSpectator()); }
	}
	Stats.LastTickObservers = Positions.Num();
	Stats.bObserverCoverageIncomplete = !bComplete || Positions.IsEmpty();
	return !Stats.bObserverCoverageIncomplete;
}

void UProject_JNPCDecisionSubsystem::StopScheduler()
{
	check(IsInGameThread());
	bAccepting = false;
	const auto OldActions = MoveTemp(Actions);
	ActionCursor = 0;
	for (const auto& Entry : OldActions) { if (auto* Action = Entry.Component.Get()) { Action->StopActions(); } }
	if (auto* Service = Scoring.Get()) { Service->CancelForOwner(this); }
	const auto PreviousAgents = MoveTemp(Agents);
	Ready.Empty(); ActiveBatches.Empty(); Targets.Empty(); Observers.Empty(); OutstandingDecisions = 0;
	++TargetRegistryRevision;
	for (const auto& Agent : PreviousAgents)
	{
		Agent->bRegistered = false;
		if (auto* Component = Agent->Component.Get())
		{
			Component->bNPCBatchRegistered = false;
			Component->NPCDecisionTeam = INDEX_NONE;
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
bool UProject_JNPCDecisionSubsystem::IsTickable() const { return IsActiveServer() && (!Agents.IsEmpty() || !Actions.IsEmpty() || OutstandingDecisions > 0); }
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
	if (!IsActiveServer() || !Scoring.IsValid() || bTicking) { return; }
	TGuardValue<bool> TickGuard(bTicking, true);
	UpdateActions();
	if (!IsActiveServer()) { return; }
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NPCDecision_CollectAndApply);
	const double Started = FPlatformTime::Seconds();
	Stats.LastTickAgentVisits = Stats.LastTickCandidateVisits = Stats.LastTickResults = 0;
	Stats.LastTickSnapshotValues = 0;
	Stats.LastTickTargetPositionReads = Stats.LastTickSnapshotBuilds = Stats.LastTickSpatialCells = Stats.LastTickLinearFallbacks = 0;
	Stats.LastTickObservers = Stats.LastTickImportanceUpdates = Stats.LastTickPromotions = 0;
	Stats.bObserverCoverageIncomplete = false;
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

	// Keep completed-result draining independent of producer admission pressure.
	if (Agents.IsEmpty() || OutstandingDecisions >= MaxOutstandingDecisions
		|| Scoring->GetPendingCount() >= Scoring->MaxPendingRequests || !WithinBudget())
	{
		Stats.LastTickGameThreadMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.0;
		return;
	}
	TArray<FVector> ObserverPositions;
	const bool bCompleteObservers = CaptureObserverPositions(ObserverPositions);
	// These caches belong only to this Tick. No actor position is reused in another collection pass.
	ProjectJ::TargetScoring::FTargetSpatialSnapshot Spatial;
	TArray<TWeakObjectPtr<AActor>> CapturedActors;
	bool bCapturedTargets = false;
	uint64 CapturedRegistryRevision = 0;
	const auto CaptureTargets = [&]()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NPCDecision_SharedSnapshot);
		CapturedRegistryRevision = TargetRegistryRevision;
		const auto Registry = Targets; // IsDead is a GT interface call; do not hold registry references across it.
		TArray<ProjectJ::TargetScoring::FSpatialTarget> Values;
		for (const FTarget& Entry : Registry)
		{
			AActor* Target = Entry.Actor.Get();
			if (!IsLivingActor(Target) || !IsValid(Target) || Target->GetWorld() != GetWorld()) { continue; }
			const FVector Position = Target->GetActorLocation();
			++Stats.LastTickTargetPositionReads;
			if (Position.ContainsNaN()) { continue; }
			const int32 Id = CapturedActors.Add(Target);
			Values.Add({Id, Position, Entry.Team});
		}
		++Stats.LastTickSnapshotBuilds;
		return IsActiveServer() && CapturedRegistryRevision == TargetRegistryRevision && Spatial.Build(MoveTemp(Values));
	};
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
		const FVector Origin = NPC->GetActorLocation();
		const auto PreviousTier = Agent->DecisionTier;
		Agent->DecisionTier = EProject_JNPCUpdateBudgetTier::Near;
		if (bCompleteObservers && !Origin.ContainsNaN() && !Component->bUseUrgentNPCDecisionInterval)
		{
			double NearestSquared = TNumericLimits<double>::Max();
			for (const FVector& Position : ObserverPositions) { NearestSquared = FMath::Min(NearestSquared, FVector::DistSquared(Origin, Position)); }
			Agent->DecisionTier = NPC->GetDecisionTierForDistance(FMath::Sqrt(NearestSquared), PreviousTier);
		}
		++Stats.LastTickImportanceUpdates;
		if (static_cast<uint8>(Agent->DecisionTier) < static_cast<uint8>(PreviousTier)) { ++Stats.LastTickPromotions; }
		const float Recommended = NPC->GetRecommendedAIUpdateIntervalForTier(Agent->DecisionTier);
		Agent->Interval = FMath::IsFinite(Recommended) ? FMath::Clamp(double(Recommended), 0.05, 2.0) : 1.0;
		// Recalculate from the last issue time, so promotion does not wait out an old far/hidden interval.
		Agent->NextDue = Agent->LastScheduled < 0.0 ? 0.0 : Agent->LastScheduled + Agent->Interval;
		if (Agent->bPending || Now < Agent->NextDue) { continue; }
		if (Agent->LastScheduled >= 0) { Stats.MaxDecisionLatenessMilliseconds = FMath::Max(Stats.MaxDecisionLatenessMilliseconds, (Now - Agent->NextDue) * 1000); }
		if (!IsLivingActor(NPC)) { Component->InvalidateQueryContext(); continue; }
		if (!bCapturedTargets)
		{
			if (!CaptureTargets()) { break; }
			bCapturedTargets = true;
		}
		if (!IsActiveServer() || CapturedRegistryRevision != TargetRegistryRevision) { break; }
		TArray<int32> CandidateIndices;
		ProjectJ::TargetScoring::FSpatialQueryStats QueryStats;
		if (!Spatial.Query(Origin, Component->Range, Agent->Team, CandidateIndices, QueryStats)) { Component->InvalidateQueryContext(); continue; }
		Stats.LastTickCandidateVisits += QueryStats.CandidatesVisited;
		Stats.LastTickSpatialCells += QueryStats.CellsVisited;
		Stats.LastTickLinearFallbacks += QueryStats.bLinearFallback ? 1 : 0;
		// Admission is bounded by total candidate values as well as query count. A dense crowd must
		// flush a smaller valid batch, rather than repeatedly reject the entire batch in Core.
		if (Stats.LastTickSnapshotValues + CandidateIndices.Num() > ProjectJ::TargetScoring::MaxCandidates)
		{
			++Stats.BatchCapacityDeferrals;
			Cursor = (Cursor + Agents.Num() - 1) % Agents.Num(); // Retry this unsubmitted agent first next tick.
			break;
		}
		TArray<AActor*> Candidates;
		TArray<FVector> CapturedPositions;
		for (int32 Index : CandidateIndices)
		{
			const auto& Value = Spatial.GetTargets()[Index];
			AActor* Target = CapturedActors[Value.Id].Get();
			if (IsValid(Target) && Target != NPC && !Target->IsActorBeingDestroyed())
			{
				Candidates.Add(Target);
				CapturedPositions.Add(Value.Position);
			}
		}
		ProjectJ::TargetScoring::FSnapshot Snapshot;
		if (!Component->PrepareSnapshot(Candidates, Snapshot, &CapturedPositions)) { continue; }
		Agent->LastScheduled = Now;
		Stats.LastTickSnapshotValues += Snapshot.Candidates.Num();
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
