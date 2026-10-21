// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_J.h"
#include "Engine/ReplicationDriver.h"

#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Net/Core/Trace/Private/NetTraceInternal.h"
#include "NetworkPredictionWorldManager.h"
#include "NetworkPredictionComponent.h"
#include "GameFramework/Pawn.h"
#include "UObject/UnrealType.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if !UE_BUILD_SHIPPING
namespace Project_JNetworkDiagnostics
{
	// Explicit, bounded, read-only capture. No per-frame console/MCP round trip,
	// flush or file IO can perturb the receive timeline under test. Discover new
	// pawn components once per second; sample their weak references thereafter.
	class FMoverBufferCapture
	{
		FDelegateHandle Handle;
		FString Rows, ObjectRows, Output;
		double Started=0., Duration=0., NextDiscovery=0.;
		TArray<TWeakObjectPtr<UNetworkPredictionComponent>> Components;
		struct FReceive { int32 Latest=0; double ChangedAt=0.; };
		TMap<FString,FReceive> Received;
	public:
		~FMoverBufferCapture() { Stop(); }
		void Stop()
		{
			if(!Handle.IsValid()) return;
			FCoreDelegates::OnEndFrame.Remove(Handle); Handle.Reset();
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output),true);
			const bool bSaved=FFileHelper::SaveStringToFile(Rows,*Output);
			FFileHelper::SaveStringToFile(ObjectRows,*(FPaths::ChangeExtension(Output,TEXT("actors.csv"))));
			UE_LOG(LogProject_J,Display,TEXT("MoverBufferCapture Saved=%d File=%s"),bSaved,*Output);
			Rows.Reset(); ObjectRows.Reset(); Components.Reset(); Received.Reset();
		}
		void Start(const TArray<FString>& Args)
		{
			if(Handle.IsValid()) { UE_LOG(LogProject_J,Warning,TEXT("Mover buffer capture already running.")); return; }
			Duration=FMath::Clamp(Args.IsEmpty()?60.:FCString::Atod(*Args[0]),1.,120.);
			Started=FPlatformTime::Seconds();
			Output=FPaths::ProjectSavedDir()/TEXT("Validation/MoverBuffers")/(FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT(".csv"));
			Rows=TEXT("WallSeconds,World,NetMode,WorldDeltaMS,SimulationMS,LatestReceiveMS,InterpolationMS,BufferedMS,ReceiveGapMS,ReceiveAdvanceMS,DesiredMS,MaxMS\n");
			ObjectRows=TEXT("WallSeconds,World,Actor,Role,NetHz,MinNetHz,PendingFrame,CachedAutonomousFrame,CachedSimulatedFrame,Dormancy\n");
			NextDiscovery=0.;
			Handle=FCoreDelegates::OnEndFrame.AddRaw(this,&FMoverBufferCapture::Sample);
			UE_LOG(LogProject_J,Display,TEXT("MoverBufferCapture Started Seconds=%.1f File=%s"),Duration,*Output);
		}
		void Sample()
		{
			const double Now=FPlatformTime::Seconds();
			if(Now-Started>=Duration) { Stop(); return; }
			if(!GEngine) return;
			for(const FWorldContext& Context:GEngine->GetWorldContexts())
			{
				auto* World=Context.World();
				if(!World || !World->IsGameWorld() || World->bIsTearingDown) continue;
				const auto* Manager=World->GetSubsystem<UNetworkPredictionWorldManager>();
				if(!Manager) continue;
				if(Now>=NextDiscovery)
					for(TActorIterator<APawn> It(World);It;++It)
						if(auto* Component=It->FindComponentByClass<UNetworkPredictionComponent>()) Components.AddUnique(Component);
				const auto& State=Manager->GetVariableTickState();
				const int32 Latest=State.Interpolation.LatestRecvTimeMS;
				const FString Name=World->GetPathName();
				auto& Receive=Received.FindOrAdd(Name);
				const double Gap=Receive.ChangedAt>0.?(Now-Receive.ChangedAt)*1000.:0.;
				const int32 Advance=Latest-Receive.Latest;
				if(Advance) { Receive.Latest=Latest; Receive.ChangedAt=Now; }
				Rows+=FString::Printf(TEXT("%.6f,%s,%d,%.3f,%d,%d,%.3f,%.3f,%.3f,%d,%d,%d\n"),
					Now-Started,*Name,int32(World->GetNetMode()),World->GetDeltaSeconds()*1000.,
					State.Frames[State.PendingFrame].TotalMS,Latest,State.Interpolation.fTimeMS,
					Latest-State.Interpolation.fTimeMS,Gap,Advance,
					Manager->GetSettings().IndependentTickInterpolationBufferedMS,
					Manager->GetSettings().IndependentTickInterpolationMaxBufferedMS);
			}
			if(Now>=NextDiscovery) NextDiscovery=Now+1.;
			static const auto* Autonomous=FindFProperty<FStructProperty>(UNetworkPredictionComponent::StaticClass(),TEXT("ReplicationProxy_Autonomous"));
			static const auto* Simulated=FindFProperty<FStructProperty>(UNetworkPredictionComponent::StaticClass(),TEXT("ReplicationProxy_Simulated"));
			static const auto* Prediction=FindFProperty<FStructProperty>(UNetworkPredictionComponent::StaticClass(),TEXT("NetworkPredictionProxy"));
			for(const auto& Weak:Components)
				if(const auto* Component=Weak.Get())
					if(const auto* Owner=Component->GetOwner();Owner && Owner->GetWorld() && !Owner->GetWorld()->bIsTearingDown)
						ObjectRows+=FString::Printf(TEXT("%.6f,%s,%s,%d,%.3f,%.3f,%d,%d,%d,%d\n"),Now-Started,
							*Owner->GetWorld()->GetPathName(),*Owner->GetName(),int32(Owner->GetLocalRole()),Owner->GetNetUpdateFrequency(),Owner->GetMinNetUpdateFrequency(),
							Prediction?Prediction->ContainerPtrToValuePtr<FNetworkPredictionProxy>(Component)->GetPendingFrame():-1,
							Autonomous?Autonomous->ContainerPtrToValuePtr<FReplicationProxy>(Component)->GetCachedPendingFrame():-1,
							Simulated?Simulated->ContainerPtrToValuePtr<FReplicationProxy>(Component)->GetCachedPendingFrame():-1,int32(Owner->NetDormancy));
		}
	};
	static FMoverBufferCapture MoverBufferCapture;
	static FAutoConsoleCommand CaptureMoverBuffersCommand(
		TEXT("ProjectJ.CaptureMoverBuffers"),TEXT("Read-only Mover receive/simulation/interpolation CSV capture. Optional seconds (1-120, default 60)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args){MoverBufferCapture.Start(Args);}),ECVF_Default);

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
