#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "TimerManager.h"

UProject_JAnimationUpdateCoordinatorComponent::UProject_JAnimationUpdateCoordinatorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void UProject_JAnimationUpdateCoordinatorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreRemoteAnimationUpdateRateOptimization();
	Super::EndPlay(EndPlayReason);
}

void UProject_JAnimationUpdateCoordinatorComponent::RequestUrgentRemoteAnimationUpdate(
	float DurationSeconds)
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

	// Player meshes intentionally use URO for MMO scalability. Sparse replicated
	// one-shot boundaries briefly take presentation priority, then restore the
	// exact mesh policy that was active before the first overlapping request.
	bUrgentAnimationUpdateActive = true;
	MeshComponent->bEnableUpdateRateOptimizations = false;

	const float UrgentUpdateDuration = FMath::Max(0.0f, DurationSeconds);
	if (UrgentUpdateDuration <= 0.0f)
	{
		RestoreRemoteAnimationUpdateRateOptimization();
		return;
	}

	World->GetTimerManager().SetTimer(
		RestoreAnimationUpdateRateTimer,
		this,
		&UProject_JAnimationUpdateCoordinatorComponent::RestoreRemoteAnimationUpdateRateOptimization,
		UrgentUpdateDuration,
		false);
}

void UProject_JAnimationUpdateCoordinatorComponent::RestoreRemoteAnimationUpdateRateOptimization()
{
	if (!bUrgentAnimationUpdateActive)
	{
		return;
	}

	if (UWorld* World = GetWorld())
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
