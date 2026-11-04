#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Equipment/Project_JEquipmentTypes.h"
#include "Optimization/Project_JGameplayAsyncTypes.h"
#include "Project_JTargetScoringComponent.generated.h"

class UAbilitySystemComponent;
class UProject_JNPCActivationComponent;
/** GT-only source proof for opt-in damage threat. Worker candidate layout remains unchanged. */
struct FProject_JNPCTargetThreat
{
    TWeakObjectPtr<UAbilitySystemComponent> ASC;
    TWeakObjectPtr<AActor> Owner;
    uint64 Epoch = 0;
    double Value = 0;
};

class UProject_JEquipmentManagerComponent;
class UProject_JEquipmentItemDefinition;
class UProject_JTargetScoringSubsystem;
class UProject_JNPCDecisionSubsystem;
struct FProjectJTargetScoringCompletion;
namespace ProjectJ::TargetScoring { struct FSnapshot; class FTargetSpatialSnapshot; }

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProjectJTargetScored, AActor*, Target, double, Score);
DECLARE_MULTICAST_DELEGATE(FProjectJTargetContextInvalidated);

/** Optional learning/profiling seam. Advisory only: no RPC, GAS, damage, or automatic actor discovery. */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JTargetScoringComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UProject_JTargetScoringComponent();
	/** Learning default: worker dispatch with batched parallel scoring. Serial remains a comparison mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Target Scoring")
	EProject_JTargetScoringExecution Execution = EProject_JTargetScoringExecution::TaskParallelFor;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Target Scoring", meta=(ClampMin="1.0", ClampMax="1000000000.0"))
	double Range = 2000.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Target Scoring", meta=(ClampMin="0.0", ClampMax="1000.0"))
	double DistanceWeight = 1.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Target Scoring", meta=(ClampMin="0.0", ClampMax="1000.0"))
	double DirectionWeight = 1.0;
	/** Explicit combat/quest urgency seam for the NPC scheduler; does not grant authority or activate abilities. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Target Scoring|NPC")
	bool bUseUrgentNPCDecisionInterval = false;

	/** Accepted means queued. A completed query may produce nullptr; cancellation has no callback. */
	UFUNCTION(BlueprintCallable, Category="Target Scoring")
	bool RequestTargets(const TArray<AActor*>& Candidates);
	/** Opt-in NPC/server scheduler. Team is explicit experiment metadata, not replicated faction state. */
	UFUNCTION(BlueprintCallable, Category="Target Scoring|NPC")
	bool StartBatchedNPCDecisions(int32 TeamId);
	UFUNCTION(BlueprintCallable, Category="Target Scoring|NPC")
	void StopBatchedNPCDecisions();
	bool IsBatchedNPCDecisionRegistered() const { return bNPCBatchRegistered; }
	uint64 GetBatchRegistrationRevision() const { return BatchRegistrationRevision; }
	/** Call when class, skill, possession, or query eligibility changes. Equipment changes bind automatically. */
	UFUNCTION(BlueprintCallable, Category="Target Scoring")
	void InvalidateQueryContext();
	UFUNCTION(BlueprintPure, Category="Target Scoring")
	AActor* GetLastScoredTarget() const { return SelectedTarget.Get(); }
	UPROPERTY(BlueprintAssignable, Category="Target Scoring")
	FProjectJTargetScored OnQueryCompleted;
	/** Semantic invalidation only; preparing the next snapshot does not invalidate a consumer's intent. */
	FProjectJTargetContextInvalidated OnContextInvalidated;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
private:
	friend class UProject_JNPCDecisionSubsystem;
	friend class UProject_JNPCActivationComponent;
	friend class FProjectJNPCDamageThreatTest;
	bool BindDamageThreatProvider(UProject_JNPCActivationComponent* Provider, uint64 Registration);
	void UnbindDamageThreatProvider(UProject_JNPCActivationComponent* Provider, uint64 Registration);
	TWeakObjectPtr<UProject_JNPCActivationComponent> DamageThreatProvider;
	uint64 DamageThreatRegistration = 0;
	TArray<FProject_JNPCTargetThreat> RequestedThreatSources;
	bool bRequestedDamageThreat = false;
	friend class FProjectJTargetScoringComponentTest;
	void ResetPendingQuery();
	bool BeginSnapshot(int32 CandidateCount, ProjectJ::TargetScoring::FSnapshot& Snapshot);
	bool PrepareSnapshot(const TArray<AActor*>& Candidates, ProjectJ::TargetScoring::FSnapshot& Snapshot);
	/** GT-only gather from the existing immutable shared spatial snapshot, without temporary actor/position arrays. */
	bool PrepareSpatialSnapshot(const ProjectJ::TargetScoring::FTargetSpatialSnapshot& Spatial,
		TConstArrayView<int32> Indices, TConstArrayView<TWeakObjectPtr<AActor>> Actors,
		ProjectJ::TargetScoring::FSnapshot& Snapshot);
	UProject_JEquipmentManagerComponent* ResolveEquipmentManager() const;
	void BindEquipment(UProject_JEquipmentManagerComponent* Manager);
	/** Optional batch deadline; standalone requests keep their Core admission deadline. */
	void ApplyResult(const FProjectJTargetScoringCompletion& Completion, double DeadlineSeconds = 0.0);
	UFUNCTION()
	void OnEquipmentChanged(EProject_JEquipmentSlot Slot, UProject_JEquipmentItemDefinition* Item);
	TWeakObjectPtr<UProject_JEquipmentManagerComponent> BoundEquipment;
	TWeakObjectPtr<UProject_JTargetScoringSubsystem> QuerySubsystem;
	TWeakObjectPtr<UProject_JNPCDecisionSubsystem> NPCDecisionSubsystem;
	TArray<TWeakObjectPtr<AActor>> CandidateActors;
	TWeakObjectPtr<AActor> SelectedTarget;
	FProject_JGameplayAsyncRequestToken PendingToken;
	uint64 ContextRevision = 0;
	double RequestedRange = 0.0;
	bool bEndingPlay = false;
	bool bSharedBatchRequest = false;
	bool bNPCBatchRegistered = false;
	bool bNotifyingContext = false;
	int32 NPCDecisionTeam = INDEX_NONE;
	uint64 BatchRegistrationRevision = 0;
	void AdvanceRegistrationRevision() { if (BatchRegistrationRevision != MAX_uint64) { ++BatchRegistrationRevision; } }
};
