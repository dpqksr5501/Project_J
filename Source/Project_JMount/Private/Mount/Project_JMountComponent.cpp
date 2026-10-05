#include "Mount/Project_JMountComponent.h"

#include "Mount/Project_JMountCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "InputAction.h"

UProject_JMountComponent::UProject_JMountComponent()
{
	SetIsReplicatedByDefault(true);
}

void UProject_JMountComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UProject_JMountComponent, MountedMount);
}

bool UProject_JMountComponent::RequestMount(AProject_JMountCharacter* Mount)
{
	if (!Mount || MountedMount)
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
	if (Mount && !MountedMount)
	{
		Mount->TryMountRider(Cast<ACharacter>(GetOwner()));
	}
}

void UProject_JMountComponent::OnRep_MountedMount(AProject_JMountCharacter* PreviousMount)
{
	OnMountChanged.Broadcast(PreviousMount, MountedMount);
}

void UProject_JMountComponent::SetRiderInteractAction(UInputAction* Action)
{
	if (RiderInteractAction == Action) { return; }
	RiderInteractAction = Action;
	if (IsValid(MountedMount)) { MountedMount->RefreshDismountInputBindings(); }
}

void UProject_JMountComponent::SetMountedMount(AProject_JMountCharacter* NewMount)
{
	if (MountedMount == NewMount)
	{
		return;
	}

	AProject_JMountCharacter* PreviousMount = MountedMount;
	MountedMount = NewMount;

	if (const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()); OwnerCharacter && OwnerCharacter->HasAuthority())
	{
		ReleaseMountedTag();
		if (MountedMount)
		{
		if (const IAbilitySystemInterface* AbilityOwner = Cast<IAbilitySystemInterface>(OwnerCharacter); AbilityOwner)
		if (UProject_JAbilitySystemComponent* AbilitySystemComponent = Cast<UProject_JAbilitySystemComponent>(AbilityOwner->GetAbilitySystemComponent()))
		{
			const FGameplayTag MountedTag = FProject_JGameplayTags::Get().State_Mounted;
			MountedTagAbilitySystem = AbilitySystemComponent;
			AbilitySystemComponent->AddProjectJLooseGameplayTag(MountedTag, true);
		}
		}
	}

	OnMountChanged.Broadcast(PreviousMount, MountedMount);
}

void UProject_JMountComponent::ReleaseMountedTag()
{
	UProject_JAbilitySystemComponent* ASC = MountedTagAbilitySystem.Get();
	MountedTagAbilitySystem.Reset();
	if (ASC)
	{
		ASC->RemoveProjectJLooseGameplayTag(FProject_JGameplayTags::Get().State_Mounted, true);
	}
}

void UProject_JMountComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (IsValid(MountedMount)) { MountedMount->DismountRider(true, false); }
		ReleaseMountedTag();
	}
	Super::EndPlay(EndPlayReason);
}

void UProject_JMountComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (GetOwner() && GetOwner()->HasAuthority() && IsValid(MountedMount)) { MountedMount->DismountRider(true); }
	ReleaseMountedTag();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}
