#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Project_JAnimationUpdateCoordinatorComponent.generated.h"

/**
 * Coordinates short presentation-priority animation update windows.
 *
 * Replicated locomotion boundary components own transport and semantic state;
 * this component alone owns the temporary skeletal-mesh URO override used to
 * make those boundaries visible promptly on simulated proxies.
 */
UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JAnimationUpdateCoordinatorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProject_JAnimationUpdateCoordinatorComponent();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Extends the current urgent update window without changing replication. */
	void RequestUrgentRemoteAnimationUpdate(float DurationSeconds);

private:
	void RestoreRemoteAnimationUpdateRateOptimization();

	FTimerHandle RestoreAnimationUpdateRateTimer;
	bool bUrgentAnimationUpdateActive = false;
	bool bRestoreAnimationUpdateRateOptimization = false;
};
