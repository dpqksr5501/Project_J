#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JAnimationUpdateCoordinatorComponent.generated.h"

/**
 * Coordinates short presentation-priority animation update windows.
 *
 * Replicated locomotion boundary components own transport and semantic state;
 * this component owns the duration of urgent presentation requests. Project
 * meshes compose those requests with gameplay pose requirements before changing URO.
 */
UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JAnimationUpdateCoordinatorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JAnimationUpdateCoordinatorComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	/** Extends the current urgent update window without changing replication. */
	void RequestUrgentRemoteAnimationUpdate(float DurationSeconds);

private:
	friend class FProjectJUrgentAnimationWindowTest;
	void RestoreRemoteAnimationUpdateRateOptimization();

	TWeakObjectPtr<class USkeletalMeshComponent> OverriddenMesh;
	FTimerHandle RestoreAnimationUpdateRateTimer;
	bool bUrgentAnimationUpdateActive = false;
	bool bRestoreAnimationUpdateRateOptimization = false;
};
