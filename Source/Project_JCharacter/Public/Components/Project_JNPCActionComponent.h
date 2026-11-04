#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "AITypes.h"
#include "TimerManager.h"
#include "Project_JNPCActionComponent.generated.h"

class AAIController;
class UAbilitySystemComponent;
class UGameplayAbility;
class UProject_JTargetScoringComponent;
class UProject_JNPCPathSubsystem;
struct FProjectJNPCPathCompletion;
struct FPathFollowingResult;
struct FAbilityEndedData;

struct FProjectJNPCActionSuspension
{
	TWeakObjectPtr<UProject_JTargetScoringComponent> Scoring;
	FGameplayAbilitySpecHandle Attack;
	uint64 Revision = 0;
	uint64 OwnershipToken = 0;
	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;
	uint64 ActorInfoRevision = 0;
	bool IsValid() const { return Revision != 0; }
};
enum class EProjectJNPCActionResume : uint8 { Resumed, Deferred, Superseded };

UENUM(BlueprintType)
enum class EProjectJNPCActionState : uint8 { Disabled, Idle, AwaitingPath, FollowingPath, InRange, Attacking, Backoff };

/** Per-consumer lifetime counts. RetryScheduled counts backoff decisions, not successful new submissions. */
struct FProjectJNPCActionStats
{
	uint64 PathRequests = 0, PathRejected = 0, PathFailed = 0, PathStale = 0, RetryScheduled = 0, Arrivals = 0;
	uint64 ReplacedMoves = 0;
	uint64 ResumeDeferred = 0, ResumeExhausted = 0;
};

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
	/** Pure follow intent: no hostile scoring, attack grant or hit authority. The captured ASC may belong to a mounted rider. */
	bool StartFollowing(AActor* FollowOwner, UAbilitySystemComponent* OwnerASC, double FollowDistance, TFunction<void()> OnStopped = {});
	/** One finite nav-feet-space destination. Admission only; the existing scheduler
	 * reports real native reach=true or retirement=false after releasing this lease.
	 * Failure to admit never calls OnStopped. An enabled action is not replaced. */
	bool StartNavigationTo(FVector Destination, double AcceptanceRadius, TFunction<void(bool)> OnStopped = {});
	static bool SupportsAttackAbility(const UGameplayAbility* Ability);
	UFUNCTION(BlueprintCallable, Category="NPC|Action")
	void StopActions();
	/** Activation ownership survives a Mass suspension; an explicit stop/new start supersedes it. */
	uint64 GetOwnershipToken() const { return OwnershipToken; }
	bool StopActionsIfOwned(uint64 Token);
	UFUNCTION(BlueprintPure, Category="NPC|Action")
	AActor* GetIntentTarget() const { return IntentTarget.Get(); }
	/** Current registry, authority, life, range and decision age, rechecked at ability/hit commit. */
	bool CanCommitToTarget(AActor* Target) const;
	/** Additional authored melee contacts never replace the current primary intent or grant follow owners attack authority. */
	bool CanCommitSweepHit(AActor* PrimaryTarget, AActor* HitTarget) const;
	/** Stops only this consumer's pursuit before the ability commits. Rejects another system's movement. */
	bool PrepareForAttack(AActor* Target);
	void UpdateAction();
	UFUNCTION(BlueprintPure, Category="NPC|Action")
	EProjectJNPCActionState GetActionState() const { return State; }
	const FProjectJNPCActionStats& GetStats() const { return Stats; }
#if WITH_DEV_AUTOMATION_TESTS
	/** Identity observation only; never changes admission, cadence or navigation. */
	uint64 GetPendingPathTokenForTest() const { return PathToken; }
