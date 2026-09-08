#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "NavigationData.h"
#include "Project_JNPCPathSubsystem.generated.h"

class APawn;
struct FProjectJNPCPathEntry;

enum class EProjectJNPCPathStatus : uint8 { Success, Failed, Expired };
struct FProjectJNPCPathCompletion
{
	uint64 Token = 0;
	uint64 IntentRevision = 0;
	EProjectJNPCPathStatus Status = EProjectJNPCPathStatus::Failed;
	FVector Start = FVector::ZeroVector;
	FVector Goal = FVector::ZeroVector;
	FNavPathSharedPtr Path;
};
struct FProjectJNPCPathStats
{
	uint64 Accepted = 0, Rejected = 0, Dispatched = 0, Cancelled = 0, Expired = 0, Delivered = 0, Discarded = 0;
	int32 LastTickDispatches = 0, LastTickDeliveries = 0;
};

/** GT admission/delivery around engine-owned async navigation. No custom worker accesses UObjects. */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JNPCPathSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	static constexpr int32 MaxRequests = 64;
	static constexpr int32 MaxDispatchesPerTick = 4;
	static constexpr int32 MaxDeliveriesPerTick = 8;
	static constexpr double RequestLifetimeSeconds = 2.0;
	/** One outstanding request per owner. Rejected submissions return zero, without a callback. */
	uint64 Submit(UObject* Owner, APawn* Pawn, const FVector& Goal, uint64 IntentRevision,
		TFunction<void(const FProjectJNPCPathCompletion&)> Completion);
	/** No callback after cancellation. Dispatched slots remain reserved until engine completion. */
	void Cancel(uint64 Token);
	int32 GetRequestCount() const { return Requests.Num(); }
	const FProjectJNPCPathStats& GetStats() const { return Stats; }
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
	friend class FProjectJNPCPathLateCompletionTest;
	/** Deterministic engine-boundary simulation; does not execute navigation or create a worker. */
	void SimulateDispatchForTest(uint64 Token, bool bExpired);
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
};
