#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "AITypes.h"
#include "Project_JNPCActionComponent.generated.h"

class AAIController;
class UAbilitySystemComponent;
class UProject_JTargetScoringComponent;
class UProject_JNPCPathSubsystem;
struct FProjectJNPCPathCompletion;
struct FPathFollowingResult;
struct FAbilityEndedData;

UENUM(BlueprintType)
enum class EProjectJNPCActionState : uint8 { Disabled, Idle, AwaitingPath, FollowingPath, InRange, Attacking, Backoff };

/** Explicit server-only consumer. Existing controller, faction registry, grants and abilities remain authoritative. */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JNPCActionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UProject_JNPCActionComponent();
	/** Scoring must already be batch-registered on this NPC. Invalid ability handle enables pursuit only.
	 * Attack must be an NPC-compatible, InstancedPerActor ServerOnly/ServerInitiated ability already granted to the ASC.
	 * Its targeting code can read GetIntentTarget(), and must validate hits/cancellation itself. */
	UFUNCTION(BlueprintCallable, Category="NPC|Action")
	bool StartActions(UProject_JTargetScoringComponent* Scoring, FGameplayAbilitySpecHandle AttackAbility);
	UFUNCTION(BlueprintCallable, Category="NPC|Action")
	void StopActions();
	UFUNCTION(BlueprintPure, Category="NPC|Action")
	AActor* GetIntentTarget() const { return IntentTarget.Get(); }
	/** Current registry, authority, life, range and decision age, rechecked at ability/hit commit. */
	bool CanCommitToTarget(AActor* Target) const;
	void UpdateAction();
	UFUNCTION(BlueprintPure, Category="NPC|Action")
	EProjectJNPCActionState GetActionState() const { return State; }
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Action", meta=(ClampMin="50", ClampMax="2000"))
	double AttackRange = 200;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Action", meta=(ClampMin="25", ClampMax="1000"))
	double RepathDistance = 150;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Action", meta=(ClampMin="0.1", ClampMax="5"))
	double RetryInterval = 0.5;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Action", meta=(ClampMin="0.5", ClampMax="10"))
	double DecisionLifetime = 3.0;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
private:
	UFUNCTION()
	void OnScored(AActor* Target, double Score);
	void ClearIntent();
	void CancelPathAndMove();
	void CancelOwnedAttack();
	void OnPathReady(const FProjectJNPCPathCompletion& Completion);
	void OnMoveFinished(FAIRequestID RequestId, const FPathFollowingResult& Result);
	void OnAbilityEnded(const FAbilityEndedData& Data);
	void OnTearDown(UWorld* World);
	bool IsContextValid() const;
	bool IsTargetValid() const;
	bool HasForeignMovement() const;
	bool IsAttackSupported() const;
	FVector GetTargetGoal() const;
	TWeakObjectPtr<UProject_JTargetScoringComponent> ScoringComponent;
	TWeakObjectPtr<UProject_JNPCPathSubsystem> Paths;
	TWeakObjectPtr<AAIController> Controller;
	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;
	TWeakObjectPtr<AActor> IntentTarget;
	FGameplayAbilitySpecHandle AttackHandle;
	FAIRequestID MoveId = FAIRequestID::InvalidRequest;
	FDelegateHandle ContextHandle, MoveHandle, AbilityEndedHandle, TearDownHandle;
	uint64 IntentRevision = 0, PathToken = 0;
	FVector RequestedGoal = FVector::ZeroVector;
	double LastDecision = 0, NextPathTime = 0, NextAttackTime = 0;
	EProjectJNPCActionState State = EProjectJNPCActionState::Disabled;
	bool bEnabled = false, bEndingPlay = false, bOwnsAttack = false, bStopping = false;
};
