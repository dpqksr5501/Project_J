#include "Mass/Project_JNPCPoolSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "Project_JNPCCharacter.h"

void UProject_JNPCPoolSubsystem::Deinitialize()
{
	FreePools.Reset();
	VATProxies.Reset();
	Super::Deinitialize();
}

AProject_JNPCCharacter* UProject_JNPCPoolSubsystem::AcquireNPC(
	TSubclassOf<AProject_JNPCCharacter> NPCClass,
	const FTransform& SpawnTransform)
{
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown || !NPCClass)
	{
		return nullptr;
	}

	if (auto* Pool = FreePools.Find(TObjectKey<UClass>(NPCClass.Get())))
	{
		while (!Pool->IsEmpty())
		{
			AProject_JNPCCharacter* Reused = Pool->Pop(EAllowShrinking::No).Get();
			if (IsValid(Reused) && !Reused->IsActorBeingDestroyed())
			{
				Reused->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
				Reused->SetActorHiddenInGame(false);
				Reused->SetActorEnableCollision(true);
				Reused->SetActorTickEnabled(true);
				return Reused;
			}
		}
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	return World->SpawnActor<AProject_JNPCCharacter>(NPCClass, SpawnTransform, Params);
}

bool UProject_JNPCPoolSubsystem::ReleaseNPCToPool(AProject_JNPCCharacter* NPC)
{
	if (!IsValid(NPC) || NPC->IsActorBeingDestroyed())
	{
		return false;
	}
	TArray<TWeakObjectPtr<AProject_JNPCCharacter>>& Pool = FreePools.FindOrAdd(TObjectKey<UClass>(NPC->GetClass()));
	// Prune dead/destroyed entries first so stale weak pointers never prematurely exhaust MaxPooledPerClass.
	Pool.RemoveAllSwap([](const TWeakObjectPtr<AProject_JNPCCharacter>& Entry)
	{
		return !Entry.IsValid() || Entry->IsActorBeingDestroyed();
	}, EAllowShrinking::No);
	// Reject duplicate release so AcquireNPC can never return the same live actor twice (aliasing bug).
	if (Pool.Contains(NPC))
	{
		return false;
	}
	if (Pool.Num() >= MaxPooledPerClass)
	{
		NPC->Destroy();
		return false;
	}
	NPC->SetActorHiddenInGame(true);
	NPC->SetActorEnableCollision(false);
	NPC->SetActorTickEnabled(false);
	Pool.Add(NPC);
	return true;
}

EProject_JNPCCrowdTier UProject_JNPCPoolSubsystem::EvaluateTargetTier(
	EProject_JNPCCrowdTier CurrentTier,
	double NearestObserverDistanceCm,
	bool bInCombatOrBoss)
{
	if (bInCombatOrBoss)
	{
		return EProject_JNPCCrowdTier::Tier0_NearFullActor;
	}

	switch (CurrentTier)
	{
	case EProject_JNPCCrowdTier::Tier0_NearFullActor:
		if (NearestObserverDistanceCm >= MidDemoteCm) return EProject_JNPCCrowdTier::Tier2_FarMassVAT;
		if (NearestObserverDistanceCm >= NearDemoteCm) return EProject_JNPCCrowdTier::Tier1_MidLightweightActor;
		return EProject_JNPCCrowdTier::Tier0_NearFullActor;

	case EProject_JNPCCrowdTier::Tier1_MidLightweightActor:
		if (NearestObserverDistanceCm <= NearPromoteCm) return EProject_JNPCCrowdTier::Tier0_NearFullActor;
		if (NearestObserverDistanceCm >= MidDemoteCm) return EProject_JNPCCrowdTier::Tier2_FarMassVAT;
		return EProject_JNPCCrowdTier::Tier1_MidLightweightActor;

	case EProject_JNPCCrowdTier::Tier2_FarMassVAT:
	default:
		if (NearestObserverDistanceCm <= NearPromoteCm) return EProject_JNPCCrowdTier::Tier0_NearFullActor;
		if (NearestObserverDistanceCm <= MidPromoteCm) return EProject_JNPCCrowdTier::Tier1_MidLightweightActor;
		return EProject_JNPCCrowdTier::Tier2_FarMassVAT;
	}
}

void UProject_JNPCPoolSubsystem::UpsertVATProxy(const FProject_JMassVATProxyDescriptor& Proxy)
{
	if (Proxy.AgentId != 0)
	{
		VATProxies.Add(Proxy.AgentId, Proxy);
	}
}

bool UProject_JNPCPoolSubsystem::RemoveVATProxy(uint64 AgentId)
{
	return VATProxies.Remove(AgentId) > 0;
}

int32 UProject_JNPCPoolSubsystem::GetPooledCount(TSubclassOf<AProject_JNPCCharacter> NPCClass) const
{
	if (const auto* Pool = FreePools.Find(TObjectKey<UClass>(NPCClass.Get())))
	{
		int32 Valid = 0;
		for (const auto& Entry : *Pool)
		{
			if (Entry.IsValid()) ++Valid;
		}
		return Valid;
	}
	return 0;
}

int32 UProject_JNPCPoolSubsystem::SyncVATProxiesToISM(UInstancedStaticMeshComponent* TargetISM, FName ArchetypeFilter) const
{
	if (!IsValid(TargetISM))
	{
		return 0;
	}
	TArray<FTransform> Transforms;
	TArray<float> ClipIndices;
	Transforms.Reserve(VATProxies.Num());
	ClipIndices.Reserve(VATProxies.Num());
	for (const auto& Pair : VATProxies)
	{
		const FProject_JMassVATProxyDescriptor& Proxy = Pair.Value;
		if (!ArchetypeFilter.IsNone() && Proxy.ArchetypeId != ArchetypeFilter)
		{
			continue;
		}
		Transforms.Add(Proxy.WorldTransform);
		ClipIndices.Add(static_cast<float>(Proxy.VATAnimClipIndex));
	}
	TargetISM->ClearInstances();
	if (!Transforms.IsEmpty())
	{
		TargetISM->NumCustomDataFloats = FMath::Max(TargetISM->NumCustomDataFloats, 1);
		TargetISM->AddInstances(Transforms, false, true);
		for (int32 i = 0; i < ClipIndices.Num(); ++i)
		{
			TargetISM->SetCustomDataValue(i, 0, ClipIndices[i], i == ClipIndices.Num() - 1);
		}
	}
	return Transforms.Num();
}
