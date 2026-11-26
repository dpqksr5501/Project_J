#include "Combat/Project_JProjectileManagerSubsystem.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Async/ParallelFor.h"
#include "Combat/Project_JCombatQueryTypes.h"
#include "Combat/Project_JMultiPartHitComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
TAutoConsoleVariable<int32> CVarProjectileParallelThreshold(
	TEXT("ProjectJ.Projectile.ParallelThreshold"),
	64,
	TEXT("Minimum active projectile count before ballistic kinematics use ParallelFor across worker threads."));

TAutoConsoleVariable<int32> CVarProjectileUseAsyncSweep(
	TEXT("ProjectJ.Projectile.UseAsyncSweep"),
	0,
	TEXT("Opt-in Chaos AsyncSweepByChannel pipeline for massive projectile storms (>= AsyncSweepThreshold). 0: deterministic same-tick sweep (default)."));

TAutoConsoleVariable<int32> CVarProjectileAsyncSweepThreshold(
	TEXT("ProjectJ.Projectile.AsyncSweepThreshold"),
	128,
	TEXT("Minimum active projectile count before opt-in AsyncSweepByChannel engages."));
}

void UProject_JProjectileManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ActiveProjectiles.Reserve(128);
	ReusableSteps.Reserve(128);
	ReusableQueryParams = FCollisionQueryParams(SCENE_QUERY_STAT(ProjectJ_PooledProjectile), false);
}

void UProject_JProjectileManagerSubsystem::Deinitialize()
{
	ActiveProjectiles.Reset();
	ReusableSteps.Reset();
	ReusablePendingHits.Reset();
	MultiHitCooldowns.Reset();
	Super::Deinitialize();
}

TStatId UProject_JProjectileManagerSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UProject_JProjectileManagerSubsystem, STATGROUP_Tickables);
}

uint64 UProject_JProjectileManagerSubsystem::SpawnProjectile(
	AActor* Instigator,
	const FVector& Origin,
	const FVector& Velocity,
	float CollisionRadius,
	float MaxLifeSeconds,
	int32 MaxPierces,
	float GravityScale,
	FProjectJOnProjectileHit OnHit)
{
	check(IsInGameThread());
	const UWorld* World = GetWorld();
	if (!IsInitialized() || !World || World->bIsTearingDown || World->GetNetMode() == NM_Client
		|| (Instigator && (!IsValid(Instigator) || Instigator->IsActorBeingDestroyed() || Instigator->GetWorld() != World))
		|| ActiveProjectiles.Num() >= MaxProjectiles || Origin.ContainsNaN() || Velocity.ContainsNaN()
		|| Velocity.IsNearlyZero() || !FMath::IsFinite(CollisionRadius) || CollisionRadius <= 0.0f
		|| !FMath::IsFinite(MaxLifeSeconds) || MaxLifeSeconds <= 0.0f || !FMath::IsFinite(GravityScale))
	{
		return 0;
	}

	FProject_JActiveProjectile Proj;
	Proj.Id = ++NextProjectileId;
	Proj.Instigator = Instigator;
	Proj.Location = Origin;
	Proj.Velocity = Velocity;
	Proj.CollisionRadius = CollisionRadius;
	Proj.GravityScale = GravityScale;
	Proj.RemainingLifeSeconds = FMath::Min(MaxLifeSeconds, 30.0f);
	Proj.RemainingPierces = FMath::Max(0, MaxPierces);
	Proj.OnHit = MoveTemp(OnHit);
	ActiveProjectiles.Add(MoveTemp(Proj));
	return Proj.Id;
}

