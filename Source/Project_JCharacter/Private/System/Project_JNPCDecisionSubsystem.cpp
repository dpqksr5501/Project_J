#include "System/Project_JNPCDecisionSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/Project_JTargetScoringComponent.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Project_JNPCCharacter.h"
#include "Project_JCombatInterface.h"
#include "Optimization/Project_JTargetSpatialSnapshot.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Misc/ScopeExit.h"

struct FProjectJNPCDecisionAgent
{
	TWeakObjectPtr<UProject_JTargetScoringComponent> Component, Previous, Next;
	int32 Team = 0;
	double NextDue = 0.0;
	double LastScheduled = -1.0;
	double Interval = 0.05;
	EProject_JNPCUpdateBudgetTier DecisionTier = EProject_JNPCUpdateBudgetTier::Near;
	bool bRegistered = true;
	bool bPending = false;
};

namespace
{
	bool IsLivingActor(AActor* Actor)
	{
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()) { return false; }
		const TStrongObjectPtr<AActor> PinnedActor(Actor);
		const bool bDead = Actor->Implements<UProject_JCombatInterface>() && IProject_JCombatInterface::Execute_IsDead(Actor);
		return !bDead && IsValid(Actor) && !Actor->IsActorBeingDestroyed();
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
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<UProject_JTargetScoringComponent> PinnedComponent(Component);
	const TStrongObjectPtr<AProject_JNPCCharacter> PinnedOwner(NPC);
	const uint64 RequestRevision = Component->BatchRegistrationRevision;
	const auto IsCurrent = [&]()
	{
		return IsActiveServer() && IsValid(Component) && !Component->bEndingPlay && !Component->IsBeingDestroyed()
			&& Component->IsRegistered() && Component->GetOwner() == NPC && IsValid(NPC) && NPC->HasAuthority()
			&& !NPC->IsActorBeingDestroyed() && NPC->GetWorld() == GetWorld()
			&& Component->BatchRegistrationRevision == RequestRevision;
	};
	// Copy the shared entry: invalidation can remove/reallocate the registry.
	const int32* Existing = AgentIndices.Find(Component);
	if (Existing)
	{
		const auto Agent = Agents[*Existing];
		if (Agent->Team != TeamId) { Agent->Team = TeamId; Component->NPCDecisionTeam = TeamId; Component->InvalidateQueryContext(); }
		const int32* CurrentSlot = AgentIndices.Find(Component);
		return IsCurrent() && Agent->bRegistered && CurrentSlot && Agents[*CurrentSlot] == Agent && Agent->Team == TeamId
			&& Component->bNPCBatchRegistered && Component->NPCDecisionSubsystem.Get() == this && Component->NPCDecisionTeam == TeamId;
	}
	if (Agents.Num() >= MaxAgents) { return false; }
	Component->InvalidateQueryContext();
	if (!IsCurrent() || Agents.Num() >= MaxAgents
		|| AgentIndices.Contains(Component)) { return false; }
	const auto Agent = MakeShared<FProjectJNPCDecisionAgent>();
	Agent->Component = Component;
	Agent->Team = TeamId;
	Agent->NextDue = FPlatformTime::Seconds();
	if (Agents.IsEmpty())
	{
		AgentCursor = Component;
		Agent->Previous = Agent->Next = Component;
	}
	else
	{
		const auto Next = Agents[AgentIndices.FindChecked(AgentCursor)];
		const auto Tail = Agents[AgentIndices.FindChecked(Next->Previous)];
		Agent->Previous = Tail->Component; Agent->Next = Next->Component;
		Tail->Next = Component; Next->Previous = Component;
	}
	AgentIndices.Add(Component, Agents.Add(Agent));
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
	if (const int32* Existing = ActionIndices.Find(Component))
	{
		checkSlow(Actions.IsValidIndex(*Existing) && Actions[*Existing].Component.Get() == Component);
		return true;
	}
	if (Actions.Num() >= MaxAgents) { return false; }
	// Spread the first update; round-robin preserves fairness when the soft budget is exhausted.
	FActionEntry Entry;
	Entry.Component = Component;
	Entry.NextDue = FPlatformTime::Seconds() + (Actions.Num() % 10) * 0.01;
	if (Actions.IsEmpty())
	{
		ActionCursor = Component;
		Entry.Previous = Entry.Next = Component;
	}
	else
	{
		const int32 NextIndex = ActionIndices.FindChecked(ActionCursor);
		const int32 TailIndex = ActionIndices.FindChecked(Actions[NextIndex].Previous);
		Entry.Previous = Actions[TailIndex].Component; Entry.Next = Actions[NextIndex].Component;
		Actions[TailIndex].Next = Component; Actions[NextIndex].Previous = Component;
	}
	// New registrations join behind the current round, never ahead of surviving work.
	ActionIndices.Add(Component, Actions.Add(MoveTemp(Entry)));
	return true;
}

