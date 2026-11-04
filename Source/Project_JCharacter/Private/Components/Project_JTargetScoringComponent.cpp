#include "Components/Project_JTargetScoringComponent.h"
#include "Project_JCombatInterface.h"
#include "Components/Project_JNPCActivationComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "System/Project_JTargetScoringSubsystem.h"
#include "System/Project_JNPCDecisionSubsystem.h"
#include "Optimization/Project_JTargetSpatialSnapshot.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "HAL/PlatformTime.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "UObject/StrongObjectPtr.h"

UProject_JTargetScoringComponent::UProject_JTargetScoringComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UProject_JEquipmentManagerComponent* UProject_JTargetScoringComponent::ResolveEquipmentManager() const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (const APlayerState* State = Pawn->GetPlayerState())
		{
			if (auto* Manager = State->FindComponentByClass<UProject_JEquipmentManagerComponent>()) { return Manager; }
		}
	}
	return GetOwner() ? GetOwner()->FindComponentByClass<UProject_JEquipmentManagerComponent>() : nullptr;
}

void UProject_JTargetScoringComponent::BeginPlay()
{
	bEndingPlay = false;
	Super::BeginPlay();
}

void UProject_JTargetScoringComponent::BindEquipment(UProject_JEquipmentManagerComponent* Manager)
{
	if (BoundEquipment.Get() == Manager) { return; }
	if (auto* Previous = BoundEquipment.Get())
	{
		Previous->OnEquipmentEquipped.RemoveDynamic(this, &ThisClass::OnEquipmentChanged);
		Previous->OnEquipmentUnequipped.RemoveDynamic(this, &ThisClass::OnEquipmentChanged);
	}
	BoundEquipment = Manager;
	if (Manager)
	{
		Manager->OnEquipmentEquipped.AddUniqueDynamic(this, &ThisClass::OnEquipmentChanged);
		Manager->OnEquipmentUnequipped.AddUniqueDynamic(this, &ThisClass::OnEquipmentChanged);
	}
}

bool UProject_JTargetScoringComponent::RequestTargets(const TArray<AActor*>& Candidates)
{
	check(IsInGameThread());
	if (bNPCBatchRegistered) { return false; }
	ProjectJ::TargetScoring::FSnapshot Snapshot;
	if (!PrepareSnapshot(Candidates, Snapshot)) { return false; }
	UProject_JTargetScoringSubsystem* Subsystem = QuerySubsystem.Get();
	if (!Subsystem)
	{
		return false;
	}
	const TWeakObjectPtr<ThisClass> WeakThis(this);
	PendingToken = Subsystem->Submit(this, MoveTemp(Snapshot), Execution, ContextRevision,
		[WeakThis](const FProjectJTargetScoringCompletion& Completion)
		{
			if (auto* Self = WeakThis.Get()) { Self->ApplyResult(Completion); }
		});
	if (!PendingToken.IsValid()) { CandidateActors.Empty(); }
	return PendingToken.IsValid();
}

bool UProject_JTargetScoringComponent::BeginSnapshot(int32 CandidateCount, ProjectJ::TargetScoring::FSnapshot& Snapshot)
{
	check(IsInGameThread());
	ResetPendingQuery();
	if (bEndingPlay || IsBeingDestroyed() || !IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed() || !GetWorld()
		|| CandidateCount < 0 || CandidateCount > ProjectJ::TargetScoring::MaxCandidates) { return false; }
	auto* Subsystem = GetWorld()->GetSubsystem<UProject_JTargetScoringSubsystem>();
	if (!Subsystem) { return false; }
	BindEquipment(ResolveEquipmentManager());
	QuerySubsystem = Subsystem;
	Snapshot.Origin = GetOwner()->GetActorLocation();
	Snapshot.Forward = GetOwner()->GetActorForwardVector();
	Snapshot.Range = Range;
	Snapshot.DistanceWeight = DistanceWeight;
	Snapshot.DirectionWeight = DirectionWeight;
	Snapshot.Candidates.Reset(CandidateCount);
	CandidateActors.Reserve(CandidateCount);
	RequestedRange = Range;
	return true;
}

