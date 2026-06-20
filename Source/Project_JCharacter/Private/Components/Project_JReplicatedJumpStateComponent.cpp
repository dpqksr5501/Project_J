#include "Components/Project_JReplicatedJumpStateComponent.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Animation/Project_JMotionMatchingCVars.h"
#include "Project_JLocomotionAnimStateComponent.h"

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

	if (Project_J::MotionMatchingCVars::IsDebugJumpLatencyEnabled())
	{
		const ACharacter* CharacterOwner = Cast<ACharacter>(Owner);
		const UCharacterMovementComponent* Movement =
			CharacterOwner ? CharacterOwner->GetCharacterMovement() : nullptr;
		const FVector CurrentVelocity = Owner->GetVelocity();
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("ProjectJ.JumpLatency Stage=MulticastReceive Frame=%llu WorldTime=%.3f Owner=%s Role=%d Sequence=%d Age=%.3f LaunchSpeed2D=%.1f LaunchVelocityZ=%.1f CurrentSpeed2D=%.1f CurrentVelocityZ=%.1f MovementMode=%d"),
			GFrameCounter,
			GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f,
			*GetNameSafe(Owner),
			static_cast<int32>(Owner->GetLocalRole()),
			ConfirmedState.Sequence,
			ResolveServerStartAgeSeconds(ConfirmedState),
			FVector(ConfirmedState.LaunchVelocity.X, ConfirmedState.LaunchVelocity.Y, 0.0f).Size(),
			ConfirmedState.LaunchVelocity.Z,
			FVector(CurrentVelocity.X, CurrentVelocity.Y, 0.0f).Size(),
			CurrentVelocity.Z,
			Movement ? static_cast<int32>(Movement->MovementMode) : INDEX_NONE);
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
	World->GetTimerManager().SetTimer(
		RestoreAnimationUpdateRateTimer,
		this,
		&UProject_JReplicatedJumpStateComponent::RestoreRemoteAnimationUpdateRateOptimization,
		0.10f,
		false);

	if (Project_J::MotionMatchingCVars::IsDebugJumpLatencyEnabled())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("ProjectJ.JumpLatency Stage=UrgentAnimUpdateBegin Frame=%llu Owner=%s RestoreURO=%s"),
			GFrameCounter,
			*GetNameSafe(CharacterOwner),
			bRestoreAnimationUpdateRateOptimization ? TEXT("true") : TEXT("false"));
	}
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

	if (Project_J::MotionMatchingCVars::IsDebugJumpLatencyEnabled() && CharacterOwner)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("ProjectJ.JumpLatency Stage=UrgentAnimUpdateEnd Frame=%llu Owner=%s RestoredURO=%s"),
			GFrameCounter,
			*GetNameSafe(CharacterOwner),
			bRestoreAnimationUpdateRateOptimization ? TEXT("true") : TEXT("false"));
	}
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