void UProject_JNPCDecisionSubsystem::UnregisterAction(UProject_JNPCActionComponent* Component)
{
	check(IsInGameThread());
	if (const int32* Index = ActionIndices.Find(Component)) { RemoveActionAt(*Index); }
}

void UProject_JNPCDecisionSubsystem::RemoveActionAt(int32 Index)
{
	check(Actions.IsValidIndex(Index));
	const auto Entry = Actions[Index];
	if (Actions.Num() == 1) { ActionCursor.Reset(); }
	else
	{
		Actions[ActionIndices.FindChecked(Entry.Previous)].Next = Entry.Next;
		Actions[ActionIndices.FindChecked(Entry.Next)].Previous = Entry.Previous;
		if (ActionCursor == Entry.Component) ActionCursor = Entry.Next;
	}
	ActionIndices.Remove(Entry.Component);
	Actions.RemoveAtSwap(Index, EAllowShrinking::No);
	if (Actions.IsValidIndex(Index)) { ActionIndices.FindChecked(Actions[Index].Component) = Index; }
	checkSlow(ActionIndices.Num() == Actions.Num());
}

void UProject_JNPCDecisionSubsystem::UpdateTargetCaptureStats()
{
    Stats.bCapturePending = TargetCapture.IsSet();
    Stats.bCaptureReady = TargetCapture.IsSet() && TargetCapture->bBuilt;
    Stats.CaptureNextIndex = TargetCapture.IsSet() ? TargetCapture->NextIndex : 0;
    Stats.CaptureTargetCount = TargetCapture.IsSet() ? TargetCapture->TargetCount : 0;
    Stats.CaptureAgeMilliseconds = TargetCapture.IsSet() ?
        FMath::Max(0.0, (FPlatformTime::Seconds() - TargetCapture->CapturedAt) * 1000.0) : 0.0;
}

void UProject_JNPCDecisionSubsystem::DiscardTargetCapture(bool bExpired)
{
    if (!TargetCapture.IsSet()) return;
    if (bExpired) ++Stats.CaptureExpired;
    else ++Stats.CaptureInvalidated;
    TargetCapture.Reset();
    UpdateTargetCaptureStats();
}

void UProject_JNPCDecisionSubsystem::RemoveAgentAt(int32 Index)
{
	check(Agents.IsValidIndex(Index));
	const auto Agent = Agents[Index];
	Agent->bRegistered = false; // Retained worker completions belong to this retired registration.
	if (Agents.Num() == 1) { AgentCursor.Reset(); }
	else
	{
		Agents[AgentIndices.FindChecked(Agent->Previous)]->Next = Agent->Next;
		Agents[AgentIndices.FindChecked(Agent->Next)]->Previous = Agent->Previous;
		if (AgentCursor == Agent->Component) AgentCursor = Agent->Next;
	}
	AgentIndices.Remove(Agent->Component);
	Agents.RemoveAtSwap(Index, EAllowShrinking::No);
	if (Agents.IsValidIndex(Index)) { AgentIndices.FindChecked(Agents[Index]->Component) = Index; }
	checkSlow(AgentIndices.Num() == Agents.Num());
    if (Agents.IsEmpty()) DiscardTargetCapture(false);
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
		const int32 Index = ActionIndices.FindChecked(ActionCursor);
		auto& Entry = Actions[Index];
		auto* Component = Entry.Component.Get();
		++Stats.LastTickActionVisits;
		if (!IsValid(Component) || Component->IsBeingDestroyed())
		{
			RemoveActionAt(Index); continue;
		}
		const double Now = FPlatformTime::Seconds();
		const bool bDue = Now >= Entry.NextDue;
		if (bDue)
		{
			Stats.MaxActionLatenessMilliseconds = FMath::Max(Stats.MaxActionLatenessMilliseconds, (Now - Entry.NextDue) * 1000.0);
			Entry.NextDue = Now + 0.1;
		}
		ActionCursor = Entry.Next;
		if (bDue)
		{
			TStrongObjectPtr<UProject_JNPCActionComponent> PinnedComponent(Component);
			TStrongObjectPtr<AActor> PinnedOwner(Component->GetOwner());
			++Stats.LastTickActionUpdates; Component->UpdateAction();
		}
		// No array reference is used after a callback (which may unregister or end the world).
	}
	Stats.LastTickActionMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.0;
}

