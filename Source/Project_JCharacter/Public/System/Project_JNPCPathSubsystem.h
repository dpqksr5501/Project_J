#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NavigationData.h"
#include "Project_JNPCPathSubsystem.generated.h"

class APawn;
struct FProjectJNPCPathEntry;

enum class EProjectJNPCPathStatus : uint8 { Success, Failed, Expired };
enum class EProjectJNPCPathPriority : uint8 { Normal, Urgent };
struct FProjectJNPCPathCompletion
{
	uint64 Token = 0;
	uint64 IntentRevision = 0;
	EProjectJNPCPathStatus Status = EProjectJNPCPathStatus::Failed;
	FVector Start = FVector::ZeroVector;
	FVector Goal = FVector::ZeroVector;
	FNavPathSharedPtr Path;
	/** Wall latencies; engine latency includes its queue and GT completion dispatch, not worker CPU. */
	double QueueMilliseconds = 0, EngineMilliseconds = 0, DeliveryMilliseconds = 0, TotalMilliseconds = 0;
};
struct FProjectJNPCPathStats
{
	uint64 Accepted = 0, Rejected = 0, Dispatched = 0, Cancelled = 0, Expired = 0, Delivered = 0, Discarded = 0;
	int32 LastTickDispatches = 0, LastTickDeliveries = 0;
	int32 InFlight = 0, PeakInFlight = 0, Queued = 0;
	int32 PeakQueued = 0, Tombstones = 0;
	uint64 Succeeded = 0, Failed = 0, EngineCompleted = 0, StoppedRequests = 0;
	uint64 TickSequence = 0;
	uint64 IgnoredAfterStop = 0;
	double QueueMilliseconds = 0, EngineMilliseconds = 0, DeliveryMilliseconds = 0;
	double LastTickMilliseconds = 0, MaxDeliveryMilliseconds = 0;
};

#if WITH_DEV_AUTOMATION_TESTS
/** Value-only diagnostic record: never retains owner, path or callback. */
struct FProjectJNPCPathLatencySample
{
	uint64 Token = 0;
	uint32 PawnId = 0;
	int32 Phase = 0;
	EProjectJNPCPathStatus Status = EProjectJNPCPathStatus::Failed;
	double QueueMilliseconds = 0, EngineMilliseconds = 0, DeliveryMilliseconds = 0, TotalMilliseconds = 0;
};
#endif

/** GT admission/delivery around engine-owned async navigation. No custom worker accesses UObjects. */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JNPCPathSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	static constexpr int32 MaxRequests = 64;
	static constexpr int32 MaxDispatchesPerTick = 4;
	static constexpr int32 MaxInFlight = 16;
	static constexpr double UrgentBoostSeconds = 0.25;
	static constexpr int32 MaxDeliveriesPerTick = 8;
	static constexpr double RequestLifetimeSeconds = 2.0;
	static constexpr double GameThreadBudgetMilliseconds = 0.5;
	/** One outstanding request per owner. Rejected submissions return zero, without a callback. */
	uint64 Submit(UObject* Owner, APawn* Pawn, const FVector& Goal, uint64 IntentRevision,
		TFunction<void(const FProjectJNPCPathCompletion&)> Completion,
		EProjectJNPCPathPriority Priority = EProjectJNPCPathPriority::Normal);
	/** No callback after cancellation. Dispatched slots remain reserved until engine completion. */
	void Cancel(uint64 Token);
	int32 GetRequestCount() const { return Requests.Num(); }
	const FProjectJNPCPathStats& GetStats() const { return Stats; }
#if WITH_DEV_AUTOMATION_TESTS
	/** Opt-in bounded capture of delivered results, including Action consumers. Phase is captured on Submit. */
	void SetLatencyCapturePhaseForTest(int32 Phase) { check(IsInGameThread()); CapturePhase = Phase; }
	void DrainLatencySamplesForTest(TArray<FProjectJNPCPathLatencySample>& Out) { check(IsInGameThread()); Out = MoveTemp(LatencySamples); LatencySamples.Reset(); }
	uint64 GetDroppedLatencySamplesForTest() const { return DroppedLatencySamples; }
#endif
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
protected:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
private:
#if WITH_DEV_AUTOMATION_TESTS
	int32 CapturePhase = INDEX_NONE;
	TArray<FProjectJNPCPathLatencySample> LatencySamples;
	uint64 DroppedLatencySamples = 0;
	friend class FProjectJNPCPathLateCompletionTest;
	friend class FProjectJNPCPathPressureTest;
	/** Deterministic engine-boundary simulation; does not execute navigation or create a worker. */
	void SimulateDispatchForTest(uint64 Token, bool bExpired);
	void AgeForTest(uint64 Token, double Seconds);
#endif
	void Stop();
	void OnTearDown(UWorld* World);
	void Complete(uint64 Token, uint32 EngineId, ENavigationQueryResult::Type Result, FNavPathSharedPtr Path);
	bool IsActiveServer() const;
	TArray<TSharedPtr<FProjectJNPCPathEntry>> Requests;
	FDelegateHandle TearDownHandle;
	FProjectJNPCPathStats Stats;
	uint64 NextToken = 0;
	bool bAccepting = false;
	bool bTicking = false;
};
