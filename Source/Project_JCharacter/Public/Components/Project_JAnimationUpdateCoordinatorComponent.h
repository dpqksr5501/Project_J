#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Animation/Project_JAnimationUpdateSchedule.h"
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
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	/** Extends the current urgent update window without changing replication. */
	void RequestUrgentRemoteAnimationUpdate(float DurationSeconds);
	/** Per-character rollback; local and gameplay-critical characters remain full rate. */
	UPROPERTY(EditAnywhere, Category="Animation|Visual Budget") bool bAllowVisualBudget = true;
	int32 GetManagedVisualCount() const { return Visuals.Num(); }
	bool DidRequestVisualEvaluation() const { return bEvaluateVisuals; }
	float GetVisualEvaluationInterval() const { return VisualInterval; }
	void RefreshVisualBudget(float DeltaTime);
	bool IsVisualBudgetEnabled() const;

private:
	friend class FProjectJUrgentAnimationWindowTest;
	void RestoreRemoteAnimationUpdateRateOptimization();
	void RestoreVisualBudget();
	void DiscoverVisualMeshes();
	void RefreshSourceRequirement();
	struct FVisualMesh
	{
		TWeakObjectPtr<class USkeletalMeshComponent> Mesh;
		bool bSavedURO = false, bSavedTick = true, bDisabledTick = false;
		bool bAddedSourcePrerequisite = false;
		EVisibilityBasedAnimTickOption SavedVisibility = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		void Restore(UActorComponent* Coordinator, USkeletalMeshComponent* Source);
	};
	TArray<FVisualMesh> Visuals;
	TWeakObjectPtr<class USkeletalMeshComponent> VisualSource;
	FProjectJAnimationUpdateSchedule VisualSchedule;
	float VisualInterval = 0, AccumulatedVisualDelta = 0, DiscoveryElapsed = .5f;
	double VisualWakeUntil = 0, DowngradeSince = -1, AuthoredReturnUntil = 0;
	bool bEvaluateVisuals = true, bVisualHidden = false, bVisualWakeActive = false;
	bool bSawAuthoredPresentation = false;

	TWeakObjectPtr<class USkeletalMeshComponent> OverriddenMesh;
	FTimerHandle RestoreAnimationUpdateRateTimer;
	bool bUrgentAnimationUpdateActive = false;
	bool bRestoreAnimationUpdateRateOptimization = false;
};
