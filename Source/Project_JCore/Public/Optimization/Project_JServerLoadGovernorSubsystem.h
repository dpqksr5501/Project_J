#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Project_JServerLoadGovernorSubsystem.generated.h"

UENUM(BlueprintType)
enum class EProject_JServerLoadTier : uint8
{
	Green = 0,
	Yellow = 1,
	Red = 2
};

USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JServerGovernorSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Server|Governor")
	EProject_JServerLoadTier Tier = EProject_JServerLoadTier::Green;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Server|Governor")
	double LastTickMs = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Server|Governor")
	double EwmaTickMs = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Server|Governor")
	double TickP50Ms = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Server|Governor")
	double TickP95Ms = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Server|Governor")
	double TickP99Ms = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Server|Governor")
	int64 OverBudgetTicksTotal = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Server|Governor")
	int32 ActiveCCU = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Server|Governor")
	int64 TierTransitionsTotal = 0;
};

/** Cumulative observations for Prometheus aggregation across server instances. */
struct PROJECT_JCORE_API FProject_JTickIntervalHistogram
{
	static constexpr double BoundsSeconds[] = {0.010, 0.020, 0.030, 0.033333, 0.035, 0.050, 0.075, 0.100, 0.250, 1.000};
	static constexpr int32 BucketCount = static_cast<int32>(UE_ARRAY_COUNT(BoundsSeconds));
	int64 BucketCounts[BucketCount]{};
	int64 Count = 0;
	double SumSeconds = 0.0;
};

/**
 * Adaptive 3-tier hysteresis server load governor (AAA MMO survival valve).
 * Monitors authoritative server tick latency against the 30Hz (33.33ms) budget and
 * smoothly scales down non-critical replication/AI/cosmetic work under load while
 * protecting combat, movement, bosses, and durable outbox commits.
 * Lives in Project_JCore so all gameplay, network, and telemetry modules can query it
 * without violating the 8-module DAG.
 */
UCLASS(Config = Engine, DefaultConfig)
class PROJECT_JCORE_API UProject_JServerLoadGovernorSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr double TargetBudgetMs = 33.333;
	static constexpr int32 SampleWindowSize = 128;
	static constexpr int32 MinCrowdCapFloor = 16;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Records one authoritative world tick sample (in ms) and updates the hysteresis state machine. */
	void RecordTickSample(double TickDurationMs, int32 CurrentCCU, double NowSeconds, double TickIntervalSeconds = -1.0);

	/** Forces a specific tier for testing/operator drills (-1 clears forced override). */
	void SetForcedTierOverride(int32 ForcedTierIndex);
	int32 GetForcedTierOverride() const { return ForcedTierOverride; }

	EProject_JServerLoadTier GetCurrentTier() const;
	FProject_JServerGovernorSnapshot GetSnapshot() const;
	const FProject_JTickIntervalHistogram& GetTickIntervalHistogram() const { return TickIntervalHistogram; }

	/** Scales a base remote-crowd replication cap according to current load tier. */
	int32 GetEffectiveCrowdCap(int32 BaseCap) const;

	/** Multiplier for distant/non-combat NPC decision intervals (1.0x Green, 1.5x Yellow, 2.5x Red). */
	float GetNpcDecisionCadenceScale() const;

	/** True in Yellow/Red tiers to shed optional non-critical cosmetic broadcasts. */
	bool ShouldThrottleNonCriticalCosmetics() const;

	/** Max nearby non-party/non-owner recipients for cosmetic hit feedback under load. */
	int32 GetCombatFeedbackMaxNearbyRecipients() const;

	UPROPERTY(Config)
	double YellowEnterMs = 20.0;

	UPROPERTY(Config)
	double YellowExitMs = 15.0;

	UPROPERTY(Config)
	double RedEnterMs = 26.0;

	UPROPERTY(Config)
	double RedExitMs = 22.0;

	UPROPERTY(Config)
	double EscalateHoldSeconds = 2.0;

	UPROPERTY(Config)
	double RecoverHoldSeconds = 8.0;

private:
	void OnWorldTickStart(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void OnWorldTickEnd(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void RecomputePercentiles();
	void EvaluateTierTransitions(double NowSeconds);

	FDelegateHandle TickStartHandle;
	FDelegateHandle TickEndHandle;
	TWeakObjectPtr<UWorld> SampledWorld;
	TWeakObjectPtr<UWorld> TickIntervalWorld;
	double TickStartedAt = 0.;
	double LastWorldTickStartSeconds = 0.0;
	double SampledTickIntervalSeconds = -1.0;
	FProject_JTickIntervalHistogram TickIntervalHistogram;

	TArray<double> TickRing;
	int32 TickRingHead = 0;
	int32 TickRingCount = 0;

	EProject_JServerLoadTier CurrentTier = EProject_JServerLoadTier::Green;
	int32 ForcedTierOverride = -1;
	double EscalateCandidateSince = -1.0;
	double RecoverCandidateSince = -1.0;

	double LastTickMs = 0.0;
	double EwmaTickMs = 0.0;
	double CachedP50Ms = 0.0;
	double CachedP95Ms = 0.0;
	double CachedP99Ms = 0.0;
	int64 OverBudgetTicksTotal = 0;
	int64 TierTransitionsTotal = 0;
	int32 LastCCU = 0;
};
