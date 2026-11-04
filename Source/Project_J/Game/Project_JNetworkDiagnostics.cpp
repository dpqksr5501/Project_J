// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_J.h"
#include "Engine/ReplicationDriver.h"

#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Engine/Level.h"
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
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Backends/MoverNetworkPredictionLiaison.h"
#include "MoverDataModelTypes.h"
#include "Net/Iris/ReplicationSystem/ReplicationSystemUtil.h"
#include "Net/Iris/ReplicationSystem/EngineReplicationBridge.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Async/Async.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#endif

#if !UE_BUILD_SHIPPING
namespace Project_JNetworkDiagnostics
{
	// Read-only, frame-resolution capture. Stop with an explicit partial result
	// rather than letting diagnostics allocate or discover without a bound.
	class FMoverBufferCapture
	{
		static constexpr int32 MaxWorldChars = 1024 * 1024;
		static constexpr int32 MaxActorChars = 7 * 1024 * 1024;
		static constexpr int32 MaxRowChars = 8192;
		static constexpr int32 MaxComponents = 1024;
		static constexpr int32 MaxWorlds = 8;
		static constexpr int32 MaxDiscoveryActorVisits = 8192;
		static constexpr int32 MaxDiscoveryLevels = 128;
		static constexpr SIZE_T MaxRetainedBytes = 18 * 1024 * 1024;
		FDelegateHandle Handle, PreExitHandle;
		FString Rows, ObjectRows, Output;
		double Started=0., Duration=0., NextDiscovery=0.;
		TSet<TWeakObjectPtr<UNetworkPredictionComponent>> Components;
		struct FReceive { int32 Latest=0; double ChangedAt=0.; };
		TMap<TWeakObjectPtr<UWorld>,FReceive> Received;
		int64 WorldRowCount=0, ActorRowCount=0, DiscoveryActorVisits=0;
		int32 PeakComponents=0, PeakDiscoveryActorVisits=0;
		SIZE_T PeakRetainedBytes=0;
  bool bRawRequested=false, bAuthorityRequested=false;
  int32 PreviousRawLevel=0;
  int64 RawClientRows=0, RawReceivedRows=0, AuthorityRows=0, AuthorityEncodedRows=0;
  // Optional existing-capture filter, at most3 current Iris actor identities.
  TArray<uint64,TInlineAllocator<3>> ActorFilter;
  uint8 FocusedSeenMask=0;

  static const TCHAR* ReceiveStatusName(ENPModelWireStatus Status)
  {
   switch(Status)
   {
   case ENPModelWireStatus::AwaitingReceive:return TEXT("AwaitingReceive");
   case ENPModelWireStatus::ReceiveApplied:return TEXT("Received");
   case ENPModelWireStatus::AwaitingEncode:return TEXT("AwaitingEncode");
   case ENPModelWireStatus::AuthorityEncoded:return TEXT("AuthorityEncoded");
   case ENPModelWireStatus::UnsupportedMode:return TEXT("UnsupportedMode");
   case ENPModelWireStatus::UnsupportedDriver:return TEXT("UnsupportedDriver");
   case ENPModelWireStatus::Retired:return TEXT("Retired");
   case ENPModelWireStatus::ArchiveError:return TEXT("ArchiveError");
   case ENPModelWireStatus::Overflow:return TEXT("Overflow");
   case ENPModelWireStatus::StaleBinding:return TEXT("StaleBinding");
   case ENPModelWireStatus::BeforeCapture:return TEXT("BeforeCapture");
   case ENPModelWireStatus::NotClient:return TEXT("NotClient");
   case ENPModelWireStatus::Disabled:return TEXT("Disabled");
   default:return TEXT("Unbound");
   }
  }