void UProject_JNPCDecisionSubsystem::UnregisterAgent(UProject_JTargetScoringComponent* Component)
{
	check(IsInGameThread());
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<UProject_JTargetScoringComponent> PinnedComponent(Component);
	const TStrongObjectPtr<AActor> PinnedOwner(IsValid(Component) ? Component->GetOwner() : nullptr);
	// Detach storage and execution links before invalidation can admit a newer lease.
	if (const int32* Index = AgentIndices.Find(Component)) { RemoveAgentAt(*Index); }
	if (IsValid(Component))
	{
		Component->AdvanceRegistrationRevision();
		Component->bNPCBatchRegistered = false;
		Component->NPCDecisionTeam = INDEX_NONE;
		Component->NPCDecisionSubsystem.Reset();
		Component->InvalidateQueryContext();
	}
	if (Agents.IsEmpty())
	{
		if (auto* Service = Scoring.Get()) { Service->CancelForOwner(this); }
		Ready.Empty(); ActiveBatches.Empty(); OutstandingDecisions = 0; AgentCursor.Reset();
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
		|| Component->IsBeingDestroyed() || Component->NPCDecisionSubsystem.Get() != this || Component->NPCDecisionTeam < 0) { return false; }
	const TStrongObjectPtr<const ThisClass> PinnedThis(this);
	const TStrongObjectPtr<const UProject_JTargetScoringComponent> PinnedComponent(Component);
	const TStrongObjectPtr<AActor> PinnedOwner(Component->GetOwner());
	const TStrongObjectPtr<AActor> PinnedTarget(Target);
	const uint64 Revision = Component->BatchRegistrationRevision;
	const int32 Team = Component->NPCDecisionTeam;
	return IsLivingActor(PinnedOwner.Get()) && PinnedOwner->HasAuthority() && IsEligibleTarget(Target, Team)
		&& IsActiveServer() && IsValid(Component) && !Component->IsBeingDestroyed() && Component->IsRegistered()
		&& Component->GetOwner() == PinnedOwner.Get() && IsValid(PinnedOwner.Get()) && !PinnedOwner->IsActorBeingDestroyed()
		&& Component->bNPCBatchRegistered && Component->NPCDecisionSubsystem.Get() == this
		&& Component->BatchRegistrationRevision == Revision && Component->NPCDecisionTeam == Team;
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
	const TStrongObjectPtr<AActor> PinnedTarget(Target);
	if (!IsLivingActor(Target) || !IsActiveServer() || Target->GetWorld() != GetWorld()) return false;
	// IsDead above can reenter gameplay and change registration. Build/read only
	// afterward, with no callbacks while holding a derived map reference.
	if (TargetTeamsRevision != TargetRegistryRevision)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NPCDecision_TargetMembership);
		TargetTeams.Reset(); TargetTeams.Reserve(Targets.Num());
		for (const auto& Entry : Targets) TargetTeams.Add(Entry.Actor, Entry.Team);
		TargetTeamsRevision = TargetRegistryRevision;
	}
	const int32* RegisteredTeam = TargetTeams.Find(Target);
	return RegisteredTeam && *RegisteredTeam != Team;
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
	const int32* Slot = AgentIndices.Find(Component);
	if (!Slot) return false;
	const auto& Agent = Agents[*Slot];
	OutTier = Agent->DecisionTier; OutInterval = Agent->Interval;
	return true;
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
	TStrongObjectPtr<ThisClass> PinnedThis(this);
	bAccepting = false;
    DiscardTargetCapture(false); // Detach before StopActions/invalidation can reenter.
	const auto OldActions = MoveTemp(Actions);
	ActionIndices.Reset();
	ActionCursor.Reset();
	for (const auto& Entry : OldActions)
	{
		if (auto* Action = Entry.Component.Get())
		{
			TStrongObjectPtr<UProject_JNPCActionComponent> PinnedAction(Action);
			TStrongObjectPtr<AActor> PinnedOwner(Action->GetOwner());
			Action->StopActions();
		}
	}
	if (auto* Service = Scoring.Get()) { Service->CancelForOwner(this); }
	const auto PreviousAgents = MoveTemp(Agents);
	AgentIndices.Reset(); AgentCursor.Reset();
	Ready.Empty(); ActiveBatches.Empty(); Targets.Empty(); Observers.Empty(); OutstandingDecisions = 0;
	TargetTeams.Empty();
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
	const TArray<TSharedPtr<FProjectJNPCDecisionAgent>>& BatchAgents, const TArray<uint64>& Revisions, double CapturedAt)
{
	check(IsInGameThread());
	if (!IsActiveServer()) { return; }
	ActiveBatches.RemoveAll([&](const auto Token) { return Token.Value == Batch.Token.Value; });
	// Capacity was reserved before dispatch, including both in-flight and ready decisions.
	check(Ready.Num() + BatchAgents.Num() <= MaxOutstandingDecisions);
	++Stats.CompletedBatches;
	Stats.CompletedComputeMicroseconds += Batch.Result.ComputeMicroseconds;
	Stats.CompletedCoreDeliveryMilliseconds += Batch.DeliveryMilliseconds;
	for (int32 Index = 0; Index < BatchAgents.Num(); ++Index)
	{
		auto& Entry = Ready.EmplaceLast();
		Entry.Agent = BatchAgents[Index];
		Entry.CapturedAt = CapturedAt;
		Entry.Completion.Token = Batch.Token;
		Entry.Completion.WorldEpoch = Batch.WorldEpoch;
		Entry.Completion.ContextRevision = Revisions[Index];
		Entry.Completion.DeliveryMilliseconds = Batch.DeliveryMilliseconds;
		if (Batch.Result.Results.IsValidIndex(Index)) { Entry.Completion.Result = Batch.Result.Results[Index]; }
	}
}

