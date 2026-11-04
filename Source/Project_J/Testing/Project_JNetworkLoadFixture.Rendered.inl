#include "GameFramework/GameStateBase.h"
#include "Net/NetAnalyticsTypes.h"
#include "Serialization/JsonReader.h"
#include "Misc/SecureHash.h"
#include "Misc/App.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PawnMovementComponent.h"

// Optional real-role render cohort in the existing AOI/socket owner. The static
// AOI probes remain counters. Only the authored Avatars below execute movement.
namespace
{
constexpr int32 RenderedPopulation=200;
constexpr int32 RenderedAIPlayerIdBase=10000;
const TCHAR* RenderedAvatarClass=TEXT("/Game/Character_BPs/Spellblade/BP_Spellblade.BP_Spellblade_C");
FVector RenderedGrid(int32 Index) { return FVector((Index%20)*180,(Index/20)*200,100); }
int32 RenderedIndex(const AProject_JPlayerState* State)
{
    if (!IsValid(State)) return INDEX_NONE;
    const FName Id=State->GetPublicClassId();
    if (Id==TEXT("DClient0")) return 0;
    if (Id==TEXT("DClient1")) return 1;
    const int32 PlayerId=State->GetPlayerId();
    return PlayerId>=RenderedAIPlayerIdBase+2 && PlayerId<RenderedAIPlayerIdBase+RenderedPopulation ?
        PlayerId-RenderedAIPlayerIdBase:INDEX_NONE;
}
uint64 RenderedPublicId(const AActor* Actor)
{
    const auto* Bridge=Actor ? UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(Actor):nullptr;
    return Bridge ? Bridge->GetReplicatedRefHandle(Actor).GetId():0;
}

// Local, opt-in diagnosis only. The phase3 observer writes its selection before
// acknowledging phase3 through the existing control RPC; server phase4 starts
// only after both acknowledgements. No additional wire traffic or frame scan.
bool ReadRenderedProbeJson(const FString& Path, TSharedPtr<FJsonObject>& Object, FString& Fingerprint, int64& Size)
{
    TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path, FILEREAD_Silent));
    Size=Reader ? Reader->TotalSize():-1;
    if (Size<=0 || Size>4096) return false;
    TArray<uint8,TInlineAllocator<4097>> Bytes;
    Bytes.SetNumUninitialized(int32(Size)+1);
    Reader->Serialize(Bytes.GetData(),Size);
    if (Reader->IsError()) return false;
    for (int32 I=0;I<Size;++I) if (!Bytes[I]) return false;
    Bytes[int32(Size)]=0;
    FSHAHash Hash;
    FSHA1::HashBuffer(Bytes.GetData(),uint32(Size),Hash.Hash);
    Fingerprint=Hash.ToString();
    const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()),int32(Size));
    return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text.Length(),Text.Get())),Object) && Object.IsValid();
}

bool ReadRenderedProbeId(const FString& Text,uint64& Id)
{
    Id=0;
    if (Text.IsEmpty() || Text.Len()>20) return false;
    for (const TCHAR Character:Text)
    {
        if (Character<TEXT('0') || Character>TEXT('9')) return false;
        const uint64 Digit=uint64(Character-TEXT('0'));
        if (Id>(MAX_uint64-Digit)/10) return false;
        Id=Id*10+Digit;
    }
    return Id!=0;
}

