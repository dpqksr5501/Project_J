#pragma once
#include "CoreMinimal.h"
#include "SkeletalMeshComponentBudgeted.h"
#include "Project_JBudgetedSkeletalMeshComponent.generated.h"

class UProject_JCharacterAnimationBudgetSubsystem;
class UAnimInstance;
class UAnimMontage;

/** Character mesh with explicit ABA ownership. Gameplay-critical poses use the normal engine path. */
UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JBudgetedSkeletalMeshComponent : public USkeletalMeshComponentBudgeted
{
	GENERATED_BODY()
public:
	UProject_JBudgetedSkeletalMeshComponent(const FObjectInitializer& Initializer);
	void SetCombatCritical(bool bCritical);
	bool IsManagedByBudget() const { return bManaged; }
	bool IsCombatCritical() const { return bCombatCritical; }
	bool CanUseBudget() const;
	virtual void SetComponentTickEnabled(bool bEnabled) override;

	/** Actor-level escape hatch; never changes process CVars or other characters. */
	UPROPERTY(EditAnywhere, Category="Budgeting|ProjectJ")
	bool bAllowProjectBudget = true;
	/** Optional animation-only offscreen workload. Gameplay notifies are protected separately. */
	UPROPERTY(EditAnywhere, Category="Budgeting|ProjectJ")
	bool bBudgetTickWhenNotRendered = false;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
private:
	friend class UProject_JCharacterAnimationBudgetSubsystem;
	void EnterBudget();
	void LeaveBudget();
	void DetachService();
	UFUNCTION() void BindAnimationEvents();
	UFUNCTION() void OnMontageStarted(UAnimMontage* Montage);
	TWeakObjectPtr<UAnimInstance> BoundAnimation;
	TWeakObjectPtr<UProject_JCharacterAnimationBudgetSubsystem> Service;
	bool bManaged = false, bCombatCritical = false, bEnding = false;
	bool bSavedURO = false, bRequestedTick = true;
};
