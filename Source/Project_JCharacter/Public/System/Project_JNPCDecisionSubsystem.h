#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "System/Project_JTargetScoringSubsystem.h"
#include "Project_JNPCDecisionSubsystem.generated.h"

class UProject_JTargetScoringComponent;
struct FProjectJNPCDecisionAgent;
struct FProjectJNPCDecisionReady;

struct FProjectJNPCDecisionStats
{
	uint64 SubmittedBatches = 0;
	uint64 SubmittedDecisions = 0;
	uint64 AppliedDecisions = 0;
	uint64 DiscardedDecisions = 0;
	uint64 RejectedBatches = 0;
	int32 LastTickAgentVisits = 0;
	int32 LastTickCandidateVisits = 0;
	int32 LastTickResults = 0;
	double LastTickGameThreadMilliseconds = 0.0;
};

/** GT collection/scheduling only. Reuses Core's bounded task service; never owns worker threads. */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JNPCDecisionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	static constexpr int32 MaxAgents = 2048;
	static constexpr int32 MaxTargets = 256;
	static constexpr int32 MaxQueriesPerDispatch = 32;
	static constexpr int32 MaxOutstandingDecisions = 64;
	static constexpr int32 MaxAgentVisitsPerTick = 128;
	static constexpr int32 MaxResultsPerTick = 16;
	static constexpr double GameThreadBudgetMilliseconds = 1.0;
	static constexpr double MaxResultAgeSeconds = 0.5;

	bool RegisterAgent(UProject_JTargetScoringComponent* Component, int32 TeamId);
	void UnregisterAgent(UProject_JTargetScoringComponent* Component);
	/** Explicit, bounded registry. Caller updates team metadata when its authoritative faction changes. */
	UFUNCTION(BlueprintCallable, Category="NPC|Decision")
	bool RegisterTarget(AActor* Target, int32 TeamId);
	UFUNCTION(BlueprintCallable, Category="NPC|Decision")
	void UnregisterTarget(AActor* Target);
	int32 GetAgentCount() const { return Agents.Num(); }
	int32 GetTargetCount() const { return Targets.Num(); }
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
	struct FTarget { TWeakObjectPtr<AActor> Actor; int32 Team = 0; };
	void StopScheduler();
	void OnWorldTearDown(UWorld* World);
	bool IsActiveServer() const;
	bool IsEligibleTarget(AActor* Target, int32 AgentTeam) const;
	void QueueResults(const FProjectJTargetScoringBatchCompletion& Completion,
		const TArray<TSharedPtr<FProjectJNPCDecisionAgent>>& BatchAgents, const TArray<uint64>& Revisions, double Submitted);
	TArray<TSharedPtr<FProjectJNPCDecisionAgent>> Agents;
	TArray<FTarget> Targets;
	TArray<TSharedPtr<FProjectJNPCDecisionReady>> Ready;
	TArray<FProject_JGameplayAsyncRequestToken> ActiveBatches;
	TWeakObjectPtr<UProject_JTargetScoringSubsystem> Scoring;
	FDelegateHandle TearDownHandle;
	FProjectJNPCDecisionStats Stats;
	int32 Cursor = 0;
	int32 OutstandingDecisions = 0;
	bool bAccepting = false;
};