void UProject_JNPCDecisionSubsystem::Tick(float DeltaTime)
{
	check(IsInGameThread());
	if (!IsActiveServer() || !Scoring.IsValid() || bTicking) { return; }
	TStrongObjectPtr<ThisClass> PinnedThis(this);
	TGuardValue<bool> TickGuard(bTicking, true);
	const double FullStarted = FPlatformTime::Seconds();
	ON_SCOPE_EXIT
	{
        UpdateTargetCaptureStats();
		Stats.LastFullTickMilliseconds = (FPlatformTime::Seconds() - FullStarted) * 1000;
		Stats.MaxFullTickMilliseconds = FMath::Max(Stats.MaxFullTickMilliseconds, Stats.LastFullTickMilliseconds);
	};
	Stats.LastSharedSnapshotMilliseconds = Stats.OldestReadyResultMilliseconds = 0;
	for (const auto& Entry : Ready) { Stats.OldestReadyResultMilliseconds = FMath::Max(Stats.OldestReadyResultMilliseconds, (FullStarted - Entry.CapturedAt) * 1000); }
	UpdateActions();
	if (!IsActiveServer()) { return; }
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NPCDecision_CollectAndApply);
	const double Started = FPlatformTime::Seconds();
	Stats.LastTickAgentVisits = Stats.LastTickCandidateVisits = Stats.LastTickResults = 0;
	Stats.LastTickSnapshotValues = 0;
	Stats.LastTickTargetPositionReads = Stats.LastTickSnapshotBuilds = Stats.LastTickSpatialCells = Stats.LastTickLinearFallbacks = 0;
	Stats.LastTickObservers = Stats.LastTickImportanceUpdates = Stats.LastTickPromotions = 0;
	Stats.LastTickObserverCandidateVisits = 0;
	Stats.bObserverCoverageIncomplete = false;
	const auto WithinBudget = [&]() { return (FPlatformTime::Seconds() - Started) * 1000.0 < GameThreadBudgetMilliseconds; };
	while (!Ready.IsEmpty() && Stats.LastTickResults < MaxResultsPerTick && WithinBudget())
	{
		auto Entry = MoveTemp(Ready.First());
		Ready.PopFirst();
		--OutstandingDecisions;
		++Stats.LastTickResults;
		Entry.Agent->bPending = false;
		auto* Component = Entry.Agent->Component.Get();
		AActor* Owner = Component ? Component->GetOwner() : nullptr;
		const bool bCurrent = Component && Entry.Agent->bRegistered && Component->bNPCBatchRegistered
			&& Component->PendingToken.Value == Entry.Completion.Token.Value && Component->ContextRevision == Entry.Completion.ContextRevision;
		if (!bCurrent) { ++Stats.DiscardedDecisions; continue; }
		const int32 BestId = Entry.Completion.Result.BestId;
		AActor* Target = Component->CandidateActors.IsValidIndex(BestId) ? Component->CandidateActors[BestId].Get() : nullptr;
		const TStrongObjectPtr<UProject_JTargetScoringComponent> PinnedComponent(Component);
		const TStrongObjectPtr<AActor> PinnedOwner(Owner);
		const TStrongObjectPtr<AActor> PinnedTarget(Target);
		// Query expiry is advisory loss, not authority/context revocation. Extensible
		// life/eligibility queries run once, before the final exact-context fence.
		const bool bOwnerLiving = IsLivingActor(Owner);
		const bool bExpiredBefore = FPlatformTime::Seconds() - Entry.CapturedAt > MaxResultAgeSeconds;
		const auto Status = Entry.Completion.Result.Status;
		const bool bAdvisoryStatus = Status == ProjectJ::TargetScoring::EStatus::Completed || Status == ProjectJ::TargetScoring::EStatus::Expired;
		const bool bTargetEligible = bOwnerLiving && IsValid(Owner) && !Owner->IsActorBeingDestroyed() && Owner->HasAuthority()
			&& bAdvisoryStatus && (BestId == INDEX_NONE || IsEligibleTarget(Target, Entry.Agent->Team));
		const bool bAuthorityCurrent = bOwnerLiving && bTargetEligible && IsActiveServer() && IsValid(Component) && !Component->IsBeingDestroyed()
			&& Component->IsRegistered() && Entry.Agent->bRegistered && IsValid(Owner) && !Owner->IsActorBeingDestroyed() && Owner->HasAuthority()
			&& Owner->GetWorld() == GetWorld() && Component->GetOwner() == Owner && Component->bNPCBatchRegistered
			&& Component->NPCDecisionSubsystem.Get() == this
			&& Component->PendingToken.Value == Entry.Completion.Token.Value && Component->ContextRevision == Entry.Completion.ContextRevision;
		const bool bExpired = bExpiredBefore || FPlatformTime::Seconds() - Entry.CapturedAt > MaxResultAgeSeconds || Status == ProjectJ::TargetScoring::EStatus::Expired;
		if (!bAuthorityCurrent || Status != ProjectJ::TargetScoring::EStatus::Completed || bExpired)
		{
			// A newer query/context owns its own pending result; only retire this completion.
			if (IsValid(Component) && !Component->IsBeingDestroyed() && Component->PendingToken.Value == Entry.Completion.Token.Value
				&& Component->ContextRevision == Entry.Completion.ContextRevision)
			{
				if (bAuthorityCurrent && bAdvisoryStatus && bExpired) Component->ResetPendingQuery();
				else Component->InvalidateQueryContext();
			}
			++Stats.DiscardedDecisions;
			continue;
		}
		++Stats.AppliedDecisions;
		Component->ApplyResult(Entry.Completion, Entry.CapturedAt + MaxResultAgeSeconds); // Queue/counters are already retired.
		if (!IsActiveServer()) { return; }
	}

    // Retire data-only work even while producer slots are full. A target
    // registry epoch/order change can never resume an old indexed traversal.
    if (TargetCapture.IsSet())
    {
        if (Agents.IsEmpty() || TargetCapture->RegistryRevision != TargetRegistryRevision || TargetCapture->TargetCount != Targets.Num()) DiscardTargetCapture(false);
        else if (FPlatformTime::Seconds() - TargetCapture->CapturedAt > MaxResultAgeSeconds) DiscardTargetCapture(true);
    }

	// Keep completed-result draining independent of producer admission pressure.
	if (Agents.IsEmpty() || OutstandingDecisions >= MaxOutstandingDecisions
		|| Scoring->GetPendingCount() >= Scoring->MaxPendingRequests || !WithinBudget())
	{
		Stats.LastTickGameThreadMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.0;
		return;
	}
    // New captures start before this pass's observers/origins. A continuation
    // keeps its earlier timestamp; no yield can make an old input fresh.
    const double PassCapturedAt = FPlatformTime::Seconds();
    double CapturedAt = PassCapturedAt;
	TArray<FVector> ObserverPositions;
	const bool bCompleteObservers = CaptureObserverPositions(ObserverPositions);
    // Completed capture storage is detached here before any component/provider
    // callbacks. Only one eventual dispatch pass consumes that completed value.
	ProjectJ::TargetScoring::FTargetSpatialSnapshot Spatial;
	TArray<TWeakObjectPtr<AActor>> CapturedActors;
	bool bCapturedTargets = false;
	uint64 CapturedRegistryRevision = 0;
    const auto CaptureTargets = [&]()
    {
        const double SnapshotStarted = FPlatformTime::Seconds();
        ON_SCOPE_EXIT { Stats.LastSharedSnapshotMilliseconds += (FPlatformTime::Seconds() - SnapshotStarted) * 1000; };
        TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_NPCDecision_SharedSnapshot);
        if (!TargetCapture.IsSet())
        {
            auto& Capture = TargetCapture.Emplace();
            Capture.Serial = ++NextTargetCaptureSerial;
            Capture.RegistryRevision = TargetRegistryRevision;
            Capture.TargetCount = Targets.Num();
            Capture.CapturedAt = PassCapturedAt;
            Capture.Values.Reserve(Capture.TargetCount);
            Capture.Actors.Reserve(Capture.TargetCount);
        }
        const uint64 Serial = TargetCapture->Serial;
        const uint64 Registry = TargetCapture->RegistryRevision;
        const int32 TargetCount = TargetCapture->TargetCount;
        while (true)
        {
            if (!IsActiveServer() || !TargetCapture.IsSet() || TargetCapture->Serial != Serial) return false;
            if (Registry != TargetRegistryRevision || TargetCount != Targets.Num()) { DiscardTargetCapture(false); return false; }
            if (FPlatformTime::Seconds() - TargetCapture->CapturedAt > MaxResultAgeSeconds) { DiscardTargetCapture(true); return false; }
            const int32 Index = TargetCapture->NextIndex;
            if (Index == TargetCount) break;
            if (!WithinBudget()) { ++Stats.CaptureDeferrals; return false; }
            // No member/array reference survives the extensible life query.
            // Stop/last-agent retirement may destroy the optional capture here.
            const FTarget Entry = Targets[Index];
            AActor* Target = Entry.Actor.Get();
            const TStrongObjectPtr<AActor> PinnedTarget(Target);
            const bool bLiving = IsLivingActor(Target);
            if (!IsActiveServer() || !TargetCapture.IsSet() || TargetCapture->Serial != Serial) return false;
            if (Registry != TargetRegistryRevision || TargetCount != Targets.Num() || TargetCapture->NextIndex != Index)
            { DiscardTargetCapture(false); return false; }
            if (FPlatformTime::Seconds() - TargetCapture->CapturedAt > MaxResultAgeSeconds) { DiscardTargetCapture(true); return false; }
            ++TargetCapture->NextIndex;
            if (!bLiving || !IsValid(Target) || Target->GetWorld() != GetWorld()) continue;
            const FVector Position = Target->GetActorLocation();
            ++Stats.LastTickTargetPositionReads;
            if (Position.ContainsNaN()) continue;
            const int32 Id = TargetCapture->Actors.Add(Target);
            TargetCapture->Values.Add({Id, Position, Entry.Team});
        }
        if (!TargetCapture->bBuilt)
        {
            if (!WithinBudget()) { ++Stats.CaptureDeferrals; return false; }
            // One bounded value build may overrun this soft time budget. Keep
            // that complete result rather than rebuilding it on the next Tick.
            ++Stats.LastTickSnapshotBuilds;
            if (!TargetCapture->Spatial.Build(MoveTemp(TargetCapture->Values))) { DiscardTargetCapture(false); return false; }
            TargetCapture->bBuilt = true;
        }
        if (FPlatformTime::Seconds() - TargetCapture->CapturedAt > MaxResultAgeSeconds) { DiscardTargetCapture(true); return false; }
        if (!WithinBudget()) { ++Stats.CaptureDeferrals; return false; }
        CapturedRegistryRevision = TargetCapture->RegistryRevision;
        CapturedAt = TargetCapture->CapturedAt;
        Spatial = MoveTemp(TargetCapture->Spatial);
        CapturedActors = MoveTemp(TargetCapture->Actors);
        TargetCapture.Reset();
        UpdateTargetCaptureStats();
        return true;
    };
	TArray<ProjectJ::TargetScoring::FSnapshot> Snapshots;
	TArray<TSharedPtr<FProjectJNPCDecisionAgent>> BatchAgents;
	TArray<uint64> Revisions;
	TArray<uint64> RegistrationRevisions;
	// Tick-local scratch: Query resets it; PrepareSpatialSnapshot consumes its view synchronously.
	TArray<int32> CandidateIndices;
	const int32 VisitLimit = FMath::Min(Agents.Num(), MaxAgentVisitsPerTick);
	for (int32 Visit = 0; Visit < VisitLimit && !Agents.IsEmpty() && WithinBudget()
		&& BatchAgents.Num() < MaxQueriesPerDispatch && OutstandingDecisions + BatchAgents.Num() < MaxOutstandingDecisions; ++Visit)
	{
		const int32 Index = AgentIndices.FindChecked(AgentCursor);
		const auto Agent = Agents[Index];
		AgentCursor = Agent->Next;
		++Stats.LastTickAgentVisits;
		auto* Component = Agent->Component.Get();
		auto* NPC = Component ? Cast<AProject_JNPCCharacter>(Component->GetOwner()) : nullptr;
		if (!Component || Component->IsBeingDestroyed() || !IsValid(NPC) || NPC->IsActorBeingDestroyed() || !NPC->HasAuthority())
		{
			RemoveAgentAt(Index);
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
			Stats.LastTickObserverCandidateVisits += ObserverPositions.Num();
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
		const TStrongObjectPtr<UProject_JTargetScoringComponent> PinnedComponent(Component);
		const TStrongObjectPtr<AProject_JNPCCharacter> PinnedNPC(NPC);
		const uint64 RegistrationRevision = Component->BatchRegistrationRevision;
		const uint64 QueryRevision = Component->ContextRevision;
		const auto IsCurrent = [&]
		{
			return IsActiveServer() && Agent->bRegistered && IsValid(Component) && !Component->IsBeingDestroyed() && Component->IsRegistered()
				&& IsValid(NPC) && !NPC->IsActorBeingDestroyed() && Component->GetOwner() == NPC && Component->bNPCBatchRegistered
				&& Component->NPCDecisionSubsystem.Get() == this && Component->BatchRegistrationRevision == RegistrationRevision
				&& Component->ContextRevision == QueryRevision;
		};
		if (!IsLivingActor(NPC)) { if (IsCurrent()) { Component->InvalidateQueryContext(); } continue; }
		if (!IsCurrent()) { continue; }
		if (!bCapturedTargets)
		{
			if (!CaptureTargets()) { break; }
			bCapturedTargets = true;
		}
		if (!IsCurrent()) { continue; }
		if (!IsActiveServer() || CapturedRegistryRevision != TargetRegistryRevision) { break; }
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
			// This still-current request was not submitted; retain its place for the next dispatch.
			const int32* CurrentSlot = AgentIndices.Find(Agent->Component);
			if (Agent->bRegistered && CurrentSlot && Agents[*CurrentSlot] == Agent) AgentCursor = Agent->Component;
			break;
		}
		ProjectJ::TargetScoring::FSnapshot Snapshot;
		if (!Component->PrepareSpatialSnapshot(Spatial, CandidateIndices, CapturedActors, Snapshot)) { continue; }
		Agent->LastScheduled = Now;
		Stats.LastTickSnapshotValues += Snapshot.Candidates.Num();
		Snapshots.Add(MoveTemp(Snapshot));
		BatchAgents.Add(Agent);
		Revisions.Add(Component->ContextRevision);
		RegistrationRevisions.Add(Component->BatchRegistrationRevision);
	}
	// A later agent/target life callback may retire an earlier prepared query.
	// Never install that old batch token on a newer registration/context.
	for (int32 Index = BatchAgents.Num() - 1; Index >= 0; --Index)
	{
		const auto& Agent = BatchAgents[Index];
		const auto* Component = Agent->Component.Get();
		const auto* Owner = Component ? Component->GetOwner() : nullptr;
		if (!Agent->bRegistered || !IsValid(Component) || Component->IsBeingDestroyed() || !Component->IsRegistered()
			|| !Component->bNPCBatchRegistered || Component->NPCDecisionSubsystem.Get() != this
			|| Component->ContextRevision != Revisions[Index] || Component->BatchRegistrationRevision != RegistrationRevisions[Index]
			|| !IsValid(Owner) || Owner->IsActorBeingDestroyed() || !Owner->HasAuthority())
		{
			Snapshots.RemoveAt(Index); BatchAgents.RemoveAt(Index); Revisions.RemoveAt(Index); RegistrationRevisions.RemoveAt(Index);
		}
	}
	if (!Snapshots.IsEmpty() && IsActiveServer() && Scoring.IsValid() && CapturedRegistryRevision == TargetRegistryRevision)
	{
		if (FPlatformTime::Seconds() - CapturedAt > MaxResultAgeSeconds)
		{
			Stats.DiscardedDecisions += BatchAgents.Num();
			// No Core token or reservation exists yet. Expiry retires only this advisory query; hard owner loss still invalidates.
			for (int32 Index = 0; Index < BatchAgents.Num(); ++Index)
			{
				const auto Agent = BatchAgents[Index];
				const TStrongObjectPtr<UProject_JTargetScoringComponent> Component(Agent->Component.Get());
				const TStrongObjectPtr<AActor> Owner(Component ? Component->GetOwner() : nullptr);
				const bool bOwnerLiving = IsLivingActor(Owner.Get());
				const int32* Slot = AgentIndices.Find(Agent->Component);
				if (IsActiveServer() && Agent->bRegistered && Slot && Agents[*Slot] == Agent
					&& IsValid(Component.Get()) && !Component->IsBeingDestroyed() && Component->IsRegistered()
					&& Component->bNPCBatchRegistered && Component->NPCDecisionSubsystem.Get() == this
					&& Component->ContextRevision == Revisions[Index] && Component->BatchRegistrationRevision == RegistrationRevisions[Index]
					&& IsValid(Owner.Get()) && !Owner->IsActorBeingDestroyed() && Component->GetOwner() == Owner.Get())
				{
					if (bOwnerLiving && Owner->HasAuthority() && Owner->GetWorld() == GetWorld()) Component->ResetPendingQuery();
					else Component->InvalidateQueryContext();
				}
			}
			Stats.LastTickGameThreadMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.0;
			return;
		}
		const TWeakObjectPtr<ThisClass> WeakThis(this);
		const auto Token = Scoring->SubmitBatch(this, MoveTemp(Snapshots), EProject_JTargetScoringExecution::TaskParallelFor, 0,
			[WeakThis, BatchAgents, Revisions, CapturedAt](const FProjectJTargetScoringBatchCompletion& Completion)
			{
				if (auto* Self = WeakThis.Get()) { Self->QueueResults(Completion, BatchAgents, Revisions, CapturedAt); }
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
