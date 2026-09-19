#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
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

void UProject_JAnimationUpdateCoordinatorComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	RestoreRemoteAnimationUpdateRateOptimization();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UProject_JAnimationUpdateCoordinatorComponent::RequestUrgentRemoteAnimationUpdate(
	float DurationSeconds)
{
	if (!FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.0f)
	{
		return;
	}
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

	// Release the exact mesh we changed if an avatar replaces its mesh mid-window.
	if (bUrgentAnimationUpdateActive && OverriddenMesh.Get() != MeshComponent)
	{
		RestoreRemoteAnimationUpdateRateOptimization();
	}
	const float RemainingDuration = World->GetTimerManager().GetTimerRemaining(RestoreAnimationUpdateRateTimer);
	if (!bUrgentAnimationUpdateActive)
	{
		OverriddenMesh = MeshComponent;
		bRestoreAnimationUpdateRateOptimization = MeshComponent->bEnableUpdateRateOptimizations;
	}

	// Player meshes intentionally use URO for MMO scalability. Sparse replicated
	// one-shot boundaries briefly take presentation priority, then restore the
	// exact mesh policy that was active before the first overlapping request.
	bUrgentAnimationUpdateActive = true;
	if (auto* Budgeted = Cast<UProject_JBudgetedSkeletalMeshComponent>(MeshComponent))
	{
		Budgeted->RequestAnimationUpdate(this, EProject_JAnimationUpdateRequirement::Presentation);
	}
	else { MeshComponent->bEnableUpdateRateOptimizations = false; }

	// Requests compose by their latest expiry. A short landing/jump event must
	// never shorten the presentation window already granted to another event.
	const float UrgentUpdateDuration = FMath::Max(DurationSeconds, RemainingDuration);

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

	USkeletalMeshComponent* MeshComponent = OverriddenMesh.Get();
	if (auto* Budgeted = Cast<UProject_JBudgetedSkeletalMeshComponent>(MeshComponent))
	{
		Budgeted->ReleaseAnimationUpdate(this);
	}
	else if (MeshComponent)
	{
		MeshComponent->bEnableUpdateRateOptimizations = bRestoreAnimationUpdateRateOptimization;
	}

	bUrgentAnimationUpdateActive = false;
	OverriddenMesh.Reset();
}
