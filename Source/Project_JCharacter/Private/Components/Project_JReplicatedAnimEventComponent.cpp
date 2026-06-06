#include "Components/Project_JReplicatedAnimEventComponent.h"

#include "Project_JLocomotionAnimStateComponent.h"

UProject_JReplicatedAnimEventComponent::UProject_JReplicatedAnimEventComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProject_JReplicatedAnimEventComponent::MarkMoveStarted(FProject_JReplicatedAnimEventState& State, bool bWasSprintingForStart) const
{
	State.bMoveStartWasSprinting = bWasSprintingForStart;
	++State.MoveStartCounter;
}

void UProject_JReplicatedAnimEventComponent::MarkMoveStopped(FProject_JReplicatedAnimEventState& State) const
{
	++State.MoveStopCounter;
}

void UProject_JReplicatedAnimEventComponent::MarkJumpStarted(FProject_JReplicatedAnimEventState& State) const
{
	++State.JumpStartCounter;
}

void UProject_JReplicatedAnimEventComponent::MarkFallOffStarted(FProject_JReplicatedAnimEventState& State) const
{
	++State.FallOffStartCounter;
}

void UProject_JReplicatedAnimEventComponent::MarkLandingCancelled(FProject_JReplicatedAnimEventState& State) const
{
	++State.LandingCancelCounter;
}

void UProject_JReplicatedAnimEventComponent::ApplyReplicatedEvents(
	const FProject_JReplicatedAnimEventState& CurrentState,
	const FProject_JReplicatedAnimEventState& PreviousState,
	UProject_JLocomotionAnimStateComponent* LocomotionAnimStateComponent) const
{
	if (!LocomotionAnimStateComponent)
	{
		return;
	}

	if (CurrentState.MoveStopCounter != PreviousState.MoveStopCounter)
	{
		LocomotionAnimStateComponent->HandleReplicatedMoveStopped();
	}

	if (CurrentState.MoveStartCounter != PreviousState.MoveStartCounter)
	{
		LocomotionAnimStateComponent->HandleReplicatedMoveStarted(CurrentState.bMoveStartWasSprinting);
	}

	if (CurrentState.JumpStartCounter != PreviousState.JumpStartCounter)
	{
		LocomotionAnimStateComponent->HandleReplicatedJumpStarted();
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
