#include "Interaction/Project_JInteractionQuery.h"
#include "GameFramework/Pawn.h"
#include "Movement/Project_JMovementQueries.h"
#include "Interaction/Project_JInteractable.h"
#include "Interaction/Project_JInteractionTargetComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Character.h"
#include "UObject/StrongObjectPtr.h"

bool Project_J::Interaction::IsEligible(APawn& Interactor, AActor* Candidate, float SearchRadius)
{
	check(IsInGameThread());
	if (!IsValid(&Interactor) || Interactor.IsActorBeingDestroyed()) return false;
	UWorld* World = Interactor.GetWorld();
	if (!World || !IsValid(Candidate) || Candidate == &Interactor || Candidate->IsActorBeingDestroyed() ||
		Candidate->GetWorld() != World || !Candidate->GetClass()->ImplementsInterface(UProject_JInteractable::StaticClass()) ||
		!FMath::IsFinite(SearchRadius) || SearchRadius <= 0.0f) return false;
	const auto* Policy = Candidate->FindComponentByClass<UProject_JInteractionTargetComponent>();
	if (Policy && !FMath::IsFinite(Policy->InteractionRange)) return false;
	const float Range = Policy ? FMath::Min(SearchRadius, Policy->InteractionRange) : SearchRadius;
	const FVector Origin = Interactor.GetActorLocation();
	const FVector Target = Candidate->GetActorLocation();
	if ((Policy && !Policy->IsAvailable()) || !FMath::IsFinite(Range) || Range < 0.0f ||
		Origin.ContainsNaN() || Target.ContainsNaN() || FVector::DistSquared(Origin, Target) > FMath::Square(Range)) return false;
	if (Policy && Policy->bRequireLineOfSight)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ProjectJInteractionVisibility), false, &Interactor);
		Params.AddIgnoredActor(Candidate);
		if (World->LineTraceTestByChannel(Origin, Target, ECC_Visibility, Params)) return false;
	}
	return true;
}

bool Project_J::Interaction::TryInteract(APawn& Interactor, float SearchRadius)
{
	check(IsInGameThread());
	if (!IsValid(&Interactor) || Interactor.IsActorBeingDestroyed()) return false;
	// Reflected CanInteract may destroy the caller and collect garbage synchronously.
	const TStrongObjectPtr<APawn> KeepInteractor(&Interactor);
	UWorld* World = Interactor.GetWorld();
	if (!Interactor.HasAuthority() || !World || !FMath::IsFinite(SearchRadius) || SearchRadius <= 0.0f) return false;
	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_Pawn);
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ProjectJInteract), false, &Interactor);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Interactor.GetActorLocation(), FQuat::Identity,
		ObjectTypes, FCollisionShape::MakeSphere(SearchRadius), Params);
	TSet<TWeakObjectPtr<AActor>> Seen;
	TWeakObjectPtr<AActor> Best;
	int32 BestPriority = MIN_int32;
	double BestDistance = TNumericLimits<double>::Max();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Candidate = Overlap.GetActor();
		if (!Candidate || Seen.Contains(Candidate)) continue;
		Seen.Add(Candidate);
		if (!IsEligible(Interactor, Candidate, SearchRadius)) continue;
		const auto* Policy = Candidate->FindComponentByClass<UProject_JInteractionTargetComponent>();
		const int32 Priority = Policy ? Policy->Priority : 0;
		const double Distance = FVector::DistSquared(Interactor.GetActorLocation(), Candidate->GetActorLocation());
		if (Priority < BestPriority || (Priority == BestPriority && Distance >= BestDistance)) continue;
		const TWeakObjectPtr<AActor> WeakCandidate(Candidate);
		if (!IProject_JInteractable::Execute_CanInteract(Candidate, &Interactor) || !WeakCandidate.IsValid()) continue;
		Best = WeakCandidate; BestPriority = Priority; BestDistance = Distance;
	}
	// CanInteract is extensible and may change actors; validate again at the commit boundary.
	if (!Interactor.HasAuthority() || !IsEligible(Interactor, Best.Get(), SearchRadius) ||
		!IProject_JInteractable::Execute_CanInteract(Best.Get(), &Interactor) ||
		!Interactor.HasAuthority() || !IsEligible(Interactor, Best.Get(), SearchRadius)) return false;
	IProject_JInteractable::Execute_Interact(Best.Get(), &Interactor);
	return true;
}
