#include "Components/Project_JReplicatedJumpStateComponent.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
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

void UProject_JReplicatedJumpStateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreRemoteAnimationUpdateRateOptimization();
	Super::EndPlay(EndPlayReason);
}

void UProject_JReplicatedJumpStateComponent::Initialize(
	UProject_JLocomotionAnimStateComponent* InLocomotionAnimStateComponent)
{
	LocomotionAnimStateComponent = InLocomotionAnimStateComponent;
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
	BeginUrgentRemoteAnimationUpdate();
	LocomotionAnimStateComponent->HandleConfirmedRemoteJump(
		ConfirmedState.Sequence,
		ResolveServerStartAgeSeconds(ConfirmedState),
		ConfirmedState.LaunchVelocity);
}

void UProject_JReplicatedJumpStateComponent::BeginUrgentRemoteAnimationUpdate()
{
	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	if (!CharacterOwner || CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		return;
	}

	USkeletalMeshComponent* MeshComponent = CharacterOwner->GetMesh();
	UWorld* World = GetWorld();
	if (!MeshComponent || !World)
	{
		return;
	}

	if (!bUrgentAnimationUpdateActive)
	{
		bRestoreAnimationUpdateRateOptimization = MeshComponent->bEnableUpdateRateOptimizations;
	}

	// IMPORTANT REFACTORING GUARD:
	// Player meshes intentionally use URO for MMO scalability, but a visible simulated
	// proxy can otherwise wait several render frames before consuming this replicated
	// JumpStart. This previously made moving remote players appear to keep Run/Sprint
	// briefly after their capsule had already jumped.
	//
	// Keep this exception event-driven and short. Do not remove it while changing
	// animation budgeting, significance, or URO without repeating the two-client
	// moving-jump regression test documented in Docs/MotionMatchingNextSteps.md.
	// Conversely, do not disable URO permanently; normal MMO budgeting must resume once
	// the transition has been consumed.
	bUrgentAnimationUpdateActive = true;
	MeshComponent->bEnableUpdateRateOptimizations = false;
	const AProject_JPlayerCharacter* PlayerCharacter =
		Cast<AProject_JPlayerCharacter>(CharacterOwner);
	const UProject_JLocomotionProfile* LocomotionProfile =
		PlayerCharacter ? PlayerCharacter->GetLocomotionProfile() : nullptr;
	const float UrgentUpdateDuration = FMath::Max(
		0.0f,
		LocomotionProfile
			? LocomotionProfile->MotionMatchingSearchPolicy.RemoteJumpUrgentAnimationUpdateDuration
			: 0.10f);
	if (UrgentUpdateDuration <= 0.0f)
	{
		RestoreRemoteAnimationUpdateRateOptimization();
		return;
	}
	World->GetTimerManager().SetTimer(
		RestoreAnimationUpdateRateTimer,
		this,
		&UProject_JReplicatedJumpStateComponent::RestoreRemoteAnimationUpdateRateOptimization,
		UrgentUpdateDuration,
		false);

}

void UProject_JReplicatedJumpStateComponent::RestoreRemoteAnimationUpdateRateOptimization()
{
	if (!bUrgentAnimationUpdateActive)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(RestoreAnimationUpdateRateTimer);
	}

	ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* MeshComponent = CharacterOwner ? CharacterOwner->GetMesh() : nullptr;
	if (MeshComponent)
	{
		MeshComponent->bEnableUpdateRateOptimizations = bRestoreAnimationUpdateRateOptimization;
	}
	bUrgentAnimationUpdateActive = false;

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
