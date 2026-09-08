#include "Components/Project_JTargetScoringComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "System/Project_JTargetScoringSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

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
	InvalidateQueryContext();
	if (bEndingPlay || IsBeingDestroyed() || !IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed() || !GetWorld()
		|| Candidates.Num() > ProjectJ::TargetScoring::MaxCandidates) { return false; }
	auto* Subsystem = GetWorld()->GetSubsystem<UProject_JTargetScoringSubsystem>();
	if (!Subsystem) { return false; }
	BindEquipment(ResolveEquipmentManager());
	QuerySubsystem = Subsystem;
	ProjectJ::TargetScoring::FSnapshot Snapshot;
	Snapshot.Origin = GetOwner()->GetActorLocation();
	Snapshot.Forward = GetOwner()->GetActorForwardVector();
	Snapshot.Range = Range;
	Snapshot.DistanceWeight = DistanceWeight;
	Snapshot.DirectionWeight = DirectionWeight;
	RequestedRange = Range;
	for (AActor* Candidate : Candidates)
	{
		if (!IsValid(Candidate) || Candidate == GetOwner() || Candidate->IsActorBeingDestroyed() || Candidate->GetWorld() != GetWorld()) { continue; }
		const int32 Id = CandidateActors.Add(Candidate);
		Snapshot.Candidates.Add({Id, Candidate->GetActorLocation()});
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

void UProject_JTargetScoringComponent::InvalidateQueryContext()
{
	check(IsInGameThread());
	++ContextRevision;
	if (auto* Subsystem = QuerySubsystem.Get()) { Subsystem->Cancel(PendingToken); }
	PendingToken = {};
	CandidateActors.Empty();
	SelectedTarget.Reset();
}

void UProject_JTargetScoringComponent::ApplyResult(const FProjectJTargetScoringCompletion& Completion)
{
	check(IsInGameThread());
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
	InvalidateQueryContext();
	BindEquipment(nullptr);
	Super::EndPlay(Reason);
}

void UProject_JTargetScoringComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	bEndingPlay = true;
	InvalidateQueryContext();
	BindEquipment(nullptr);
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}
