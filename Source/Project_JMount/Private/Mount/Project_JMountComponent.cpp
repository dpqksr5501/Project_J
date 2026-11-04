#include "Mount/Project_JMountComponent.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "Movement/Project_JMovementQueries.h"

#include "Mount/Project_JMountCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAbilitySystemOwnerInterface.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Controller.h"
#include "Project_JGameplayTags.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "InputAction.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

UProject_JMountComponent::UProject_JMountComponent()
{
	SetIsReplicatedByDefault(true);
}

void UProject_JMountComponent::BeginPlay()
{
	bEndingMount = false;
	Super::BeginPlay();
}

void UProject_JMountComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UProject_JMountComponent, MountedMount);
}

bool UProject_JMountComponent::RequestMount(AProject_JMountCharacter* Mount)
{
	if (!CanAcceptMountRequest() || !IsValid(Mount) || Mount->GetWorld() != GetWorld() || MountedMount)
	{
		return false;
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return Mount->TryMountRider(Cast<APawn>(GetOwner()));
	}

	ServerRequestMount(Mount);
	return true;
}

void UProject_JMountComponent::ServerRequestMount_Implementation(AProject_JMountCharacter* Mount)
{
	if (!UProject_JCommandBudgetComponent::AdmitClientCommand(GetOwner(), NextClientMountRequestTime, 0.2)) return;
	if (CanAcceptMountRequest() && IsValid(Mount) && Mount->GetWorld() == GetWorld() && !MountedMount)
	{
		Mount->TryMountRider(Cast<APawn>(GetOwner()));
	}
}

void UProject_JMountComponent::OnRep_MountedMount(AProject_JMountCharacter* PreviousMount)
{
	if (!CanAcceptMountRequest()) { return; }
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
	const TStrongObjectPtr<AProject_JMountCharacter> KeepPrevious(PreviousMount), KeepCurrent(MountedMount);
	OnMountChanged.Broadcast(KeepPrevious.Get(), KeepCurrent.Get());
	NotifyRuntimeOwnerPrerequisitesChanged();
}

void UProject_JMountComponent::NotifyRuntimeOwnerPrerequisitesChanged()
{
	if (!CanAcceptMountRequest()) return;
	const TStrongObjectPtr<ThisClass> KeepSelf(this);
	const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
	RuntimeOwnerPrerequisitesChanged.Broadcast();
}

bool UProject_JMountComponent::CanAcceptMountRequest() const
{
	const AActor* Owner = GetOwner();
	return !bEndingMount && IsValid(this) && IsValid(Owner) && !Owner->IsActorBeingDestroyed()
		&& GetWorld() && !GetWorld()->bIsTearingDown;
}

APlayerState* UProject_JMountComponent::ResolveRuntimeOwner() const
{
	const auto* Rider = Cast<APawn>(GetOwner());
	const auto* Mount = MountedMount.Get();
	if (!CanAcceptMountRequest() || !IsValid(Rider) || Rider->GetController() || !IsValid(Mount)
		|| Mount->IsActorBeingDestroyed() || Mount->bEndingPlay || Mount->GetWorld() != GetWorld()
		|| Mount->GetRider() != Rider || (Mount->GetMountState() != EProject_JMountState::Mounting
			&& Mount->GetMountState() != EProject_JMountState::Mounted)) { return nullptr; }
	const auto* Controller = Mount->GetController();
	if (!Controller)
	{
		// Foreign player controllers are owner-only. Public mounted identity uses
		// only the current reciprocal replicated session on simulated clients.
		// Authority and the owning/AP mount retain the controller/session checks below.
		if (GetNetMode() != NM_Client || Rider->GetLocalRole() != ROLE_SimulatedProxy
			|| Mount->GetLocalRole() != ROLE_SimulatedProxy || Rider->GetWorld() != GetWorld()) { return nullptr; }
		auto* PublicState = Mount->GetPlayerState();
		if (!IsValid(PublicState) || PublicState->IsActorBeingDestroyed() || PublicState->GetWorld() != GetWorld()
			|| PublicState->GetPawn() != Mount) { return nullptr; }
		const auto* PublicOwnerInterface = Cast<IProject_JAbilitySystemOwnerInterface>(PublicState);
		const auto* PublicASC = PublicOwnerInterface ? PublicOwnerInterface->GetProjectJAbilitySystemComponent() : nullptr;
		return IsValid(PublicASC) && PublicASC->IsRegistered() && !PublicASC->IsBeingDestroyed()
			&& PublicASC->GetWorld() == GetWorld() && PublicASC->GetOwner() == PublicState
			&& !PublicASC->IsActorInfoChanging() && PublicASC->GetOwnerActor() == PublicState
			&& PublicASC->GetAvatarActor() == Rider ? PublicState : nullptr;
	}
	if (!IsValid(Controller) || Controller->IsActorBeingDestroyed() || Controller->GetWorld() != GetWorld()
		|| Controller->GetPawn() != Mount || (Rider->HasAuthority() && Mount->SessionController.Get() != Controller)) { return nullptr; }
	auto* State = Controller->GetPlayerState<APlayerState>();
	if (!IsValid(State) || State->IsActorBeingDestroyed() || State->GetWorld() != GetWorld()
		|| Mount->GetPlayerState() != State || State->GetPawn() != Mount || State->GetOwner() != Controller) { return nullptr; }
	const auto* OwnerInterface = Cast<IProject_JAbilitySystemOwnerInterface>(State);
	const auto* ASC = OwnerInterface ? OwnerInterface->GetProjectJAbilitySystemComponent() : nullptr;
	// A new/cleared recipient must be explicitly initialized before it can serve
	// this retained Avatar. The mount's own ASC never substitutes for the rider's.
	return IsValid(ASC) && !ASC->IsActorInfoChanging() && ASC->GetOwnerActor() == State
		&& ASC->GetAvatarActor() == Rider ? State : nullptr;
}

