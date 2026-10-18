#include "Mount/Project_JMountComponent.h"

#include "Mount/Project_JMountCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Project_JAbilitySystemComponent.h"
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
		return Mount->TryMountRider(Cast<ACharacter>(GetOwner()));
	}

	ServerRequestMount(Mount);
	return true;
}

void UProject_JMountComponent::ServerRequestMount_Implementation(AProject_JMountCharacter* Mount)
{
	if (CanAcceptMountRequest() && IsValid(Mount) && Mount->GetWorld() == GetWorld() && !MountedMount)
	{
		Mount->TryMountRider(Cast<ACharacter>(GetOwner()));
	}
}

void UProject_JMountComponent::OnRep_MountedMount(AProject_JMountCharacter* PreviousMount)
{
	if (!CanAcceptMountRequest()) { return; }
	OnMountChanged.Broadcast(PreviousMount, MountedMount);
}

bool UProject_JMountComponent::CanAcceptMountRequest() const
{
	const AActor* Owner = GetOwner();
	return !bEndingMount && IsValid(this) && IsValid(Owner) && !Owner->IsActorBeingDestroyed()
		&& GetWorld() && !GetWorld()->bIsTearingDown;
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

	if (const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()); OwnerCharacter && OwnerCharacter->HasAuthority())
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