void ProbeRenderedIrisSelection(UWorld* World,const FString& ServerOutput,
    TConstArrayView<TObjectPtr<AProject_JPlayerCharacter>> Cohort,
    TConstArrayView<TObjectPtr<AProject_JNetworkLoadController>> Owners,uint64 CohortRevision,bool bStart)
{
    using namespace UE::Net;
    if (!RenderedClientEnabled() || !FParse::Param(FCommandLine::Get(),TEXT("ProjectJRenderedDecodedEndpoints"))) return;
    check(IsInGameThread());
    const TCHAR* Boundary=bStart ? TEXT("start"):TEXT("end");
    const FString Destination=ServerOutput/FString::Printf(TEXT("iris-selected-phase4-%s.json"),Boundary);
    // The two explicit stage boundaries are the only callers. Preserve any
    // existing receipt rather than silently repeating a read/print on reentry.
    if (IFileManager::Get().FileExists(*Destination)) return;
    const auto Receipt=MakeShared<FJsonObject>();
    Receipt->SetStringField(TEXT("boundary"),Boundary);
    Receipt->SetNumberField(TEXT("phase"),4);
    Receipt->SetStringField(TEXT("cohort_revision"),LexToString(CohortRevision));
    Receipt->SetNumberField(TEXT("sample_qpc_seconds"),FPlatformTime::Seconds());
    Receipt->SetNumberField(TEXT("sample_world_seconds"),World ? World->GetTimeSeconds():-1.);
    Receipt->SetNumberField(TEXT("selection_max_bytes"),4096);
    Receipt->SetNumberField(TEXT("objects_printed"),0);
    Receipt->SetBoolField(TEXT("additional_rpc"),false);
    Receipt->SetBoolField(TEXT("protocol_state_dumped"),false);
    const auto Finish=[&](bool bComplete,const TCHAR* Reason)
    {
        Receipt->SetBoolField(TEXT("complete"),bComplete);
        Receipt->SetStringField(TEXT("reason"),Reason);
        FString Json;
        const bool bSerialized=FJsonSerializer::Serialize(Receipt,TJsonWriterFactory<>::Create(&Json));
        const bool bSaved=bSerialized && FFileHelper::SaveStringToFile(Json,*Destination,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogProjectJDNetwork,Display,TEXT("RENDERED_IRIS_PROBE boundary=%s complete=%d saved=%d reason=%s"),Boundary,bComplete,bSaved,Reason);
    };
    if (!IsValid(World) || World->bIsTearingDown || World->GetNetMode()==NM_Client || Cohort.Num()!=RenderedPopulation || Owners.Num()!=2)
    { Finish(false,TEXT("UnsupportedWorldOrCohort")); return; }
    const TStrongObjectPtr<UWorld> KeepWorld(World);
    FString Output=FPaths::ConvertRelativePathToFull(ServerOutput);
    FPaths::NormalizeDirectoryName(Output);
    if (FPaths::GetCleanFilename(Output)!=TEXT("Server")) { Finish(false,TEXT("UnexpectedRunOutput")); return; }
    const FString SelectionPath=FPaths::GetPath(Output)/TEXT("Client1/Rendered/decoded-endpoint-selection.json");
    TSharedPtr<FJsonObject> Selection;
    FString SelectionHash;
    int64 SelectionBytes=0;
    if (!ReadRenderedProbeJson(SelectionPath,Selection,SelectionHash,SelectionBytes))
    { Finish(false,TEXT("SelectionMissingOrInvalid")); return; }
    Receipt->SetNumberField(TEXT("selection_bytes"),double(SelectionBytes));
    Receipt->SetStringField(TEXT("selection_sha1"),SelectionHash);
    FString ObserverText;
    uint64 ObserverId=0;
    const TArray<TSharedPtr<FJsonValue>>* Selected=nullptr;
    double SelectionPhase=0., ObservationPhase=0.;
    if (!Selection->TryGetStringField(TEXT("observer_pawn_actor_id"),ObserverText) || !ReadRenderedProbeId(ObserverText,ObserverId)
        || !Selection->TryGetNumberField(TEXT("selection_phase"),SelectionPhase) || SelectionPhase!=3.
        || !Selection->TryGetNumberField(TEXT("observation_phase"),ObservationPhase) || ObservationPhase!=4.
        || !Selection->TryGetArrayField(TEXT("selected"),Selected) || Selected->IsEmpty() || Selected->Num()>3)
    { Finish(false,TEXT("SelectionContractInvalid")); return; }
    auto* Driver=World->GetNetDriver();
    auto* System=Driver ? Driver->GetReplicationSystem():nullptr;
    auto* Bridge=System ? Cast<UEngineReplicationBridge>(System->GetReplicationBridge()):nullptr;
    if (!System || !System->IsServer() || !Bridge) { Finish(false,TEXT("ServerBridgeUnavailable")); return; }
    const TStrongObjectPtr<UEngineReplicationBridge> KeepBridge(Bridge);
    AProject_JNetworkLoadController* Observer=nullptr;
    int32 ObserverIndex=INDEX_NONE;
    for (int32 Index=0;Index<Owners.Num();++Index)
    {
        auto* PC=Owners[Index].Get();
        if (IsValid(PC) && PC->GetPawn()==Cohort[Index].Get() && RenderedPublicId(PC->GetPawn())==ObserverId)
        { if (Observer) { Finish(false,TEXT("ObserverAmbiguous")); return; } Observer=PC; ObserverIndex=Index; }
    }
    const TStrongObjectPtr<AProject_JNetworkLoadController> KeepObserver(Observer);
    const TStrongObjectPtr<AProject_JPlayerCharacter> ObserverPawn(Observer ? Cast<AProject_JPlayerCharacter>(Observer->GetPawn()):nullptr);
    const TStrongObjectPtr<UNetConnection> Connection(Observer ? Observer->GetNetConnection():nullptr);
    const FConnectionHandle ConnectionHandle=Connection.IsValid() ? Connection->GetConnectionHandle():FConnectionHandle();
    const uint32 ConnectionId=ConnectionHandle.GetParentConnectionId();
    if (!IsValid(Observer) || Observer->IsActorBeingDestroyed() || Observer->GetWorld()!=World || Observer->FixtureSlot!=ObserverIndex
        || !Connection.IsValid() || Connection->GetDriver()!=Driver || !Driver->ClientConnections.Contains(Connection.Get())
        || !ConnectionHandle.IsValid() || !ConnectionHandle.IsParentConnection() || !System->IsValidConnection(ConnectionId)
        || !ObserverPawn.IsValid() || ObserverPawn->IsActorBeingDestroyed() || ObserverPawn->GetWorld()!=World || !ObserverPawn->HasAuthority()
        || ObserverPawn.Get()!=Observer->GetPawn() || FReplicationSystemUtil::GetActorReplicationBridge(ObserverPawn.Get())!=Bridge
        || !Bridge->GetReplicatedRefHandle(ObserverPawn.Get()).IsCompleteHandle() || RenderedPublicId(ObserverPawn.Get())!=ObserverId
        || Bridge->GetReplicatedObject(Bridge->GetReplicatedRefHandle(ObserverPawn.Get()))!=ObserverPawn.Get()
        || System->GetOwningNetConnection(Bridge->GetReplicatedRefHandle(ObserverPawn.Get()))!=ConnectionId)
    { Finish(false,TEXT("ObserverConnectionInvalid")); return; }
    TArray<FNetRefHandle,TInlineAllocator<3>> Handles;
    TArray<TStrongObjectPtr<AProject_JPlayerCharacter>,TInlineAllocator<3>> Players;
    TArray<TSharedPtr<FJsonValue>> IdRows;
    TArray<TSharedPtr<FJsonValue>> CanonicalRows;
    TSet<uint64> Seen;
    for (const auto& Value:*Selected)
    {
        const auto Entry=Value.IsValid() && Value->Type==EJson::Object ? Value->AsObject():nullptr;
        FString IdText;
        double SampleIndex=-1.;
        uint64 Id=0;
        if (!Entry || !Entry->TryGetStringField(TEXT("actor_id"),IdText) || !ReadRenderedProbeId(IdText,Id) || Id==ObserverId || Seen.Contains(Id)
            || !Entry->TryGetNumberField(TEXT("sample_index"),SampleIndex) || !FMath::IsFinite(SampleIndex)
            || SampleIndex<1. || SampleIndex>=RenderedPopulation || SampleIndex!=FMath::FloorToDouble(SampleIndex))
        { Finish(false,TEXT("SelectedIdentityInvalid")); return; }
        // The observer sampler swaps only its actual owner with canonical0.
        const int32 CanonicalIndex=int32(SampleIndex)==ObserverIndex ? 0:int32(SampleIndex);
        auto* Player=Cohort[CanonicalIndex].Get();
        const FNetRefHandle Handle=IsValid(Player) ? Bridge->GetReplicatedRefHandle(Player):FNetRefHandle();
        if (!IsValid(Player) || Player->IsActorBeingDestroyed() || Player->GetWorld()!=World || !Player->HasAuthority()
            || FReplicationSystemUtil::GetActorReplicationBridge(Player)!=Bridge || !Handle.IsCompleteHandle() || Handle.GetId()!=Id
            || Bridge->GetReplicatedObject(Handle)!=Player || !Player->GetPlayerState<AProject_JPlayerState>()
            || RenderedIndex(Player->GetPlayerState<AProject_JPlayerState>())!=CanonicalIndex)
        { Finish(false,TEXT("SelectedLiveBindingInvalid")); return; }
        Seen.Add(Id); Handles.Add(Handle); Players.Emplace(Player);
        IdRows.Add(MakeShared<FJsonValueString>(LexToString(Id)));
        CanonicalRows.Add(MakeShared<FJsonValueNumber>(CanonicalIndex));
    }
    Receipt->SetStringField(TEXT("observer_pawn_actor_id"),LexToString(ObserverId));
    Receipt->SetNumberField(TEXT("observer_slot"),ObserverIndex);
    Receipt->SetNumberField(TEXT("connection_id"),ConnectionId);
    Receipt->SetNumberField(TEXT("replication_system_id"),System->GetId());
    Receipt->SetArrayField(TEXT("selected_actor_ids"),IdRows);
    Receipt->SetArrayField(TEXT("selected_canonical_indices"),CanonicalRows);
    if (!bStart)
    {
        TSharedPtr<FJsonObject> Start;
        FString StartHash, Text;
        int64 StartBytes=0;
        double StartConnection=-1.,StartSystem=-1.;
        bool bStartComplete=false;
        if (!ReadRenderedProbeJson(Output/TEXT("iris-selected-phase4-start.json"),Start,StartHash,StartBytes)
            || !Start->TryGetBoolField(TEXT("complete"),bStartComplete) || !bStartComplete
            || !Start->TryGetStringField(TEXT("selection_sha1"),Text) || Text!=SelectionHash
            || !Start->TryGetStringField(TEXT("cohort_revision"),Text) || Text!=LexToString(CohortRevision)
            || !Start->TryGetNumberField(TEXT("connection_id"),StartConnection) || StartConnection!=double(ConnectionId)
            || !Start->TryGetNumberField(TEXT("replication_system_id"),StartSystem) || StartSystem!=double(System->GetId()))
        { Finish(false,TEXT("StartBindingOrSelectionChanged")); return; }
    }
    Receipt->SetNumberField(TEXT("current_net_speed_bytes_per_second"),Connection->CurrentNetSpeed);
    // Read-only actual native envelope at both selected phase boundaries.
    // Normal Full-batch allowance is not an absolute cap on all traffic/huge/OOB writes.
    const auto* PacketCap = IConsoleManager::Get().FindConsoleVariable(TEXT("net.Iris.ReplicationWriterMaxAllowedPacketsIfNotHugeObject"));
    const auto* Throttling = IConsoleManager::Get().FindConsoleVariable(TEXT("net.DisableBandwithThrottling"));
    Receipt->SetNumberField(TEXT("normal_writer_packet_cap"),PacketCap ? PacketCap->GetInt() : -1);
    Receipt->SetNumberField(TEXT("bandwidth_throttling_disabled"),Throttling ? Throttling->GetInt() : -1);
    Receipt->SetNumberField(TEXT("max_packet_bytes"),Connection->MaxPacket);
    Receipt->SetNumberField(TEXT("max_single_bunch_bits"),Connection->GetMaxSingleBunchSizeBits());
    // These are the public inputs consumed by NetDriver's replication-view
    // producer, sampled here; not a private read of its last published view.
    FVector ViewLocation; FRotator ViewRotation;
    Observer->GetPlayerViewPoint(ViewLocation,ViewRotation);
    const auto ViewInput=MakeShared<FJsonObject>();
    ViewInput->SetStringField(TEXT("scope"),TEXT("Current public NetDriver replication-view inputs at probe; not last published Iris view"));
    ViewInput->SetNumberField(TEXT("x"),ViewLocation.X);
    ViewInput->SetNumberField(TEXT("y"),ViewLocation.Y);
    ViewInput->SetNumberField(TEXT("z"),ViewLocation.Z);
    ViewInput->SetNumberField(TEXT("pitch"),ViewRotation.Pitch);
    ViewInput->SetNumberField(TEXT("yaw"),ViewRotation.Yaw);
    ViewInput->SetStringField(TEXT("legacy_movement_component"),GetNameSafe(ObserverPawn->GetMovementComponent()));
    if(const auto* Camera=Observer->PlayerCameraManager.Get())
    {
        ViewInput->SetBoolField(TEXT("use_client_side_camera_updates"),Camera->bUseClientSideCameraUpdates);
        ViewInput->SetBoolField(TEXT("should_send_client_side_camera_update"),Camera->bShouldSendClientSideCameraUpdate);
        ViewInput->SetNumberField(TEXT("fov"),Camera->GetFOVAngle());
    }
    Receipt->SetObjectField(TEXT("server_view_input"),ViewInput);
    // Read the cached inputs actually consumed by UNetConnection::Tick; calling
    // GEngine->GetMaxTickRate here could itself update engine timing state.
    Receipt->SetNumberField(TEXT("driver_cached_engine_max_tick_rate"),Driver->CachedEngineMaxTickRate);
    Receipt->SetNumberField(TEXT("driver_max_net_tick_rate"),Driver->MaxNetTickRate);
    Receipt->SetNumberField(TEXT("connection_last_frame_seconds"),Connection->FrameTime);
    Receipt->SetNumberField(TEXT("connection_average_frame_seconds"),Connection->AverageFrameTime);
    Receipt->SetBoolField(TEXT("connection_internal_ack"),Connection->IsInternalAck());
    Receipt->SetNumberField(TEXT("out_bytes_per_second"),Connection->OutBytesPerSecond);
    Receipt->SetNumberField(TEXT("out_packets_per_second"),Connection->OutPacketsPerSecond);
    // These engine totals are signed32 counters, not the reset-per-period rates.
    // A negative/decreasing endpoint cannot certify a lifetime interval or rollover.
    Receipt->SetNumberField(TEXT("out_total_bytes"),Connection->OutTotalBytes);
    Receipt->SetNumberField(TEXT("out_total_packets"),Connection->OutTotalPackets);
    Receipt->SetStringField(TEXT("out_total_scope"),TEXT("UNetConnection FlushNet signed32 accounting for all connection traffic; not UDP delivery. Reject negative or decreasing endpoints."));
    Receipt->SetNumberField(TEXT("queued_bits"),Connection->QueuedBits);
    Receipt->SetNumberField(TEXT("send_buffer_bits"),Connection->SendBuffer.GetNumBits());
    Receipt->SetBoolField(TEXT("is_net_ready"),Connection->IsNetReady());
    Receipt->SetNumberField(TEXT("in_packets_lost"),Connection->InPacketsLost);
    Receipt->SetNumberField(TEXT("out_packets_lost"),Connection->OutPacketsLost);
    Receipt->SetStringField(TEXT("packet_loss_snapshot_scope"),TEXT("in/out_packets_lost reset each connection stats period; use in/out_total_packets_lost for lifetime differences"));
    Receipt->SetNumberField(TEXT("in_total_packets_lost"),Connection->InTotalPacketsLost);
    Receipt->SetNumberField(TEXT("out_total_packets_lost"),Connection->OutTotalPacketsLost);
    const auto& Saturation=Connection->GetSaturationAnalytics();
    Receipt->SetNumberField(TEXT("tracked_frames"),Saturation.GetNumberOfTrackedFrames());
    Receipt->SetNumberField(TEXT("saturated_frames"),Saturation.GetNumberOfSaturatedFrames());
    Receipt->SetNumberField(TEXT("longest_saturated_frame_run"),Saturation.GetLongestRunOfSaturatedFrames());
    for (int32 Index=0;Index<Handles.Num();++Index)
    {
        UE_LOG(LogProjectJDNetwork,Display,TEXT("RENDERED_IRIS_OBJECT boundary=%s actor_id=%llu connection=%u system=%u"),Boundary,Handles[Index].GetId(),ConnectionId,System->GetId());
        Bridge->PrintDebugInfoForNetRefHandle(Handles[Index],ConnectionId,UObjectReplicationBridge::EPrintDebugInfoTraits::NoProtocolState);
    }
    Receipt->SetNumberField(TEXT("objects_printed"),Handles.Num());
    Finish(true,TEXT("ObservedCurrentSelectedRoots"));
}