void UProject_JMountComponent::SetRiderInteractAction(UInputAction* Action)
{
	if (!CanAcceptMountRequest()) { return; }
	if (RiderInteractAction == Action) { return; }
	RiderInteractAction = Action;
	if (IsValid(MountedMount)) { MountedMount->RefreshDismountInputBindings(); }
}

void UProject_JMountComponent::SetMountedMount(AProject_JMountCharacter* NewMount)
{
	if (NewMount && !CanAcceptMountRequest()) { return; }
	if (MountedMount == NewMount)
	{
		return;
	}

	const TStrongObjectPtr<UProject_JMountComponent> KeepSelf(this);
	AProject_JMountCharacter* PreviousMount = MountedMount;
	MountedMount = NewMount;
	const uint32 Revision = ++MountRevision;

	if (const APawn* OwnerCharacter = Cast<APawn>(GetOwner()); OwnerCharacter && OwnerCharacter->HasAuthority())
	{
		ReleaseMountedTag();
		// Tag removal may synchronously end this transition and mount another actor.
		if (Revision != MountRevision || MountedMount != NewMount) { return; }
		if (NewMount && CanAcceptMountRequest())
		{
			if (const IAbilitySystemInterface* AbilityOwner = Cast<IAbilitySystemInterface>(OwnerCharacter); AbilityOwner)
			if (UProject_JAbilitySystemComponent* AbilitySystemComponent = Cast<UProject_JAbilitySystemComponent>(AbilityOwner->GetAbilitySystemComponent()))
			{
				const TStrongObjectPtr<UProject_JAbilitySystemComponent> KeepASC(AbilitySystemComponent);
				MountedTagAbilitySystem = AbilitySystemComponent;
				AbilitySystemComponent->AddProjectJLooseGameplayTag(FProject_JGameplayTags::Get().State_Mounted, true);
			}
		}
	}
	if (Revision != MountRevision || MountedMount != NewMount) { return; }
	OnMountChanged.Broadcast(PreviousMount, MountedMount);
}

void UProject_JMountComponent::ReleaseMountedTag()
{
	const TStrongObjectPtr<UProject_JAbilitySystemComponent> ASC(MountedTagAbilitySystem.Get());
	MountedTagAbilitySystem.Reset();
	if (ASC)
	{
		ASC->RemoveProjectJLooseGameplayTag(FProject_JGameplayTags::Get().State_Mounted, true);
	}
}

void UProject_JMountComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// DestroyComponent routes EndPlay before OnComponentDestroyed. Preserve that
	// callback's intended restoration only while the rider/world are still live.
	const bool bLiveComponentRemoval = EndPlayReason == EEndPlayReason::Destroyed &&
		IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed() && GetWorld() && !GetWorld()->bIsTearingDown;
	RetireMount(bLiveComponentRemoval);
	Super::EndPlay(EndPlayReason);
}

void UProject_JMountComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	RetireMount(true);
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UProject_JMountComponent::RetireMount(bool bRestorePossession)
{
	if (bEndingMount) { return; }
	bEndingMount = true;
	++MountRevision;
	if (GetOwner() && GetOwner()->HasAuthority() && IsValid(MountedMount))
	{
		MountedMount->DismountRider(true, bRestorePossession);
	}
	SetMountedMount(nullptr);
	ReleaseMountedTag();
}
