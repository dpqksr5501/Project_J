// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_J.h"
#include "Modules/ModuleManager.h"
#include "Misc/NetworkVersion.h"
#include "Backend/Project_JProfileDatabase.h"

#if defined(PROJECT_J_LANDSCAPE_PSO_REACTIVATION_WORKAROUND)
#include "DynamicRHI.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/PlatformProperties.h"
#include "LandscapeComponent.h"
#include "LandscapeProxy.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "PSOPrecache.h"
#include "Streaming/LevelStreamingDelegates.h"
#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"

namespace
{
void PrepareMainWorldLandscapePSOs(UWorld* World, const ULevelStreaming* StreamingLevel, ULevel* Level)
{
	static const FName MainWorldPackage(TEXT("/Game/World/MainWorld/Maps/L_MainWorld"));
	if (!World || World->WorldType != EWorldType::Game || World->bIsTearingDown || !World->Scene
		|| World->GetNetMode() == NM_DedicatedServer || World->GetOutermost()->GetFName() != MainWorldPackage
		|| !Level || Level->GetWorld() != World || !Cast<UWorldPartitionLevelStreamingDynamic>(StreamingLevel)
		|| StreamingLevel->GetLoadedLevel() != Level || !GDynamicRHI || RHIGetInterfaceType() != ERHIInterfaceType::D3D12
		|| !IsComponentPSOPrecachingEnabled())
	{
		return;
	}

	// UE-404796: unregister clears the request state of a retained Landscape component.
	// This event precedes incremental registration and the creation of its scene proxy.
	TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_MainWorldLandscapePSOReactivation);
	int32 PreparedComponents = 0;
	for (const auto& Actor : Level->Actors)
	{
		const auto* Landscape = Cast<ALandscapeProxy>(Actor.Get());
		if (!IsValid(Landscape) || Landscape->IsActorBeingDestroyed()) continue;
		for (ULandscapeComponent* Component : Landscape->LandscapeComponents)
		{
			if (!IsValid(Component) || Component->IsBeingDestroyed() || Component->IsRegistered()
				|| Component->GetPSOPrecacheComponentData().IsPSOPrecacheCalled()) continue;
			Component->PrecachePSOs();
			++PreparedComponents;
		}
	}
	if (PreparedComponents > 0)
	{
		UE_LOG(LogProject_J, Verbose, TEXT("MainWorld Landscape PSO requests reissued for %d components in %s"),
			PreparedComponents, *Level->GetOutermost()->GetName());
	}
}
}
#endif

class FProjectJGameModule final:public FDefaultGameModuleImpl
{
#if defined(PROJECT_J_LANDSCAPE_PSO_REACTIVATION_WORKAROUND)
	FDelegateHandle LandscapePSORegistration;
#endif

	void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();
		// Use the engine's existing game protocol slot before any NetDriver
		// handshake. Current and replay-compatible versions advance together
		// until replay compatibility is explicitly implemented and tested.
		FNetworkVersion::SetGameNetworkProtocolVersion(ProjectJ::Network::RealtimeProtocolVersion);
		FNetworkVersion::SetGameCompatibleNetworkProtocolVersion(ProjectJ::Network::RealtimeProtocolVersion);
#if defined(PROJECT_J_LANDSCAPE_PSO_REACTIVATION_WORKAROUND)
		if (FPlatformProperties::RequiresCookedData())
			LandscapePSORegistration = FLevelStreamingDelegates::OnLevelBeginMakingVisible.AddStatic(&PrepareMainWorldLandscapePSOs);
#endif
#if !UE_SERVER
		// OnlineSubsystemUtils asks for Voice on the first client connection even
		// when capture is disabled. Load this known dependency before Iris creates
		// a replication system, rather than changing serializer registrations live.
		FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("Voice"));
#endif
#if WITH_EDITOR || UE_SERVER
		// NetDriver lazily loads server counters on the first connection. Load
		// that known dependency before Iris freezes its serializer registrations.
		FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("PerfCounters"));
#endif
	}
	void ShutdownModule() override
	{
#if defined(PROJECT_J_LANDSCAPE_PSO_REACTIVATION_WORKAROUND)
		FLevelStreamingDelegates::OnLevelBeginMakingVisible.Remove(LandscapePSORegistration);
		LandscapePSORegistration.Reset();
#endif
		// GameInstance persistence drains its native jobs before module unloading.
		FProject_JProfileDatabase::ShutdownVFS();
		FDefaultGameModuleImpl::ShutdownModule();
	}
};
IMPLEMENT_PRIMARY_GAME_MODULE( FProjectJGameModule, Project_J, "Project_J" );

DEFINE_LOG_CATEGORY(LogProject_J)
