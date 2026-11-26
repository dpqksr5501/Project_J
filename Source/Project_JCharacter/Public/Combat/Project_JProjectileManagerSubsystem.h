#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
#include "Combat/Project_JCombatQueryTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldCollision.h"
#include "Project_JProjectileManagerSubsystem.generated.h"

class UGameplayEffect;

DECLARE_DELEGATE_ThreeParams(FProjectJOnProjectileHit, uint64 /*ProjectileId*/, AActor* /*HitActor*/, const FHitResult& /*Hit*/);

/** Actor-less pooled projectile record (zero AActor spawn/GC overhead in 200-player fights). */
struct FProject_JActiveProjectile
{
	uint64 Id = 0;
	TWeakObjectPtr<AActor> Instigator;
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	float CollisionRadius = 18.0f;
	float GravityScale = 0.0f;
	float RemainingLifeSeconds = 3.0f;
	int32 RemainingPierces = 0;
	TSet<TWeakObjectPtr<AActor>> AlreadyHitActors;
	FProjectJOnProjectileHit OnHit;
	TOptional<FTraceHandle> PendingAsyncTraceHandle;
	FVector PendingAsyncEndLocation = FVector::ZeroVector;
};

/**
 * Server-authoritative actor-less projectile pool & multi-hit cooldown manager (BP04 / A09).
 * Bounded to MaxProjectiles (2048) to prevent memory/CPU spikes in large battles.
 */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JProjectileManagerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxProjectiles = 2048;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsTickable() const override { return Super::IsTickable() && (!ActiveProjectiles.IsEmpty() || !MultiHitCooldowns.IsEmpty()); }
	virtual TStatId GetStatId() const override;

	/** Spawns a pooled actor-less projectile; returns 0 if capacity reached or inputs invalid. */
	uint64 SpawnProjectile(
		AActor* Instigator,
		const FVector& Origin,
		const FVector& Velocity,
		float CollisionRadius,
		float MaxLifeSeconds,
		int32 MaxPierces,
		float GravityScale,
		FProjectJOnProjectileHit OnHit);

	/**
	 * Blueprint/Ability one-call helper: spawns a pooled actor-less projectile that automatically applies
	 * DamageEffect via GAS on server hit (and evaluates MultiPartBoss hit scaling when present).
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Combat|Projectile")
	int64 SpawnCombatProjectile(
		AActor* Instigator,
		const FVector& Origin,
		const FVector& Velocity,
		float CollisionRadius,
		float MaxLifeSeconds,
		int32 MaxPierces,
		float GravityScale,
		TSubclassOf<UGameplayEffect> DamageEffect);

	bool CancelProjectile(uint64 ProjectileId);
	int32 GetActiveProjectileCount() const { return ActiveProjectiles.Num(); }

	/** A successful hit sets this target's cooldown duration for the channel. */
	bool AdmitMultiHitTarget(uint64 ChannelId, AActor* Target, double NowSeconds, float CooldownSeconds);
	void ClearMultiHitChannel(uint64 ChannelId);
	/** Removes expired or dead targets and empty cooldown channels. */
	int32 PruneStaleMultiHitCooldowns();

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override { return Type == EWorldType::Game || Type == EWorldType::PIE; }

private:
	struct FProjectileStep
	{
		FVector Displacement = FVector::ZeroVector;
		bool bExpired = false;
	};
	struct FMultiHitCooldown
	{
		double LastHit = 0.0;
		double EndTime = 0.0;
		double WorldExpiry = 0.0;
	};
	struct FPendingHitCallback
	{
		uint64 ProjectileId = 0;
		TWeakObjectPtr<AActor> HitActor;
		FHitResult Hit;
		FProjectJOnProjectileHit Callback;
	};
	TArray<FProject_JActiveProjectile> ActiveProjectiles;
	TArray<FProjectileStep> ReusableSteps;
	TArray<FPendingHitCallback> ReusablePendingHits;
	FCollisionQueryParams ReusableQueryParams;
	void PrepareSweep(const FProject_JActiveProjectile& Projectile);
	bool ResolveSweep(UWorld& World, FProject_JActiveProjectile& Projectile, const FVector& End, const FTraceHandle* Handle);
	TMap<uint64, TMap<TWeakObjectPtr<AActor>, FMultiHitCooldown>> MultiHitCooldowns;
	uint64 NextProjectileId = 0;
	uint32 TickCounter = 0;
	bool bTicking = false;
};