  static uint64 ActorIdentity(const AActor* Actor)
  {
   const auto* Bridge=Actor?UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(Actor):nullptr;
   return Bridge?Bridge->GetReplicatedRefHandle(Actor).GetId():0;
  }
  static bool ParseActorFilter(const TArray<FString>& Args,TArray<uint64,TInlineAllocator<3>>& Out)
  {
   Out.Empty(); bool bFound=false;
   for(const FString& Arg:Args)
   {
    if(!Arg.StartsWith(TEXT("ActorIds="))) continue;
    if(bFound || Arg.Len()<=9 || Arg.Len()>71) return false;
    bFound=true; uint64 Value=0; int32 Digits=0;
    for(int32 I=9;I<=Arg.Len();++I)
    {
     if(I==Arg.Len() || Arg[I]==TEXT(','))
     {
      if(!Digits || !Value || Out.Num()>=3 || Out.Contains(Value)) return false;
      Out.Add(Value); Value=0; Digits=0; continue;
     }
     const TCHAR Character=Arg[I];
     if(Character<TEXT('0') || Character>TEXT('9')) return false;
     const uint64 Digit=uint64(Character-TEXT('0'));
     if(Value>(MAX_uint64-Digit)/10) return false;
     Value=Value*10+Digit; ++Digits;
    }
   }
   return true;
  }
  static bool ReadStoredScalars(const FMoverSyncState* State,float* Out)
  {
   const auto* Data=State?State->SyncStateCollection.FindDataByType<FMoverDefaultSyncState>():nullptr;
   if(!Data) return false;
   const FVector Position=Data->GetLocation_BaseSpace(), Velocity=Data->GetVelocity_BaseSpace();
   const float Values[4]={float(Position.X),float(Position.Y),float(Velocity.X),float(Velocity.Y)};
   for(float Value:Values) if(!FMath::IsFinite(Value)) return false;
   for(int32 I=0;I<4;++I) Out[I]=Values[I];
   return true;
  }
		SIZE_T RetainedBytes() const
		{
			return sizeof(*this) + Rows.GetAllocatedSize() + ObjectRows.GetAllocatedSize()
				+ Output.GetAllocatedSize() + Components.GetAllocatedSize() + Received.GetAllocatedSize() + ActorFilter.GetAllocatedSize();
		}
		bool CheckMemoryBudget()
		{
			PeakRetainedBytes=FMath::Max(PeakRetainedBytes,RetainedBytes());
			if(PeakRetainedBytes<=MaxRetainedBytes) return true;
			Stop(TEXT("MemoryLimit"),false); return false;
		}
		bool AppendRow(FString& Target,const FString& Line,int32 MaxChars,bool bActorRow,bool bRawClient=false,bool bRawReceived=false,bool bAuthority=false,bool bEncoded=false)
		{
			if(Line.Len()>MaxRowChars || Line.Len()>=MaxChars-Target.Len())
			{
				Stop(Line.Len()>MaxRowChars?TEXT("RowLimit"):(bActorRow?TEXT("ActorBufferLimit"):TEXT("WorldBufferLimit")),false);
				return false;
			}
			Target+=Line;
			if(bActorRow) ++ActorRowCount; else ++WorldRowCount;
   if(bRawClient) ++RawClientRows; if(bRawReceived) ++RawReceivedRows;
   if(bAuthority) ++AuthorityRows; if(bEncoded) ++AuthorityEncodedRows;
			return CheckMemoryBudget();
		}
		void PreExit() { Stop(TEXT("EnginePreExit"),true); }
		void PruneRetiredSources()
		{
			for(auto It=Components.CreateIterator();It;++It)
			{
				const auto* Component=It->Get();
				const auto* Owner=Component?Component->GetOwner():nullptr;
				const auto* World=Owner?Owner->GetWorld():nullptr;
				if(!IsValid(Component) || Component->IsBeingDestroyed() || !Component->HasBeenInitialized() || !Component->HasBegunPlay() || !Component->IsRegistered() || !IsValid(Owner)
					|| Owner->IsActorBeingDestroyed() || !World || World->bIsTearingDown || !World->IsGameWorld()) It.RemoveCurrent();
			}
			for(auto It=Received.CreateIterator();It;++It)
				if(const auto* World=It.Key().Get();!World || World->bIsTearingDown || !World->IsGameWorld()) It.RemoveCurrent();
		}
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
		friend class FMoverBufferCaptureBoundsTest;
#endif
	public:
		~FMoverBufferCapture() { Stop(TEXT("Destructor"),false); }
		void Stop(const TCHAR* Reason=TEXT("ExplicitStop"),bool bComplete=true)
		{
			if(!Handle.IsValid()) return;
			FCoreDelegates::OnEndFrame.Remove(Handle); Handle.Reset();
			FCoreDelegates::OnEnginePreExit.Remove(PreExitHandle); PreExitHandle.Reset();
   const bool bRawThreadSupported=!FReplicationProxy::HasUnsupportedModelWireThread();
   const bool bRawEnabled=FReplicationProxy::IsModelWireObservationEnabled();
   const bool bScalarEnabled=FReplicationProxy::IsModelWireScalarObservationEnabled();
   if(bRawRequested || bAuthorityRequested)
   {
    FReplicationProxy::ExchangeModelWireObservationLevel(PreviousRawLevel);
    if(bComplete && (!bRawThreadSupported || !bRawEnabled || (bAuthorityRequested?AuthorityEncodedRows:RawReceivedRows)==0))
    { bComplete=false; Reason=!bRawThreadSupported?TEXT("UnsupportedModelWireThread"):(!bRawEnabled?TEXT("ModelWireObservationDisabled"):(bAuthorityRequested?TEXT("NoAuthorityEncodeObserved"):TEXT("NoReceiveObserved"))); }
   }
   if(bComplete && !ActorFilter.IsEmpty() && !bScalarEnabled) { bComplete=false; Reason=TEXT("DecodedScalarObservationDisabled"); }
   if(bComplete && !ActorFilter.IsEmpty() && FocusedSeenMask!=uint8((1u<<ActorFilter.Num())-1u))
   { bComplete=false; Reason=TEXT("FocusedActorNotObserved"); }
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output),true);
			const bool bSaved=FFileHelper::SaveStringToFile(Rows,*Output);
			const bool bActorsSaved=FFileHelper::SaveStringToFile(ObjectRows,*(FPaths::ChangeExtension(Output,TEXT("actors.csv"))));
			FString Summary=FString::Printf(TEXT("{\"schema_version\":1,\"complete\":%s,\"truncated\":%s,\"stop_reason\":\"%s\",\"requested_seconds\":%.6f,\"elapsed_seconds\":%.6f,\"world_rows\":%lld,\"actor_rows\":%lld,\"peak_components\":%d,\"peak_retained_bytes\":%llu,\"max_retained_bytes\":%llu,\"discovery_actor_visits\":%lld,\"peak_discovery_actor_visits\":%d,\"max_discovery_actor_visits\":%d,\"max_components\":%d,\"max_worlds\":%d,\"csv_saved\":%s,\"actors_csv_saved\":%s}\n"),
				bComplete?TEXT("true"):TEXT("false"),bComplete?TEXT("false"):TEXT("true"),Reason,Duration,FMath::Max(0.,FPlatformTime::Seconds()-Started),
				WorldRowCount,ActorRowCount,PeakComponents,static_cast<uint64>(PeakRetainedBytes),static_cast<uint64>(MaxRetainedBytes),
				DiscoveryActorVisits,PeakDiscoveryActorVisits,MaxDiscoveryActorVisits,MaxComponents,MaxWorlds,
				bSaved?TEXT("true"):TEXT("false"),bActorsSaved?TEXT("true"):TEXT("false"));
   // Optional diagnostic contract; existing capture schema/first ten actor fields stay intact.
   Summary.RemoveFromEnd(TEXT("}\n"));
   Summary+=FString::Printf(TEXT(",\"raw_receive_requested\":%s,\"raw_receive_capable\":true,\"raw_receive_enabled\":%s,\"raw_receive_thread_supported\":%s,\"raw_receive_client_rows\":%lld,\"raw_receive_received_rows\":%lld,\"raw_receive_snapshot_bytes\":%llu,\"raw_receive_max_tracked_snapshot_bytes\":%llu,\"capture_started_seconds\":%.9f}\n"),
    bRawRequested?TEXT("true"):TEXT("false"),bRawEnabled?TEXT("true"):TEXT("false"),bRawThreadSupported?TEXT("true"):TEXT("false"),RawClientRows,RawReceivedRows,
    uint64(sizeof(FNPModelWireSnapshot)),uint64(sizeof(FNPModelWireSnapshot))*4*MaxComponents,Started);
   Summary.RemoveFromEnd(TEXT("}\n"));
   FString Host;
   for(TCHAR Character:FString(FPlatformProcess::ComputerName()))
   {
    if(Character==TEXT('\\') || Character==TEXT('"')) { Host+=TEXT('\\'); Host+=Character; }
    else if(uint32(Character)<0x20) Host+=FString::Printf(TEXT("\\u%04x"),uint32(Character));
    else Host+=Character;
   }
   Summary+=FString::Printf(TEXT(",\"model_wire_snapshot_bytes\":%llu,\"model_wire_max_tracked_snapshot_bytes\":%llu,\"authority_encode_requested\":%s,\"authority_encode_capable\":true,\"authority_encode_enabled\":%s,\"authority_encode_thread_supported\":%s,\"authority_rows\":%lld,\"authority_encoded_rows\":%lld,\"observation_clock\":\"%s\",\"observation_host\":\"%s\",\"observation_process_id\":%u}\n"),
    uint64(sizeof(FNPModelWireSnapshot)),uint64(sizeof(FNPModelWireSnapshot))*4*MaxComponents,bAuthorityRequested?TEXT("true"):TEXT("false"),bRawEnabled?TEXT("true"):TEXT("false"),bRawThreadSupported?TEXT("true"):TEXT("false"),AuthorityRows,AuthorityEncodedRows,
    PLATFORM_WINDOWS?TEXT("WindowsQPC"):TEXT("ProcessMonotonic"),*Host,FPlatformProcess::GetCurrentProcessId());
   Summary.RemoveFromEnd(TEXT("}\n"));
   Summary+=FString::Printf(TEXT(",\"focused_actor_count\":%d,\"focused_observed_mask\":%u,\"focused_actor_ids\":["),ActorFilter.Num(),uint32(FocusedSeenMask));
   for(int32 I=0;I<ActorFilter.Num();++I)
    Summary+=FString::Printf(TEXT("%s\"%llu\""),I?TEXT(","):TEXT(""),ActorFilter[I]);
   Summary+=TEXT("],\"decoded_scalar_contract\":\"MoverStoredBaseOrWorldXYVelocityXYFloat32\",\"decoded_basis_world_proven\":false}\n");
			const bool bSummarySaved=FFileHelper::SaveStringToFile(Summary,*(FPaths::ChangeExtension(Output,TEXT("summary.json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			UE_LOG(LogProject_J,Display,TEXT("MoverBufferCapture Saved=%d ActorsSaved=%d SummarySaved=%d Complete=%d Reason=%s WorldRows=%lld ActorRows=%lld File=%s"),
				bSaved,bActorsSaved,bSummarySaved,bComplete,Reason,WorldRowCount,ActorRowCount,*Output);
			// Reset() retains the large allocations in the static command instance.
			Rows.Empty(); ObjectRows.Empty(); Components.Empty(); Received.Empty(); ActorFilter.Empty(); FocusedSeenMask=0; Output.Empty();
		}
		void Start(const TArray<FString>& Args)
		{
   if(Args.Num()==1 && Args[0]==TEXT("Stop")) { Stop(TEXT("ExplicitStop"),true); return; }
			if(Handle.IsValid()) { UE_LOG(LogProject_J,Warning,TEXT("Mover buffer capture already running.")); return; }
   TArray<uint64,TInlineAllocator<3>> RequestedActors;
   if(!ParseActorFilter(Args,RequestedActors) || (!RequestedActors.IsEmpty() && (!Args.Contains(TEXT("RawReceive")) || Args.Contains(TEXT("AuthorityEncode")))))
   { UE_LOG(LogProject_J,Warning,TEXT("Mover buffer actor filter requires RawReceive and1..3 unique nonzero uint64 IDs.")); return; }
   ActorFilter=MoveTemp(RequestedActors); FocusedSeenMask=0;
			const double Requested=Args.IsEmpty()?60.:FCString::Atod(*Args[0]);
			Duration=FMath::IsFinite(Requested)?FMath::Clamp(Requested,1.,120.):60.;
			Started=FPlatformTime::Seconds();
			Output=FPaths::ProjectSavedDir()/TEXT("Validation/MoverBuffers")/(FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".csv"));
			Rows=TEXT("WallSeconds,World,NetMode,WorldDeltaMS,SimulationMS,LatestReceiveMS,InterpolationMS,BufferedMS,ReceiveGapMS,ReceiveAdvanceMS,DesiredMS,MaxMS\n");
   bAuthorityRequested=Args.Contains(TEXT("AuthorityEncode"));
   bRawRequested=!bAuthorityRequested && (Args.Contains(TEXT("RawReceive")) || FParse::Param(FCommandLine::Get(),TEXT("ProjectJCaptureRawReceives")));

   RawClientRows=RawReceivedRows=AuthorityRows=AuthorityEncodedRows=0;
   if(bRawRequested || bAuthorityRequested)
   {
    FReplicationProxy::ResetModelWireThreadStatus();
    PreviousRawLevel=FReplicationProxy::ExchangeModelWireObservationLevel(ActorFilter.IsEmpty()?1:2);
   }
			ObjectRows=TEXT("WallSeconds,World,Actor,Role,NetHz,MinNetHz,PendingFrame,CachedAutonomousFrame,CachedSimulatedFrame,Dormancy\n");
   if(bRawRequested)
   {
    ObjectRows.RemoveFromEnd(TEXT("\n"));
    ObjectRows+=TEXT(",RawStatus,RawSimID,RawBindingEpoch,RawChannel,RawApplySerial,RawDecodeSeconds,RawDecodeEngineFrame,RawFrameKind,RawFrameValue,RawTimeKind,RawTimeMS,RawViewTimeMS,RawWorldInterpolationMS\n");
   }
   if(bRawRequested && !ActorFilter.IsEmpty())
   {
    ObjectRows.RemoveFromEnd(TEXT("\n"));
    ObjectRows+=TEXT(",ActorId,ObservationWorldSeconds,RawScalarBasis,RawScalarsValid,RawPosX,RawPosY,RawVelX,RawVelY,ConsumedValid,ConsumedPosX,ConsumedPosY,ConsumedVelX,ConsumedVelY,PresentationValid,PresentationPosX,PresentationPosY,PresentationVelX,PresentationVelY,ActorWorldX,ActorWorldY,PublishedWorldVelX,PublishedWorldVelY,ConsumedBaseResolved,PresentationBaseResolved\n");
   }
   if(bAuthorityRequested)
   {
    ObjectRows.RemoveFromEnd(TEXT("\n"));
    ObjectRows+=TEXT(",EncodeStatus,EncodeSimID,EncodeBindingEpoch,EncodeChannel,EncodeSerial,EncodeSeconds,EncodeEngineFrame,EncodeTimeKind,EncodeServerTimeMS\n");
   }
			// Reserve once; useful CSV characters stay below 8M TCHARs (16MiB on
			// Windows). Account for allocator capacity/terminators and source maps
			// with RetainedBytes(), bounded separately by 18MiB.
			Rows.Reserve(MaxWorldChars); ObjectRows.Reserve(MaxActorChars);
			Components.Reserve(MaxComponents); Received.Reserve(MaxWorlds);
			WorldRowCount=ActorRowCount=DiscoveryActorVisits=0;
			PeakComponents=PeakDiscoveryActorVisits=0; PeakRetainedBytes=RetainedBytes();
			NextDiscovery=0.;
			Handle=FCoreDelegates::OnEndFrame.AddRaw(this,&FMoverBufferCapture::Sample);
			PreExitHandle=FCoreDelegates::OnEnginePreExit.AddRaw(this,&FMoverBufferCapture::PreExit);
			UE_LOG(LogProject_J,Display,TEXT("MoverBufferCapture Started Seconds=%.1f File=%s"),Duration,*Output);
			CheckMemoryBudget();
		}
		void Sample()
		{
   const double Now=FPlatformTime::Seconds();
   if(!ActorFilter.IsEmpty() && !FReplicationProxy::IsModelWireScalarObservationEnabled()) { Stop(TEXT("DecodedScalarObservationDisabled"),false); return; }
   if((bRawRequested || bAuthorityRequested) && !FReplicationProxy::IsModelWireObservationEnabled()) { Stop(TEXT("ModelWireObservationDisabled"),false); return; }
   if((bRawRequested || bAuthorityRequested) && FReplicationProxy::HasUnsupportedModelWireThread()) { Stop(TEXT("UnsupportedModelWireThread"),false); return; }
			if(Now-Started>=Duration) { Stop(TEXT("Duration"),true); return; }
			if(!GEngine) return;
			PruneRetiredSources();
			const bool bDiscover=Now>=NextDiscovery;
			int32 DiscoveryVisits=0, DiscoveryLevels=0;
			for(const FWorldContext& Context:GEngine->GetWorldContexts())
			{
				auto* World=Context.World();
				if(!World || !World->IsGameWorld() || World->bIsTearingDown) continue;
				const auto* Manager=World->GetSubsystem<UNetworkPredictionWorldManager>();
				if(!Manager) continue;
				if(!Received.Contains(World) && Received.Num()>=MaxWorlds) { Stop(TEXT("WorldLimit"),false); return; }
				if(bDiscover)
				{
					// TActorIterator first collects every matching object. Visit the
					// existing level arrays directly so discovery itself has a bound.
					for(const auto* Level:World->GetLevels())
					{
						if(++DiscoveryLevels>MaxDiscoveryLevels) { Stop(TEXT("DiscoveryLevelLimit"),false); return; }
						if(!Level) continue;
						for(const auto& Actor:Level->Actors)
						{
							if(DiscoveryVisits>=MaxDiscoveryActorVisits) { Stop(TEXT("DiscoveryActorLimit"),false); return; }
							++DiscoveryVisits; ++DiscoveryActorVisits;
							PeakDiscoveryActorVisits=FMath::Max(PeakDiscoveryActorVisits,DiscoveryVisits);
							const auto* Pawn=Cast<APawn>(Actor.Get());
							if(!IsValid(Pawn) || Pawn->IsActorBeingDestroyed()) continue;
       if(!ActorFilter.IsEmpty() && !ActorFilter.Contains(ActorIdentity(Pawn))) continue;
							if(auto* Component=Pawn->FindComponentByClass<UNetworkPredictionComponent>();IsValid(Component) && !Component->IsBeingDestroyed() && Component->HasBeenInitialized() && Component->HasBegunPlay() && Component->IsRegistered())
							{
								if(!Components.Contains(Component) && Components.Num()>=MaxComponents) { Stop(TEXT("ComponentLimit"),false); return; }
								Components.Add(Component); PeakComponents=FMath::Max(PeakComponents,Components.Num());
							}
						}
					}
				}
				const auto& State=Manager->GetVariableTickState();
				const int32 Latest=State.Interpolation.LatestRecvTimeMS;
				const FString Name=World->GetPathName();
				auto& Receive=Received.FindOrAdd(World);
				const double Gap=Receive.ChangedAt>0.?(Now-Receive.ChangedAt)*1000.:0.;
				const int32 Advance=Latest-Receive.Latest;
				if(Advance) { Receive.Latest=Latest; Receive.ChangedAt=Now; }
				if(!AppendRow(Rows,FString::Printf(TEXT("%.6f,%s,%d,%.3f,%d,%d,%.3f,%.3f,%.3f,%d,%d,%d\n"),
					Now-Started,*Name,int32(World->GetNetMode()),World->GetDeltaSeconds()*1000.,
					State.Frames[State.PendingFrame].TotalMS,Latest,State.Interpolation.fTimeMS,
					Latest-State.Interpolation.fTimeMS,Gap,Advance,
					Manager->GetSettings().IndependentTickInterpolationBufferedMS,
					Manager->GetSettings().IndependentTickInterpolationMaxBufferedMS),MaxWorldChars,false)) return;
			}
   if(!ActorFilter.IsEmpty() && Received.Num()>1) { Stop(TEXT("FocusedWorldAmbiguous"),false); return; }
			if(bDiscover) NextDiscovery=Now+1.;
			static const auto* Autonomous=FindFProperty<FStructProperty>(UNetworkPredictionComponent::StaticClass(),TEXT("ReplicationProxy_Autonomous"));
			static const auto* Simulated=FindFProperty<FStructProperty>(UNetworkPredictionComponent::StaticClass(),TEXT("ReplicationProxy_Simulated"));
			static const auto* Prediction=FindFProperty<FStructProperty>(UNetworkPredictionComponent::StaticClass(),TEXT("NetworkPredictionProxy"));
   for(const auto& Weak:Components)
   {
    const auto* Component=Weak.Get();
    const auto* Owner=Component?Component->GetOwner():nullptr;
    if(!Owner || !Owner->GetWorld() || Owner->GetWorld()->bIsTearingDown) continue;
    FString Line=FString::Printf(TEXT("%.6f,%s,%s,%d,%.3f,%.3f,%d,%d,%d,%d"),Now-Started,
     *Owner->GetWorld()->GetPathName(),*Owner->GetName(),int32(Owner->GetLocalRole()),Owner->GetNetUpdateFrequency(),Owner->GetMinNetUpdateFrequency(),
     Prediction?Prediction->ContainerPtrToValuePtr<FNetworkPredictionProxy>(Component)->GetPendingFrame():-1,
     Autonomous?Autonomous->ContainerPtrToValuePtr<FReplicationProxy>(Component)->GetCachedPendingFrame():-1,
     Simulated?Simulated->ContainerPtrToValuePtr<FReplicationProxy>(Component)->GetCachedPendingFrame():-1,int32(Owner->NetDormancy));
    if(bAuthorityRequested)
    {
     const auto Mode=Owner->GetWorld()->GetNetMode();
     const bool bAuthority=Owner->GetLocalRole()==ROLE_Authority && (Mode==NM_DedicatedServer || Mode==NM_ListenServer);
     FNPModelWireSnapshot Encoded;
     if(bAuthority && Simulated) Encoded=Simulated->ContainerPtrToValuePtr<FReplicationProxy>(Component)->GetModelWireObservation();
     else Encoded.Status=ENPModelWireStatus::UnsupportedMode;
     const auto* Transport=Simulated?Simulated->ContainerPtrToValuePtr<FReplicationProxy>(Component):nullptr;
     if(!Transport || !Transport->NetSimProxy || Transport->NetSimProxy->GetCachedArchetype().TickingMode!=ENetworkPredictionTickingPolicy::Independent)
      Encoded.Status=ENPModelWireStatus::UnsupportedMode;
     if(Encoded.Status==ENPModelWireStatus::AuthorityEncoded && Encoded.EventSeconds<Started) Encoded.Status=ENPModelWireStatus::BeforeCapture;
     const bool bSupported=Encoded.Status==ENPModelWireStatus::AuthorityEncoded || Encoded.Status==ENPModelWireStatus::AwaitingEncode || Encoded.Status==ENPModelWireStatus::BeforeCapture;
     Line+=FString::Printf(TEXT(",%s,%d,%llu,SP,%llu,%.9f,%llu,%d,%d\n"),ReceiveStatusName(Encoded.Status),Encoded.SimID,Encoded.BindingEpoch,
      Encoded.EventSerial,Encoded.EventSeconds,Encoded.EventEngineFrame,int32(Encoded.TimeKind),Encoded.TimeMS);
     if(!AppendRow(ObjectRows,Line,MaxActorChars,true,false,false,bAuthority,Encoded.Status==ENPModelWireStatus::AuthorityEncoded)) return;
     if(!bSupported) { Stop(TEXT("UnsupportedAuthorityEncodeSource"),false); return; }
    }
    else if(bRawRequested)
    {
     const bool bClient=Owner->GetWorld()->GetNetMode()==NM_Client;
     const bool bAP=Owner->GetLocalRole()==ROLE_AutonomousProxy;
     const auto* Property=bAP?Autonomous:Simulated;
     FNPModelWireSnapshot Raw;
     if(bClient && Property) Raw=Property->ContainerPtrToValuePtr<FReplicationProxy>(Component)->GetModelWireObservation();
     if(!bClient) Raw.Status=ENPModelWireStatus::NotClient;
     if(Raw.Status==ENPModelWireStatus::ReceiveApplied && Raw.EventSeconds<Started) Raw.Status=ENPModelWireStatus::BeforeCapture;
     const bool bSupported=Raw.Status==ENPModelWireStatus::ReceiveApplied || Raw.Status==ENPModelWireStatus::AwaitingReceive
      || Raw.Status==ENPModelWireStatus::BeforeCapture || Raw.Status==ENPModelWireStatus::NotClient;
     Line+=FString::Printf(TEXT(",%s,%d,%llu,%s,%llu,%.9f,%llu,%d,%d,%d,%d,%d,%.3f"),ReceiveStatusName(Raw.Status),Raw.SimID,Raw.BindingEpoch,
      bClient?(bAP?TEXT("AP"):TEXT("SP")):TEXT("None"),Raw.EventSerial,Raw.EventSeconds,Raw.EventEngineFrame,
      int32(Raw.FrameKind),Raw.FrameValue,int32(Raw.TimeKind),Raw.TimeMS,
      Prediction?Prediction->ContainerPtrToValuePtr<FNetworkPredictionProxy>(Component)->GetTotalSimTimeMS():-1,
      Owner->GetWorld()->GetSubsystem<UNetworkPredictionWorldManager>()?Owner->GetWorld()->GetSubsystem<UNetworkPredictionWorldManager>()->GetVariableTickState().Interpolation.fTimeMS:-1.);
     if(!ActorFilter.IsEmpty())
     {
      const TStrongObjectPtr<UNetworkPredictionComponent> KeepComponent(Weak.Get());
      const TStrongObjectPtr<AActor> KeepOwner(KeepComponent.IsValid()?KeepComponent->GetOwner():nullptr);
      const TStrongObjectPtr<UWorld> KeepWorld(KeepOwner.IsValid()?KeepOwner->GetWorld():nullptr);
      const uint64 ActorId=ActorIdentity(KeepOwner.Get());
      const int32 FilterIndex=ActorFilter.IndexOfByKey(ActorId);
      if(!KeepWorld.IsValid() || KeepWorld->bIsTearingDown || KeepOwner.Get()!=Owner || FilterIndex==INDEX_NONE ||
       !bClient || Owner->GetLocalRole()!=ROLE_SimulatedProxy || !Cast<UMoverNetworkPredictionLiaisonComponent>(KeepComponent.Get()) || !Prediction)
      { Stop(TEXT("UnsupportedFocusedSource"),false); return; }
      auto* View=Prediction->ContainerPtrToValuePtr<FNetworkPredictionProxy>(KeepComponent.Get());
      if(View->GetCachedArchetype().TickingMode!=ENetworkPredictionTickingPolicy::Independent)
      { Stop(TEXT("UnsupportedFocusedMode"),false); return; }
      const auto* Consumed=View->ReadSyncState<FMoverSyncState>(ENetworkPredictionStateRead::Simulation);
      const auto* Presentation=View->ReadSyncState<FMoverSyncState>(ENetworkPredictionStateRead::Presentation);
      float ConsumedValues[4], PresentationValues[4];
      for(int32 I=0;I<4;++I) ConsumedValues[I]=PresentationValues[I]=std::numeric_limits<float>::quiet_NaN();
      const bool bConsumed=ReadStoredScalars(Consumed,ConsumedValues), bPresentation=ReadStoredScalars(Presentation,PresentationValues);
      bool bRawValues=true; for(float Value:Raw.ModelScalars) bRawValues&=FMath::IsFinite(Value);
      const auto* ConsumedData=Consumed?Consumed->SyncStateCollection.FindDataByType<FMoverDefaultSyncState>():nullptr;
      const auto* PresentationData=Presentation?Presentation->SyncStateCollection.FindDataByType<FMoverDefaultSyncState>():nullptr;
      const bool bConsumedBaseResolved=ConsumedData&&ConsumedData->GetMovementBase()!=nullptr;
      const bool bPresentationBaseResolved=PresentationData&&PresentationData->GetMovementBase()!=nullptr;
      const double SampleStarted=Started;
      const FDelegateHandle SampleHandle=Handle;
      const FVector Position=Owner->GetActorLocation(), Velocity=Owner->GetVelocity();
      // A virtual query may synchronously Stop/Start this same static capturer.
      // Do not append into, or retire, the replacement session or its TSet iterator.
      if(!Handle.IsValid() || Handle!=SampleHandle || Started!=SampleStarted) return;
      if(!IsValid(KeepComponent.Get()) || !IsValid(KeepOwner.Get()) || KeepOwner->IsActorBeingDestroyed() || KeepComponent->IsBeingDestroyed() ||
       !KeepComponent->HasBeenInitialized() || !KeepComponent->HasBegunPlay() || !KeepComponent->IsRegistered() ||
       KeepOwner->GetLocalRole()!=ROLE_SimulatedProxy || KeepComponent->GetOwner()!=KeepOwner.Get() ||
       KeepComponent->GetWorld()!=KeepWorld.Get() || KeepOwner->GetWorld()!=KeepWorld.Get() || KeepWorld->bIsTearingDown || ActorIdentity(KeepOwner.Get())!=ActorId)
      { Stop(TEXT("FocusedBindingChanged"),false); return; }
      const auto After=Property->ContainerPtrToValuePtr<FReplicationProxy>(KeepComponent.Get())->GetModelWireObservation();
      if(After.BindingEpoch!=Raw.BindingEpoch || After.EventSerial!=Raw.EventSerial ||
       After.SimID!=Raw.SimID || After.Role!=Raw.Role || After.Status==ENPModelWireStatus::StaleBinding)
      { Stop(TEXT("FocusedBindingChanged"),false); return; }
      if(Raw.Status==ENPModelWireStatus::ReceiveApplied && !bRawValues)
      { Stop(TEXT("DecodedScalarsUnavailable"),false); return; }
      if(Raw.Status==ENPModelWireStatus::ReceiveApplied) FocusedSeenMask|=uint8(1u<<FilterIndex);
      Line+=FString::Printf(TEXT(",%llu,%.9f,MoverStoredBaseOrWorldXYVelocityXYFloat32,%d,%.9g,%.9g,%.9g,%.9g,%d,%.9g,%.9g,%.9g,%.9g,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%d"),
       ActorId,KeepWorld->GetTimeSeconds(),bRawValues,Raw.ModelScalars[0],Raw.ModelScalars[1],Raw.ModelScalars[2],Raw.ModelScalars[3],
       bConsumed,ConsumedValues[0],ConsumedValues[1],ConsumedValues[2],ConsumedValues[3],
       bPresentation,PresentationValues[0],PresentationValues[1],PresentationValues[2],PresentationValues[3],
       Position.X,Position.Y,Velocity.X,Velocity.Y,bConsumedBaseResolved,bPresentationBaseResolved);
     }
     Line+=TEXT("\n");
     if(!AppendRow(ObjectRows,Line,MaxActorChars,true,bClient,Raw.Status==ENPModelWireStatus::ReceiveApplied)) return;
     if(!bSupported) { Stop(TEXT("UnsupportedReceiveSource"),false); return; }
    }
    else if(!AppendRow(ObjectRows,Line+TEXT("\n"),MaxActorChars,true)) return;
   }
		}
	};
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMoverBufferCaptureBoundsTest,"ProjectJ.Foundation.Network.MoverCaptureBounds",
		EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
	bool FMoverBufferCaptureBoundsTest::RunTest(const FString&)
	{
		auto ReadSummary=[this](const FString& Output,TSharedPtr<FJsonObject>& Result)
		{
			FString Json;
			return TestTrue(TEXT("Capture writes its completion metadata"),FFileHelper::LoadFileToString(Json,*(FPaths::ChangeExtension(Output,TEXT("summary.json")))))
				&& TestTrue(TEXT("Completion metadata is valid JSON"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Result));
		};
		FMoverBufferCapture Capture;
		Capture.Start({TEXT("120")});
		const FString PartialOutput=Capture.Output;
		TestTrue(TEXT("Capture registers both frame and early engine shutdown leases"),Capture.Handle.IsValid()&&Capture.PreExitHandle.IsValid());
		TestTrue(TEXT("Actual reserved capacities and metadata stay within the retained memory budget"),Capture.RetainedBytes()<=FMoverBufferCapture::MaxRetainedBytes);
		const FString Row=FString::ChrN(FMoverBufferCapture::MaxRowChars-1,TEXT('x'))+TEXT("\n");
		while(Capture.Handle.IsValid()) Capture.AppendRow(Capture.Rows,Row,FMoverBufferCapture::MaxWorldChars,false);
		TSharedPtr<FJsonObject> Partial;
		if(!ReadSummary(PartialOutput,Partial)) return false;
		TestTrue(TEXT("A full buffer is explicitly partial"),Partial->GetBoolField(TEXT("truncated"))&&!Partial->GetBoolField(TEXT("complete")));
		TestEqual(TEXT("Buffer exhaustion has an exact stop reason"),Partial->GetStringField(TEXT("stop_reason")),FString(TEXT("WorldBufferLimit")));
		TestEqual(TEXT("Metadata preserves the actual accepted row count"),Partial->GetNumberField(TEXT("world_rows")),static_cast<double>(Capture.WorldRowCount));
		TestTrue(TEXT("No over-limit row was appended"),Capture.WorldRowCount*Row.Len()<FMoverBufferCapture::MaxWorldChars);
		TestFalse(TEXT("Budget stop releases the frame delegate"),Capture.Handle.IsValid());
		TestFalse(TEXT("Budget stop releases the shutdown delegate"),Capture.PreExitHandle.IsValid());
		TestEqual(TEXT("Budget stop returns both large CSV buffers"),Capture.Rows.GetAllocatedSize()+Capture.ObjectRows.GetAllocatedSize(),SIZE_T(0));

		Capture.Start({TEXT("120")});
		const FString NormalOutput=Capture.Output;
		TestTrue(TEXT("A world row is accepted"),Capture.AppendRow(Capture.Rows,TEXT("0,Fixture,3,16,100,200,100,100,0,16,100,300\n"),FMoverBufferCapture::MaxWorldChars,false));
		TestTrue(TEXT("An actor row is accepted"),Capture.AppendRow(Capture.ObjectRows,TEXT("0,Fixture,Pawn,2,30,2,1,1,1,0\n"),FMoverBufferCapture::MaxActorChars,true));
		// An unowned/retired source cannot occupy capacity across discovery passes.
		Capture.Components.Add(NewObject<UMoverNetworkPredictionLiaisonComponent>());
		Capture.PruneRetiredSources();
		TestTrue(TEXT("Unowned components are removed"),Capture.Components.IsEmpty());
		Capture.PreExit();
		TSharedPtr<FJsonObject> Normal;
		if(!ReadSummary(NormalOutput,Normal)) return false;
		TestTrue(TEXT("Normal engine shutdown preserves a complete observed interval"),Normal->GetBoolField(TEXT("complete"))&&!Normal->GetBoolField(TEXT("truncated")));
		TestEqual(TEXT("Shutdown is distinguishable from requested duration"),Normal->GetStringField(TEXT("stop_reason")),FString(TEXT("EnginePreExit")));
		TestEqual(TEXT("New capture does not inherit the previous row count"),Normal->GetNumberField(TEXT("world_rows")),1.);
		TestEqual(TEXT("Actor row count is exact"),Normal->GetNumberField(TEXT("actor_rows")),1.);
		TestTrue(TEXT("All source and delegate leases are released"),Capture.Components.IsEmpty()&&Capture.Received.IsEmpty()&&!Capture.Handle.IsValid()&&!Capture.PreExitHandle.IsValid());
		Capture.Stop(); // Repeated shutdown is harmless and does not rewrite files.

  static_assert(std::is_copy_constructible_v<FReplicationProxy> && std::is_copy_assignable_v<FReplicationProxy>);
  TestEqual(TEXT("Fixed per-proxy diagnostic storage is64bytes"),sizeof(FNPModelWireSnapshot),SIZE_T(64));
  TArray<uint64,TInlineAllocator<3>> Filter;
  TestTrue(TEXT("Three uint64 identities fit the fixed actor filter"),FMoverBufferCapture::ParseActorFilter({TEXT("120"),TEXT("RawReceive"),TEXT("ActorIds=1,2,18446744073709551615")},Filter) && Filter.Num()==3);
  TestFalse(TEXT("A fourth identity is rejected before allocation"),FMoverBufferCapture::ParseActorFilter({TEXT("ActorIds=1,2,3,4")},Filter));
  TestFalse(TEXT("Duplicate identities are rejected"),FMoverBufferCapture::ParseActorFilter({TEXT("ActorIds=1,1")},Filter));
  TestFalse(TEXT("Overflow is rejected rather than wrapped"),FMoverBufferCapture::ParseActorFilter({TEXT("ActorIds=18446744073709551616")},Filter));
  TestFalse(TEXT("Negative or empty IDs are rejected"),FMoverBufferCapture::ParseActorFilter({TEXT("ActorIds=-1")},Filter));
  const bool bEnabledBefore=FReplicationProxy::IsModelWireObservationEnabled();
  FNetworkPredictionProxy PredictionProxy;
  PredictionProxy.GetID()=FNetworkPredictionID(71);
  PredictionProxy.InitForNetworkRole(ROLE_SimulatedProxy,false);
  FReplicationProxy Proxy;
  Proxy.Init(&PredictionProxy,EReplicationProxyTarget::SimulatedProxy);
  const uint64 Epoch=Proxy.GetModelWireObservationEpoch();
  Proxy.RecordModelWireReceive(Epoch,ENPModelWireFrameKind::None,88,ENPModelWireTimeKind::ServerVariableMS,1234);
  TestTrue(TEXT("Unsupported scalar projection is NaN rather than zero"),!FMath::IsFinite(Proxy.GetModelWireObservation().ModelScalars[0]));
  const float Decoded[4]={1.25f,-2.5f,-499.f,7.f};
  Proxy.RecordModelWireReceive(Epoch,ENPModelWireFrameKind::None,88,ENPModelWireTimeKind::ServerVariableMS,1235,Decoded);
  const auto SP=Proxy.GetModelWireObservation();
  TestEqual(TEXT("Decode values share the same committed serial"),SP.EventSerial,uint64(2));
  for(int32 I=0;I<4;++I) TestEqual(TEXT("Decoded scalar projection is copied as a value"),SP.ModelScalars[I],Decoded[I]);
  TestEqual(TEXT("Independent SP has no serialized server frame"),SP.FrameValue,INDEX_NONE);
  TestEqual(TEXT("SP clock is explicitly server-variable time"),uint8(SP.TimeKind),uint8(ENPModelWireTimeKind::ServerVariableMS));
  FReplicationProxy Shadow=Proxy;
  FReplicationProxy Assigned; Assigned=Shadow;
  Proxy.ResetModelWireObservation(71,ROLE_SimulatedProxy);
  TestEqual(TEXT("Iris-style copy remains independent of later binding reset"),Assigned.GetModelWireObservation().EventSerial,uint64(2));
  Proxy.RecordModelWireReceive(Epoch,ENPModelWireFrameKind::ServerFrame,99,ENPModelWireTimeKind::None,0);
  TestEqual(TEXT("An old binding callback cannot write a new epoch"),Proxy.GetModelWireObservation().EventSerial,uint64(0));
  TestTrue(TEXT("Binding reset invalidates former coordinates"),!FMath::IsFinite(Proxy.GetModelWireObservation().ModelScalars[0]));
  TestEqual(TEXT("Shadow copies keep decoded values across a live reset"),Assigned.GetModelWireObservation().ModelScalars[2],-499.f);
  PredictionProxy.InitForNetworkRole(ROLE_AutonomousProxy,true);
  Proxy.ResetModelWireObservation(71,ROLE_AutonomousProxy);
  Proxy.RecordModelWireReceive(Proxy.GetModelWireObservationEpoch(),ENPModelWireFrameKind::ClientInputAck,42,ENPModelWireTimeKind::IndependentSimulationMS,321);
  TestEqual(TEXT("AP metadata preserves input-ack clock distinction"),uint8(Proxy.GetModelWireObservation().FrameKind),uint8(ENPModelWireFrameKind::ClientInputAck));
  PredictionProxy.GetID()=FNetworkPredictionID(72);
  TestEqual(TEXT("ID remap without a matching binding is explicitly stale"),uint8(Proxy.GetModelWireObservation().Status),uint8(ENPModelWireStatus::StaleBinding));
  PredictionProxy.GetID()=FNetworkPredictionID(71);
  PredictionProxy.InitForNetworkRole(ROLE_Authority,false);
  Proxy.ResetModelWireObservation(71,ROLE_Authority);
  const uint64 AuthorityEpoch=Proxy.GetModelWireObservationEpoch();
  Proxy.RecordAuthorityModelEncode(AuthorityEpoch,4321);
  const auto Encode=Proxy.GetModelWireObservation();
  TestEqual(TEXT("Authority has an explicit model encode status"),uint8(Encode.Status),uint8(ENPModelWireStatus::AuthorityEncoded));
  TestEqual(TEXT("Authority SP encode carries server-variable time and no wire frame"),Encode.TimeMS,4321);
  TestEqual(TEXT("Authority encode is not a guessed server frame"),Encode.FrameValue,INDEX_NONE);
  Proxy.RecordModelWireReceive(AuthorityEpoch,ENPModelWireFrameKind::None,0,ENPModelWireTimeKind::ServerVariableMS,9999);
  TestEqual(TEXT("Client receive cannot overwrite an authority encode"),Proxy.GetModelWireObservation().EventSerial,uint64(1));
  Proxy.ResetModelWireObservation(71,ROLE_Authority);
  Proxy.RecordAuthorityModelEncode(AuthorityEpoch,9999);
  TestEqual(TEXT("An old encoder callback cannot cross a binding reset"),Proxy.GetModelWireObservation().EventSerial,uint64(0));
  Proxy.SetModelWireStatus(Proxy.GetModelWireObservationEpoch(),ENPModelWireStatus::Overflow);
  Proxy.RecordAuthorityModelEncode(Proxy.GetModelWireObservationEpoch(),9999);
  TestEqual(TEXT("A saturated diagnostic cannot resume silently"),uint8(Proxy.GetModelWireObservation().Status),uint8(ENPModelWireStatus::Overflow));
  const int32 PriorLevel=FReplicationProxy::ExchangeModelWireObservationLevel(2);
  Capture.Start({TEXT("120"),TEXT("RawReceive")});
  TestFalse(TEXT("Existing raw capture stays metadata-only"),FReplicationProxy::IsModelWireScalarObservationEnabled());
  const FString UnsupportedOutput=Capture.Output;
  TestTrue(TEXT("Only explicitly requested capture arms raw observation"),FReplicationProxy::IsModelWireObservationEnabled());
  Async(EAsyncExecution::Thread,[]{ FReplicationProxy::ReportUnsupportedModelWireThread(); }).Wait();
  Capture.Sample();
  TSharedPtr<FJsonObject> Unsupported;
  if(!ReadSummary(UnsupportedOutput,Unsupported)) return false;
  TestEqual(TEXT("All tracked fixed snapshots have an explicit separate storage bound"),Unsupported->GetNumberField(TEXT("raw_receive_max_tracked_snapshot_bytes")),double(sizeof(FNPModelWireSnapshot)*4*FMoverBufferCapture::MaxComponents));
  TestFalse(TEXT("Unsupported writer thread is an explicit partial result"),Unsupported->GetBoolField(TEXT("complete")));
  TestEqual(TEXT("Thread capability failure is not raw zero"),Unsupported->GetStringField(TEXT("stop_reason")),FString(TEXT("UnsupportedModelWireThread")));
  TestTrue(TEXT("Capture restores an exact previous scalar level"),FReplicationProxy::IsModelWireScalarObservationEnabled());
  FReplicationProxy::ExchangeModelWireObservationLevel(PriorLevel);
  TestEqual(TEXT("Raw capture restores its diagnostic optin"),FReplicationProxy::IsModelWireObservationEnabled(),bEnabledBefore);
  TestFalse(TEXT("Capability failure releases both capture leases"),Capture.Handle.IsValid()||Capture.PreExitHandle.IsValid());
  FReplicationProxy::ResetModelWireThreadStatus();
  Capture.Start({TEXT("120"),TEXT("AuthorityEncode")});
  const FString AuthorityOutput=Capture.Output;
  TestTrue(TEXT("Authority capture has a separate typed header"),Capture.ObjectRows.Contains(TEXT("EncodeServerTimeMS")) && !Capture.ObjectRows.Contains(TEXT("RawApplySerial")));
  TestTrue(TEXT("Authority accepted-row counters share the existing bounded append"),Capture.AppendRow(Capture.ObjectRows,TEXT("0,Fixture,Pawn,3,10,2,5,5,5,1,AuthorityEncoded,71,4,SP,1,1,1,1,4321\n"),FMoverBufferCapture::MaxActorChars,true,false,false,true,true));
  Capture.PreExit();
  TSharedPtr<FJsonObject> AuthoritySummary;
  if(!ReadSummary(AuthorityOutput,AuthoritySummary)) return false;
  TestTrue(TEXT("Authority observational completeness is distinct from client receive"),AuthoritySummary->GetBoolField(TEXT("complete")) && AuthoritySummary->GetBoolField(TEXT("authority_encode_requested")) && !AuthoritySummary->GetBoolField(TEXT("raw_receive_requested")));
  TestEqual(TEXT("Authority receipt counts actual encoded rows"),AuthoritySummary->GetNumberField(TEXT("authority_encoded_rows")),1.);
  TestEqual(TEXT("Authority capture restores its diagnostic value"),FReplicationProxy::IsModelWireObservationEnabled(),bEnabledBefore);
		return true;
	}
#endif
	static FMoverBufferCapture MoverBufferCapture;
	static FAutoConsoleCommand CaptureMoverBuffersCommand(
		TEXT("ProjectJ.CaptureMoverBuffers"),TEXT("Bounded read-only Mover CSV capture with completion metadata. Optional seconds (1-120, default60), then RawReceive or AuthorityEncode for typed model-wire metadata. RawReceive may use ActorIds=id1,id2,id3 (max3 SPs); Stop closes the existing capture."),
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