bool UProject_JTargetScoringComponent::PrepareSnapshot(const TArray<AActor*>& Candidates, ProjectJ::TargetScoring::FSnapshot& Snapshot)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_TargetScoring_PrepareExplicit);
	if (!BeginSnapshot(Candidates.Num(), Snapshot)) { return false; }
	for (AActor* Candidate : Candidates)
	{
		if (!IsValid(Candidate) || Candidate == GetOwner() || Candidate->IsActorBeingDestroyed() || Candidate->GetWorld() != GetWorld()) { continue; }
		const int32 Id = CandidateActors.Add(Candidate);
		Snapshot.Candidates.Add({Id, Candidate->GetActorLocation()});
	}
	return true;
}

bool UProject_JTargetScoringComponent::BindDamageThreatProvider(UProject_JNPCActivationComponent* Provider, uint64 Registration)
{
    check(IsInGameThread());
    if (!IsValid(Provider) || Provider->GetOwner() != GetOwner() || !bNPCBatchRegistered || Registration != BatchRegistrationRevision
        || (DamageThreatProvider.IsValid() && DamageThreatRegistration == Registration)) return false;
    DamageThreatProvider = Provider; DamageThreatRegistration = Registration;
    return true;
}

void UProject_JTargetScoringComponent::UnbindDamageThreatProvider(UProject_JNPCActivationComponent* Provider, uint64 Registration)
{
    check(IsInGameThread());
    if (DamageThreatProvider.Get() != Provider || DamageThreatRegistration != Registration) return;
    DamageThreatProvider.Reset(); DamageThreatRegistration = 0;
    if (bNPCBatchRegistered && BatchRegistrationRevision == Registration) InvalidateQueryContext();
}

bool UProject_JTargetScoringComponent::PrepareSpatialSnapshot(const ProjectJ::TargetScoring::FTargetSpatialSnapshot& Spatial,
    TConstArrayView<int32> Indices, TConstArrayView<TWeakObjectPtr<AActor>> Actors, ProjectJ::TargetScoring::FSnapshot& Snapshot)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_TargetScoring_PrepareSpatial);
    if (!BeginSnapshot(Indices.Num(), Snapshot)) return false;
    const auto& Values = Spatial.GetTargets();
    const auto AddCandidate = [&](int32 Index, const FProject_JNPCTargetThreat* Threat)
    {
        if (!Values.IsValidIndex(Index)) return;
        const auto& Value = Values[Index];
        AActor* Candidate = Actors.IsValidIndex(Value.Id) ? Actors[Value.Id].Get() : nullptr;
        if (!IsValid(Candidate) || Candidate == GetOwner() || Candidate->IsActorBeingDestroyed() || Candidate->GetWorld() != GetWorld()) return;
        const int32 Id = CandidateActors.Add(Candidate);
        Snapshot.Candidates.Add({Id, Value.Position});
        if (Threat) RequestedThreatSources.Add(*Threat);
    };
    const TStrongObjectPtr<UProject_JNPCActivationComponent> Provider(DamageThreatProvider.Get());
    if (Provider && DamageThreatRegistration == BatchRegistrationRevision)
    {
        const TStrongObjectPtr<ThisClass> KeepSelf(this);
        const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
        const uint64 Query = ContextRevision, Registration = BatchRegistrationRevision;
        const auto Current = [&]
        {
            return !bEndingPlay && !IsBeingDestroyed() && IsRegistered() && IsValid(KeepOwner.Get()) && !KeepOwner->IsActorBeingDestroyed()
                && GetOwner() == KeepOwner.Get() && GetWorld() && !GetWorld()->bIsTearingDown
                && ContextRevision == Query && bNPCBatchRegistered && BatchRegistrationRevision == Registration
                && DamageThreatProvider.Get() == Provider.Get() && DamageThreatRegistration == Registration;
        };
        const bool bReady = Provider->PrepareDamageThreatSelection();
        if (!Current()) return false;
        if (bReady)
        {
            struct FThreatCandidate { int32 Index; FProject_JNPCTargetThreat Source; };
            TArray<FThreatCandidate, TInlineAllocator<8>> Threats;
            double Highest = 0;
            const uint64 Revision = Provider->GetDamageThreatRevision();
            for (int32 Index : Indices)
            {
                if (!Values.IsValidIndex(Index)) continue;
                const auto& Value = Values[Index];
                AActor* Candidate = Actors.IsValidIndex(Value.Id) ? Actors[Value.Id].Get() : nullptr;
                FProject_JNPCTargetThreat Source;
                const bool bHasThreat = Provider->ReadDamageThreat(Candidate, Source);
                if (!Current() || Provider->GetDamageThreatRevision() != Revision) return false;
                if (bHasThreat) { Highest = FMath::Max(Highest, Source.Value); Threats.Add({Index, Source}); }
            }
            if (Highest > 0)
            {
                // Preserve the existing spatial ID order among equal highest-threat candidates.
                bRequestedDamageThreat = true;
                for (const auto& Candidate : Threats)
                    if (Candidate.Source.Value == Highest) AddCandidate(Candidate.Index, &Candidate.Source);
                return Current();
            }
        }
    }
    for (int32 Index : Indices) AddCandidate(Index, nullptr);
    return true;
}