void DriveRenderedInput(AProject_JPlayerCharacter* Player,int32 Phase,double Age,bool bJumpIssued,bool bAttackIssued)
{
    const float Direction=(Phase==3 || Phase==4) ? (FMath::Fmod(Age,2.)<1.?1.f:-1.f):0.f;
    const FVector Local=FRotator(0,Player->GetControlRotation().Yaw,0).UnrotateVector(FVector(Direction,0,0));
    Player->GetLocomotionAnimStateComponent()->SetMoveInput(FVector2D(Local.Y,Local.X));
    if (Phase==4 && !bJumpIssued) Player->Jump();
    else if (Phase!=4 || Player->GetMover()->IsAirborne()) Player->StopJumping();
    if (Phase==5 && !bAttackIssued)
    {
        Player->HandleSkillInputTagPressed(FProject_JGameplayTags::Get().InputTag_Skill_R);
        Player->HandleSkillInputTagReleased(FProject_JGameplayTags::Get().InputTag_Skill_R);
    }
}
}

void AProject_JNetworkLoadController::ConfigureRenderedStage(int32 InStage)
{
    RenderedOwnerPhaseAt=GetWorld()->GetTimeSeconds(); bRenderedOwnerJump=bRenderedOwnerAttack=false;
    if (RenderedObserverProcess())
    {
        auto* Observer=GetGameInstance()->GetSubsystem<UProject_JRenderedCrowdFixture>();
        if (Observer && Observer->PrepareNetworkView(this)) Observer->SetNetworkPhase(InStage-39);
    }
}

