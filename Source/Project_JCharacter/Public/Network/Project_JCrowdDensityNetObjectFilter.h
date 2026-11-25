#pragma once

#include "CoreMinimal.h"
#include "Iris/ReplicationSystem/Filtering/NetObjectFilter.h"
#include "Project_JCrowdDensityNetObjectFilter.generated.h"

class AActor;
class UWorld;
class UNetConnection;
class UObjectReplicationBridge;
class UReplicationSystem;

/** Pure candidate representation for deterministic crowd-density culling (shared by Iris and unit tests). */
struct PROJECT_JCHARACTER_API FProject_JCrowdDensityCandidate
{
	uint32 ObjectId = 0;
	FVector Location = FVector::ZeroVector;
	double MaxCullDistanceSq = 12000.0 * 12000.0;
	float CombatWeight = 1.0f;
	bool bAlwaysExempt = false; // Owner, Boss, Party, Guild, Target
};

struct PROJECT_JCHARACTER_API FProject_JCrowdDensityStats
{
	int32 TrackedObjects = 0;
	int32 EffectiveCap = 60;
	int64 LastPassAllowedPairs = 0;
	int64 LastPassCulledPairs = 0;
};

/**
 * Pure Top-K crowd-density budget solver with hysteresis retention.
 * Prevents O(N^2) replication blowup when 200-500 players gather around a field boss or town plaza
 * while guaranteeing zero culling of Bosses, Party/Guild members, Owners, and active Targets.
 */
struct PROJECT_JCHARACTER_API FProject_JCrowdDensityBudgetSolver
{
	static void EvaluateForViewer(
		const FVector& ViewerLocation,
		TConstArrayView<FProject_JCrowdDensityCandidate> Candidates,
		const TSet<uint32>& PreviouslyAllowed,
		int32 MaxNonExemptAllowed,
		float HysteresisBonus,
		TSet<uint32>& OutAllowed,
		int32& OutCulledCount);
};

UCLASS(Transient, Config = Engine)
class PROJECT_JCHARACTER_API UProject_JCrowdDensityNetObjectFilterConfig : public UNetObjectFilterConfig
{
	GENERATED_BODY()

public:
	UPROPERTY(Config)
	int32 DefaultMaxVisibleRemoteActors = 60;

	UPROPERTY(Config)
	float DefaultMaxDistanceCm = 12000.0f;

	UPROPERTY(Config)
	float HysteresisRetentionBonus = 1.35f;
};

/**
 * Production UE 5.8 Iris UNetObjectFilter implementing spatial distance culling +
 * per-connection dynamic Top-K crowd density capping coupled to UProject_JServerLoadGovernorSubsystem.
 */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JCrowdDensityNetObjectFilter : public UNetObjectFilter
{
	GENERATED_BODY()

public:
	static const FName FilterName_CrowdDensity;

	static bool IsCrowdDensityFilterEnabled();
	static int32 GetConfiguredBaseCrowdCap();
	static bool AssignActorToCrowdFilter(AActor* TargetActor);
	static FProject_JCrowdDensityStats GetLatestStats(const UWorld* World);

	void RegisterTrackedActor(UE::Net::FNetRefHandle Handle, AActor* Actor);

protected:
	virtual void OnInit(const FNetObjectFilterInitParams& Params) override;
	virtual void OnDeinit() override;
	virtual void OnMaxInternalNetRefIndexIncreased(UE::Net::FInternalNetRefIndex NewMaxInternalIndex) override;
	virtual void AddConnection(uint32 ConnectionId) override;
	virtual void RemoveConnection(uint32 ConnectionId) override;
	virtual bool AddObject(UE::Net::FInternalNetRefIndex ObjectIndex, FNetObjectFilterAddObjectParams& Params) override;
	virtual void RemoveObject(UE::Net::FInternalNetRefIndex ObjectIndex, const FNetObjectFilteringInfo& Info) override;
	virtual void PreFilter(FNetObjectPreFilteringParams& Params) override;
	virtual void Filter(FNetObjectFilteringParams& Params) override;
	virtual void PostFilter(FNetObjectPostFilteringParams& Params) override;

private:
	struct FPreFilteredActor
	{
		UE::Net::FInternalNetRefIndex ObjectIndex = UE::Net::InvalidInternalNetRefIndex;
		TWeakObjectPtr<AActor> Actor;
		FVector Location = FVector::ZeroVector;
		double MaxCullDistSq = 12000.0 * 12000.0;
		float CombatWeight = 1.0f;
		FName PartyId;
		FName GuildId;
		TWeakObjectPtr<UNetConnection> OwnerConnection;
		bool bIsBoss = false;
		bool bAlwaysRelevant = false;
		bool bOnlyRelevantToOwner = false;
	};

	UPROPERTY(Transient)
	TObjectPtr<UReplicationSystem> ReplicationSystem;

	UPROPERTY(Transient)
	TObjectPtr<UObjectReplicationBridge> Bridge;

	UPROPERTY(Transient)
	TObjectPtr<UProject_JCrowdDensityNetObjectFilterConfig> FilterConfig;

	TMap<UE::Net::FInternalNetRefIndex, TWeakObjectPtr<AActor>> TrackedActors;
	TArray<FPreFilteredActor> FrameActors;
	TArray<TSet<uint32>> PreviousAllowedByConnection;
	TArray<FProject_JCrowdDensityCandidate> ScratchCandidates;
	TSet<uint32> ScratchAllowed;

	FProject_JCrowdDensityStats LatestStats;
	int32 ActiveEffectiveCap = 60;
	int64 PassAllowedPairs = 0;
	int64 PassCulledPairs = 0;
};
