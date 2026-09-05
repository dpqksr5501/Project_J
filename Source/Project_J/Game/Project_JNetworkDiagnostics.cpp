// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_J.h"

#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Net/Core/Trace/Private/NetTraceInternal.h"

#if !UE_BUILD_SHIPPING
namespace Project_JNetworkDiagnostics
{
	static const TCHAR* GetNetModeName(const ENetMode NetMode)
	{
		switch (NetMode)
		{
		case NM_Standalone:
			return TEXT("Standalone");
		case NM_DedicatedServer:
			return TEXT("DedicatedServer");
		case NM_ListenServer:
			return TEXT("ListenServer");
		case NM_Client:
			return TEXT("Client");
		default:
			return TEXT("Unknown");
		}
	}

	static int32 ReadCVar(const TCHAR* Name)
	{
		if (const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			return Variable->GetInt();
		}
		return INDEX_NONE;
	}

	static void DumpNetworkRuntime()
	{
		if (!GEngine)
		{
			UE_LOG(LogProject_J, Warning, TEXT("NetworkRuntime unavailable: GEngine is null."));
			return;
		}

		bool bFoundGameWorld = false;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* const World = Context.World();
			if (!World || (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE))
			{
				continue;
			}

			bFoundGameWorld = true;
			UNetDriver* const NetDriver = World->GetNetDriver();
			int32 ReplicatedActorCount = 0;
			int32 ActorCount = 0;
			for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
			{
				++ActorCount;
				ReplicatedActorCount += ActorIt->GetIsReplicated() ? 1 : 0;
			}

			const int32 ClientConnectionCount = NetDriver ? NetDriver->ClientConnections.Num() : 0;
			const bool bHasServerConnection = NetDriver && NetDriver->ServerConnection != nullptr;
			const bool bUsingIris = NetDriver && NetDriver->IsUsingIrisReplication();
			const FString ReplicationDriverClass = NetDriver && NetDriver->GetReplicationDriver()
				? NetDriver->GetReplicationDriver()->GetClass()->GetName()
				: TEXT("None");
			const TCHAR* const ReplicationModel = bUsingIris ? TEXT("Iris") : TEXT("Legacy");

			UE_LOG(
				LogProject_J,
				Display,
				TEXT("NetworkRuntime World=%s NetMode=%s Driver=%s DriverClass=%s IrisActive=%d IrisUseReplicationCVar=%d ReplicationModel=%s ReplicationDriver=%s ClientConnections=%d HasServerConnection=%d Actors=%d ReplicatedActors=%d"),
				*World->GetName(),
				GetNetModeName(World->GetNetMode()),
				*GetNameSafe(NetDriver),
				*GetNameSafe(NetDriver ? NetDriver->GetClass() : nullptr),
				bUsingIris ? 1 : 0,
				ReadCVar(TEXT("net.Iris.UseIrisReplication")),
				ReplicationModel,
				*ReplicationDriverClass,
				ClientConnectionCount,
				bHasServerConnection ? 1 : 0,
				ActorCount,
				ReplicatedActorCount);
		}

