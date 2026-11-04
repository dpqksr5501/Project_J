#pragma once
#include "CoreMinimal.h"
#include "SkeletalMeshComponentBudgeted.h"
#include "Animation/Project_JSharedPhysicsBounds.h"
#include "Project_JBudgetedSkeletalMeshComponent.generated.h"

class UProject_JCharacterAnimationBudgetSubsystem;
class UAnimInstance;
class UAnimMontage;

/** Temporary demands compose on the mesh, independently of their callers' lifetimes. */
enum class EProject_JAnimationUpdateRequirement : uint8
{
	Presentation,
	GameplayPose
};

/** Character mesh with explicit ABA ownership. Gameplay-critical poses use the normal engine path. */
UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JBudgetedSkeletalMeshComponent : public USkeletalMeshComponentBudgeted
{
	GENERATED_BODY()
public:
	UProject_JBudgetedSkeletalMeshComponent(const FObjectInitializer& Initializer);
	void SetCombatCritical(bool bCritical);
	void RequestAnimationUpdate(UObject* Requester, EProject_JAnimationUpdateRequirement Requirement);
	void ReleaseAnimationUpdate(UObject* Requester);
	bool IsManagedByBudget() const { return bManaged; }
	bool IsCombatCritical() const { return bCombatCritical || bGameplayPoseOverride; }
	bool HasAnimationUpdateRequirement() const { return bUpdateOverride; }
	/** GT observation shared by animation consumers; never writes gameplay/AI significance. */
	float GetAnimationDistanceTier() const;
	/** Actual local-view observation only. Unknown remains unknown to optional cosmetic consumers. GT only. */
	TOptional<float> GetObservedAnimationDistanceTier() const { check(IsInGameThread()); return ObservedDistanceTier; }
	/** Independent cosmetic cloth grant; unknown observation preserves authored quality. */
	bool HasFullClothQualityGrant() const { return ObservedFullClothQuality.Get(true); }
	/** Requested tick state is distinct from the allocator's current scheduled tick. */
	bool GetRequestedTickEnabled() const { return bRequestedTick; }
	bool CanUseBudget() const;
	virtual void SetComponentTickEnabled(bool bEnabled) override;
    virtual void SetSkeletalMesh(USkeletalMesh* NewMesh,bool bReinitPose=true) override;
    virtual void SetPhysicsAsset(class UPhysicsAsset* NewAsset,bool bForceReInit=false) override;

	/** Actor-level escape hatch; never changes process CVars or other characters. */
	UPROPERTY(EditAnywhere, Category="Budgeting|ProjectJ")
	bool bAllowProjectBudget = true;
	/** Optional animation-only offscreen workload. Gameplay notifies are protected separately. */
	UPROPERTY(EditAnywhere, Category="Budgeting|ProjectJ")
	bool bBudgetTickWhenNotRendered = true;
protected:
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
private:
	friend class UProject_JCharacterAnimationBudgetSubsystem;
    friend class UProject_JModularMeshComponent;
    bool IsSharedPhysicsBoundsAvailable() const;
    bool TryReadSharedPhysicsBounds(const Project_J::Bounds::FKey& Key,TConstArrayView<Project_J::Bounds::FBodyValue> BodyValues,FBoxSphereBounds& OutLocal) const;
    void PublishSharedPhysicsBounds(const Project_J::Bounds::FKey& Key,TConstArrayView<Project_J::Bounds::FBodyValue> BodyValues,const FBoxSphereBounds& Local) const;
    void RetireSharedPhysicsBounds();
    mutable Project_J::Bounds::FEntry SharedPhysicsBounds;
    bool bChangingSharedBoundsSource=false;
	void EnterBudget();
	void LeaveBudget();
	void DetachService();
	void RefreshAnimationUpdateRequirements();
	void RefreshMontageUpdateRequirement();
	TMap<TWeakObjectPtr<UObject>, EProject_JAnimationUpdateRequirement> UpdateRequirements;
	bool bUpdateOverride = false, bGameplayPoseOverride = false;
	bool bMontageUpdateRequirement = false;
	bool bOverrideSavedURO = false, bOverrideSavedSuppressNotifies = false;
	EVisibilityBasedAnimTickOption OverrideSavedVisibility = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	UFUNCTION() void BindAnimationEvents();
	UFUNCTION() void OnMontageStarted(UAnimMontage* Montage);
	TWeakObjectPtr<UAnimInstance> BoundAnimation;
	TWeakObjectPtr<UProject_JCharacterAnimationBudgetSubsystem> Service;
	TOptional<float> ObservedDistanceTier;
	TOptional<bool> ObservedFullClothQuality;
	bool bManaged = false, bCombatCritical = false, bEnding = false;
	bool bSavedURO = false, bRequestedTick = true;
};

namespace Project_J::Anim
{
	PROJECT_JCHARACTER_API bool GetBudgetTickWhenNotRendered();
}
