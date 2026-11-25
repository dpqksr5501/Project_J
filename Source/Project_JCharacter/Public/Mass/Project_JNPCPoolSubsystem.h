#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Project_JNPCPoolSubsystem.generated.h"

class AProject_JNPCCharacter;

/** 3-Tier Mass <-> Pooled Actor representation tier (BP04 / A03; NPCs stay strictly non-MM). */
UENUM(BlueprintType)
enum class EProject_JNPCCrowdTier : uint8
{
	/** < 22m or in active combat: full pooled NPC actor (Mover/GAS/StateTree/SkeletalMesh). */
	Tier0_NearFullActor,
	/** 22–60m non-combat: pooled lightweight actor with low-rate URO SkeletalMesh. */
	Tier1_MidLightweightActor,
	/** 60–150m non-combat: Mass entity only + ISM/VAT proxy (no live AActor in pool). */
	Tier2_FarMassVAT
};

USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JMassVATProxyDescriptor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Mass|Crowd")
	uint64 AgentId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass|Crowd")
	FName ArchetypeId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass|Crowd")
	FTransform WorldTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass|Crowd")
	uint8 VATAnimClipIndex = 0; // 0=Idle, 1=Walk, 2=Run
};

/**
 * Pooled NPC lifecycle & 3-tier crowd LOD coordinator (BP04 / A03).
 * Eliminates repeated SpawnActor/DestroyActor GC spikes and enforces combat demotion protection.
 */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JNPCPoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static constexpr double NearPromoteCm = 2200.0;
	static constexpr double NearDemoteCm = 2800.0;
	static constexpr double MidPromoteCm = 6000.0;
	static constexpr double MidDemoteCm = 7000.0;
	static constexpr int32 MaxPooledPerClass = 128;

	virtual void Deinitialize() override;

	/** Acquires a dormant pooled NPC actor or spawns a fresh one if pool is empty. */
	AProject_JNPCCharacter* AcquireNPC(TSubclassOf<AProject_JNPCCharacter> NPCClass, const FTransform& SpawnTransform);

	/** Releases a non-combat NPC back to the pool (disabled collision/tick/visibility, zero GC churn). */
	bool ReleaseNPCToPool(AProject_JNPCCharacter* NPC);

	/** Computes target crowd tier with distance hysteresis; active combat/bosses never demote below Tier 0. */
	static EProject_JNPCCrowdTier EvaluateTargetTier(
		EProject_JNPCCrowdTier CurrentTier,
		double NearestObserverDistanceCm,
		bool bInCombatOrBoss);

	/** Registers/updates a Tier-2 Far Mass VAT proxy instance for ISM rendering. */
	void UpsertVATProxy(const FProject_JMassVATProxyDescriptor& Proxy);
	bool RemoveVATProxy(uint64 AgentId);

	/** Batch-syncs all active Tier-2 VAT proxies matching ArchetypeFilter into a single ISM component. */
	int32 SyncVATProxiesToISM(class UInstancedStaticMeshComponent* TargetISM, FName ArchetypeFilter = NAME_None) const;

	int32 GetPooledCount(TSubclassOf<AProject_JNPCCharacter> NPCClass) const;
	int32 GetActiveVATProxyCount() const { return VATProxies.Num(); }

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override { return Type == EWorldType::Game || Type == EWorldType::PIE; }

private:
	TMap<TObjectKey<UClass>, TArray<TWeakObjectPtr<AProject_JNPCCharacter>>> FreePools;
	TMap<uint64, FProject_JMassVATProxyDescriptor> VATProxies;
};