		if (!bFoundGameWorld)
		{
			UE_LOG(LogProject_J, Warning, TEXT("NetworkRuntime unavailable: no Game or PIE world."));
		}
	}

	static void SetNetTraceVerbosity(const TArray<FString>& Args)
	{
#if UE_NET_TRACE_ENABLED
		int32 RequestedVerbosity = ENetTraceVerbosity::Trace;
		if (Args.Num() > 0)
		{
			RequestedVerbosity = FMath::Clamp(FCString::Atoi(*Args[0]), 0, ENetTraceVerbosity::VeryVerbose);
		}

		FNetTrace::SetTraceVerbosity(static_cast<uint32>(RequestedVerbosity));
		UE_LOG(
			LogProject_J,
			Display,
			TEXT("NetworkTrace Verbosity=%u. Start Trace.File with the net channel after this command; use 0 to disable packet tracing."),
			FNetTrace::GetTraceVerbosity());
#else
		UE_LOG(LogProject_J, Warning, TEXT("NetworkTrace unavailable: this build has UE_NET_TRACE_ENABLED=0."));
#endif
	}

	static void DumpServerReplicationPolicy(const TArray<FString>& Args)
	{
		if (!GEngine)
		{
			UE_LOG(LogProject_J, Warning, TEXT("ServerReplicationPolicy unavailable: GEngine is null."));
			return;
		}

		const int32 MaxDetailedActors = Args.Num() > 0 ? FMath::Max(0, FCString::Atoi(*Args[0])) : 32;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* const World = Context.World();
			if (!World || (World->GetNetMode() != NM_DedicatedServer && World->GetNetMode() != NM_ListenServer))
			{
				continue;
			}

			int32 ReplicatedActorCount = 0;
			int32 ReplicatedMovementCount = 0;
			int32 DetailedActorsPrinted = 0;
			TMap<FString, int32> ReplicatedActorClasses;

			for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
			{
				AActor* const Actor = *ActorIt;
				if (!Actor || !Actor->GetIsReplicated())
				{
					continue;
				}

				++ReplicatedActorCount;
				ReplicatedMovementCount += Actor->IsReplicatingMovement() ? 1 : 0;
				ReplicatedActorClasses.FindOrAdd(Actor->GetClass()->GetName())++;

				if (DetailedActorsPrinted >= MaxDetailedActors)
				{
					continue;
				}

				const float CullDistance = FMath::Sqrt(FMath::Max(0.0f, Actor->GetNetCullDistanceSquared()));
				UE_LOG(
					LogProject_J,
					Display,
					TEXT("ServerReplicationPolicy World=%s Actor=%s Class=%s Movement=%d NetUpdateHz=%.2f MinNetUpdateHz=%.2f CullDistance=%.0f Owner=%s"),
					*World->GetName(),
					*GetNameSafe(Actor),
					*GetNameSafe(Actor->GetClass()),
					Actor->IsReplicatingMovement() ? 1 : 0,
					Actor->GetNetUpdateFrequency(),
					Actor->GetMinNetUpdateFrequency(),
					CullDistance,
					*GetNameSafe(Actor->GetOwner()));
				++DetailedActorsPrinted;
			}

			UE_LOG(
				LogProject_J,
				Display,
				TEXT("ServerReplicationPolicy Summary World=%s ReplicatedActors=%d ReplicatedMovementActors=%d Classes=%d Detailed=%d/%d"),
				*World->GetName(),
				ReplicatedActorCount,
				ReplicatedMovementCount,
				ReplicatedActorClasses.Num(),
				DetailedActorsPrinted,
				MaxDetailedActors);

			for (const TPair<FString, int32>& ClassCount : ReplicatedActorClasses)
			{
				UE_LOG(LogProject_J, Display, TEXT("ServerReplicationPolicy Class=%s Count=%d"), *ClassCount.Key, ClassCount.Value);
			}
		}
	}

	static FAutoConsoleCommand DumpNetworkRuntimeCommand(
		TEXT("ProjectJ.DumpNetworkRuntime"),
		TEXT("Logs the live NetDriver, Iris state, connection count, and replicated actor count for every Game/PIE world."),
		FConsoleCommandDelegate::CreateStatic(&DumpNetworkRuntime),
		ECVF_Default);

	static FAutoConsoleCommand SetNetTraceVerbosityCommand(
		TEXT("ProjectJ.SetNetTraceVerbosity"),
		TEXT("Sets runtime NetTrace verbosity (0=None, 1=Trace, 2=Verbose, 3=VeryVerbose). Invoke before Trace.File ... net."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SetNetTraceVerbosity),
		ECVF_Default);

	static FAutoConsoleCommand DumpServerReplicationPolicyCommand(
		TEXT("ProjectJ.DumpServerReplicationPolicy"),
		TEXT("Logs live server replicated actor classes and actor network defaults. Optional argument: max detailed actors."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&DumpServerReplicationPolicy),
		ECVF_Default);
}
#endif