void AProject_JNetworkLoadController::DriveRenderedOwner()
{
    const TStrongObjectPtr<AProject_JNetworkLoadController> KeepSelf(this);
    auto* OwnedAvatar=Cast<AProject_JPlayerCharacter>(GetPawn());
    // This real autonomous connection owns exactly this Pawn's prediction input.
    // No loop over the 199 simulated proxies can write movement/ability input.
    if (!OwnedAvatar || OwnedAvatar->GetLocalRole()!=ROLE_AutonomousProxy || !OwnedAvatar->IsLocallyControlled()) return;
    const TStrongObjectPtr<AProject_JPlayerCharacter> KeepPlayer(OwnedAvatar);
    const int32 InputStage=CurrentStage;
    DriveRenderedInput(OwnedAvatar,CurrentStage-39,GetWorld()->GetTimeSeconds()-RenderedOwnerPhaseAt,bRenderedOwnerJump,bRenderedOwnerAttack);
    if (IsActorBeingDestroyed() || !IsValid(OwnedAvatar) || GetPawn()!=OwnedAvatar || CurrentStage!=InputStage) return;
    bRenderedOwnerJump|=CurrentStage==43; bRenderedOwnerAttack|=CurrentStage==44;
}

bool AProject_JNetworkLoadController::ObserveRenderedCrowd()
{
    const auto WaitForReadiness=[this](const FString& Detail)
    {
        if (LastObservation!=Detail)
        {
            LastObservation=Detail;
            UE_LOG(LogProjectJDNetwork,Display,TEXT("RENDERED_READY_WAIT slot=%d stage=%d %s"),LocalSlot,CurrentStage,*Detail);
        }
        return false;
    };
    if (RenderedObserved.IsEmpty())
    {
        TArray<TWeakObjectPtr<AProject_JPlayerCharacter>> Pending; Pending.SetNum(RenderedPopulation);
        int32 Found=0, Total=0, Unknown=0, FirstUnknownId=INDEX_NONE;
        int32 RawPSNull=0, WrongPSClass=0, TypedUnknownId=0;
        uint64 FirstUnknownPawnId=0;
        FString FirstRawClass=TEXT("None");
        FName FirstUnknownClass;
        for (TActorIterator<AProject_JPlayerCharacter> It(GetWorld()); It; ++It)
        {
            ++Total;
            const APlayerState* RawState=It->GetPlayerState();
            const auto* State=Cast<AProject_JPlayerState>(RawState);
            const int32 Index=RenderedIndex(State);
            if (Index==INDEX_NONE)
            {
                RawPSNull+=RawState==nullptr;
                WrongPSClass+=RawState!=nullptr && State==nullptr;
                TypedUnknownId+=State!=nullptr;
                if (Unknown++==0)
                {
                    FirstUnknownPawnId=RenderedPublicId(*It);
                    FirstRawClass=GetPathNameSafe(RawState ? RawState->GetClass():nullptr);
                    if (State) { FirstUnknownId=State->GetPlayerId(); FirstUnknownClass=State->GetPublicClassId(); }
                }
                continue;
            }
            if (Pending[Index].IsValid() || It->GetClass()->GetPathName()!=RenderedAvatarClass)
                return WaitForReadiness(FString::Printf(TEXT("DiscoveryDuplicate index=%d"),Index));
            // Same explicit collision policy on real prediction/authority worlds.
            It->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
            Pending[Index]=*It; ++Found;
        }
        if (Found!=RenderedPopulation)
        {
            int32 WorldPS=0, WorldProjectPS=0;
            for (TActorIterator<APlayerState> It(GetWorld()); It; ++It)
            { ++WorldPS; WorldProjectPS+=It->IsA<AProject_JPlayerState>(); }
            const auto* StateOfWorld=GetWorld()->GetGameState();
            return WaitForReadiness(FString::Printf(TEXT("Discovery total=%d matched=%d unknown=%d rawPSNull=%d wrongPSClass=%d typedUnknownId=%d worldPS=%d worldProjectPS=%d gameStatePS=%d firstPlayerId=%d firstClass=%s firstRawPSClass=%s firstPawnId=%llu"),
                Total,Found,Unknown,RawPSNull,WrongPSClass,TypedUnknownId,WorldPS,WorldProjectPS,
                StateOfWorld ? StateOfWorld->PlayerArray.Num():INDEX_NONE,FirstUnknownId,*FirstUnknownClass.ToString(),*FirstRawClass,FirstUnknownPawnId));
        }
        RenderedObserved=MoveTemp(Pending);
    }
    FString Roster=TEXT("actor_index,class_id,actor_id,role,local,foreign_private_empty,public_equipment,presented,health,passed\n");
    TArray<AProject_JPlayerCharacter*> Actors; Actors.Reserve(RenderedPopulation);
    TSet<uint64> Ids;
    int32 Owners=0, Simulated=0;
    for (int32 I=0; I<RenderedObserved.Num(); ++I)
    {
        auto* OwnedAvatar=RenderedObserved[I].Get();
        auto* State=OwnedAvatar ? OwnedAvatar->GetPlayerState<AProject_JPlayerState>():nullptr;
        auto* ASC=State ? State->GetAbilitySystemComponent():nullptr;
        const auto* Runtime=OwnedAvatar ? OwnedAvatar->FindComponentByClass<UProject_JEquipmentRuntimeComponent>():nullptr;
        const uint64 Id=RenderedPublicId(OwnedAvatar);
        const bool bMine=I==LocalSlot;
        const bool bPrivate=State && (bMine || (State->GetInventoryComponent()->GetReplicationDiagnosticSummary().Contains(TEXT("Items=0")) &&
            State->GetEquipmentManagerComponent()->GetPrivateEquipmentCount()==0));
        if (!OwnedAvatar || !State || RenderedIndex(State)!=I || !ASC || ASC->GetOwnerActor()!=State || ASC->GetAvatarActor()!=OwnedAvatar || OwnedAvatar->GetWorld()!=GetWorld() || OwnedAvatar->HasAuthority() ||
            OwnedAvatar->GetLocalRole()!=(bMine ? ROLE_AutonomousProxy:ROLE_SimulatedProxy) || OwnedAvatar->IsLocallyControlled()!=bMine ||
            (bMine && (OwnedAvatar!=GetPawn() || State!=PlayerState)) || !bPrivate || !Id || Ids.Contains(Id) ||
            !Runtime || Runtime->GetPublicEquipment().Items.Num()!=(bMine ? 0:1) ||
            !Runtime->GetPresentedItemInSlot(EProject_JEquipmentSlot::Weapon) || !OwnedAvatar->GetCurrentCombatStyle() ||
            !OwnedAvatar->GetCurrentWeaponPresentationProfile())
            return WaitForReadiness(FString::Printf(TEXT("Representation index=%d role=%d private=%d publicItems=%d item=%d style=%d profile=%d ascOwner=%d ascAvatar=%d actorId=%llu"),
                I,OwnedAvatar ? int32(OwnedAvatar->GetLocalRole()):-1,bPrivate,Runtime ? Runtime->GetPublicEquipment().Items.Num():-1,
                Runtime && Runtime->GetPresentedItemInSlot(EProject_JEquipmentSlot::Weapon)!=nullptr,
                OwnedAvatar && OwnedAvatar->GetCurrentCombatStyle()!=nullptr,OwnedAvatar && OwnedAvatar->GetCurrentWeaponPresentationProfile()!=nullptr,
                ASC && ASC->GetOwnerActor()==State,ASC && ASC->GetAvatarActor()==OwnedAvatar,Id));
        const float Health=ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
        if (!FMath::IsFinite(Health) || Health<=0)
            return WaitForReadiness(FString::Printf(TEXT("Vitals index=%d health=%.9g"),I,Health));
        Ids.Add(Id); Owners+=bMine; Simulated+=!bMine; Actors.Add(OwnedAvatar);
        if (!bRenderedRosterRecorded) Roster+=FString::Printf(TEXT("%d,%s,%llu,%d,%d,1,%d,1,%.9g,1\n"),I,
            *State->GetPublicClassId().ToString(),Id,int32(OwnedAvatar->GetLocalRole()),bMine,Runtime->GetPublicEquipment().Items.Num(),Health);
    }
    if (Owners!=1 || Simulated!=199) return false;
    if (RenderedObserverProcess())
    {
        auto* Observer=GetGameInstance()->GetSubsystem<UProject_JRenderedCrowdFixture>();
        if (!Observer || Observer->HasNetworkFailed()) return false;
        // Bind is idempotently unnecessary after phase1 completion. During its
        // first warmup it may still wait for bounded async weapon/body readiness.
        if (!bRenderedRosterRecorded)
        {
            // RHI startup can change socket arrival order. Select the actual
            // autonomous Pawn for sample index0, keeping canonical roster IDs.
            Actors.Swap(0,LocalSlot);
            if (!Observer->BindNetworkPlayers(Actors))
                return WaitForReadiness(TEXT("SamplerAppearancePending"));
        }
    }
    if (!bRenderedRosterRecorded)
    {
        if (!FFileHelper::SaveStringToFile(Roster,*(OutputRoot()/TEXT("rendered-roster.csv")))) return false;
        bRenderedRosterRecorded=true;
    }
    if (RenderedObserverProcess())
    {
        const auto* Observer=GetGameInstance()->GetSubsystem<UProject_JRenderedCrowdFixture>();
        return Observer && Observer->IsNetworkPhaseComplete(CurrentStage-39);
    }
    return true;
}