int64 UProject_JProjectileManagerSubsystem::SpawnCombatProjectile(
	AActor* Instigator,
	const FVector& Origin,
	const FVector& Velocity,
	float CollisionRadius,
	float MaxLifeSeconds,
	int32 MaxPierces,
	float GravityScale,
	TSubclassOf<UGameplayEffect> DamageEffect)
{
	if (!IsValid(Instigator) || !Instigator->HasAuthority() || !DamageEffect)
	{
		return 0;
	}
	TWeakObjectPtr<AActor> WeakInstigator(Instigator);
	TSubclassOf<UGameplayEffect> EffectClass = DamageEffect;
	const uint64 Id = SpawnProjectile(
		Instigator, Origin, Velocity, CollisionRadius, MaxLifeSeconds, MaxPierces, GravityScale,
		FProjectJOnProjectileHit::CreateLambda([WeakInstigator, EffectClass](uint64, AActor* HitActor, const FHitResult& Hit)
		{
			AActor* Source = WeakInstigator.Get();
			if (!IsValid(Source) || !Source->HasAuthority() || !IsValid(HitActor) || !EffectClass)
			{
				return;
			}
			UAbilitySystemComponent* SourceASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Source);
			UAbilitySystemComponent* TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(HitActor);
			if (!SourceASC || !TargetASC || !Project_J::Combat::CanAffectCombatTarget(Source, HitActor, TargetASC))
			{
				return;
			}
			float PartLevel = 1.0f;
			if (const auto* MultiPart = HitActor->FindComponentByClass<UProject_JMultiPartHitComponent>())
			{
				FName PartName;
				float Mult = 1.0f;
				FVector Center;
				const double Now = HitActor->GetWorld() ? HitActor->GetWorld()->GetTimeSeconds() : 0.0;
				if (MultiPart->VerifyRewoundSegmentHit(Now, Hit.TraceStart, Hit.ImpactPoint, 25.0f, PartName, Mult, Center))
				{
					PartLevel = FMath::Max(0.1f, Mult);
				}
			}
			FGameplayEffectContextHandle Ctx = SourceASC->MakeEffectContext();
			Ctx.AddHitResult(Hit);
			Ctx.AddInstigator(Source, Source);
			const FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(EffectClass, PartLevel, Ctx);
			if (Spec.IsValid())
			{
				SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
			}
		}));
	return static_cast<int64>(Id);
}

bool UProject_JProjectileManagerSubsystem::CancelProjectile(uint64 ProjectileId)
{
	check(IsInGameThread());
	const int32 Index = ActiveProjectiles.IndexOfByPredicate([ProjectileId](const FProject_JActiveProjectile& P) { return P.Id == ProjectileId; });
	if (Index != INDEX_NONE)
	{
		ActiveProjectiles.RemoveAtSwap(Index);
		return true;
	}
	return false;
}

