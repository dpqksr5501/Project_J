namespace
{
// Bounded evidence only: one stable snapshot per existing phase plus final.
// No pointer writes, RPCs, timers, camera/view setters, guard or assertion changes.
FString PartyOwnershipSnapshot(AProject_JNetworkLoadController* PC, int32 Phase, int32 Slot, const TCHAR* Boundary)
{
    if (!IsValid(PC) || !PC->GetWorld()) return FString();
    UWorld* World = PC->GetWorld();
    auto* Bridge = UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(PC);
    auto* System = World->GetNetDriver() ? World->GetNetDriver()->GetReplicationSystem() : nullptr;
    const auto Ref = [Bridge, System](const UObject* Object)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("valid"), IsValid(Object));
        Result->SetStringField(TEXT("name"), GetNameSafe(Object));
        Result->SetStringField(TEXT("path"), GetPathNameSafe(Object));
        const auto Handle = Bridge && IsValid(Object) ? Bridge->GetReplicatedRefHandle(Object) : UE::Net::FNetRefHandle();
        Result->SetStringField(TEXT("net_id"), LexToString(Handle.GetId()));
        Result->SetStringField(TEXT("net_serial"), LexToString(Handle.GetSerial()));
        Result->SetBoolField(TEXT("complete"), Handle.IsCompleteHandle());
        Result->SetNumberField(TEXT("rep_system"), Handle.IsCompleteHandle() ? int32(Handle.GetReplicationSystemId()) : -1);
        Result->SetNumberField(TEXT("authority_owner_connection"), System && System->IsServer() && Handle.IsCompleteHandle() ? int32(System->GetOwningNetConnection(Handle)) : -1);
        if (const auto* Actor = Cast<AActor>(Object))
        {
            Result->SetNumberField(TEXT("role"), int32(Actor->GetLocalRole()));
            Result->SetNumberField(TEXT("remote_role"), int32(Actor->GetRemoteRole()));
            Result->SetBoolField(TEXT("destroying"), Actor->IsActorBeingDestroyed());
            Result->SetStringField(TEXT("position_uu"), Actor->GetActorLocation().ToString());
        }
        if (const auto* State = Cast<APlayerState>(Object))
        {
            Result->SetNumberField(TEXT("native_player_id"), State->GetPlayerId());
            Result->SetBoolField(TEXT("fixture_listen_observer"), State->GetPlayerId() == FixtureListenObserverPlayerId);
        }
        return Result;
    };
    auto Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("boundary"), Boundary); Row->SetNumberField(TEXT("phase"), Phase); Row->SetNumberField(TEXT("slot"), Slot);
    Row->SetNumberField(TEXT("net_mode"), int32(World->GetNetMode())); Row->SetNumberField(TEXT("world_time"), World->GetTimeSeconds());
    Row->SetObjectField(TEXT("pc"), Ref(PC)); Row->SetObjectField(TEXT("pc_pawn"), Ref(PC->GetPawn()));
    if (const auto* Scope=PC->FindComponentByClass<UProject_JOwnedPawnScopeComponent>())
    {
        const auto Evidence=Scope->Query(); auto Owned=MakeShared<FJsonObject>();
        Owned->SetBoolField(TEXT("logical_ready"), Evidence.bLogicalReady); Owned->SetBoolField(TEXT("identity_matches"), Evidence.bIdentityMatches);
        Owned->SetBoolField(TEXT("group_valid"), Evidence.bGroupValid); Owned->SetBoolField(TEXT("member_present"), Evidence.bMemberPresent);
        Owned->SetBoolField(TEXT("allow_requested"), Evidence.bAllowRequested); Owned->SetBoolField(TEXT("mutation_pending"), Evidence.bMutationPending);
        Owned->SetBoolField(TEXT("admission_failed"), Evidence.bAdmissionFailed); Owned->SetStringField(TEXT("failure"), Evidence.Failure.ToString());
        Owned->SetBoolField(TEXT("closed"), Evidence.bClosed);
        Owned->SetNumberField(TEXT("connection"), Evidence.ConnectionId); Owned->SetNumberField(TEXT("groups_created"), Evidence.GroupsCreated);
        Owned->SetStringField(TEXT("group_token"), LexToString(Evidence.GroupToken)); Owned->SetStringField(TEXT("revision"), LexToString(Evidence.Revision));
        Owned->SetStringField(TEXT("pawn_full_handle"), Evidence.PawnHandle.ToString());
        Owned->SetStringField(TEXT("scope"), TEXT("Read-only live exact identity + public native group/member checks. Allow is last requested status, not a private native allow-bit query. Logical close may precede safe-phase native retirement."));
        Row->SetObjectField(TEXT("owned_pawn_scope"), Owned);
    }
    Row->SetObjectField(TEXT("acknowledged_pawn"), Ref(PC->AcknowledgedPawn));
    Row->SetObjectField(TEXT("pc_state"), Ref(PC->GetPlayerState<APlayerState>()));
    Row->SetStringField(TEXT("view_sample_scope"), TEXT("Raw camera targets and stored camera cache only; no GetViewTarget/CheckViewTarget/GetPlayerViewPoint call, fallback evaluation or effective filter-view claim"));
    auto* Connection = PC->GetNetConnection();
    if (!Connection && World->GetNetDriver()) Connection = World->GetNetDriver()->ServerConnection.Get();
    Row->SetObjectField(TEXT("connection_view_target"), Ref(Connection ? Connection->ViewTarget.Get() : nullptr));
    Row->SetNumberField(TEXT("parent_connection"), Connection ? int32(Connection->GetConnectionHandle().GetParentConnectionId()) : -1);
    if (auto* Camera = PC->PlayerCameraManager.Get())
    {
        // Bypass virtual overrides: read only the base manager's stored cache.
        const FMinimalViewInfo& CachedPOV = Camera->APlayerCameraManager::GetCameraCacheView();
        Row->SetObjectField(TEXT("camera_raw_view_target"), Ref(Camera->ViewTarget.Target.Get()));
        Row->SetObjectField(TEXT("camera_raw_pending_view_target"), Ref(Camera->PendingViewTarget.Target.Get()));
        Row->SetNumberField(TEXT("camera_cache_time"), Camera->GetCameraCacheTime());
        Row->SetStringField(TEXT("camera_position_uu"), CachedPOV.Location.ToString());
        Row->SetStringField(TEXT("camera_rotation"), CachedPOV.Rotation.ToString());
        Row->SetBoolField(TEXT("client_camera_updates"), Camera->bUseClientSideCameraUpdates);
        Row->SetBoolField(TEXT("should_send_camera"), Camera->bShouldSendClientSideCameraUpdate);
    }
    TArray<TSharedPtr<FJsonValue>> Pawns;
    for (TActorIterator<AProject_JPlayerCharacter> It(World); It && Pawns.Num() < 8; ++It)
    {
        auto Actor = Ref(*It);
        Actor->SetObjectField(TEXT("raw_ps"), Ref(It->GetPlayerState()));
        Actor->SetObjectField(TEXT("owner"), Ref(It->GetOwner()));
        Actor->SetObjectField(TEXT("controller"), Ref(It->GetController()));
        Actor->SetBoolField(TEXT("pc_pawn"), PC->GetPawn() == *It);
        Actor->SetBoolField(TEXT("locally_controlled"), It->IsLocallyControlled());
        Actor->SetObjectField(TEXT("resolved_asc"), Ref(It->GetAbilitySystemComponent()));
        Actor->SetObjectField(TEXT("resolved_attributes"), Ref(It->GetAttributeSet()));
        Actor->SetBoolField(TEXT("movement_component_present"), It->GetMovementComponent() != nullptr);
        Actor->SetBoolField(TEXT("owner_camera_request_prerequisites"), PC->GetPawn() == *It && It->GetController() == PC && !It->GetMovementComponent());
        Actor->SetNumberField(TEXT("cull_radius_uu"), FMath::Sqrt(It->GetNetCullDistanceSquared()));
        if (auto* Runtime = It->FindComponentByClass<UProject_JEquipmentRuntimeComponent>())
        {
            Actor->SetNumberField(TEXT("public_equipment"), Runtime->GetPublicEquipment().Items.Num());
            if (auto* Property = FindFProperty<FObjectPropertyBase>(Runtime->GetClass(), TEXT("BoundEquipmentManager")))
                Actor->SetObjectField(TEXT("bound_private_manager"), Ref(Property->GetObjectPropertyValue_InContainer(Runtime)));
        }
        Pawns.Add(MakeShared<FJsonValueObject>(Actor));
    }
    Row->SetArrayField(TEXT("player_pawns"), Pawns);
    TArray<TSharedPtr<FJsonValue>> States;
    int32 ForeignCohortStates = 0, ObserverStates = 0;
    const auto* OwnState = PC->GetPlayerState<AProject_JPlayerState>();
    for (TActorIterator<AProject_JPlayerState> It(World); It; ++It)
    {
        if (*It != OwnState && !It->IsActorBeingDestroyed())
        {
            if (It->GetPlayerId() == FixtureListenObserverPlayerId) ++ObserverStates;
            else ++ForeignCohortStates;
        }
        // Exact live cardinalities; preserve the bounded diagnostic payload.
        if (States.Num() >= 4) continue;
        auto State = Ref(*It);
        State->SetObjectField(TEXT("pawn_cache"), Ref(It->GetPawn())); State->SetObjectField(TEXT("actor_owner"), Ref(It->GetOwner()));
        auto* ASC = It->GetProjectJAbilitySystemComponent();
        State->SetObjectField(TEXT("asc"), Ref(ASC));
        if (IsValid(ASC))
        {
            State->SetObjectField(TEXT("asc_component_owner"), Ref(ASC->GetOwner()));
            State->SetObjectField(TEXT("asc_direct_owner"), Ref(ASC->GetOwnerActor()));
            State->SetObjectField(TEXT("asc_direct_avatar"), Ref(ASC->GetAvatarActor_Direct()));
            State->SetObjectField(TEXT("asc_cached_owner"), Ref(ASC->AbilityActorInfo.IsValid() ? ASC->AbilityActorInfo->OwnerActor.Get() : nullptr));
            State->SetObjectField(TEXT("asc_cached_avatar"), Ref(ASC->GetAvatarActor()));
            State->SetStringField(TEXT("asc_epoch"), LexToString(ASC->GetActorInfoRevision()));
            State->SetBoolField(TEXT("asc_changing"), ASC->IsActorInfoChanging());
        }
        States.Add(MakeShared<FJsonValueObject>(State));
    }
    Row->SetArrayField(TEXT("states"), States);
    Row->SetNumberField(TEXT("foreign_cohort_states"), ForeignCohortStates);
    Row->SetNumberField(TEXT("observer_states"), ObserverStates);
    Row->SetBoolField(TEXT("own_state_observer_marker"), OwnState && OwnState->GetPlayerId() == FixtureListenObserverPlayerId);
    Row->SetStringField(TEXT("creation_parent_scope"), TEXT("Raw Pawn PS and current State/Pawn full handles identify the contract participants; live Iris creation-link graph has no public getter and is not queried"));
    FString Output;
    const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(Row, Writer);
    return Output + TEXT("\n");
}
// Explicit public detached-camera contract probe; reads stored cache without
// invoking mutating GetViewTarget/CheckViewTarget fallback during observation.
bool PartyOwnerViewDetached(AProject_JNetworkLoadController* PC, APawn* Retired)
{
    const APawn* const Pawn = IsValid(PC) ? PC->GetPawn().Get() : nullptr;
    const auto* Camera = IsValid(PC) ? PC->PlayerCameraManager.Get() : nullptr;
    if (!IsValid(Pawn) || !IsValid(Retired) || Pawn == Retired || !IsValid(Camera)
        || Camera->ViewTarget.Target != Retired || Camera->GetCameraCacheTime() <= 0) return false;
    const FVector Position = Camera->APlayerCameraManager::GetCameraCacheView().Location;
    const double RadiusSq = Pawn->GetNetCullDistanceSquared();
    return !Position.ContainsNaN() && FMath::IsFinite(RadiusSq) && RadiusSq > 0
        && FVector::DistSquared(Position, Pawn->GetActorLocation()) > 4.0 * RadiusSq;
}
bool PartyOwnerViewRestored(AProject_JNetworkLoadController* PC)
{
    const APawn* const Pawn = IsValid(PC) ? PC->GetPawn().Get() : nullptr;
    const auto* Camera = IsValid(PC) ? PC->PlayerCameraManager.Get() : nullptr;
    if (!IsValid(Pawn) || !IsValid(Camera) || Camera->ViewTarget.Target != Pawn || Camera->GetCameraCacheTime() <= 0) return false;
    const FVector Position = Camera->APlayerCameraManager::GetCameraCacheView().Location;
    const double RadiusSq = Pawn->GetNetCullDistanceSquared();
    return !Position.ContainsNaN() && FMath::IsFinite(RadiusSq) && RadiusSq > 0
        && FVector::DistSquared(Position, Pawn->GetActorLocation()) < RadiusSq;
}
FString PartyOwnerViewCSV(AProject_JNetworkLoadController* PC, int32 Phase, int32 Observer, APawn* Retired, double StableSeconds)
{
    APawn* const Pawn = PC->GetPawn().Get(); auto* State = PC->GetPlayerState<AProject_JPlayerState>();
    auto* ASC = State ? State->GetProjectJAbilitySystemComponent() : nullptr;
    auto* Camera = PC->PlayerCameraManager.Get();
    auto* Bridge = UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(PC);
    const auto Id = [Bridge](const UObject* Object) { return Bridge && IsValid(Object) ? Bridge->GetReplicatedRefHandle(Object).GetId() : uint64(0); };
    const bool Required = Phase == 29 && (Observer == -1 || Observer == 1);
    const bool Linked = Pawn && State && Pawn->GetPlayerState() == State && State->GetPawn() == Pawn && ASC
        && ASC->GetOwnerActor() == State && ASC->GetAvatarActor_Direct() == Pawn && ASC->GetAvatarActor() == Pawn && Pawn->GetController() == PC;
    const double Distance = Camera && Pawn ? FVector::Distance(Camera->APlayerCameraManager::GetCameraCacheView().Location, Pawn->GetActorLocation()) : -1;
    const double Radius = Pawn ? FMath::Sqrt(Pawn->GetNetCullDistanceSquared()) : -1;
    const bool Passed = Linked && Pawn->GetLocalRole() == (Observer == -1 ? ROLE_Authority : ROLE_AutonomousProxy)
        && StableSeconds >= (Required ? 8.0 : 1.0) && (Required ? PartyOwnerViewDetached(PC, Retired) : PartyOwnerViewRestored(PC));
    return FString::Printf(TEXT("%d,%d,%d,%llu,%llu,%llu,%llu,%llu,%d,%d,%d,%.3f,%.3f,%.3f,%d,%d\n"),
        Phase, Observer, Observer == -1 ? 1 : Observer, Id(Pawn), Id(State ? State->GetPawn() : nullptr), Id(ASC ? ASC->GetAvatarActor_Direct() : nullptr),
        Id(Retired), Id(Camera ? Camera->ViewTarget.Target.Get() : nullptr), Pawn ? int32(Pawn->GetLocalRole()) : -1, Pawn && Pawn->GetController() == PC,
        Required && PartyOwnerViewDetached(PC, Retired), Distance, Radius, StableSeconds, Required ? 1 : 0, Passed);
}
int32 PartyExpectedCount(int32 Phase, int32 Slot)
{
    if (Phase == 20) return 0;
    if (Phase == 21 || Phase == 24) return Slot == 0 ? 1 : 0;
    return Phase >= 26 ? 8 : 2;
}
FString PartyMemberCSV(int32 Phase, int32 Slot, const TArray<FProject_JPartyMemberSummary>& Rows)
{
    FString Result;
    if (Rows.IsEmpty()) return FString::Printf(TEXT("%d,%d,-1,,,0,0,,0,0,0,1\n"), Phase, Slot);
    for (const auto& Row : Rows)
        Result += FString::Printf(TEXT("%d,%d,%d,%s,%s,%d,%d,%s,%d,%d,%d,1\n"), Phase, Slot, Row.Order,
            *Row.CharacterId.ToString(EGuidFormats::Digits), *Row.PartyId.ToString(), Row.bLeader, Row.bAvailableHere,
            *Row.DisplayName, Row.Level, Row.bHealthKnown, Row.HealthPermille);
    return Result;
}
}
void AProject_JNetworkLoadController::ClientPartyStage_Implementation(int32 Phase)
{
    if (!PartyRosterEnabled() || Phase < 20 || Phase > PartyLastSupportedPhase()) return;
    CurrentStage = Phase; PartyStableSince = 0; NextObservation = 0;
}
void AProject_JNetworkLoadController::ObserveParty()
{
    const double Now = FPlatformTime::Seconds();
    if (Now < NextObservation) return;
    NextObservation = Now + .25;
    if (bPartyScreenshotProcessed && PartyScreenshotHandle.IsValid())
    {
        // The callback has returned before this poll removes its lambda captures.
        FScreenshotRequest::OnScreenshotRequestProcessed().Remove(PartyScreenshotHandle); PartyScreenshotHandle.Reset();
        if (!FPaths::FileExists(OutputRoot() / TEXT("party-roster.png"))) bPartyViolation = true;
    }
    auto* State = GetPlayerState<AProject_JPlayerState>();
    if (!State || !State->GetCharacterId().IsValid() || LocalSlot < 0 || LocalSlot > 1) return;
    if (!PartyUI)
    {
        // The fixture Controller is plain APlayerController. Attach the existing
        // production local-screen owner; no special panel/model implementation.
        PartyUI = NewObject<UProject_JPlayerUIComponent>(this);
        AddInstanceComponent(PartyUI); PartyUI->RegisterComponent();
    }
    const auto Members = State->GetPartyRoster();
    const int32 Expected = PartyExpectedCount(CurrentStage, LocalSlot);
    int32 ForeignRows = 0, RemotePawns = 0, ForeignStates = 0, ObserverStates = 0, ForeignPrivate = 0;
    bool Linked = true, RetiredCleared = true; float RemoteHealth = 0;
    int32 RetiredPawns = 0, RemotePublic = 0;
    AProject_JPlayerCharacter* RetiredPawn = nullptr;
    AProject_JPlayerCharacter* CurrentPeer = LocalSlot == 1 ? Cast<AProject_JPlayerCharacter>(GetPawn()) : nullptr;
    const bool Near = AudienceNearPhase(CurrentStage);
    APawn* OwnPawn = GetPawn();
    auto* OwnASC = State->GetProjectJAbilitySystemComponent();
    const int32 OwnBag = State->GetInventoryComponent()->GetItemInstances().Num();
    const int32 OwnPrivate = State->GetEquipmentManagerComponent()->GetPrivateEquipmentCount();
    auto* OwnRuntime = OwnPawn ? OwnPawn->FindComponentByClass<UProject_JEquipmentRuntimeComponent>() : nullptr;
    const int32 OwnPublic = OwnRuntime ? OwnRuntime->GetPublicEquipment().Items.Num() : -1;
    FProject_JItemInstanceData OwnWeapon;
    const bool HasOwnWeapon = State->GetEquipmentManagerComponent()->GetEquippedItemInstance(EProject_JEquipmentSlot::Weapon, OwnWeapon);
    const bool OwnLinked = OwnPawn && OwnPawn->GetPlayerState() == State && State->GetPawn() == OwnPawn && OwnASC &&
        OwnASC->GetOwnerActor() == State && OwnASC->GetAvatarActor() == OwnPawn;
    const int32 ExpectedForeignStates = Near || (AudienceLifetimeAfterReplacement(CurrentStage) && LocalSlot == 0 && CurrentStage != 27) ? 1 : 0;
    for (TActorIterator<AProject_JPlayerState> It(GetWorld()); It; ++It)
        if (*It != State && !It->IsActorBeingDestroyed())
        {
            // Only the explicit startup role marker is outside the remote
            // participant count. Unknown/default IDs remain ordinary foreign PS.
            if (It->GetPlayerId() == FixtureListenObserverPlayerId) ++ObserverStates;
            else ++ForeignStates;
            // The observer is still foreign: never skip its privacy checks.
            ForeignRows += It->GetPartyRoster().Num();
            ForeignPrivate += It->GetInventoryComponent()->GetItemInstances().Num() + It->GetEquipmentManagerComponent()->GetPrivateEquipmentCount();
            if (const auto* Quest = It->FindComponentByClass<UProject_JQuestComponent>()) ForeignPrivate += Quest->States.Num();
            ForeignPrivate += It->GetAccountId().IsValid() || It->GetCharacterId().IsValid();
        }
    for (TActorIterator<AProject_JPlayerCharacter> It(GetWorld()); It; ++It)
        if (!It->IsActorBeingDestroyed() && It->GetPlayerState() != State)
        {
            if (PlayerStateAudienceLifetimeEnabled() && !It->GetPlayerState())
            {
                ++RetiredPawns;
                RetiredPawn = *It;
                RetiredCleared &= !It->GetAbilitySystemComponent() && !It->GetController();
                continue;
            }
            ++RemotePawns;
            if (LocalSlot == 0) CurrentPeer = *It;
            auto* RemoteRuntime = It->FindComponentByClass<UProject_JEquipmentRuntimeComponent>();
            RemotePublic += RemoteRuntime ? RemoteRuntime->GetPublicEquipment().Items.Num() : -100;
            auto* RemoteState = It->GetPlayerState<AProject_JPlayerState>();
            auto* ASC = RemoteState ? RemoteState->GetProjectJAbilitySystemComponent() : nullptr;
            Linked &= IsValid(RemoteState) && ASC && It->GetAbilitySystemComponent() == ASC && ASC->GetOwnerActor() == RemoteState && ASC->GetAvatarActor() == *It && RemoteState->GetPawn() == *It;
            RemoteHealth = ASC ? ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()) : -1;
        }
    if (ForeignRows != 0 || (PlayerStateAudienceEnabled() && ForeignPrivate != 0)
        || State->GetPlayerId() == FixtureListenObserverPlayerId || ObserverStates > 1) bPartyViolation = true;
    const int32 Presented = PartyUI->GetPresentedPartyMemberCount();
    bool Ready = !bPartyViolation && Members.Num() == Expected && Presented == Expected && RemotePawns == (Near ? 1 : 0);
    if (PlayerStateAudienceEnabled()) Ready &= ForeignStates == ExpectedForeignStates && Linked && (!Near || FMath::IsNearlyEqual(RemoteHealth, LocalSlot == 0 ? 60.f : 80.f));
    if (PlayerStateAudienceLifetimeEnabled())
    {
        const bool OwnEquipped = AudienceLifetimeAfterReplacement(CurrentStage) && LocalSlot == 1;
        const int32 ExpectedPublic = Near && AudienceLifetimeAfterReplacement(CurrentStage) && LocalSlot == 0 ? 1 : 0;
        Ready &= OwnLinked && RetiredCleared && RetiredPawns == (CurrentStage == 30 || (PlayerStateAudienceOwnerViewEnabled() && CurrentStage == 29 && LocalSlot == 1) ? 1 : 0) &&
            OwnBag == 0 && OwnPrivate == int32(OwnEquipped) && OwnPublic == 0 && RemotePublic == ExpectedPublic &&
            HasOwnWeapon == OwnEquipped && (!OwnEquipped || OwnWeapon.InstanceId.IsValid());
    }
    TSet<FGuid> Ids; FName Group; bool bOwnMember = false;
    for (int32 Index = 0; Index < Members.Num(); ++Index)
    {
        const auto& Row = Members[Index];
        const bool Available = Index < 2 && !(CurrentStage == 27 && Index == 1);
        const int32 Health = Index == 0 ? 800 : CurrentStage >= 23 ? 600 : 400;
        const FString Name = Index == 0 ? TEXT("PartyFixtureLeader") : TEXT("PartyFixtureMember");
        Ready &= Row.CharacterId.IsValid() && !Ids.Contains(Row.CharacterId) && !Row.PartyId.IsNone()
            && Row.Order == Index && Row.bLeader == (Index == 0) && Row.bAvailableHere == Available;
        if (!Group.IsNone()) Ready &= Group == Row.PartyId;
        Group = Row.PartyId; Ids.Add(Row.CharacterId);
        bOwnMember |= Row.CharacterId == State->GetCharacterId().Value;
        if (Available) Ready &= Row.DisplayName == Name && Row.Level == 1 && Row.bHealthKnown && Row.HealthPermille == Health;
        else Ready &= Row.DisplayName.IsEmpty() && Row.Level == 0 && !Row.bHealthKnown && Row.HealthPermille == 0;
    }
    Ready &= Expected == 0 || bOwnMember;
    if (Ready && PlayerStateAudienceLifetimeEnabled() && CurrentStage == 30 && !bLatePlayerStateCallbackProbed)
    {
        // Actual client role, controlled callback probe. Stage only the old
        // replicated field: public SetPlayerState would corrupt State.Pawn
        // before the production pre-Super guard gets a chance to reject it.
        auto* PeerState = CurrentPeer ? CurrentPeer->GetPlayerState<AProject_JPlayerState>() : nullptr;
        auto* PeerASC = PeerState ? PeerState->GetProjectJAbilitySystemComponent() : nullptr;
        auto* PSProperty = FindFProperty<FObjectPropertyBase>(APawn::StaticClass(), TEXT("PlayerState"));
        auto* ManagerProperty = FindFProperty<FObjectPropertyBase>(UProject_JEquipmentRuntimeComponent::StaticClass(), TEXT("BoundEquipmentManager"));
        auto* OldRuntime = RetiredPawn ? RetiredPawn->FindComponentByClass<UProject_JEquipmentRuntimeComponent>() : nullptr;
        bool Passed = GetNetMode() == NM_Client && RetiredPawn && !RetiredPawn->HasAuthority() && CurrentPeer &&
            !CurrentPeer->HasAuthority() && PeerState && PeerASC && PSProperty && ManagerProperty && OldRuntime &&
            !RetiredPawn->GetPlayerState() && PeerState->GetPawn() == CurrentPeer && PeerASC->GetAvatarActor() == CurrentPeer;
        bLatePlayerStateCallbackProbed = true;
        if (Passed)
        {
            const uint64 Revision = PeerASC->GetActorInfoRevision();
            AActor* ObservedOwner = PeerASC->GetOwnerActor();
            AActor* DirectAvatar = PeerASC->GetAvatarActor_Direct();
            PSProperty->SetObjectPropertyValue_InContainer(RetiredPawn, PeerState);
            RetiredPawn->ProcessEvent(RetiredPawn->FindFunctionChecked(TEXT("OnRep_PlayerState")), nullptr);
            Passed = PeerState->GetPawn() == CurrentPeer && PeerASC->GetOwnerActor() == ObservedOwner && ObservedOwner == PeerState &&
                PeerASC->GetAvatarActor_Direct() == DirectAvatar && DirectAvatar == CurrentPeer &&
                PeerASC->GetAvatarActor() == CurrentPeer && PeerASC->GetActorInfoRevision() == Revision &&
                CurrentPeer->GetAbilitySystemComponent() == PeerASC && !RetiredPawn->GetAbilitySystemComponent() &&
                !RetiredPawn->GetAttributeSet() && !ManagerProperty->GetObjectPropertyValue_InContainer(OldRuntime);
            // Restore only the injected old field. Never repair the tuple/back-
            // reference on failure; that would hide the original regression.
            PSProperty->SetObjectPropertyValue_InContainer(RetiredPawn, nullptr);
        }
        bLatePlayerStateCallbackInvariant = Passed;
        if (!Passed) bPartyViolation = true;
    }
    if (PlayerStateAudienceLifetimeEnabled() && CurrentStage == 30)
        Ready &= bLatePlayerStateCallbackProbed && bLatePlayerStateCallbackInvariant && !bPartyViolation;
    if (Ready && CurrentStage == 26 && FParse::Param(FCommandLine::Get(), TEXT("ProjectJCapturePartyUI")))
    {
        if (!bPartyScreenshotRequested)
        {
            if (FScreenshotRequest::IsScreenshotRequested()) Ready = false;
            else
            {
                IFileManager::Get().MakeDirectory(*OutputRoot(), true);
                bPartyScreenshotRequested = true;
                PartyScreenshotHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddWeakLambda(this, [this]
                {
                    bPartyScreenshotProcessed = true;
                });
                FScreenshotRequest::RequestScreenshot(OutputRoot() / TEXT("party-roster.png"), true, false);
            }
        }
        Ready &= bPartyScreenshotProcessed;
    }
    if (PlayerStateAudienceOwnerViewEnabled() && (CurrentStage == 29 || CurrentStage == 31))
        Ready &= OwnPawn && OwnASC && OwnPawn->GetController() == this && OwnPawn->GetLocalRole() == ROLE_AutonomousProxy && OwnASC->GetAvatarActor_Direct() == OwnPawn;
    if (PlayerStateAudienceOwnerViewEnabled() && CurrentStage == 29)
        Ready &= (LocalSlot == 1 ? PartyOwnerViewDetached(this, RetiredPawn) : PartyOwnerViewRestored(this));
    if (PlayerStateAudienceOwnerViewEnabled() && CurrentStage == 31) Ready &= PartyOwnerViewRestored(this);
    if (Ready) { if (PartyStableSince == 0) PartyStableSince = Now; }
    else PartyStableSince = 0;
    const double RequiredStableSeconds = PlayerStateAudienceOwnerViewEnabled() && CurrentStage == 29 && LocalSlot == 1 ? 8.0 : 1.0;
    const bool Stable = Ready && Now - PartyStableSince >= RequiredStableSeconds;
    if (Stable && PartyRecordedPhase != CurrentStage)
    {
        PartyRows += FString::Printf(TEXT("%d,%d,%d,%d,%d,%d,1\n"), CurrentStage, LocalSlot, Members.Num(), ForeignRows, RemotePawns, Presented);
        PartyMemberRows += PartyMemberCSV(CurrentStage, LocalSlot, Members);
        if (PlayerStateAudienceEnabled()) PlayerStateInterestRows += FString::Printf(TEXT("%d,%d,%d,%d,%d,%d,%.1f,1\n"), CurrentStage, LocalSlot, ForeignStates, RemotePawns, ForeignPrivate, Linked, RemoteHealth);
        if (PlayerStateAudienceLifetimeEnabled()) PlayerStateLifetimeRows += FString::Printf(TEXT("%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%d,%d,1\n"), CurrentStage, LocalSlot, RetiredPawns, RetiredCleared, OwnLinked, OwnBag, OwnPrivate, OwnPublic, RemotePublic, HasOwnWeapon ? *OwnWeapon.InstanceId.ToString(EGuidFormats::Digits) : TEXT(""), CurrentStage == 30 && bLatePlayerStateCallbackProbed, CurrentStage == 30 && bLatePlayerStateCallbackInvariant);
        if (PlayerStateAudienceLifetimeEnabled()) PartyOwnershipDiagnosticRows += PartyOwnershipSnapshot(this, CurrentStage, LocalSlot, TEXT("stable"));
        if (PlayerStateAudienceOwnerViewEnabled() && (CurrentStage == 29 || CurrentStage == 31)) OwnerViewRows += PartyOwnerViewCSV(this, CurrentStage, LocalSlot, RetiredPawn, Now - PartyStableSince);
        PartyRecordedPhase = CurrentStage;
    }
    ServerPartyObserved(CurrentStage, Stable, bPartyViolation, FString::Printf(TEXT("owner=%d expected=%d foreign=%d remotePawn=%d actualHUD=%d shot=%d foreignPS=%d observerPS=%d ownPlayerId=%d private=%d linked=%d hp=%.1f retired=%d clear=%d own=%d"), Members.Num(), Expected, ForeignRows, RemotePawns, Presented, bPartyScreenshotProcessed, ForeignStates, ObserverStates, State->GetPlayerId(), ForeignPrivate, Linked, RemoteHealth, RetiredPawns, RetiredCleared, OwnLinked));
}
void AProject_JNetworkLoadController::ServerPartyObserved_Implementation(int32 Phase, bool bReady, bool bViolation, const FString& Detail)
{
    if (!PartyRosterEnabled() || Detail.Len() > 256) return;
    if (auto* Mode = GetWorld()->GetAuthGameMode<AProject_JNetworkLoadGameMode>()) Mode->AcceptPartyObservation(this, Phase, bReady, bViolation, Detail);
}
void AProject_JNetworkLoadGameMode::AcceptPartyObservation(AProject_JNetworkLoadController* PC, int32 Phase, bool bReady, bool bViolation, const FString& Detail)
{
    if (!bFixture || bFinished || !PartyRosterEnabled() || !Clients.Contains(PC) || Phase != Stage || Phase < 20 || Phase > PartyLastSupportedPhase()) return;
    PC->LastObservation = FString::Printf(TEXT("party phase=%d %s"), Phase, *Detail);
    if (PlayerStateAudienceEnabled() && !bReady)
    {
        FProjectJPlayerStateAudienceBinding Binding;
        auto* Filter = UProject_JPlayerStateAudienceFilter::Find(GetWorld());
        const bool Found = Filter && Filter->QueryBinding(PC->GetPlayerState<AProject_JPlayerState>(), Binding);
        PC->LastObservation += FString::Printf(TEXT(" binding=%d assigned=%d spatial=%d fallback=%s center=%s radius=%.1f"),
            Found, Binding.bAssigned, Binding.bSpatial, *UProject_JPlayerStateAudienceFilter::DescribeFallback(Binding.FallbackReasons), *Binding.Center.ToCompactString(), Binding.Radius);
    }
    if (bViolation) { Finish(false, TEXT("Actual private roster or screenshot contract violated")); return; }
    PC->ConfirmedStage = bReady ? Stage : INDEX_NONE;
}
void AProject_JNetworkLoadGameMode::StartPartyStage(int32 Phase)
{
    const int32 Required = Phase == 27 ? 1 : 2;
    if (Clients.Num() != 2) { Finish(false, TEXT("Party cohort needs exactly2 clients")); return; }
    for (int32 Slot = 0; Slot < Required; ++Slot)
        if (!IsValid(Clients[Slot]) || !Clients[Slot]->GetPlayerState<AProject_JPlayerState>() || !Clients[Slot]->GetPawn())
        { Finish(false, TEXT("Party owner retired before expected phase")); return; }
    for (int32 Slot = 0; Slot < Required; ++Slot)
        if (Clients[Slot]->GetPlayerState<AProject_JPlayerState>()->GetPlayerId() == FixtureListenObserverPlayerId)
        { Finish(false, TEXT("Party participant has reserved listen observer PlayerId")); return; }
    auto* Social = GetGameInstance()->GetSubsystem<UProject_JSocialSubsystem>();
    auto* Leader = Clients[0]->GetPlayerState<AProject_JPlayerState>();
    auto* Peer = Phase == 27 ? nullptr : Clients[1]->GetPlayerState<AProject_JPlayerState>();
    if (!Social) { Finish(false, TEXT("Actual social owner unavailable")); return; }
    Stage = Phase; StageStarted = FPlatformTime::Seconds();
    if (Phase == 20)
    {
        for (int32 Slot = 0; Slot < 2; ++Slot)
        {
            auto* Player = CastChecked<AProject_JPlayerCharacter>(Clients[Slot]->GetPawn());
            const auto* Authored = Player->GetClass()->GetDefaultObject<AProject_JPlayerCharacter>();
            const float Radius = FMath::Sqrt(Authored->GetNetCullDistanceSquared());
            if (Authored->bAlwaysRelevant || Authored->bOnlyRelevantToOwner || !FMath::IsFinite(Radius) || Radius <= 0 || Radius >= 35000)
            { Finish(false, TEXT("Authored Pawn relevance does not support the bounded far-roster fixture; no force override was applied")); return; }
            // Undo only the old animation helper's1000m fixture override. Production
            // CDO relevance is preserved, and no party/full-Pawn filter is installed.
            Player->SetNetCullDistanceSquared(Authored->GetNetCullDistanceSquared());
            auto Teleport = MakeShared<FTeleportEffect>(); Teleport->TargetLocation = FVector(Slot == 0 ? -40000 : 40000, 0, 100);
            Player->GetMover()->QueueInstantMovementEffect(Teleport); Player->ForceNetUpdate(); Clients[Slot]->SetViewTarget(Player);
            UE_LOG(LogProjectJDNetwork, Display, TEXT("PartyPawn authoredCull=%.0f always=%d slot=%d"), Radius, Player->bAlwaysRelevant, Slot);
            auto* State = Clients[Slot]->GetPlayerState<AProject_JPlayerState>();
            State->SetPlayerName(Slot == 0 ? TEXT("PartyFixtureLeader") : TEXT("PartyFixtureMember"));
            auto* ASC = State->GetProjectJAbilitySystemComponent();
            ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 100);
            ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), Slot == 0 ? 80 : 40);
        }
    }
    else if (Phase == 21)
    {
        const auto Created = Social->CreateParty(Leader);
        if (!Created.bSucceeded) { Finish(false, TEXT("Actual party creation failed")); return; }
        PartyFixtureGroup = Created.GroupId;
    }
    else if (Phase == 22 || Phase == 25)
    {
        if (!Social->JoinParty(Peer, PartyFixtureGroup).bSucceeded) { Finish(false, TEXT("Actual party join/rejoin failed")); return; }
    }
    else if (Phase == 23)
    {
        for (float Health : {25.f,35.f,60.f}) Peer->GetProjectJAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), Health);
    }
    else if (Phase == 24)
    {
        if (!Social->LeaveParty(Peer).bSucceeded) { Finish(false, TEXT("Actual party leave failed")); return; }
    }
    else if (Phase == 26)
    {
        FProject_JSocialGroupSnapshot Group;
        if (!Social->GetGroupSnapshot(EProject_JSocialGroupKind::Party, PartyFixtureGroup, Group)) { Finish(false, TEXT("Actual party snapshot missing")); return; }
        while (Group.Members.Num() < ProjectJMaxPartyRosterMembers) Group.Members.Add(FGuid::NewGuid());
        ++Group.Revision;
        if (!Social->RestoreGroupSnapshot(EProject_JSocialGroupKind::Party, Group)) { Finish(false, TEXT("Bounded offline-member snapshot failed")); return; }
    }
    if (PlayerStateAudienceLifetimeEnabled() && Phase == 30)
    {
        auto* Old = Cast<AProject_JPlayerCharacter>(Clients[1]->GetPawn());
        auto* Bridge = UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(Peer);
        if (!Old || !Bridge) { Finish(false, TEXT("Replacement source/bridge missing")); return; }
        RetiredAudienceHandle = Bridge->GetReplicatedRefHandle(Old);
        RetiredAudienceStateHandle = Bridge->GetReplicatedRefHandle(Peer);
        if (!RetiredAudienceHandle.IsCompleteHandle() || !RetiredAudienceStateHandle.IsCompleteHandle())
        { Finish(false, TEXT("Replacement requires actual complete old Pawn/State handles")); return; }
        RetiredAudiencePawn = Old;
        auto* NewPawn = ProjectJAuthoredAnimationFixture::Spawn(GetWorld(), Clients[1], Peer, Old->GetActorLocation(), true);
        if (!NewPawn || NewPawn == Old || Clients[1]->GetPawn() != NewPawn || Peer->GetPawn() != NewPawn ||
            !UE::Net::FNetRefHandle::FullCompare(Bridge->GetReplicatedRefHandle(Peer), RetiredAudienceStateHandle) ||
            Old->GetPlayerState() || Old->GetAbilitySystemComponent() || Old->GetController())
        { Finish(false, TEXT("Actual replacement failed to preserve State or clear old Pawn ownership")); return; }
        // Undo only the shared helper's socket-fixture cull override. Do not alter old Actor
        // ownership or ASC references: production UnPossessed must have cleared them.
        NewPawn->SetNetCullDistanceSquared(NewPawn->GetClass()->GetDefaultObject<AProject_JPlayerCharacter>()->GetNetCullDistanceSquared());
        NewPawn->ForceNetUpdate(); Old->ForceNetUpdate(); Clients[1]->SetViewTarget(NewPawn);
        FProject_JItemInstanceData Weapon;
        if (!Peer->GetEquipmentManagerComponent()->GetEquippedItemInstance(EProject_JEquipmentSlot::Weapon, Weapon) || !Weapon.InstanceId.IsValid())
        { Finish(false, TEXT("Shared authored spawn failed to provide its prototype weapon")); return; }
        ReplacementAudienceWeapon = Weapon.InstanceId;
    }
    if (PlayerStateAudienceLifetimeEnabled() && Phase == 31)
    {
        // Restore the real current-Pawn view before retiring the old public actor.
        if (PlayerStateAudienceOwnerViewEnabled()) Clients[1]->SetViewTarget(Clients[1]->GetPawn());
        auto* Old = RetiredAudiencePawn.Get();
        if (!Old || Old->IsActorBeingDestroyed() || !Old->Destroy())
        { Finish(false, TEXT("Actual old Pawn destruction failed")); return; }
        bRetiredAudienceDestroyed = true;
    }
    if (PlayerStateAudienceEnabled() && (Phase == 28 || Phase == 29))
    {
        for (int32 Slot = 0; Slot < 2; ++Slot)
        {
            auto* Player = CastChecked<AProject_JPlayerCharacter>(Clients[Slot]->GetPawn());
            const double Distance = Phase == 28 ? 2000.0 : 40000.0;
            auto Teleport = MakeShared<FTeleportEffect>(); Teleport->TargetLocation = FVector(Slot == 0 ? -Distance : Distance, 0, 100);
            Player->GetMover()->QueueInstantMovementEffect(Teleport); Player->ForceNetUpdate();
            if (PlayerStateAudienceOwnerViewEnabled() && Phase == 29 && Slot == 1)
            {
                auto* Old = RetiredAudiencePawn.Get();
                if (!IsValid(Old) || Old->IsActorBeingDestroyed()) { Finish(false, TEXT("Detached owner-view requires the retained actual old Pawn")); return; }
                OwnerViewDetachedSince = 0;
                Clients[Slot]->SetViewTarget(Old); // Normal native ClientSetViewTarget/cache path; no synthetic camera writes.
            }
            else Clients[Slot]->SetViewTarget(Player);
        }
    }
    for (int32 Slot = 0; Slot < Required; ++Slot)
    {
        Clients[Slot]->ConfirmedStage = INDEX_NONE; Clients[Slot]->ClientPartyStage(Phase);
    }
}
void AProject_JNetworkLoadGameMode::RecordPartyAuthority()
{
    const int32 Required = Stage == 27 ? 1 : 2;
    auto* Filter = PlayerStateAudienceEnabled() ? UProject_JPlayerStateAudienceFilter::Find(GetWorld()) : nullptr;
    const FProjectJPlayerStateAudienceStats Stats = Filter ? Filter->GetStats() : FProjectJPlayerStateAudienceStats();
    uint32 ObserverCount = 0, ObserverSpatial = 0, ObserverFallback = 0;
    int32 ObserverPlayerId = 0;
    FProjectJPlayerStateAudienceBinding ObserverBinding;
    UE::Net::FNetRefHandle ObserverHandle;
    bool ObserverBindingValid = GetNetMode() == NM_DedicatedServer || GetNetMode() == NM_ListenServer;
    if (PlayerStateAudienceEnabled())
    {
        AProject_JPlayerState* ObserverState = nullptr;
        for (TActorIterator<AProject_JPlayerState> It(GetWorld()); It; ++It)
            if (!It->IsActorBeingDestroyed() && It->GetPlayerId() == FixtureListenObserverPlayerId)
            { ++ObserverCount; ObserverState = *It; }
        if (ObserverCount == 1)
        {
            ObserverPlayerId = ObserverState->GetPlayerId();
            auto* ObserverPC = Cast<AProject_JNetworkLoadController>(ObserverState->GetOwner());
            auto* Bridge = UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(ObserverState);
            ObserverHandle = Bridge ? Bridge->GetReplicatedRefHandle(ObserverState) : UE::Net::FNetRefHandle();
            // QueryBinding verifies the current system, full StateHandle and
            // exact entry/state/index. Never turn an unknown global entry into
            // an observer merely because the raw global total has one extra.
            ObserverBindingValid &= GetNetMode() == NM_ListenServer && ObserverState->HasAuthority() &&
                ObserverPC && ObserverPC->IsLocalController() && !ObserverPC->GetNetConnection() &&
                ObserverPC->GetPlayerState<AProject_JPlayerState>() == ObserverState && ObserverPC->FixtureSlot == INDEX_NONE && !Clients.Contains(ObserverPC) &&
                ObserverHandle.IsCompleteHandle() && Bridge->GetReplicatedObject(ObserverHandle) == ObserverState &&
                Filter && Filter->QueryBinding(ObserverState, ObserverBinding) && ObserverBinding.bAssigned && ObserverBinding.ObjectIndex > 0;
            if (ObserverBindingValid)
            {
                ObserverSpatial = ObserverBinding.bSpatial && ObserverBinding.FallbackReasons == 0 ? 1u : 0u;
                ObserverFallback = !ObserverBinding.bSpatial && ObserverBinding.FallbackReasons != 0 ? 1u : 0u;
            }
        }
        const uint32 ExpectedObservers = GetNetMode() == NM_ListenServer ? 1u : 0u;
        ObserverBindingValid &= ObserverCount == ExpectedObservers && ObserverSpatial + ObserverFallback == ObserverCount;
    }
    for (int32 Slot = 0; Slot < Required; ++Slot)
        if (auto* State = Clients[Slot]->GetPlayerState<AProject_JPlayerState>())
        {
            PartyAuthorityRows += PartyMemberCSV(Stage, Slot, State->GetPartyRoster());
            if (PlayerStateAudienceLifetimeEnabled()) PartyOwnershipAuthorityDiagnosticRows += PartyOwnershipSnapshot(Clients[Slot], Stage, Slot, TEXT("stable"));
            if (PlayerStateAudienceOwnerViewEnabled() && (Stage == 29 || Stage == 31) && Slot == 1) OwnerViewAuthorityRows += PartyOwnerViewCSV(Clients[Slot], Stage, -1, RetiredAudiencePawn.Get(), FPlatformTime::Seconds() - (Stage == 29 ? OwnerViewDetachedSince : StageStarted));
            if (PlayerStateAudienceEnabled())
            {
                FProjectJPlayerStateAudienceBinding Binding;
                const bool WantsFallback = AudienceLifetimeAfterReplacement(Stage) && Slot == 1;
                const uint32 ExpectedFallbacks = AudienceLifetimeAfterReplacement(Stage) && Stage != 27 ? 1u : 0u;
                const bool Found = Filter && Filter->QueryBinding(State, Binding);
                const FString Reason = UProject_JPlayerStateAudienceFilter::DescribeFallback(Binding.FallbackReasons);
                bool Passed = Found && Binding.bAssigned && (WantsFallback ? !Binding.bSpatial && Reason == TEXT("RetiredSource") : Binding.bSpatial && Binding.FallbackReasons == 0) &&
                    Stats.ObservedPawnCreations >= (AudienceLifetimeAfterReplacement(Stage) ? 3u : 2u) && Stats.ConsumedPawnCreations == 2 && Stats.PendingWitnesses == 0 &&
                    ObserverBindingValid && (!ObserverCount || Binding.ObjectIndex != ObserverBinding.ObjectIndex) &&
                    Stats.FallbackBindings == ExpectedFallbacks + ObserverFallback && Stats.SpatialBindings == uint32(Required) - ExpectedFallbacks + ObserverSpatial;
                if (PlayerStateAudienceLifetimeEnabled())
                {
                    auto* Bridge = UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(State);
                    auto* Old = RetiredAudiencePawn.Get();
                    const bool OldAlive = IsValid(Old) && !Old->IsActorBeingDestroyed();
                    const bool OldRegistered = Bridge && RetiredAudienceHandle.IsCompleteHandle() && Bridge->GetReplicatedObject(RetiredAudienceHandle) != nullptr;
                    const bool OldCleared = !OldAlive || (!Old->GetPlayerState() && !Old->GetAbilitySystemComponent() && !Old->GetController());
                    const bool AfterReplacement = AudienceLifetimeAfterReplacement(Stage);
                    const bool AfterDestroy = Stage == 31 || Stage == 27;
                    if (AfterReplacement) Passed &= OldCleared && OldAlive == !AfterDestroy && OldRegistered == !AfterDestroy && bRetiredAudienceDestroyed == AfterDestroy;
                    const auto Current = Bridge ? Bridge->GetReplicatedRefHandle(Clients[Slot]->GetPawn()) : UE::Net::FNetRefHandle();
                    const auto StateHandle = Bridge ? Bridge->GetReplicatedRefHandle(State) : UE::Net::FNetRefHandle();
                    if (WantsFallback) Passed &= UE::Net::FNetRefHandle::FullCompare(StateHandle, RetiredAudienceStateHandle) && !UE::Net::FNetRefHandle::FullCompare(Current, RetiredAudienceHandle);
                    const int32 BagCount = State->GetInventoryComponent()->GetItemInstances().Num();
                    const int32 PrivateEquipment = State->GetEquipmentManagerComponent()->GetPrivateEquipmentCount();
                    auto* Runtime = Clients[Slot]->GetPawn()->FindComponentByClass<UProject_JEquipmentRuntimeComponent>();
                    const int32 PublicEquipment = Runtime ? Runtime->GetPublicEquipment().Items.Num() : -1;
                    FProject_JItemInstanceData Weapon;
                    const bool HasWeapon = State->GetEquipmentManagerComponent()->GetEquippedItemInstance(EProject_JEquipmentSlot::Weapon, Weapon);
                    Passed &= BagCount == 0 && PrivateEquipment == int32(WantsFallback) && PublicEquipment == int32(WantsFallback) &&
                        HasWeapon == WantsFallback && (!WantsFallback || Weapon.InstanceId == ReplacementAudienceWeapon);
                    PlayerStateLifetimeAuthorityRows += FString::Printf(TEXT("%d,%d,%llu,%u,%d,%d,%d,%llu,%llu,%d,%s,%d,%d,%d,%s,%d\n"), Stage, Slot,
                        RetiredAudienceHandle.GetId(), RetiredAudienceHandle.IsCompleteHandle() ? RetiredAudienceHandle.GetReplicationSystemId() : 0,
                        OldAlive, OldRegistered, OldCleared, Current.GetId(), StateHandle.GetId(), WantsFallback, *Reason, BagCount, PrivateEquipment, PublicEquipment, HasWeapon ? *Weapon.InstanceId.ToString(EGuidFormats::Digits) : TEXT(""), Passed);
                }
                // active_spatial/fallback_bindings remain raw global totals.
                // Add the exact excluded observer contribution as separate evidence.
                PlayerStateBindingRows += FString::Printf(TEXT("%d,%d,%d,%d,%u,%u,%u,%u,%u,%.1f,%llu,%llu,%u,%u,%u,%d,%u,%u,%u,%d,%u,%llu,%llu,%u,%d\n"), Stage, Slot, Binding.bAssigned, Binding.bSpatial, Binding.FallbackReasons,
                    Binding.ObjectIndex, Binding.PawnIndex, Binding.AvatarIndex, Binding.OwnerConnectionId, Binding.Radius, Stats.ObservedPawnCreations, Stats.ConsumedPawnCreations, Stats.PendingWitnesses, Stats.SpatialBindings, Stats.FallbackBindings, Passed,
                    ObserverCount, ObserverSpatial, ObserverFallback, ObserverPlayerId, ObserverBinding.ObjectIndex, ObserverHandle.GetId(), ObserverHandle.GetSerial(), ObserverHandle.IsCompleteHandle() ? ObserverHandle.GetReplicationSystemId() : 0, ObserverBindingValid);
                if (!Passed) { Finish(false, FString::Printf(TEXT("PlayerState binding absent or baseline fallback: slot=%d assigned=%d spatial=%d reasons=%s"), Slot, Binding.bAssigned, Binding.bSpatial, *UProject_JPlayerStateAudienceFilter::DescribeFallback(Binding.FallbackReasons))); return; }
            }
        }
}
