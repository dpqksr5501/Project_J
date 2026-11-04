#pragma once

#include "CoreMinimal.h"
#include "Iris/ReplicationSystem/Filtering/NetObjectGridFilter.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Project_JPlayerStateAudienceFilter.generated.h"

class AActor;
class AProject_JPlayerState;
class APawn;
class UEngineReplicationBridge;
class UWorld;

/** Actual runtime assignment diagnostics, not an actor-side relevancy prediction. */
struct FProjectJPlayerStateAudienceBinding
{
	bool bAssigned = false;
	bool bSpatial = false;
	uint32 FallbackReasons = 0;
	uint32 ObjectIndex = 0;
	uint32 OwnerConnectionId = 0;
	uint32 PawnIndex = 0;
	uint32 AvatarIndex = 0;
	FVector Center = FVector::ZeroVector;
	float Radius = 0;
};

struct FProjectJPlayerStateAudienceStats
{
	uint64 PreFilterPasses = 0;
	uint64 Assignments = 0;
	uint64 RemovedBindings = 0;
	uint64 RejectedWitnesses = 0;
	uint64 ObservedPawnCreations = 0;
	uint64 ConsumedPawnCreations = 0;
	uint32 SpatialBindings = 0;
	uint32 FallbackBindings = 0;
	uint32 PendingWitnesses = 0;
};

/**
 * Opt-in experiment: supplies conservative Avatar bounds to the existing Iris grid.
 * Unknown history and retired Avatar references keep the previous broad audience.
 * Production enablement requires the separate replacement/mount/loss cohort.
 */
UCLASS(Transient)
class UProject_JPlayerStateAudienceFilter : public UNetObjectGridWorldLocFilter
{
	GENERATED_BODY()
public:
	static bool TryBind(AProject_JPlayerState* State);
	static UProject_JPlayerStateAudienceFilter* Find(UWorld* World);
	bool QueryBinding(const AProject_JPlayerState* State, FProjectJPlayerStateAudienceBinding& Out) const;
	const FProjectJPlayerStateAudienceStats& GetStats() const { return Stats; }
	static FString DescribeFallback(uint32 Reasons);
	/** Includes float squared-radius rounding, which Iris uses for exact distance tests. */
	static bool BuildConservativeBound(const FVector& PawnPosition, float PawnRadius,
		const FVector& AvatarPosition, float AvatarRadius, float MaxRadius, FVector& OutCenter, float& OutRadius);

protected:
	virtual void OnInit(const FNetObjectFilterInitParams& Params) override;
	virtual void OnDeinit() override;
	virtual bool BuildObjectInfo(uint32 ObjectIndex, FNetObjectFilterAddObjectParams& Params) override;
	virtual void UpdateObjectInfo(FPerObjectInfo& PerObjectInfo, const FObjectLocationInfo& Info,
		const UE::Net::FReplicationInstanceProtocol* InstanceProtocol) override;
	virtual void OnObjectRemoved(uint32 ObjectIndex) override;
	virtual void RemoveConnection(uint32 ConnectionId) override;
	virtual void PreFilter(FNetObjectPreFilteringParams& Params) override;
	virtual void Filter(FNetObjectFilteringParams& Params) override;

private:
	enum EFallback : uint32
	{
		Unbound = 1u << 0, MissingPawn = 1u << 1, MissingOwner = 1u << 2,
		UnknownHistory = 1u << 3, RetiredSource = 1u << 4, UnsupportedPolicy = 1u << 5,
		InvalidBounds = 1u << 6, StaleLease = 1u << 7
	};
	struct FEntry
	{
		TWeakObjectPtr<AProject_JPlayerState> State;
		uint64 AssignmentSerial = 0;
		TWeakObjectPtr<APawn> Pawn;
		TWeakObjectPtr<APawn> Avatar;
		UE::Net::FNetRefHandle StateHandle, PawnHandle, AvatarHandle;
		FProjectJPlayerStateAudienceBinding View;
		uint32 LatchedReasons = 0;
		bool bEstablished = false;
	};
	struct FPolicyState
	{
		FVector Min, Max;
		float MaxCull = 0, CellX = 0, CellY = 0;
		uint32 ViewFrames = 0;
		uint16 CullFrames = 0;
		uint8 HysteresisFrames = 0, HysteresisStride = 0;
		bool bExact = false, bHysteresis = false;
		bool operator==(const FPolicyState& Other) const;
	};
	static FPolicyState ReadPolicy(const UNetObjectGridFilterConfig* GridPolicy = nullptr);
	static bool Enabled();
	static UProject_JPlayerStateAudienceFilter* GetOrCreate(UReplicationSystem* System);
	bool PolicyMatches(UEngineReplicationBridge* Bridge) const;
	bool PrivateBootstrap(const APawn* Pawn, UEngineReplicationBridge* Bridge) const;
	bool ReadSource(APawn* Pawn, UEngineReplicationBridge* Bridge, UE::Net::FNetRefHandle& OutHandle,
		uint32& OutIndex, FVector& OutPosition, float& OutRadius) const;
	void RefreshEntry(uint32 ObjectIndex, FEntry& Entry);
	void ObservePawnPreSpawn(AActor* Actor);
	TWeakObjectPtr<UReplicationSystem> System;
	TMap<uint32, FEntry> Entries;
	// The assignment fence proves the exact PS lease existed before this real spawn.
	TMap<TWeakObjectPtr<APawn>, uint64> PawnCreations;
	TWeakObjectPtr<UWorld> WitnessWorld;
	FDelegateHandle PreSpawnHandle;
	TMap<uint32, TArray<uint32>> OwnerObjects;
	TArray<uint32> FallbackObjects;
	FProjectJPlayerStateAudienceStats Stats;
	uint32 ObjectCapacity = 0;
	bool bCurrentPolicyMatches = false;
	TWeakObjectPtr<UNetObjectGridFilterConfig> RuntimeGridPolicy;
	FPolicyState InitialPolicy;
};