void UProject_JProjectileManagerSubsystem::Tick(float DeltaSeconds)
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_ProjectileManager_Tick);
	UWorld* World = GetWorld();
	if (bTicking || !IsInitialized() || !World || World->bIsTearingDown || World->GetNetMode() == NM_Client
		|| !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f)
	{
		return;
	}
	TGuardValue<bool> TickGuard(bTicking, true);

	if ((++TickCounter % 60u) == 0u && !MultiHitCooldowns.IsEmpty())
	{
		PruneStaleMultiHitCooldowns();
	}

	const int32 Count = ActiveProjectiles.Num();
	if (Count == 0)
	{
		return;
	}

	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	const float GravityZ = World->GetGravityZ();
	ReusablePendingHits.Reset();
	ReusableSteps.SetNumUninitialized(Count, EAllowShrinking::No);

	// Phase 1: Pure POD ballistic kinematics — parallelized via ParallelFor during massive ranged/AoE storms.
	const int32 ParallelThreshold = FMath::Max(16, CVarProjectileParallelThreshold.GetValueOnGameThread());
	auto IntegrateOne = [&](int32 Index)
	{
		FProject_JActiveProjectile& Proj = ActiveProjectiles[Index];
		FProjectileStep& Step = ReusableSteps[Index];
		Proj.RemainingLifeSeconds -= Dt;
		if (Proj.RemainingLifeSeconds <= 0.0f)
		{
			Step.bExpired = true;
			return;
		}
		Step.bExpired = false;
		Proj.Velocity.Z += GravityZ * Proj.GravityScale * Dt;
		Step.Displacement = Proj.Velocity * Dt;
	};

	if (Count >= ParallelThreshold)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_ProjectileManager_ParallelKinematics);
		ParallelFor(Count, IntegrateOne);
	}
	else
	{
		for (int32 i = 0; i < Count; ++i)
		{
			IntegrateOne(i);
		}
	}

	const bool bUseAsyncSweep = CVarProjectileUseAsyncSweep.GetValueOnGameThread() != 0
		&& Count >= FMath::Max(1, CVarProjectileAsyncSweepThreshold.GetValueOnGameThread());

	// Resolve the previous segment before issuing this frame's segment. Keep the
	// projectile at the unresolved start until the engine result is consumed.
	for (int32 i = Count - 1; i >= 0; --i)
	{
		FProject_JActiveProjectile& Proj = ActiveProjectiles[i];
		if (Proj.Instigator.IsStale())
		{
			ActiveProjectiles.RemoveAtSwap(i, EAllowShrinking::No);
			continue;
		}

		if (Proj.PendingAsyncTraceHandle)
		{
			const FTraceHandle Pending = Proj.PendingAsyncTraceHandle.GetValue();
			Proj.PendingAsyncTraceHandle.Reset();
			if (!ResolveSweep(*World, Proj, Proj.PendingAsyncEndLocation, &Pending))
			{
				ActiveProjectiles.RemoveAtSwap(i, EAllowShrinking::No);
				continue;
			}
		}

		const FProjectileStep& Step = ReusableSteps[i];
		if (Step.bExpired)
		{
			ActiveProjectiles.RemoveAtSwap(i, EAllowShrinking::No);
			continue;
		}

		const FVector NextLocation = Proj.Location + Step.Displacement;
		if (bUseAsyncSweep)
		{
			PrepareSweep(Proj);
			Proj.PendingAsyncEndLocation = NextLocation;
			Proj.PendingAsyncTraceHandle = World->AsyncSweepByChannel(
				EAsyncTraceType::Single, Proj.Location, NextLocation, FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeSphere(Proj.CollisionRadius), ReusableQueryParams);
		}
		else if (!ResolveSweep(*World, Proj, NextLocation, nullptr))
		{
			ActiveProjectiles.RemoveAtSwap(i, EAllowShrinking::No);
		}
	}

	// Callbacks can spawn, cancel, or destroy targets. Resolve all pending state
	// before notifying gameplay code.
	if (ReusablePendingHits.IsEmpty()) return;
	// A hit callback may synchronously tear down the subsystem and clear its member arrays.
	// Keep this delivery batch alive until the current delegate returns.
	auto PendingHits = MoveTemp(ReusablePendingHits);
	for (const FPendingHitCallback& Pending : PendingHits)
	{
		if (!IsInitialized() || World->bIsTearingDown) break;
		Pending.Callback.ExecuteIfBound(Pending.ProjectileId, Pending.HitActor.Get(), Pending.Hit);
	}
	if (IsInitialized())
	{
		PendingHits.Reset();
		ReusablePendingHits = MoveTemp(PendingHits);
	}
}

void UProject_JProjectileManagerSubsystem::PrepareSweep(const FProject_JActiveProjectile& Proj)
{
	ReusableQueryParams.ClearIgnoredSourceObjects();
	if (AActor* Owner = Proj.Instigator.Get())
	{
		ReusableQueryParams.AddIgnoredActor(Owner);
	}
	for (const TWeakObjectPtr<AActor>& Ignored : Proj.AlreadyHitActors)
	{
		if (AActor* Actor = Ignored.Get())
		{
			ReusableQueryParams.AddIgnoredActor(Actor);
		}
	}
}

