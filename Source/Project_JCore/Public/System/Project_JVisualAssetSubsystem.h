#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Project_JVisualAssetSubsystem.generated.h"

struct FProjectJVisualAssetGroup;
struct FProjectJVisualAssetLease;
struct FProjectJVisualAssetStats
{
	uint64 Accepted = 0, Rejected = 0, Loads = 0, Delivered = 0, Failed = 0;
	int32 LastTickApplications = 0;
	double LastTickMilliseconds = 0;
};

/** Shared world-local visual loading. GT callbacks consume a bounded apply budget; a token pins the asset until Release. */
UCLASS()
class PROJECT_JCORE_API UProject_JVisualAssetSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	static constexpr int32 MaxGroups = 256;
	static constexpr int32 MaxLeases = 2048;
	static constexpr int32 MaxLoadsInFlight = 16;
	static constexpr int32 MaxStartsPerTick = 4;
	static constexpr int32 MaxApplicationsPerTick = 8;
	static constexpr int32 MaxLeaseVisitsPerTick = 128;
	static constexpr double ApplyBudgetMilliseconds = 1.0;
	/** Zero means rejected. Accepted callbacks run once on GT, with null on failure.
	 * Preload uses an empty callback and retains its token until its consumer no longer needs the asset. */
	uint64 Request(UObject* Owner, FSoftObjectPath Path, TFunction<void(UObject*)> Apply = {});
	void Release(uint64 Token);
	int32 GetLeaseCount() const { return Leases.Num(); }
	int32 GetGroupCount() const { return Groups.Num(); }
	const FProjectJVisualAssetStats& GetStats() const { return Stats; }
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldEndPlay(UWorld& World) override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
protected:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
private:
	void Stop();
	void OnTearDown(UWorld* World);
	TArray<TSharedPtr<FProjectJVisualAssetGroup>> Groups;
	TArray<TSharedPtr<FProjectJVisualAssetLease>> Leases;
	FDelegateHandle TearDownHandle;
	FProjectJVisualAssetStats Stats;
	uint64 NextToken = 0;
	int32 Cursor = 0;
	bool bAccepting = false;
};
