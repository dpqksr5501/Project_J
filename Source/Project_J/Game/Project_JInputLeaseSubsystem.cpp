#include "Game/Project_JInputLeaseSubsystem.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"

bool UProject_JInputLeaseSubsystem::Acquire(UObject* Owner, UInputMappingContext* Context)
{
	if (!bAccepting || !IsValid(Owner) || !IsValid(Context)) { return false; }
	PruneOwners();
	if (!bAccepting) { return false; }
	auto* Input = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!Input) { return false; }
	auto* Lease = Leases.Find(Context);
	if (!Lease)
	{
		FLease NewLease;
		NewLease.bAddedMapping = !Input->HasMappingContext(Context);
		NewLease.Owners.Add(Owner);
		const bool bAdd = NewLease.bAddedMapping;
		Lease = &Leases.Add(Context, MoveTemp(NewLease));
		if (bAdd) { Input->AddMappingContext(Context, 0); }
		return true;
	}
	Lease->Owners.Add(Owner);
	return true;
}

void UProject_JInputLeaseSubsystem::PruneOwners()
{
	auto* Input = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	TArray<TWeakObjectPtr<UInputMappingContext>> Contexts;
	Leases.GenerateKeyArray(Contexts);
	for (const auto& Context : Contexts)
	{
		auto* Lease = Leases.Find(Context); if (!Lease) { continue; }
		for (auto Owner = Lease->Owners.CreateIterator(); Owner; ++Owner) { if (!Owner->IsValid()) { Owner.RemoveCurrent(); } }
		if (Lease->Owners.IsEmpty())
		{
			const bool bRemove = Lease->bAddedMapping;
			Leases.Remove(Context);
			if (Input && bRemove && Context.IsValid()) { Input->RemoveMappingContext(Context.Get()); }
		}
	}
}

void UProject_JInputLeaseSubsystem::Release(UObject* Owner)
{
	for (auto& Pair : Leases) { Pair.Value.Owners.Remove(Owner); }
	PruneOwners();
}

void UProject_JInputLeaseSubsystem::Deinitialize()
{
	bAccepting = false;
	for (auto& Pair : Leases) { Pair.Value.Owners.Reset(); }
	PruneOwners();
	Super::Deinitialize();
}
