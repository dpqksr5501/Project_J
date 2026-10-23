#include "Project_JLocomotionAnimStateComponent.h"
#include "Movement/Project_JMovementQueries.h"
#include "Project_JPlayerCharacter.h"

void UProject_JLocomotionAnimStateComponent::HandleReplicatedMoveStarted(bool bWasSprintingForStart)
{ if(!ShouldUseLocalInputState()) bRemoteMoveReleasedWhileAirborne=false; }

void UProject_JLocomotionAnimStateComponent::HandleReplicatedMoveStopped(bool bWasSprintingAtStop)
{ if(!ShouldUseLocalInputState()) MarkRemoteMoveReleasedIfAirborne(); }

void UProject_JLocomotionAnimStateComponent::MarkRemoteMoveReleasedIfAirborne()
{
	const bool bInAirState = bIsInAir || bIsPhysicallyInAir || bIsJumping || bIsFallOffStart;
	if (bInAirState)
	{
		bRemoteMoveReleasedWhileAirborne = true;
	}
}

void UProject_JLocomotionAnimStateComponent::SetMoveInput(const FVector2D& InMoveInput)
{ CachedMoveInput=InMoveInput.GetClampedToMaxSize(1.f); }

void UProject_JLocomotionAnimStateComponent::ClearMoveInput()
{ ClearLocalMoveInputState(); }


void UProject_JLocomotionAnimStateComponent::HandleSprintStarted()
{ bSprintInputHeld=bWantsSprint=true; }

void UProject_JLocomotionAnimStateComponent::HandleSprintStopped()
{ bSprintInputHeld=bWantsSprint=bUseSprintLocomotion=false; }

void UProject_JLocomotionAnimStateComponent::ClearLocalMoveInputState()
{
	CachedMoveInput = FVector2D::ZeroVector;
	MoveInputSize = 0.0f;
	MoveInputHeldTime = 0.0f;
	bHasMoveInput = false;

}
