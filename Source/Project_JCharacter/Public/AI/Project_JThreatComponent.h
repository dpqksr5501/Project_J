#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "Project_JThreatComponent.generated.h"

/** Data-driven NPC / Boss Brain configuration (BP05: StateTree + Leash + Multi-Phase Boss). */
UCLASS(BlueprintType)
class PROJECT_JCHARACTER_API UProject_JNPCBrainDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
	FName BrainId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
	bool bIsGiantBoss = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|Senses", meta = (ClampMin = "100.0"))
	float SightRadiusCm = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|Leash", meta = (ClampMin = "500.0"))
	float LeashRadiusCm = 3500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|Leash", meta = (ClampMin = "200.0"))
	float MaxChaseZDeltaCm = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|Leash", meta = (ClampMin = "1.0"))
	float UnreachableEvadeSeconds = 5.0f;

	/** HP% thresholds (descending, e.g. {0.70, 0.35}) triggering Boss Phase 2, Phase 3... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|Boss")
	TArray<float> BossPhaseHealthThresholds = { 0.70f, 0.35f };
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JContributionRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Threat")
	FGuid CharacterId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Threat")
	double DamageDealt = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Threat")
	double EffectiveHealing = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Threat")
	double ParticipationSeconds = 0.0;

	double TotalScore() const { return DamageDealt + EffectiveHealing * 0.75; }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProjectJOnBossPhaseChanged, int32, OldPhase, int32, NewPhase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FProjectJOnEvadeStarted);

/**
 * Unbounded server-authoritative Threat/Aggro, Contribution, Leash/Evade & Boss Phase component (BP05).
 * Enforces 110% melee / 130% ranged aggro hysteresis to prevent ping-ponging, and preserves
 * boss ContributionMap across player deaths so revived participants still earn fair personal loot.
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class PROJECT_JCHARACTER_API UProject_JThreatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	static constexpr double MeleeHysteresisRatio = 1.10;
	static constexpr double RangedHysteresisRatio = 1.30;
	static constexpr double MeleeRangeCm = 450.0;

	UProject_JThreatComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	TObjectPtr<UProject_JNPCBrainDefinition> BrainDefinition;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Leash")
	FVector HomeLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Leash")
	bool bIsEvading = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Boss")
	int32 CurrentBossPhase = 1;

	UPROPERTY(BlueprintAssignable, Category = "AI|Boss")
	FProjectJOnBossPhaseChanged OnBossPhaseChanged;

	UPROPERTY(BlueprintAssignable, Category = "AI|Leash")
	FProjectJOnEvadeStarted OnEvadeStarted;

	/** Adds damage threat (+ records character contribution for field boss personal loot). */
	void AddDamageThreat(AActor* Source, const FGuid& CharacterId, double Damage, double SkillThreatMultiplier = 1.0, double NowSeconds = 0.0);

	/** Adds healing threat distributed across engaged mobs + contribution score. */
	void AddHealingThreat(AActor* Healer, const FGuid& CharacterId, double EffectiveHeal, int32 EngagedMobCount = 1, double NowSeconds = 0.0);

	/** Taunts this NPC: raises threat to 110% of current top and forces target for DurationSeconds. */
	void ApplyTaunt(AActor* Taunter, double DurationSeconds, double NowSeconds);

	/** Removes an actor from active aggro (death/stealth/out-of-range) while preserving boss ContributionMap. */
	void RemoveFromActiveAggro(AActor* Actor);

	/** Re-evaluates authoritative target using 110% melee / 130% ranged hysteresis. */
	AActor* ResolvePrimaryTarget(const FVector& NPCLocation, double NowSeconds);

	/** Evaluates horizontal leash, vertical Z delta, and nav-unreachable duration; enters Evade if violated. */
	bool EvaluateLeashAndEvade(const FVector& CurrentLocation, float UnreachableSeconds, bool bHasValidTarget);

	/** Completes evade return at HomeLocation (clears invulnerable evade state and resets boss phase). */
	void FinishEvadeReset();

	/** Updates boss phase from current Health/MaxHealth ratio (monotonic descending). */
	int32 EvaluateBossPhase(float CurrentHealth, float MaxHealth);

	/** Returns all character IDs meeting the boss contribution threshold (e.g. >= 0.1% of BossMaxHealth). */
	TArray<FGuid> GetEligibleContributors(double BossMaxHealth, double MinFraction = 0.001) const;

	int32 GetActiveThreatCount() const { return ActiveThreat.Num(); }
	double GetThreatValue(const AActor* Actor) const;

private:
	TMap<TWeakObjectPtr<AActor>, double> ActiveThreat;
	TMap<FGuid, FProject_JContributionRecord> ContributionMap;
	TWeakObjectPtr<AActor> CurrentTarget;
	TWeakObjectPtr<AActor> ForcedTauntTarget;
	double ForcedTauntUntil = -1000.0;
};
