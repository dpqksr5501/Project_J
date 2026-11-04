#pragma once

#include "CoreMinimal.h"
#include "Containers/Deque.h"
#include "Subsystems/WorldSubsystem.h"
#include "System/Project_JTargetScoringSubsystem.h"
#include "Optimization/Project_JNPCUpdateBudget.h"
#include "Optimization/Project_JTargetSpatialSnapshot.h"
#include "Project_JNPCDecisionSubsystem.generated.h"

class UProject_JTargetScoringComponent;
class UProject_JNPCActionComponent;
struct FProjectJNPCDecisionAgent;

struct FProjectJNPCDecisionStats
{
	uint64 SubmittedBatches = 0;
	uint64 SubmittedDecisions = 0;
	/** Actual GT batch completions; duration totals are wall elapsed, not summed CPU or worker wait. */
	uint64 CompletedBatches = 0;
	double CompletedComputeMicroseconds = 0.0, CompletedCoreDeliveryMilliseconds = 0.0;
	uint64 AppliedDecisions = 0;
	uint64 DiscardedDecisions = 0;
	uint64 RejectedBatches = 0;
	uint64 BatchCapacityDeferrals = 0, RejectedTargets = 0, RejectedObservers = 0;
	int32 LastTickSnapshotValues = 0;
	double MaxDecisionLatenessMilliseconds = 0;
	int32 LastTickAgentVisits = 0;
	int32 LastTickCandidateVisits = 0;
	int32 LastTickResults = 0;
	int32 LastTickTargetPositionReads = 0;
	int32 LastTickSnapshotBuilds = 0;
	int32 LastTickSpatialCells = 0;
	int32 LastTickLinearFallbacks = 0;
	int32 LastTickObservers = 0;
	int32 LastTickObserverCandidateVisits = 0;
	int32 LastTickImportanceUpdates = 0;
	int32 LastTickPromotions = 0;
	bool bObserverCoverageIncomplete = false;
	double LastTickGameThreadMilliseconds = 0.0;
	/** Full Tick includes action work; collection budget remains a separate existing contract. */
	double LastFullTickMilliseconds = 0, MaxFullTickMilliseconds = 0;
	double LastSharedSnapshotMilliseconds = 0, OldestReadyResultMilliseconds = 0;
	int32 LastTickActionVisits = 0, LastTickActionUpdates = 0;
	double LastTickActionMilliseconds = 0.0;
	double MaxActionLatenessMilliseconds = 0.0;
    /** Data-only collection progress; never Core/ready decision counts. */
    uint64 CaptureDeferrals = 0, CaptureExpired = 0, CaptureInvalidated = 0;
    int32 CaptureNextIndex = 0, CaptureTargetCount = 0;
    bool bCapturePending = false, bCaptureReady = false;
    /** Latest Tick observation of the oldest capture input, including time between slices. */
    double CaptureAgeMilliseconds = 0.0;
};

