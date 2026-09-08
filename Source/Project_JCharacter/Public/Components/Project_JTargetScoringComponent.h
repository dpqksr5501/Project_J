#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Equipment/Project_JEquipmentTypes.h"
#include "Optimization/Project_JGameplayAsyncTypes.h"
#include "Project_JTargetScoringComponent.generated.h"

class UProject_JEquipmentManagerComponent;
class UProject_JEquipmentItemDefinition;
class UProject_JTargetScoringSubsystem;
struct FProjectJTargetScoringCompletion;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProjectJTargetScored, AActor*, Target, double, Score);

/** Optional learning/profiling seam. Advisory only: no RPC, GAS, damage, or automatic actor discovery. */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JTargetScoringComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UProject_JTargetScoringComponent();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Target Scoring")
	EProject_JTargetScoringExecution Execution = EProject_JTargetScoringExecution::Serial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Target Scoring", meta=(ClampMin="1.0", ClampMax="1000000000.0"))
	double Range = 2000.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Target Scoring", meta=(ClampMin="0.0", ClampMax="1000.0"))
	double DistanceWeight = 1.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Target Scoring", meta=(ClampMin="0.0", ClampMax="1000.0"))
	double DirectionWeight = 1.0;

	/** Accepted means queued. A completed query may produce nullptr; cancellation has no callback. */
	UFUNCTION(BlueprintCallable, Category="Target Scoring")
	bool RequestTargets(const TArray<AActor*>& Candidates);
	/** Call when class, skill, possession, or query eligibility changes. Equipment changes bind automatically. */
	UFUNCTION(BlueprintCallable, Category="Target Scoring")
	void InvalidateQueryContext();
	UFUNCTION(BlueprintPure, Category="Target Scoring")
	AActor* GetLastScoredTarget() const { return SelectedTarget.Get(); }
	UPROPERTY(BlueprintAssignable, Category="Target Scoring")
	FProjectJTargetScored OnQueryCompleted;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
private:
	UProject_JEquipmentManagerComponent* ResolveEquipmentManager() const;
	void BindEquipment(UProject_JEquipmentManagerComponent* Manager);
	void ApplyResult(const FProjectJTargetScoringCompletion& Completion);
	UFUNCTION()
	void OnEquipmentChanged(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* Item);
	TWeakObjectPtr<UProject_JEquipmentManagerComponent> BoundEquipment;
	TWeakObjectPtr<UProject_JTargetScoringSubsystem> QuerySubsystem;
	TArray<TWeakObjectPtr<AActor>> CandidateActors;
	TWeakObjectPtr<AActor> SelectedTarget;
	FProject_JGameplayAsyncRequestToken PendingToken;
	uint64 ContextRevision = 0;
	double RequestedRange = 0.0;
	bool bEndingPlay = false;
};