bool UProject_JProjectileManagerSubsystem::ResolveSweep(
	UWorld& World, FProject_JActiveProjectile& Proj, const FVector& NextLocation, const FTraceHandle* Handle)
{
	FHitResult Hit;
	bool bParamsPrepared = false;
	const auto SweepSync = [&]()
	{
		if (!bParamsPrepared)
		{
			PrepareSweep(Proj);
			bParamsPrepared = true;
		}
		return World.SweepSingleByChannel(Hit, Proj.Location, NextLocation, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeSphere(Proj.CollisionRadius), ReusableQueryParams);
	};

	bool bHit = false;
	FTraceDatum Datum;
	if (Handle && World.QueryTraceData(*Handle, Datum))
	{
		if (!Datum.OutHits.IsEmpty())
		{
			Hit = Datum.OutHits[0];
			bHit = Hit.bBlockingHit;
			// The queried component can disappear between dispatch and delivery.
			if (bHit && !IsValid(Hit.GetComponent()))
			{
				bHit = SweepSync();
			}
		}
	}
	else
	{
		// UE exposes async results for one frame. Resolve the original segment
		// synchronously if it expired or was never queued.
		bHit = SweepSync();
	}

	constexpr int32 MaxFriendlyPassesPerStep = 8;
	for (int32 Passes = 0; bHit && Cast<APawn>(Hit.GetActor()) && Proj.Instigator.IsValid()
		&& !Project_J::Combat::CanAffectCombatTarget(Proj.Instigator.Get(), Hit.GetActor(), nullptr); ++Passes)
	{
		if (Passes == MaxFriendlyPassesPerStep)
		{
			Proj.Location = Hit.Location;
			return true;
		}
		if (bParamsPrepared)
		{
			ReusableQueryParams.AddIgnoredActor(Hit.GetActor());
		}
		Proj.AlreadyHitActors.Add(Hit.GetActor());
		bHit = SweepSync();
	}

	if (!bHit)
	{
		Proj.Location = NextLocation;
		return true;
	}

	AActor* HitActor = Hit.GetActor();
	if (Proj.OnHit.IsBound())
	{
		ReusablePendingHits.Add({Proj.Id, HitActor, Hit, Proj.OnHit});
	}
	if (!Cast<APawn>(HitActor) || Proj.RemainingPierces == 0)
	{
		return false;
	}

	Proj.Location = Hit.Location;
	Proj.AlreadyHitActors.Add(HitActor);
	--Proj.RemainingPierces;
	return true;
}

bool UProject_JProjectileManagerSubsystem::AdmitMultiHitTarget(uint64 ChannelId, AActor* Target, double NowSeconds, float CooldownSeconds)
{
	check(IsInGameThread());
	if (!ChannelId || !IsValid(Target) || !FMath::IsFinite(NowSeconds) || !FMath::IsFinite(CooldownSeconds) || CooldownSeconds < 0.0f)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	if (!IsInitialized() || !World || World->bIsTearingDown || World->GetNetMode() == NM_Client
		|| Target->IsActorBeingDestroyed() || Target->GetWorld() != World)
	{
		return false;
	}
	const double WorldNow = World->GetTimeSeconds();
	if (const auto* Map = MultiHitCooldowns.Find(ChannelId))
	{
		if (const FMultiHitCooldown* Previous = Map->Find(Target))
		{
			if (WorldNow < Previous->WorldExpiry && NowSeconds >= Previous->LastHit && NowSeconds < Previous->EndTime)
			{
				return false;
			}
		}
	}
	if (CooldownSeconds > 0.0f)
	{
		MultiHitCooldowns.FindOrAdd(ChannelId).Add(Target,
			{NowSeconds, NowSeconds + CooldownSeconds, WorldNow + CooldownSeconds});
	}
	return true;
}

void UProject_JProjectileManagerSubsystem::ClearMultiHitChannel(uint64 ChannelId)
{
	check(IsInGameThread());
	MultiHitCooldowns.Remove(ChannelId);
}

int32 UProject_JProjectileManagerSubsystem::PruneStaleMultiHitCooldowns()
{
	check(IsInGameThread());
	const double Now = GetWorld()->GetTimeSeconds();
	int32 Removed = 0;
	for (auto ChanIt = MultiHitCooldowns.CreateIterator(); ChanIt; ++ChanIt)
	{
		for (auto TargetIt = ChanIt.Value().CreateIterator(); TargetIt; ++TargetIt)
		{
			if (!TargetIt.Key().IsValid() || Now >= TargetIt.Value().WorldExpiry)
			{
				TargetIt.RemoveCurrent();
				++Removed;
			}
		}
		if (ChanIt.Value().IsEmpty())
		{
			ChanIt.RemoveCurrent();
		}
	}
	return Removed;
}