/** GT collection/scheduling only. Reuses Core's bounded task service; never owns worker threads. */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JNPCDecisionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	static constexpr int32 MaxAgents = 2048;
	static constexpr int32 MaxTargets = 2048;
	static constexpr int32 MaxQueriesPerDispatch = 32;
	static constexpr int32 MaxOutstandingDecisions = 64;
	static constexpr int32 MaxAgentVisitsPerTick = 128;
	static constexpr int32 MaxResultsPerTick = 16;
	static constexpr int32 MaxObservers = 512;
	static constexpr double GameThreadBudgetMilliseconds = 1.0;
	static constexpr double MaxResultAgeSeconds = 0.5;

	bool RegisterAgent(UProject_JTargetScoringComponent* Component, int32 TeamId);
	bool RegisterAction(UProject_JNPCActionComponent* Component);
	void UnregisterAction(UProject_JNPCActionComponent* Component);
	int32 GetActionCount() const { return Actions.Num(); }
	static constexpr int32 MaxActionVisitsPerTick = 128;
	static constexpr int32 MaxActionUpdatesPerTick = 32;
	static constexpr double ActionBudgetMilliseconds = 0.75;
	void UnregisterAgent(UProject_JTargetScoringComponent* Component);
	/** Revalidate authoritative registry membership/team/life when consuming an advisory decision. */
	bool CanActOnTarget(const UProject_JTargetScoringComponent* Component, AActor* Target) const;
	/** Explicit, bounded registry. Caller updates team metadata when its authoritative faction changes. */
	UFUNCTION(BlueprintCallable, Category="NPC|Decision")
	bool RegisterTarget(AActor* Target, int32 TeamId);
	UFUNCTION(BlueprintCallable, Category="NPC|Decision")
	void UnregisterTarget(AActor* Target);
	/** Additional server-owned interest points. Player-controller pawn positions are included automatically. */
	UFUNCTION(BlueprintCallable, Category="NPC|Decision")
	bool RegisterObserver(AActor* Observer);
	UFUNCTION(BlueprintCallable, Category="NPC|Decision")
	void UnregisterObserver(AActor* Observer);
	bool GetAgentDecisionBudget(const UProject_JTargetScoringComponent* Component,
		EProject_JNPCUpdateBudgetTier& OutTier, double& OutInterval) const;
	int32 GetAgentCount() const { return Agents.Num(); }
	int32 GetTargetCount() const { return Targets.Num(); }
	/** GT callback fence for consumers retaining eligibility across extensible life queries. */
	uint64 GetTargetRegistryRevision() const { return TargetRegistryRevision; }
	int32 GetOutstandingCount() const { return OutstandingDecisions; }
	const FProjectJNPCDecisionStats& GetStats() const { return Stats; }
	static bool CanScheduleNetworkMode(ENetMode Mode) { return Mode != NM_Client; }
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FProjectJNPCRegistryChurnTest;
#endif
	struct FTarget { TWeakObjectPtr<AActor> Actor; int32 Team = 0; };
	void StopScheduler();
	void UpdateActions();
	struct FActionEntry
	{
		TWeakObjectPtr<UProject_JNPCActionComponent> Component, Previous, Next;
		double NextDue = 0;
	};
	TArray<FActionEntry> Actions;
	/** Derived lookup into dense execution storage; no second action/state owner. */
	TMap<TWeakObjectPtr<UProject_JNPCActionComponent>, int32> ActionIndices;
	void RemoveActionAt(int32 Index);
	TWeakObjectPtr<UProject_JNPCActionComponent> ActionCursor;
	void OnWorldTearDown(UWorld* World);
	bool IsActiveServer() const;
	bool IsEligibleTarget(AActor* Target, int32 AgentTeam) const;
	bool CaptureObserverPositions(TArray<FVector>& Positions);
	void QueueResults(const FProjectJTargetScoringBatchCompletion& Completion,
		const TArray<TSharedPtr<FProjectJNPCDecisionAgent>>& BatchAgents, const TArray<uint64>& Revisions, double CapturedAt);
	TArray<TSharedPtr<FProjectJNPCDecisionAgent>> Agents;
	/** Dense storage lookup; non-owning links preserve execution order independently of swaps. */
	TMap<TWeakObjectPtr<const UProject_JTargetScoringComponent>, int32> AgentIndices;
	TWeakObjectPtr<UProject_JTargetScoringComponent> AgentCursor;
	void RemoveAgentAt(int32 Index);
	TArray<FTarget> Targets;
    /** One bounded collection in progress, not a reusable cross-batch cache. */
    struct FPendingTargetCapture
    {
        uint64 Serial = 0, RegistryRevision = 0;
        int32 TargetCount = 0, NextIndex = 0;
        double CapturedAt = 0.0;
        TArray<ProjectJ::TargetScoring::FSpatialTarget> Values;
        TArray<TWeakObjectPtr<AActor>> Actors;
        ProjectJ::TargetScoring::FTargetSpatialSnapshot Spatial;
        bool bBuilt = false;
    };
    TOptional<FPendingTargetCapture> TargetCapture;
    uint64 NextTargetCaptureSerial = 0;
    void DiscardTargetCapture(bool bExpired);
    void UpdateTargetCaptureStats();
	/** Derived membership only; ordered Targets remains the authority and snapshot/tie-break source. */
	mutable TMap<TWeakObjectPtr<AActor>, int32> TargetTeams;
	mutable uint64 TargetTeamsRevision = MAX_uint64;
	TArray<TWeakObjectPtr<AActor>> Observers;
	struct FReadyDecision
	{
		TSharedPtr<FProjectJNPCDecisionAgent> Agent;
		FProjectJTargetScoringCompletion Completion;
		/** Oldest batch input, including GT collection before Core admission. */
		double CapturedAt = 0.0;
	};
	// One bounded GT FIFO. Keep the popped value alive across consumer callbacks;
	// only the existing agent lease is shared, not each ready delivery record.
	TDeque<FReadyDecision, TInlineAllocator<MaxOutstandingDecisions>> Ready;
	TArray<FProject_JGameplayAsyncRequestToken> ActiveBatches;
	TWeakObjectPtr<UProject_JTargetScoringSubsystem> Scoring;
	FDelegateHandle TearDownHandle;
	FProjectJNPCDecisionStats Stats;
	int32 OutstandingDecisions = 0;
	bool bAccepting = false;
	bool bTicking = false;
	uint64 TargetRegistryRevision = 0;
};
