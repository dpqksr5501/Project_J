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
	int32 GetManagedClothCount() const;
	int32 GetReducedClothCount() const;

private:
	friend class FProjectJUrgentAnimationWindowTest;
	friend class FProjectJClothAllocationTest;
	friend class FProjectJClothLifecycleTest;
	void RestoreRemoteAnimationUpdateRateOptimization();
	void RestoreVisualBudget();
	void DiscoverVisualMeshes();
	bool CanManageVisualMesh(const class USkeletalMeshComponent* Mesh) const;
	void RefreshSourceRequirement();
	void DiscoverClothMesh(class USkeletalMeshComponent* Mesh);
	void RefreshClothBudget(class UProject_JBudgetedSkeletalMeshComponent* Source, bool bLocal);
	void RestoreClothBudget();
	struct FClothBinding
	{
		TWeakObjectPtr<class UClothingAssetCommon> Asset;
		TWeakObjectPtr<class UClothingSimulationInteractor> Simulation;
	};
	struct FClothMesh
	{
		TWeakObjectPtr<class UProject_JModularMeshComponent> Mesh;
		TWeakObjectPtr<class USkeletalMesh> Asset;
		TArray<FClothBinding> Bindings;
		bool bReduced = false, bAppliedDiscrete = false, bAddedCoordinatorPrerequisite = false;
		bool IsCurrent() const;
		bool Bind(class UProject_JModularMeshComponent* Component);
		bool Apply(bool bReduce);
		void Restore(UActorComponent* Coordinator = nullptr);
	};
	TArray<FClothMesh> ClothMeshes;
	double ClothDowngradeSince = -1;
	struct FVisualMesh
	{
		TWeakObjectPtr<class USkeletalMeshComponent> Mesh;
		bool bSavedURO = false, bSavedTick = true, bDisabledTick = false;
		bool bAddedSourcePrerequisite = false;
		bool bDisabledParentLODSync = false;
		bool bEnabledIndependentLeaderLOD = false;
		EVisibilityBasedAnimTickOption SavedVisibility = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		void Restore(UActorComponent* Coordinator, USkeletalMeshComponent* Source);
		void RefreshRenderLOD(bool bIndependent);
	};
	TArray<FVisualMesh> Visuals;
	/** Leader-pose consumers own render LOD only, never another animation tick. */
	TArray<FVisualMesh> RenderParts;
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