#endif
	/** GT representation handoff reads the start contract before StopActions releases it. */
	UProject_JTargetScoringComponent* GetScoringSource() const { return ScoringComponent.Get(); }
	FGameplayAbilitySpecHandle GetAttackAbilityHandle() const { return AttackHandle; }
	bool CanSuspendMovement() const { return !bFollowingOwner && !bNavigatingLocation && IsContextValid() && !HasForeignMovement(); }
	bool SuspendActions(FProjectJNPCActionSuspension& OutSuspension);
	EProjectJNPCActionResume ResumeSuspendedActions(const FProjectJNPCActionSuspension& Suspension);
	bool HasPendingResume() const { return PendingResume.IsValid(); }
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Action", meta=(ClampMin="50", ClampMax="2000"))
	double AttackRange = 200;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Action", meta=(ClampMin="25", ClampMax="1000"))
	double RepathDistance = 150;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Action", meta=(ClampMin="0.1", ClampMax="5"))
	double RetryInterval = 0.5;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Action", meta=(ClampMin="0.5", ClampMax="10"))
	double DecisionLifetime = 3.0;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
private:
	friend class FProjectJNPCPathPressureTest;
	bool StartContext(UProject_JTargetScoringComponent* Scoring, FGameplayAbilitySpecHandle AttackAbility, AActor* FollowOwner, UAbilitySystemComponent* OwnerASC, double FollowDistance, const FVector* FixedDestination = nullptr, double AcceptanceRadius = 0);
	void BackoffPath();
	void StopActionState();
	void FinishNavigation(bool bArrived);
	bool HasReachedNavigationGoal() const;
	uint32 PathFailures = 0;
	UFUNCTION()
	void OnScored(AActor* Target, double Score);
	/** Only a new completion advances LastDecision; post-attack catch-up retains its original age. */
	void ApplyScoredTarget(AActor* Target, bool bFreshDecision);
	void ClearIntent();
	void CancelPathAndMove();
	void CancelMove();
	void CancelOwnedAttack();
	void OnPathReady(const FProjectJNPCPathCompletion& Completion);
	void OnMoveFinished(FAIRequestID RequestId, const FPathFollowingResult& Result);
	void OnAbilityEnded(const FAbilityEndedData& Data);
	void OnTearDown(UWorld* World);
	void OnActorInfoChanging(AActor* NextOwner, AActor* NextAvatar);
	void OnFollowActorInfoChanging(AActor* NextOwner, AActor* NextAvatar);
	void RetrySuspendedActions();
	void ClearPendingResume();
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
	TWeakObjectPtr<AActor> FollowOwnerActor, FollowASCOwner;
	TWeakObjectPtr<UAbilitySystemComponent> FollowOwnerASC;
	TFunction<void()> FollowStopped;
	TFunction<void(bool)> NavigationStopped;
	FVector NavigationDestination = FVector::ZeroVector;
	double NavigationAcceptanceRadius = 0;
	bool bNavigatingLocation = false;
	double FollowStopDistance = 0;
	bool bFollowingOwner = false;
	FGameplayAbilitySpecHandle AttackHandle;
	FAIRequestID MoveId = FAIRequestID::InvalidRequest;
	FAIRequestID ReplacingMoveId = FAIRequestID::InvalidRequest;
	FDelegateHandle ContextHandle, MoveHandle, AbilityEndedHandle, TearDownHandle;
	FDelegateHandle ActorInfoChangingHandle, FollowActorInfoChangingHandle;
	uint64 AbilitySystemActorInfoRevision = 0;
	uint64 IntentRevision = 0, PathToken = 0;
	/** Last value owned by the pending token; independent of the currently followed path. */
	FVector QueuedGoal = FVector::ZeroVector;
	bool bCanRefreshQueuedPath = false;
	FVector FollowingGoal = FVector::ZeroVector;
	double LastDecision = 0, NextPathTime = 0, NextAttackTime = 0;
	EProjectJNPCActionState State = EProjectJNPCActionState::Disabled;
	bool bEnabled = false, bEndingPlay = false, bOwnsAttack = false, bStopping = false;
	FProjectJNPCActionStats Stats;
	uint64 ActionRevision = 0;
	uint64 NextOwnershipToken = 0, OwnershipToken = 0;
	FProjectJNPCActionSuspension PendingResume;
	FTimerHandle ResumeTimer;
	int32 ResumeAttempts = 0;
};
