#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Optimization/Project_JGameplayAsyncTypes.h"
#include "Optimization/Project_JTargetScoring.h"
#include "Project_JTargetScoringSubsystem.generated.h"

struct FProjectJTargetScoringJob;

struct FProjectJTargetScoringCompletion
{
	FProject_JGameplayAsyncRequestToken Token;
	FGuid WorldEpoch;
	uint64 ContextRevision = 0;
	ProjectJ::TargetScoring::FResult Result;
	double DeliveryMilliseconds = 0.0;
};

/** Opt-in, bounded data-only experiment. It never selects actors or applies gameplay itself. */
UCLASS()
class PROJECT_JCORE_API UProject_JTargetScoringSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	static constexpr int32 MaxPendingRequests = 8;
	static constexpr int32 MaxGlobalWorkerTasks = 2;
	static constexpr int32 MaxCompletionsPerTick = 2;
	using FCompletion = TFunction<void(const FProjectJTargetScoringCompletion&)>;

	FProject_JGameplayAsyncRequestToken Submit(UObject* Owner, ProjectJ::TargetScoring::FSnapshot Snapshot,
		EProject_JTargetScoringExecution Mode, uint64 ContextRevision, FCompletion Completion, double TimeoutSeconds = 0.25);
	void Cancel(FProject_JGameplayAsyncRequestToken Token);
	void CancelForOwner(const UObject* Owner);
	bool IsPending(FProject_JGameplayAsyncRequestToken Token) const;
	int32 GetPendingCount() const { return Jobs.Num(); }
	const FGuid& GetWorldEpoch() const { return WorldEpoch; }

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
private:
	void OnWorldTearDown(UWorld* InWorld);
	void StopQueries();
	FDelegateHandle TearDownHandle;
	bool bAcceptingQueries = false;
	FGuid WorldEpoch;
	TArray<TSharedPtr<FProjectJTargetScoringJob>> Jobs;
};
