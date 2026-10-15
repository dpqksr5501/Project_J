#include "Game/Project_JInputLeaseSubsystem.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
bool CanOwnMapping(UObject* Owner)
{
	if (!IsValid(Owner)) { return false; }
	if (const auto* Actor = Cast<AActor>(Owner); Actor && Actor->IsActorBeingDestroyed()) { return false; }
	if (const auto* Component = Cast<UActorComponent>(Owner); Component && Component->IsBeingDestroyed()) { return false; }
	return true;
}
}

bool UProject_JInputLeaseSubsystem::Acquire(UObject* Owner, UInputMappingContext* Context)
{
	check(IsInGameThread());
	if (!bAccepting || !CanOwnMapping(Owner) || !IsValid(Context)) { return false; }
	// Retain participants only through synchronous mapping callbacks, not in storage.
	TStrongObjectPtr<UProject_JInputLeaseSubsystem> ThisOperation(this);
	TStrongObjectPtr<UObject> OwnerOperation(Owner);
	TStrongObjectPtr<UInputMappingContext> ContextOperation(Context);
	PruneOwners();
	if (!bAccepting || !CanOwnMapping(Owner)) { return false; }
	TStrongObjectPtr<UEnhancedInputLocalPlayerSubsystem> Input(GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>());
	if (!Input.IsValid()) { return false; }
	auto* Lease = Leases.Find(Context);
	if (!Lease)
	{
		FLease NewLease;
		NewLease.bAddedMapping = !Input->HasMappingContext(Context);
		NewLease.Owners.Add(Owner);
		const bool bAdd = NewLease.bAddedMapping;
		Leases.Add(Context, MoveTemp(NewLease));
		if (bAdd) { Input->AddMappingContext(Context, 0); }
	}
	else { Lease->Owners.Add(Owner); }
	PruneOwners();
	// Re-query after callbacks; they may have released, deinitialized or rehashed.
	const auto* Current = Leases.Find(Context);
	return bAccepting && CanOwnMapping(Owner) && Current && Current->Owners.Contains(Owner) && Input->HasMappingContext(Context);
}

void UProject_JInputLeaseSubsystem::PruneOwners()
{
	check(IsInGameThread());
	TStrongObjectPtr<UProject_JInputLeaseSubsystem> ThisOperation(this);
	TStrongObjectPtr<UEnhancedInputLocalPlayerSubsystem> Input(GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>());
	TArray<TWeakObjectPtr<UInputMappingContext>> Contexts;
	Leases.GenerateKeyArray(Contexts);
	for (const auto& Context : Contexts)
	{
		auto* Lease = Leases.Find(Context); if (!Lease) { continue; }
		for (auto Owner = Lease->Owners.CreateIterator(); Owner; ++Owner) { if (!CanOwnMapping(Owner->Get())) { Owner.RemoveCurrent(); } }
		if (Lease->Owners.IsEmpty() || !Context.IsValid())
		{
			const bool bRemove = Lease->bAddedMapping;
			Leases.Remove(Context);
			TStrongObjectPtr<UInputMappingContext> ContextOperation(Context.Get());
			if (Input.IsValid() && bRemove && ContextOperation.IsValid()) { Input->RemoveMappingContext(ContextOperation.Get()); }
		}
	}
}

void UProject_JInputLeaseSubsystem::Release(UObject* Owner)
{
	check(IsInGameThread());
	for (auto& Pair : Leases) { Pair.Value.Owners.Remove(Owner); }
	PruneOwners();
}

void UProject_JInputLeaseSubsystem::Deinitialize()
{
	check(IsInGameThread());
	TStrongObjectPtr<UProject_JInputLeaseSubsystem> ThisOperation(this);
	bAccepting = false;
	for (auto& Pair : Leases) { Pair.Value.Owners.Reset(); }
	PruneOwners();
	Super::Deinitialize();
}
