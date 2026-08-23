#include "Components/Project_JReplicatedJumpStateComponent.h"

#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JPlayerCharacter.h"

UProject_JReplicatedJumpStateComponent::UProject_JReplicatedJumpStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProject_JReplicatedJumpStateComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(
		UProject_JReplicatedJumpStateComponent,
		JumpState,
		COND_SkipOwner);
}

void UProject_JReplicatedJumpStateComponent::Initialize(
	UProject_JLocomotionAnimStateComponent* InLocomotionAnimStateComponent,
	UProject_JAnimationUpdateCoordinatorComponent* InAnimationUpdateCoordinator)
{
	LocomotionAnimStateComponent = InLocomotionAnimStateComponent;
	AnimationUpdateCoordinator = InAnimationUpdateCoordinator;
}

void UProject_JReplicatedJumpStateComponent::RecordServerConfirmedJump(
	const FVector& LaunchVelocity)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	++JumpState.Sequence;
	if (const UWorld* World = GetWorld())
	{
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			JumpState.ServerStartTimeSeconds = GameState->GetServerWorldTimeSeconds();
		}
		else
		{
			JumpState.ServerStartTimeSeconds = World->GetTimeSeconds();
		}
	}
	JumpState.LaunchVelocity = LaunchVelocity;
	MulticastConfirmedJump(JumpState);
	Owner->ForceNetUpdate();
}

void UProject_JReplicatedJumpStateComponent::MulticastConfirmedJump_Implementation(
	FProject_JReplicatedJumpState ConfirmedState)
{
	const AActor* Owner = GetOwner();
	if (!Owner || Owner->HasAuthority())
	{
		return;
	}

	ApplyConfirmedJumpState(ConfirmedState);
}

void UProject_JReplicatedJumpStateComponent::OnRep_JumpState(
	FProject_JReplicatedJumpState PreviousState)
{
	if (!LocomotionAnimStateComponent || JumpState.Sequence == PreviousState.Sequence)
	{
		return;
	}

	ApplyConfirmedJumpState(JumpState);
}

void UProject_JReplicatedJumpStateComponent::ApplyConfirmedJumpState(
	const FProject_JReplicatedJumpState& ConfirmedState)
{
	const AActor* Owner = GetOwner();
	if (!LocomotionAnimStateComponent ||
		!Owner ||
		Owner->GetLocalRole() != ROLE_SimulatedProxy ||
		ConfirmedState.Sequence == LastAppliedRemoteJumpSequence)
	{
		return;
	}

	LastAppliedRemoteJumpSequence = ConfirmedState.Sequence;
	const AProject_JPlayerCharacter* PlayerCharacter = Cast<AProject_JPlayerCharacter>(Owner);
	const UProject_JLocomotionProfile* LocomotionProfile =
		PlayerCharacter ? PlayerCharacter->GetLocomotionProfile() : nullptr;
	if (AnimationUpdateCoordinator)
	{
		AnimationUpdateCoordinator->RequestUrgentRemoteAnimationUpdate(
			LocomotionProfile
				? LocomotionProfile->MotionMatchingSearchPolicy.RemoteJumpUrgentAnimationUpdateDuration
				: 0.10f);
	}
	LocomotionAnimStateComponent->HandleConfirmedRemoteJump(
		ConfirmedState.Sequence,
		ResolveServerStartAgeSeconds(ConfirmedState),
		ConfirmedState.LaunchVelocity);
}

float UProject_JReplicatedJumpStateComponent::ResolveServerStartAgeSeconds(
	const FProject_JReplicatedJumpState& ConfirmedState) const
{
	const UWorld* World = GetWorld();
	if (!World || ConfirmedState.ServerStartTimeSeconds <= 0.0f)
	{
		return 0.0f;
	}

	const AGameStateBase* GameState = World->GetGameState();
	if (!GameState)
	{
		return 0.0f;
	}
	const float ServerTime = GameState->GetServerWorldTimeSeconds();
	return FMath::Max(0.0f, ServerTime - ConfirmedState.ServerStartTimeSeconds);
}