void AProject_JNetworkLoadGameMode::BeginRenderedCrowd()
{
    const TStrongObjectPtr<AProject_JNetworkLoadGameMode> KeepSelf(this);
    const TStrongObjectPtr<UWorld> World(GetWorld());
    Stage=39; StageStarted=RenderedCreationStarted=FPlatformTime::Seconds();
    StageOutBytes=GetWorld()->GetNetDriver()->OutTotalBytes;
    RenderedPhaseAt=GetWorld()->GetTimeSeconds(); const uint64 Revision=++RenderedRevision;
    TArray<TStrongObjectPtr<AProject_JNetworkLoadController>> Owners;
    TArray<TStrongObjectPtr<AProject_JPlayerState>> States;
    TArray<TStrongObjectPtr<AProject_JPlayerCharacter>> Avatars;
    TArray<TStrongObjectPtr<UProject_JAbilitySystemComponent>> ASCs;
    if (Clients.Num()!=2) { Finish(false,TEXT("Rendered actual owner count changed")); return; }
    for (int32 I=0; I<2; ++I)
    {
        auto* PC=Clients[I].Get(); auto* Player=PC ? Cast<AProject_JPlayerCharacter>(PC->GetPawn()):nullptr;
        auto* State=PC ? PC->GetPlayerState<AProject_JPlayerState>():nullptr;
        auto* ASC=State ? Cast<UProject_JAbilitySystemComponent>(State->GetAbilitySystemComponent()):nullptr;
        if (!IsValid(PC) || !IsValid(Player) || !IsValid(State) || !IsValid(ASC)) { Finish(false,TEXT("Rendered actual owner retired")); return; }
        Owners.Emplace(PC); States.Emplace(State); Avatars.Emplace(Player); ASCs.Emplace(ASC);
        RenderedPlayers.Add(Player); // Define both actual owners before any inventory/gear/tag callback.
    }
    // This existing root is a cancellation lease, never the AI's net owner.
    RenderedPreparationOwner=Owners[0]->GetRootComponent();
    const auto Current=[&]
    {
        if (!IsValid(this) || IsActorBeingDestroyed() || bFinished || bRenderedRetiring || RenderedRevision!=Revision ||
            Stage!=39 || GetWorld()!=World.Get() || World->bIsTearingDown || !AreRenderedOwnersCurrent()) return false;
        for (int32 I=0; I<2; ++I)
            if (Clients[I]!=Owners[I].Get() || RenderedPlayers[I]!=Avatars[I].Get() || Owners[I]->GetPlayerState<AProject_JPlayerState>()!=States[I].Get() ||
                States[I]->GetAbilitySystemComponent()!=ASCs[I].Get() || ASCs[I]->GetOwnerActor()!=States[I].Get() || ASCs[I]->GetAvatarActor()!=Avatars[I].Get() ||
                !IsValid(States[I]->GetInventoryComponent()) || !IsValid(States[I]->GetEquipmentManagerComponent())) return false;
        return true;
    };
    const auto RequireCurrent=[&]
    {
        if (Current()) return true;
        if (!bFinished && !bRenderedRetiring) Finish(false,TEXT("Rendered owner scope changed during equipment preparation"));
        return false;
    };
    if (!RequireCurrent()) return;
    const TStrongObjectPtr<UProject_JEquipmentItemDefinition> Definition(LoadObject<UProject_JEquipmentItemDefinition>(nullptr,TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.DA_Greatsword_Equip")));
    if (!RequireCurrent()) return;
    if (!Definition.IsValid()) { Finish(false,TEXT("Rendered owner equipment definition unavailable")); return; }
    for (int32 I=0; I<2; ++I)
    {
        auto* Player=Avatars[I].Get(); auto* State=States[I].Get(); auto* ASC=ASCs[I].Get();
        const auto Item=State->GetInventoryComponent()->AddItemDefinition(Definition.Get());
        if (!RequireCurrent()) return;
        if (!Item.InstanceId.IsValid()) { Finish(false,TEXT("Rendered owner canonical inventory add failed")); return; }
        const auto Equipped=State->GetEquipmentManagerComponent()->TryEquipItemInstanceById(Item.InstanceId);
        if (!RequireCurrent()) return;
        if (!Equipped.bSucceeded)
        { Finish(false,TEXT("Rendered owner canonical inventory equip failed")); return; }
        Items[I]=Item.InstanceId;
        ASC->AddProjectJLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
        if (!RequireCurrent()) return;
        Player->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
        if (!RequireCurrent()) return;
        auto Teleport=MakeShared<FTeleportEffect>(); Teleport->TargetLocation=RenderedGrid(I); Teleport->bUseActorRotation=false; Teleport->TargetRotation=FRotator::ZeroRotator;
        Player->GetMover()->QueueInstantMovementEffect(Teleport);
        if (!RequireCurrent()) return;
    }
}

bool AProject_JNetworkLoadGameMode::AreRenderedOwnersCurrent() const
{
    if (Clients.Num()!=2 || RenderedPlayers.Num()<2 || !RenderedPreparationOwner.IsValid() ||
        !IsValid(Clients[0].Get()) || Clients[0]->GetRootComponent()!=RenderedPreparationOwner.Get()) return false;
    for (int32 I=0; I<2; ++I)
    {
        const auto* PC=Clients[I].Get(); const auto* Player=RenderedPlayers[I].Get();
        const auto* State=IsValid(PC) ? PC->GetPlayerState<AProject_JPlayerState>():nullptr;
        if (!IsValid(PC) || PC->IsActorBeingDestroyed() || !IsValid(Player) || !IsValid(State) ||
            PC->GetPawn()!=Player || Player->GetController()!=PC || Player->GetPlayerState()!=State || State->GetOwner()!=PC) return false;
    }
    return true;
}

void AProject_JNetworkLoadGameMode::UpdateRenderedCrowd()
{
    if (bRenderedRetiring || bFinished) return;
    if (!AreRenderedOwnersCurrent()) { Finish(false,TEXT("Rendered actual owner/PS/Avatar scope changed")); return; }
    if (Stage==39)
    {
        auto* Registry=GetWorld()->GetSubsystem<UProject_JWorldEventRegistry>();
        if (!Registry || !RenderedPreparationOwner.IsValid() || RenderedPreparationOwner->GetOwner()!=Clients[0])
        { Finish(false,TEXT("Rendered preparation owner retired")); return; }
        if (RenderedPlayers.Num()<RenderedPopulation)
        {
            if (RenderedPreparationToken) return;
            const uint64 Revision=RenderedRevision;
            const TWeakObjectPtr<AProject_JNetworkLoadGameMode> WeakThis(this);
            RenderedPreparationToken=Registry->RequestComponentPreparation(RenderedPreparationOwner.Get(),FSoftObjectPath(RenderedAvatarClass),
                [WeakThis,Revision](uint64 Token,bool bLoaded)
                {
                    auto* Self=WeakThis.Get();
                    if (!Self || Self->bFinished || Self->bRenderedRetiring || Self->RenderedRevision!=Revision ||
                        Self->RenderedPreparationToken!=Token || Self->Stage!=39 || Self->GetWorld()->bIsTearingDown) return;
                    const TStrongObjectPtr<AProject_JNetworkLoadGameMode> KeepSelf(Self);
                    const auto Current=[Self,Revision]
                    {
                        return !Self->bFinished && !Self->bRenderedRetiring && Self->RenderedRevision==Revision && Self->Stage==39 &&
                            !Self->GetWorld()->bIsTearingDown && Self->AreRenderedOwnersCurrent();
                    };
                    Self->RenderedPreparationToken=0;
                    if (!Current()) return;
                    if (!bLoaded) { Self->Finish(false,TEXT("Rendered bounded preparation failed")); return; }
                    // One logical Avatar bundle per existing bounded completion;
                    // it contains controller+PS+Avatar, not one physical Actor.
                    FActorSpawnParameters Params; Params.ObjectFlags|=RF_Transient;
                    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                    auto* Controller=Self->GetWorld()->SpawnActor<AAIController>(Params);
                    if (!Controller) { Self->Finish(false,TEXT("Rendered AI controller spawn failed")); return; }
                    const TStrongObjectPtr<AAIController> KeepController(Controller);
                    if (!Current() || !IsValid(Controller) || Controller->IsActorBeingDestroyed())
                    { if (IsValid(Controller)) Controller->Destroy(); return; }
                    Self->RenderedControllers.Add(Controller);
                    auto* State=Self->GetWorld()->SpawnActor<AProject_JPlayerState>(Params);
                    if (!State) { Self->Finish(false,TEXT("Rendered AI PlayerState spawn failed")); return; }
                    const TStrongObjectPtr<AProject_JPlayerState> KeepState(State);
                    if (!Current() || !IsValid(State))
                    { if (IsValid(State)) State->Destroy(); return; }
                    Self->RenderedStates.Add(State);
                    if (!IsValid(Controller) || Controller->IsActorBeingDestroyed())
                    { Self->Finish(false,TEXT("Rendered AI controller retired during PlayerState spawn")); return; }
                    const int32 Index=Self->RenderedPlayers.Num();
                    Controller->SetPlayerState(State);
                    if (!Current()) return;
                    if (!IsValid(Controller) || !IsValid(State) || Controller->IsActorBeingDestroyed() || Controller->GetPlayerState<AProject_JPlayerState>()!=State)
                    { Self->Finish(false,TEXT("Rendered AI scope changed during PlayerState assignment")); return; }
                    State->SetOwner(Controller);
                    if (!Current()) return;
                    if (!IsValid(Controller) || !IsValid(State) || Controller->GetPlayerState<AProject_JPlayerState>()!=State || State->GetOwner()!=Controller)
                    { Self->Finish(false,TEXT("Rendered AI controller/PS scope changed")); return; }
                    auto* Player=ProjectJAuthoredAnimationFixture::Spawn(Self->GetWorld(),Controller,State,RenderedGrid(Index),true);
                    if (!Player) { Self->Finish(false,TEXT("Rendered authored AI Avatar spawn failed")); return; }
                    const TStrongObjectPtr<AProject_JPlayerCharacter> KeepPlayer(Player);
                    // Construction/Possess/gear callbacks can retire this cohort.
                    if (!Current() || !IsValid(Controller) || !IsValid(State) ||
                        Player->IsActorBeingDestroyed() || Player->GetController()!=Controller || Player->GetPlayerState()!=State)
                    { if (IsValid(Player)) Player->Destroy(); return; }
                    Self->RenderedPlayers.Add(Player);
                    auto* ASC=Cast<UProject_JAbilitySystemComponent>(Player->GetAbilitySystemComponent());
                    if (!ASC || ASC->GetOwnerActor()!=State || ASC->GetAvatarActor()!=Player || Player->GetNetConnection() || Player->GetNetOwningPlayer() ||
                        (ASC->AbilityActorInfo.IsValid() && ASC->AbilityActorInfo->PlayerController.IsValid()))
                    { Self->Finish(false,TEXT("Rendered AI acquired invalid ASC/owning connection")); return; }
                    const TStrongObjectPtr<UProject_JAbilitySystemComponent> KeepASC(ASC);
                    const auto BindingCurrent=[&]
                    {
                        return IsValid(Controller) && !Controller->IsActorBeingDestroyed() && IsValid(State) && !State->IsActorBeingDestroyed() &&
                            IsValid(Player) && !Player->IsActorBeingDestroyed() && Controller->GetPawn()==Player &&
                            Controller->GetPlayerState<AProject_JPlayerState>()==State && State->GetOwner()==Controller &&
                            Player->GetController()==Controller && Player->GetPlayerState()==State && State->GetAbilitySystemComponent()==ASC &&
                            Player->GetAbilitySystemComponent()==ASC && IsValid(ASC) && ASC->GetOwnerActor()==State && ASC->GetAvatarActor()==Player;
                    };
                    // Keep the real progression class. Use the existing replicated
                    // player identity for this fixture's AI roster, after spawn.
                    State->SetPlayerId(RenderedAIPlayerIdBase+Index);
                    if (!Current()) return;
                    if (!BindingCurrent()) { Self->Finish(false,TEXT("Rendered AI scope changed during roster identity assignment")); return; }
                    Player->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
                    if (!Current()) return;
                    if (!BindingCurrent()) { Self->Finish(false,TEXT("Rendered AI scope changed during collision preparation")); return; }
                    ASC->AddProjectJLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
                    if (!Current()) return;
                    if (!BindingCurrent()) { Self->Finish(false,TEXT("Rendered AI scope changed during combat tag preparation")); return; }
                    State->ForceNetUpdate(); Player->ForceNetUpdate();
                });
            return; // Admission rejection retries only on the next existing tick, under the same stage deadline.
        }
        FString Roster=TEXT("actor_index,class_id,actor_id,role,local,foreign_private_empty,public_equipment,presented,health,passed,ps_actor_id,player_id\n");
        int32 ServerWeapons=0, ServerMeshParts=0, ServerClothParts=0, ServerClothTickParts=0, ServerClothSimulationInstances=0;
        for (int32 I=0; I<RenderedPlayers.Num(); ++I)
        {
            auto* Player=RenderedPlayers[I].Get(); const auto Physical=Project_J::Movement::ReadPhysicalFrame(Player);
            const auto* State=Player ? Player->GetPlayerState<AProject_JPlayerState>():nullptr;
            const auto* ASC=State ? State->GetAbilitySystemComponent():nullptr;
            const uint64 Id=RenderedPublicId(Player);
            if (!Player || !State || !ASC || !Id || !Physical.bValid || !Physical.bGrounded || Player->GetLocomotionAnimStateComponent()->bIsLanding ||
                FVector::DistSquared2D(Player->GetActorLocation(),RenderedGrid(I))>25.) return;
            if (Player->GetLocalRole()!=ROLE_Authority || ASC->GetOwnerActor()!=State || ASC->GetAvatarActor()!=Player || (I>=2 &&
                (Player->GetController()!=RenderedControllers[I-2] || State!=RenderedStates[I-2] || State->GetOwner()!=RenderedControllers[I-2] ||
                    Player->GetNetConnection() || Player->GetNetOwningPlayer())))
            { Finish(false,TEXT("Rendered server authority/AI ownership changed")); return; }
            const float Health=ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
            if (!FMath::IsFinite(Health) || Health<=0) { Finish(false,TEXT("Rendered server Avatar not alive")); return; }
            const auto* Runtime=Player->FindComponentByClass<UProject_JEquipmentRuntimeComponent>();
            if (!Runtime || Runtime->GetPublicEquipment().Items.Num()!=1) return;
            const auto* Presentation=Player->FindComponentByClass<UProject_JWeaponPresentationComponent>();
            ServerWeapons+=Presentation && IsValid(Presentation->GetSpawnedWeapon());
            TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player); ServerMeshParts+=Meshes.Num();
            for (const auto* Mesh:Meshes)
            {
                // Actual allocated simulation groups, separate from asset metadata and tick policy.
                ServerClothSimulationInstances+=Mesh->GetClothingSimulationInstances().Num();
                if (const auto* Asset=Mesh->GetSkeletalMeshAsset(); Asset && Asset->HasActiveClothingAssets())
                { ++ServerClothParts; ServerClothTickParts+=Mesh->IsComponentTickEnabled() && !Mesh->bDisableClothSimulation && !Mesh->IsClothingSimulationSuspended(); }
            }
            Roster+=FString::Printf(TEXT("%d,%s,%llu,3,0,1,1,1,%.9g,1,%llu,%d\n"),I,*State->GetPublicClassId().ToString(),Id,Health,RenderedPublicId(State),State->GetPlayerId());
        }
        const auto* Driver=GetWorld()->GetNetDriver();
        if (!Driver) return;
        FString ConnectionPolicies=TEXT("[");
        for (int32 Slot=0; Slot<2; ++Slot)
        {
            const auto* PC=Clients[Slot].Get();
            const auto* Connection=IsValid(PC) ? PC->GetNetConnection():nullptr;
            if (!IsValid(Connection) || Connection->GetDriver()!=Driver || !Driver->ClientConnections.Contains(Connection)) return;
            if (Slot) ConnectionPolicies+=TEXT(",");
            // Preparation-time policy inputs only, not remaining send/packet allowance.
            ConnectionPolicies+=FString::Printf(TEXT("{\"fixture_slot\":%d,\"current_net_speed_bytes_per_second\":%d,\"max_packet_bytes\":%d}"),
                PC->FixtureSlot,Connection->CurrentNetSpeed,Connection->MaxPacket);
        }
        ConnectionPolicies+=TEXT("]");
        const auto Stats=Registry->GetStats();
        const bool bObservedDedicatedProcess=IsRunningDedicatedServer();
        const bool bObservedPerformanceThreads=FApp::ShouldUseThreadingForPerformance();
        const bool bRequestedPerformanceThreads=FParse::Param(FCommandLine::Get(),TEXT("useperfthreads"));
        UE_LOG(LogProjectJDNetwork,Display,TEXT("RENDERED_SERVER_PERF_THREADS dedicated_process=%d netmode=%d requested=%d should_use_threading_for_performance=%d"),
            bObservedDedicatedProcess,int32(GetNetMode()),bRequestedPerformanceThreads,bObservedPerformanceThreads);
        const FString Preparation=FString::Printf(TEXT("{\"actors\":200,\"human_avatars\":2,\"ai_bundles\":198,\"created_physical_actors\":594,\"creation_wall_s\":%.9f,\"shared_preparations_peak\":%d,\"shared_last_applications\":%d,\"server_net_mode\":%d,\"server_is_dedicated_process\":%s,\"server_should_use_threading_for_performance\":%s,\"server_perf_threads_requested\":%s,\"server_weapon_actors\":%d,\"server_mesh_parts\":%d,\"server_cloth_parts\":%d,\"server_cloth_tick_parts\":%d,\"server_cloth_simulation_instances\":%d,\"server_connection_policies\":%s}"),
            FPlatformTime::Seconds()-RenderedCreationStarted,Stats.PeakPreparations,Stats.LastTickSpawns,int32(GetNetMode()),
            bObservedDedicatedProcess?TEXT("true"):TEXT("false"),bObservedPerformanceThreads?TEXT("true"):TEXT("false"),bRequestedPerformanceThreads?TEXT("true"):TEXT("false"),ServerWeapons,ServerMeshParts,ServerClothParts,ServerClothTickParts,ServerClothSimulationInstances,*ConnectionPolicies);
        if (!FFileHelper::SaveStringToFile(Roster,*(OutputRoot()/TEXT("rendered-roster.csv"))) ||
            !FFileHelper::SaveStringToFile(Preparation,*(OutputRoot()/TEXT("rendered-preparation.json"))))
        { Finish(false,TEXT("Rendered server roster evidence write failed")); return; }
        Rows+=FString::Printf(TEXT("39,%.3f,%d,%u\n"),FPlatformTime::Seconds()-StageStarted,Driver->ClientConnections.Num(),Driver->OutTotalBytes-StageOutBytes);
        StartRenderedStage(40); return;
    }
    const int32 Phase=Stage-39;
    const uint64 Revision=RenderedRevision;
    auto* MotionCollector=GetGameInstance()->GetSubsystem<UProject_JRenderedCrowdFixture>();
    const double MotionPhaseAge=GetWorld()->GetTimeSeconds()-RenderedPhaseAt;
    for (int32 I=2; I<RenderedPlayers.Num(); ++I)
    {
        auto* Player=RenderedPlayers[I].Get();
        if (!IsValid(Player) || Player->GetController()!=RenderedControllers[I-2] || Player->GetPlayerState()!=RenderedStates[I-2] || !Player->HasAuthority())
        { Finish(false,TEXT("Rendered AI input authority retired")); return; }
        const TStrongObjectPtr<AProject_JPlayerCharacter> KeepPlayer(Player);
        if (MotionCollector && (Phase==3 || Phase==4))
            MotionCollector->ObserveNetworkMotion(Phase,I,Player,Player->GetActorLocation(),Player->GetVelocity(),MotionPhaseAge);
        DriveRenderedInput(Player,Phase,GetWorld()->GetTimeSeconds()-RenderedPhaseAt,bRenderedJump,bRenderedAttack);
        if (bFinished || bRenderedRetiring || RenderedRevision!=Revision) return;
    }
    if (MotionCollector && (Phase==3 || Phase==4))
        for (int32 I=0; I<2; ++I)
            if (const auto* ObservedAvatar=RenderedPlayers[I].Get(); IsValid(ObservedAvatar))
                MotionCollector->ObserveNetworkMotion(Phase,I,ObservedAvatar,ObservedAvatar->GetActorLocation(),ObservedAvatar->GetVelocity(),MotionPhaseAge);
    bRenderedJump|=Phase==4; bRenderedAttack|=Phase==5;
    if (GetWorld()->GetTimeSeconds()-RenderedPhaseAt>=8 &&
        !Clients.ContainsByPredicate([this](const auto& PC){return !IsValid(PC) || PC->ConfirmedStage!=Stage;}))
    {
        const auto* Driver=GetWorld()->GetNetDriver();
        Rows+=FString::Printf(TEXT("%d,%.3f,%d,%u\n"),Stage,FPlatformTime::Seconds()-StageStarted,Driver->ClientConnections.Num(),Driver->OutTotalBytes-StageOutBytes);
        if (MotionCollector) MotionCollector->WriteNetworkMotionWitness(Phase,OutputRoot()/FString::Printf(TEXT("motion-authority-phase%d.csv"),Phase));
        if (Stage==43) ProbeRenderedIrisSelection(GetWorld(),OutputRoot(),RenderedPlayers,Clients,RenderedRevision,false);
        if (Stage<45) StartRenderedStage(Stage+1);
        else
        {
            CleanupRenderedCrowd(); Stage=6; StageStarted=FPlatformTime::Seconds(); Clients[1]->ClientFinish(true);
        }
    }
}

void AProject_JNetworkLoadGameMode::StartRenderedStage(int32 InStage)
{
    Stage=InStage; StageStarted=FPlatformTime::Seconds(); RenderedPhaseAt=GetWorld()->GetTimeSeconds();
    StageOutBytes=GetWorld()->GetNetDriver()->OutTotalBytes;
    bRenderedJump=bRenderedAttack=false; RequiredPulse=Pulse+1;
    if (InStage==43) ProbeRenderedIrisSelection(GetWorld(),OutputRoot(),RenderedPlayers,Clients,RenderedRevision,true);
    for (AProject_JNetworkLoadController* PC:Clients)
    {
        PC->ConfirmedStage=INDEX_NONE;
        PC->ClientConfigure(Stage,PC->FixtureSlot,ExpectedIds(PC->FixtureSlot),Items[PC->FixtureSlot],true,RequiredPulse);
    }
    UE_LOG(LogProjectJDNetwork,Display,TEXT("RENDERED_PHASE stage=%d actors=200 ai=198 humanOwners=2 realConnections=2"),Stage);
}

void AProject_JNetworkLoadGameMode::CleanupRenderedCrowd()
{
    if (bRenderedRetiring) return;
    const TStrongObjectPtr<AProject_JNetworkLoadGameMode> KeepSelf(this);
    bRenderedRetiring=true; ++RenderedRevision;
    const uint64 Token=RenderedPreparationToken; RenderedPreparationToken=0;
    const TStrongObjectPtr<UActorComponent> LeaseOwner(RenderedPreparationOwner.Get()); RenderedPreparationOwner.Reset();
    if (auto* Registry=GetWorld()->GetSubsystem<UProject_JWorldEventRegistry>())
        if (Token && LeaseOwner.IsValid()) Registry->ReleasePreparation(Token,LeaseOwner.Get());
    // Close lease/revision before callbacks; UPROPERTY arrays remain pinned by
    // this Mode until all reentrant input/ability/Destroy callbacks have returned.
    for (int32 I=0; I<RenderedPlayers.Num(); ++I)
        if (auto* Player=RenderedPlayers[I].Get(); IsValid(Player))
        {
            const TStrongObjectPtr<AProject_JPlayerCharacter> KeepPlayer(Player);
            Player->GetLocomotionAnimStateComponent()->SetMoveInput(FVector2D::ZeroVector); Player->StopJumping();
            if (auto* ASC=Player->GetAbilitySystemComponent()) ASC->CancelAllAbilities();
            if (I>=2 && IsValid(Player)) Player->Destroy();
        }
    for (const auto& Controller:RenderedControllers) if (IsValid(Controller.Get())) Controller->Destroy();
    for (const auto& State:RenderedStates) if (IsValid(State.Get())) State->Destroy();
    RenderedPlayers.Reset(); RenderedControllers.Reset(); RenderedStates.Reset();
}
