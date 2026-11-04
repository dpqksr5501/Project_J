#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Async/TaskGraphInterfaces.h"
#include "Components/SkinnedMeshComponent.h"
#include "Animation/Project_JAnimationUpdateSchedule.h"
#include "Project_JAnimationUpdateCoordinatorComponent.generated.h"

/** Values from already registered cloth bindings, never CPU milliseconds. */
struct FProjectJClothExecutionDemand
{
	int64 Eligible = 0, Reserved = 0, Protected = 0, Resume = 0;
	int32 Unknown = 0;
	bool bPreviouslyDesired = false;
};

/** Read-only accounting. Reserved includes protected/unmanaged known geometry;
 * Parked includes a pending native drain until Draining becomes zero.
 */
struct FProjectJClothExecutionStats
{
	int64 Eligible = 0, Reserved = 0, Protected = 0;
	int32 Unknown = 0, Active = 0, FadeOut = 0, Parked = 0, Warming = 0, FadeIn = 0;
	int32 Unmanaged = 0, Draining = 0, Desired = 0, MayResume = 0, Releasing = 0;
};

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
	virtual void Deactivate() override;
	virtual void SetComponentTickEnabled(bool bEnabled) override;
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
	/** Cached per-part sum of each unique clothing asset's largest physics LOD; not CPU milliseconds. */
	int64 GetManagedClothVertexCost(int32& OutUnknownParts) const;
	/** Called only by this world's natural post-actor-tick observer. Never waits or changes cloth state. */
	void ObserveClothNativeFrameCompletions();
	int32 GetQualifiedClothNativeFrameCount(uint64 Frame) const;
	/** 0 disables execution actuation; independent of the existing quality policy. */
	static int64 GetClothExecutionVertexCap();
	bool IsClothExecutionSource(const USkeletalMeshComponent* Source) const;
	/** Read only; measurement must never call PrepareClothExecutionDemand. */
	FProjectJClothExecutionStats GetClothExecutionStats() const;
	FProjectJClothExecutionDemand PrepareClothExecutionDemand(bool bEnabled, bool bProtected);
	void SetClothExecutionGrant(bool bDesired, bool bMayResume, bool bProtected);
	/** Synchronous explicit handoff before another writer changes component cloth controls. */
	void ReleaseClothExecution(class UProject_JModularMeshComponent* Part = nullptr);

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
	void RefreshClothBudget(class UProject_JBudgetedSkeletalMeshComponent* Source, bool bLocal, float DeltaTime = 0.f);
	void RefreshClothExecution(class UProject_JBudgetedSkeletalMeshComponent* Source, float DeltaTime);
	void RestoreClothBudget();
	struct FClothBinding
	{
		TWeakObjectPtr<class UClothingAssetCommon> Asset;
		TWeakObjectPtr<class UClothingSimulationInteractor> Simulation;
	};
	enum class EClothExecution : uint8 { Active, FadeOut, Parked, Warming, FadeIn };
	struct FClothMesh
	{
		TWeakObjectPtr<class UProject_JModularMeshComponent> Mesh;
		TWeakObjectPtr<class USkeletalMesh> Asset;
		TArray<FClothBinding> Bindings;
		TOptional<int64> MaxLODPhysicsVertices;
		TWeakObjectPtr<USkinnedMeshComponent> ExecutionLeader;
		EClothExecution Execution = EClothExecution::Active;
		float SavedBlend = 1.f, LastBlend = 1.f, ExecutionAlpha = 1.f;
		uint64 GrantFrame = MAX_uint64, ResumeFrame = MAX_uint64, DrainedFrame = MAX_uint64;
		FGraphEventRef DrainTickCompletion, DrainEndCompletion;
		bool bExecutionOwned = false, bExecutionDeclined = false, bExecutionReserved = false;
		bool bExecutionReleaseRequested = false;
		bool bExecutionDesired = true, bExecutionMayResume = false, bExecutionProtected = true;
		bool bSavedSuspended = false, bLastSuspended = false;
		bool CanOwnExecution() const;
		bool HasOwnedExecutionValues() const;
		void ReleaseExecution(bool bDecline = false);
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
