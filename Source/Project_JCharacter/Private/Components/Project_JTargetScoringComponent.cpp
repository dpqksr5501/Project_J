#include "Components/Project_JTargetScoringComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "System/Project_JTargetScoringSubsystem.h"
#include "System/Project_JNPCDecisionSubsystem.h"
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

bool UProject_JTargetScoringComponent::PrepareSnapshot(const TArray<AActor*>& Candidates, ProjectJ::TargetScoring::FSnapshot& Snapshot,
	const TArray<FVector>* CapturedPositions)
{
	check(IsInGameThread());
	ResetPendingQuery();
	if (bEndingPlay || IsBeingDestroyed() || !IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed() || !GetWorld()
		|| Candidates.Num() > ProjectJ::TargetScoring::MaxCandidates
		|| (CapturedPositions && CapturedPositions->Num() != Candidates.Num())) { return false; }
	auto* Subsystem = GetWorld()->GetSubsystem<UProject_JTargetScoringSubsystem>();
	if (!Subsystem) { return false; }
	BindEquipment(ResolveEquipmentManager());
	QuerySubsystem = Subsystem;
	Snapshot.Origin = GetOwner()->GetActorLocation();
	Snapshot.Forward = GetOwner()->GetActorForwardVector();
	Snapshot.Range = Range;
	Snapshot.DistanceWeight = DistanceWeight;
	Snapshot.DirectionWeight = DirectionWeight;
	RequestedRange = Range;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		AActor* Candidate = Candidates[Index];
		if (!IsValid(Candidate) || Candidate == GetOwner() || Candidate->IsActorBeingDestroyed() || Candidate->GetWorld() != GetWorld()) { continue; }
		const int32 Id = CandidateActors.Add(Candidate);
		Snapshot.Candidates.Add({Id, CapturedPositions ? (*CapturedPositions)[Index] : Candidate->GetActorLocation()});
	}
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
	CandidateActors.Empty();
	SelectedTarget.Reset();
}

void UProject_JTargetScoringComponent::ApplyResult(const FProjectJTargetScoringCompletion& Completion)
{
	check(IsInGameThread());
	const TStrongObjectPtr<ThisClass> PinnedThis(this);
	const TStrongObjectPtr<AActor> PinnedOwner(GetOwner());
	const auto* Subsystem = QuerySubsystem.Get();
	if (bEndingPlay || IsBeingDestroyed() || !Subsystem || Completion.WorldEpoch != Subsystem->GetWorldEpoch()
		|| Completion.Token.Value != PendingToken.Value || Completion.ContextRevision != ContextRevision) { return; }
	if (BoundEquipment.IsStale() || ResolveEquipmentManager() != BoundEquipment.Get()) { InvalidateQueryContext(); return; }
	AActor* Target = CandidateActors.IsValidIndex(Completion.Result.BestId) ? CandidateActors[Completion.Result.BestId].Get() : nullptr;
	if (Completion.Result.Status != ProjectJ::TargetScoring::EStatus::Completed || !IsValid(Target)
		|| Target->IsActorBeingDestroyed() || Target->GetWorld() != GetWorld() || !IsValid(GetOwner())
		|| GetOwner()->GetActorLocation().ContainsNaN() || Target->GetActorLocation().ContainsNaN()
		|| FVector::DistSquared(GetOwner()->GetActorLocation(), Target->GetActorLocation()) > FMath::Square(RequestedRange))
	{
		Target = nullptr;
	}
	PendingToken = {};
	CandidateActors.Empty();
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