bool UProject_JTargetScoringComponent::StartBatchedNPCDecisions(int32 TeamId)
{
	check(IsInGameThread());
	if (!GetWorld() || TeamId < 0 || bEndingPlay || IsBeingDestroyed() || BatchRegistrationRevision == MAX_uint64) { return false; }
	auto* Scheduler = GetWorld()->GetSubsystem<UProject_JNPCDecisionSubsystem>();
	if (!Scheduler) { return false; }
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	const TStrongObjectPtr<UProject_JNPCDecisionSubsystem> PinnedScheduler(Scheduler);
	// Stop/new Start during invalidation supersede this request before completion.
	AdvanceRegistrationRevision();
	const uint64 RequestRevision = BatchRegistrationRevision;
	const bool bStarted = Scheduler->RegisterAgent(this, TeamId);
	return bStarted && BatchRegistrationRevision == RequestRevision && !bEndingPlay && !IsBeingDestroyed()
		&& IsValid(PinnedOwner.Get()) && !PinnedOwner->IsActorBeingDestroyed() && GetOwner() == PinnedOwner.Get()
		&& bNPCBatchRegistered && NPCDecisionSubsystem.Get() == Scheduler && NPCDecisionTeam == TeamId;
}

void UProject_JTargetScoringComponent::StopBatchedNPCDecisions()
{
	check(IsInGameThread());
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	const auto PreviousScheduler = NPCDecisionSubsystem;
	DamageThreatProvider.Reset(); DamageThreatRegistration = 0;
	// Detach before Unregister's delegate can install a newer registration.
	bNPCBatchRegistered = false;
	NPCDecisionTeam = INDEX_NONE;
	NPCDecisionSubsystem.Reset();
	if (auto* Scheduler = PreviousScheduler.Get()) { Scheduler->UnregisterAgent(this); }
	else { AdvanceRegistrationRevision(); InvalidateQueryContext(); }
}

void UProject_JTargetScoringComponent::InvalidateQueryContext()
{
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	ResetPendingQuery();
	// Nested invalidation still retires query/revision; remaining listeners of
	// the in-flight notification observe its latest state without recursive delivery.
	if (bNotifyingContext) { return; }
	TGuardValue<bool> Guard(bNotifyingContext, true);
	OnContextInvalidated.Broadcast();
}

void UProject_JTargetScoringComponent::ResetPendingQuery()
{
	check(IsInGameThread());
	++ContextRevision;
	if (!bSharedBatchRequest)
	{
		if (auto* Subsystem = QuerySubsystem.Get()) { Subsystem->Cancel(PendingToken); }
	}
	bSharedBatchRequest = false;
	PendingToken = {};
	CandidateActors.Empty(); RequestedThreatSources.Empty(); bRequestedDamageThreat = false;
	SelectedTarget.Reset();
}

