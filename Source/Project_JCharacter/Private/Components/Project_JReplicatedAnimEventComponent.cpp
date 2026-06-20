#include "Components/Project_JReplicatedAnimEventComponent.h"

#include "Net/UnrealNetwork.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JLocomotionAnimStateComponent.h"

UProject_JReplicatedAnimEventComponent::UProject_JReplicatedAnimEventComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProject_JReplicatedAnimEventComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UProject_JReplicatedAnimEventComponent, ReplicatedAnimEvents, COND_SkipOwner);
}

void UProject_JReplicatedAnimEventComponent::Initialize(
	UProject_JLocomotionAnimStateComponent* InLocomotionAnimStateComponent)
{
	LocomotionAnimStateComponent = InLocomotionAnimStateComponent;
}

void UProject_JReplicatedAnimEventComponent::DispatchMoveStarted(bool bWasSprintingForStart)
{
	DispatchEvent(EProject_JReplicatedAnimEventType::MoveStart, bWasSprintingForStart);
}

void UProject_JReplicatedAnimEventComponent::DispatchMoveStopped()
{
	DispatchEvent(EProject_JReplicatedAnimEventType::MoveStop);
}

void UProject_JReplicatedAnimEventComponent::DispatchFallOffStarted()
{
	DispatchEvent(EProject_JReplicatedAnimEventType::FallOffStart);
}

void UProject_JReplicatedAnimEventComponent::DispatchLandingCancelled()
{
	DispatchEvent(EProject_JReplicatedAnimEventType::LandingCancel);
}

void UProject_JReplicatedAnimEventComponent::DispatchEvent(
	EProject_JReplicatedAnimEventType EventType,
	bool bFlag)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (Owner->HasAuthority())
	{
		ApplyEvent(EventType, bFlag);
		Owner->ForceNetUpdate();
		return;
	}

	if (EventType == EProject_JReplicatedAnimEventType::MoveStart ||
		EventType == EProject_JReplicatedAnimEventType::MoveStop)
	{
		ServerDispatchMoveState(EventType == EProject_JReplicatedAnimEventType::MoveStart);
		return;
	}

	ServerDispatchEvent(EventType, bFlag);
}

void UProject_JReplicatedAnimEventComponent::ServerDispatchMoveState_Implementation(bool bIsMoving)
{
	bool bServerAllowsSprint = false;
	if (bIsMoving)
	{
		if (const AProject_JPlayerCharacter* PlayerCharacter =
			Cast<AProject_JPlayerCharacter>(GetOwner()))
		{
			bServerAllowsSprint = PlayerCharacter->IsSprintLocomotionAllowed();
		}
	}

	ApplyEvent(
		bIsMoving
			? EProject_JReplicatedAnimEventType::MoveStart
			: EProject_JReplicatedAnimEventType::MoveStop,
		bServerAllowsSprint);
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UProject_JReplicatedAnimEventComponent::ServerDispatchEvent_Implementation(
	EProject_JReplicatedAnimEventType EventType,
	bool bFlag)
{
	if (EventType == EProject_JReplicatedAnimEventType::MoveStart ||
		EventType == EProject_JReplicatedAnimEventType::MoveStop)
	{
		return;
	}

	ApplyEvent(EventType, bFlag);
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UProject_JReplicatedAnimEventComponent::ApplyEvent(
	EProject_JReplicatedAnimEventType EventType,
	bool bFlag)
{
	switch (EventType)
	{
	case EProject_JReplicatedAnimEventType::MoveStart:
		ReplicatedAnimEvents.bIsMoving = true;
		ReplicatedAnimEvents.bIsSprinting = bFlag;
		++ReplicatedAnimEvents.MoveSequence;
		break;
	case EProject_JReplicatedAnimEventType::MoveStop:
		ReplicatedAnimEvents.bIsMoving = false;
		ReplicatedAnimEvents.bIsSprinting = false;
		++ReplicatedAnimEvents.MoveSequence;
		break;
	case EProject_JReplicatedAnimEventType::FallOffStart:
		++ReplicatedAnimEvents.FallOffStartCounter;
		break;
	case EProject_JReplicatedAnimEventType::LandingCancel:
		++ReplicatedAnimEvents.LandingCancelCounter;
		break;
	default:
		break;
	}
}

void UProject_JReplicatedAnimEventComponent::OnRep_ReplicatedAnimEvents(
	FProject_JReplicatedAnimEventState PreviousState)
{
	ApplyReplicatedEvents(ReplicatedAnimEvents, PreviousState);
}

void UProject_JReplicatedAnimEventComponent::ApplyReplicatedEvents(
	const FProject_JReplicatedAnimEventState& CurrentState,
	const FProject_JReplicatedAnimEventState& PreviousState) const
{
	if (!LocomotionAnimStateComponent)
	{
		return;
	}

	if (CurrentState.MoveSequence != PreviousState.MoveSequence)
	{
		if (CurrentState.bIsMoving)
		{
			LocomotionAnimStateComponent->HandleReplicatedMoveStarted(CurrentState.bIsSprinting);
		}
		else
		{
			LocomotionAnimStateComponent->HandleReplicatedMoveStopped();
		}
	}

	if (CurrentState.FallOffStartCounter != PreviousState.FallOffStartCounter)
	{
		LocomotionAnimStateComponent->HandleReplicatedFallOffStarted();
	}

	if (CurrentState.LandingCancelCounter != PreviousState.LandingCancelCounter)
	{
		LocomotionAnimStateComponent->HandleReplicatedLandingCancelled();
	}
}