void UProject_JTargetScoringComponent::ApplyResult(const FProjectJTargetScoringCompletion& Completion, double DeadlineSeconds)
{
	check(IsInGameThread());
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	const auto* Subsystem = QuerySubsystem.Get();
	if (bEndingPlay || IsBeingDestroyed() || !Subsystem || Completion.WorldEpoch != Subsystem->GetWorldEpoch()
		|| Completion.Token.Value != PendingToken.Value || Completion.ContextRevision != ContextRevision) { return; }
	if (BoundEquipment.IsStale() || ResolveEquipmentManager() != BoundEquipment.Get()) { InvalidateQueryContext(); return; }
	AActor* Target = CandidateActors.IsValidIndex(Completion.Result.BestId) ? CandidateActors[Completion.Result.BestId].Get() : nullptr;
	if (bRequestedDamageThreat)
	{
		const TStrongObjectPtr<UProject_JNPCActivationComponent> Provider(DamageThreatProvider.Get());
		const bool bSourceCurrent = Provider && DamageThreatRegistration == BatchRegistrationRevision
			&& RequestedThreatSources.IsValidIndex(Completion.Result.BestId)
			&& Provider->IsDamageThreatSourceCurrent(Target, RequestedThreatSources[Completion.Result.BestId]);
		if (bEndingPlay || IsBeingDestroyed() || Completion.Token.Value != PendingToken.Value || Completion.ContextRevision != ContextRevision) return;
		if (!bSourceCurrent) Target = nullptr;
	}
	if (Completion.Result.Status != ProjectJ::TargetScoring::EStatus::Completed || !IsValid(Target)
		|| Target->IsActorBeingDestroyed() || Target->GetWorld() != GetWorld() || !IsValid(GetOwner())
		|| GetOwner()->GetActorLocation().ContainsNaN() || Target->GetActorLocation().ContainsNaN()
		|| FVector::DistSquared(GetOwner()->GetActorLocation(), Target->GetActorLocation()) > FMath::Square(RequestedRange))
	{
		Target = nullptr;
	}
	// Threat validation can call external ASC getters after the scheduler's freshness check.
	if (DeadlineSeconds > 0.0 && FPlatformTime::Seconds() > DeadlineSeconds)
	{
		const bool bNPCShared = bNPCBatchRegistered && bSharedBatchRequest;
		if (!bNPCShared) { InvalidateQueryContext(); return; }
		const uint64 Registration = BatchRegistrationRevision;
		const TStrongObjectPtr<UProject_JNPCDecisionSubsystem> NPCScheduler(NPCDecisionSubsystem.Get());
		const TStrongObjectPtr<AActor> ExpiryTarget(Target);
		// Only a still-authorized NPC shared query may expire without revoking its
		// consumer's committed intent. Generic/direct requests retain legacy invalidation.
		const bool bEligible = bNPCShared && NPCScheduler &&
			(Completion.Result.BestId == INDEX_NONE
				? IsValid(PinnedOwner.Get()) && !PinnedOwner->IsActorBeingDestroyed() &&
					(!PinnedOwner->Implements<UProject_JCombatInterface>() || !IProject_JCombatInterface::Execute_IsDead(PinnedOwner.Get()))
				: ExpiryTarget && NPCScheduler->CanActOnTarget(this, ExpiryTarget.Get()));
		if (bEligible && IsRegistered() && !bEndingPlay && !IsBeingDestroyed() && IsValid(PinnedOwner.Get()) && !PinnedOwner->IsActorBeingDestroyed()
			&& PinnedOwner->HasAuthority() && GetOwner() == PinnedOwner.Get() && GetWorld() && !GetWorld()->bIsTearingDown
			&& PinnedOwner->GetWorld() == GetWorld() && NPCScheduler->GetWorld() == GetWorld() && NPCDecisionSubsystem.Get() == NPCScheduler.Get()
			&& bNPCBatchRegistered && bSharedBatchRequest && BatchRegistrationRevision == Registration
			&& Completion.Result.Status == ProjectJ::TargetScoring::EStatus::Completed
			&& Completion.Token.Value == PendingToken.Value && Completion.ContextRevision == ContextRevision) ResetPendingQuery();
		else if (!bEndingPlay && !IsBeingDestroyed() && Completion.Token.Value == PendingToken.Value && Completion.ContextRevision == ContextRevision) InvalidateQueryContext();
		return;
	}
	PendingToken = {};
	CandidateActors.Empty(); RequestedThreatSources.Empty(); bRequestedDamageThreat = false;
	SelectedTarget = Target;
	bSharedBatchRequest = false;
	const TStrongObjectPtr<AActor> PinnedTarget(Target);
	// Advisory result: caller must recheck team, life, LOS, range, and authority when committing gameplay.
	OnQueryCompleted.Broadcast(Target, Target ? Completion.Result.BestScore : 0.0);
}

void UProject_JTargetScoringComponent::OnEquipmentChanged(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* Item)
{
	InvalidateQueryContext();
}

void UProject_JTargetScoringComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEndingPlay = true;
	StopBatchedNPCDecisions();
	BindEquipment(nullptr);
	Super::EndPlay(Reason);
}

void UProject_JTargetScoringComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	bEndingPlay = true;
	StopBatchedNPCDecisions();
	BindEquipment(nullptr);
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}
