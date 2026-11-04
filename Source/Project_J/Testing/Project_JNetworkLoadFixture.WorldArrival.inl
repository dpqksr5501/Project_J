// Optional non-Shipping, current MainWorld production interaction/arrival acceptance.
// Included inside the existing network-fixture anonymous namespace after WriteResult.
#if !UE_BUILD_SHIPPING
void AppendArrivalPreparationStats(UWorld* World, FJsonObject& Row)
{
    const auto* Execution = World ? World->GetSubsystem<UProject_JWorldArrivalSubsystem>() : nullptr;
    if (!Execution) return;
    const auto& Stats = Execution->GetStats();
    Row.SetNumberField(TEXT("authority_preparation_permits"), Stats.ActivePreparationPermits);
    Row.SetNumberField(TEXT("authority_peak_preparation_permits"), Stats.PeakPreparationPermits);
    Row.SetNumberField(TEXT("authority_preparation_admissions"), double(Stats.PreparationAdmissions));
    Row.SetNumberField(TEXT("authority_preparation_deferrals"), double(Stats.PreparationDeferrals));
}

struct FProjectJPreparedWorldProbe : public TSharedFromThis<FProjectJPreparedWorldProbe>
{
    double Started = FPlatformTime::Seconds(), ArrivedAt = 0;
    bool bRequested = false, bObservedArrival = false;
    int32 LastPhase = INDEX_NONE;
    FVector Origin = FVector::ZeroVector, Hint = FVector::ZeroVector;
    TWeakObjectPtr<AProject_JPlayerCharacter> Pawn;
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<AProject_JWorldPortal> Portal;
    TWeakObjectPtr<UProject_JWorldStreamingComponent> Preparation;
    TArray<TSharedPtr<FJsonValue>> Samples;
    const bool bObserveHLOD = FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditHLODHandover"));
    TWeakObjectPtr<const UWorldPartitionRuntimeCell> HLODSourceCell;
    TWeakObjectPtr<UWorldPartition> HLODPartition;
    TWeakObjectPtr<AWorldPartitionHLOD> HLODProxy;
    FGuid HLODSourceGuid;
    struct FObservedHLODPart
    {
        TWeakObjectPtr<const UStaticMeshComponent> Component;
        TWeakObjectPtr<const UStaticMesh> Mesh;
        int32 Instances = 0;
    };
    TArray<FObservedHLODPart> HLODParts;
    TArray<TSharedPtr<FJsonValue>> HLODSamples;
    double NextHLODSample = 0;
    int32 HLODObservations = 0, LastHLODState = INDEX_NONE, LastHLODVisibleParts = INDEX_NONE, LastHLODAssociations = INDEX_NONE;
    bool bLastHLODAssociated = false;
    FString HLODFailure;
    bool bOriginHLODHidden = false, bSourceUnloaded = false, bHLODHandover = false;
    const bool bCaptureHLODRHI = FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditHLODRHICapture"));
    bool bOriginPrepared = false, bOriginShot = false, bFinalShot = false, bShotPending = false, bRHIError = false, bRHIDestinationPrepared = false;
    TWeakObjectPtr<ACameraActor> HLODView;
    TWeakObjectPtr<AActor> PreviousViewTarget;
    TWeakObjectPtr<UGameViewportClient> HLODViewport;
    FDelegateHandle HLODDrawHandle, HLODShotHandle;
    uint64 HLODDraws = 0, LastHLODDrawFrame = MAX_uint64, ShotStageDraws = 0, ShotRequestFrame = 0;
    int32 ShotStage = 0, LastDrawState = INDEX_NONE, LastDrawVisibleParts = INDEX_NONE;
    double ShotStageAt = 0;
    FString ShotPath;
    TArray<TSharedPtr<FJsonValue>> HLODDrawSamples, HLODShots;
    bool bSawCoarseBothHidden = false, bSawLevelAndProxyBothHidden = false;

    const bool bObserveResidency = FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditWorldResidency"));
    static constexpr int32 ResidencyCellLimit = 128, ResidencySourceLimit = 8, ResidencySampleLimit = 512;
    struct FResidencyCell
    {
        TWeakObjectPtr<const UWorldPartitionRuntimeLevelStreamingCell> Cell;
        FGuid Guid;
        FString Name;
        TWeakObjectPtr<const UWorldPartitionLevelStreamingDynamic> Level;
        bool bLevelSeen = false;
    };
    TArray<FResidencyCell> ResidencyCells;
    TWeakObjectPtr<UWorldPartition> ResidencyPartition;
    FTransform ResidencyPartitionTransform;
    TWeakObjectPtr<const UProject_JWorldDefinition> ResidencyDefinition;
    TWeakObjectPtr<AProject_JPlayerState> ResidencyPlayerState;
    TWeakObjectPtr<UProject_JAbilitySystemComponent> ResidencyASC;
    TSet<FName> ResidencyActiveLayers, ResidencyLoadedLayers;
    FWorldPartitionStreamingSource ResidencyDestination;
    FGuid ResidencyAccount, ResidencyCharacter;
    uint64 ResidencyOriginalEpoch = 0;
    FString ResidencyOldPawn, ResidencyNewPawn, ResidencyFailure;
    int32 ResidencyCycle = 1, ResidencyCompleted = 0, ResidencyStage = 0, ResidencyDrainSamples = 0;
    bool bResidencyRetired = false, bResidencyTransition = false, bResidencyLoading = false;
    double NextResidencySample = 0;
    TSharedPtr<FJsonObject> ResidencyCycleResult;
    TArray<TSharedPtr<FJsonValue>> ResidencyResults, ResidencySamples;
    FString ResidencyRows = TEXT("cycle,stage,elapsed_s,selected_cells,loading,has_loaded_level,async_requests,load_pending,streaming_pending,unloaded,other_demand,authority_permits,arrival_pending,global_async_packages,used_physical,used_virtual\n");

    static bool SameResidencyNames(const TSet<FName>& A, const TSet<FName>& B)
    {
        if (A.Num() != B.Num()) return false;
        for (FName Name : A) if (!B.Contains(Name)) return false;
        return true;
    }
    static bool SameResidencySource(const FWorldPartitionStreamingSource& A, const FWorldPartitionStreamingSource& B)
    {
        if (A.Name != B.Name || !A.Location.Equals(B.Location, .01) || !A.Rotation.Equals(B.Rotation, .001)
            || A.TargetState != B.TargetState || A.TargetBehavior != B.TargetBehavior || A.Priority != B.Priority
            || A.bForce2D != B.bForce2D || !SameResidencyNames(A.TargetGrids, B.TargetGrids) || A.Shapes.Num() != B.Shapes.Num()) return false;
        for (int32 I = 0; I < A.Shapes.Num(); ++I)
        {
            const auto& X = A.Shapes[I]; const auto& Y = B.Shapes[I];
            if (X.bUseGridLoadingRange != Y.bUseGridLoadingRange || X.LoadingRangeScale != Y.LoadingRangeScale
                || X.Radius != Y.Radius || X.bIsSector != Y.bIsSector || X.SectorAngle != Y.SectorAngle
                || !X.Location.Equals(Y.Location, .01) || !X.Rotation.Equals(Y.Rotation, .001)) return false;
        }
        return true;
    }
    bool ResidencyWorldCurrent(UWorld* World)
    {
        auto* Partition = World ? World->GetWorldPartition() : nullptr;
        auto* System = World ? World->GetSubsystem<UWorldPartitionSubsystem>() : nullptr;
        auto* PC = Controller.Get(); auto* Source = Preparation.Get(); auto* State = ResidencyPlayerState.Get();
        if (!World || World->bIsTearingDown || World->GetNetMode() != NM_Standalone || !Partition || Partition != ResidencyPartition.Get()
            || !Partition->IsInitialized() || !Partition->IsStreamingEnabled() || !Partition->RuntimeHash || !System || !Partition->GetDataLayerManager()
            || !IsValid(PC) || PC->IsActorBeingDestroyed() || PC->GetWorld() != World || !PC->HasAuthority() || !PC->IsLocalController()
            || !IsValid(Source) || Source->IsBeingDestroyed() || !Source->IsRegistered() || Source->GetWorld() != World || Source->GetOwner() != PC
            || Source->GetStreamingSourceIdentity() != ResidencyDestination.Name
            || !IsValid(State) || State->IsActorBeingDestroyed() || PC->PlayerState != State || State->GetOwner() != PC
            || State->GetAccountId().Value != ResidencyAccount || State->GetCharacterId().Value != ResidencyCharacter
            || State->GetProjectJAbilitySystemComponent() != ResidencyASC.Get())
        { ResidencyFailure = TEXT("Residency world/controller/source/session identity changed"); return false; }
        const auto Transform = Partition->GetInstanceTransform(); const auto Scale = Transform.GetScale3D();
        if (Transform.ContainsNaN() || !Transform.GetRotation().IsNormalized()
            || !FMath::IsFinite(Scale.X) || !FMath::IsFinite(Scale.Y) || !FMath::IsFinite(Scale.Z)
            || FMath::Abs(Scale.X) <= UE_SMALL_NUMBER || FMath::Abs(Scale.Y) <= UE_SMALL_NUMBER || FMath::Abs(Scale.Z) <= UE_SMALL_NUMBER
            || !Transform.Equals(ResidencyPartitionTransform, 0))
        { ResidencyFailure = TEXT("Partition instance transform changed or has invalid/noninvertible scale"); return false; }
        int32 Controllers = 0, Definitions = 0; bool bDefinitionCurrent = true;
        for (TActorIterator<APlayerController> It(World); It; ++It) if (++Controllers > 1) break;
        for (TActorIterator<AProject_JWorldState> It(World); It; ++It)
        { if (++Definitions > 1 || It->Definition != ResidencyDefinition.Get()) { bDefinitionCurrent = false; break; } }
        if (Controllers != 1 || Definitions != 1 || !bDefinitionCurrent || !ResidencyDefinition.IsValid()
            || !SameResidencyNames(ResidencyActiveLayers, Partition->GetDataLayerManager()->GetEffectiveActiveDataLayerNames())
            || !SameResidencyNames(ResidencyLoadedLayers, Partition->GetDataLayerManager()->GetEffectiveLoadedDataLayerNames()))
        { ResidencyFailure = TEXT("Residency sole-player/definition/DataLayer policy changed"); return false; }
#if WITH_EDITOR
        const auto& Scales = System->GetDynamicLoadingRangeScales();
        if (Scales.Scale2D != 1.f || Scales.Scale3D != 1.f)
        { ResidencyFailure = TEXT("Dynamic loading-range scales are not represented by this bounded query"); return false; }
#endif
        return true;
    }
    bool QueryResidencyCells(UWorld* World, const FWorldPartitionStreamingSource& Local, TSet<const UWorldPartitionRuntimeCell*>& Cells)
    {
        if (Local.Name.IsNone() || Local.Location.ContainsNaN() || Local.Rotation.ContainsNaN() || Local.bForce2D
            || Local.Shapes.Num() > UProject_JWorldDefinition::MaxPreparationShapes || Local.TargetGrids.Num() > ResidencySourceLimit
            || (Local.TargetState != EStreamingSourceTargetState::Loaded && Local.TargetState != EStreamingSourceTargetState::Activated))
        { ResidencyFailure = TEXT("Unsupported consumed source geometry/policy"); return false; }
        for (const auto& Shape : Local.Shapes)
            if (!FMath::IsFinite(Shape.Radius) || Shape.Radius <= 0 || !FMath::IsFinite(Shape.LoadingRangeScale) || Shape.LoadingRangeScale <= 0
                || Shape.Location.ContainsNaN() || Shape.Rotation.ContainsNaN() || !FMath::IsFinite(Shape.SectorAngle)
                || Shape.SectorAngle <= 0 || Shape.SectorAngle > 360)
            { ResidencyFailure = TEXT("Invalid consumed source shape"); return false; }
        FWorldPartitionStreamingQuerySource Query;
        Query.Location = Local.Location; Query.Rotation = Local.Rotation; // Consumed sources are already partition-local.
        Query.TargetBehavior = Local.TargetBehavior; Query.TargetGrids = Local.TargetGrids; Query.Shapes = Local.Shapes;
        Query.bUseGridLoadingRange = true; // Empty PC shapes mean the native grid loading-range sphere.
        Query.DataLayers = ResidencyActiveLayers.Array();
        for (FName Name : ResidencyLoadedLayers) Query.DataLayers.AddUnique(Name);
        // Public policy equivalence probe only: detect private ExtraRadius/ExtraAngle expansion.
        // This test range never replaces the actual RuntimeHash/grid loading range below.
        TArray<FSphericalSector, TInlineAllocator<8>> NativeShapes, QueryShapes;
        Local.ForEachShape(1000.f, false, [&](const FSphericalSector& Shape) { NativeShapes.Add(Shape); });
        Query.ForEachShape(1000.f, false, [&](const FSphericalSector& Shape) { QueryShapes.Add(Shape); });
        if (NativeShapes.Num() != QueryShapes.Num()) { ResidencyFailure = TEXT("Source/query expansion differs"); return false; }
        for (int32 I = 0; I < NativeShapes.Num(); ++I)
            if (!NativeShapes[I].GetCenter().Equals(QueryShapes[I].GetCenter(), 0) || NativeShapes[I].GetRadius() != QueryShapes[I].GetRadius()
                || !NativeShapes[I].GetAxis().Equals(QueryShapes[I].GetAxis(), 0) || NativeShapes[I].GetAngle() != QueryShapes[I].GetAngle())
            { ResidencyFailure = TEXT("Source extra expansion is not represented by the query"); return false; }
        int32 Visits = 0; bool bOverflow = false, bValid = true;
        World->GetWorldPartition()->RuntimeHash->ForEachStreamingCellsQuery(Query, [&](const UWorldPartitionRuntimeCell* Cell)
        {
            if (bOverflow) return false;
            ++Visits;
            if (Cell && Cell->IsSpatiallyLoaded())
            {
                const FBox Bounds = Cell->GetStreamingBounds();
                bValid &= Bounds.IsValid && !Bounds.Min.ContainsNaN() && !Bounds.Max.ContainsNaN();
                Cells.Add(Cell);
            }
            if (Visits >= ResidencyCellLimit) { bOverflow = true; return false; }
            return true;
        });
        if (bOverflow || !bValid) { ResidencyFailure = TEXT("Residency cell query exceeded128 callbacks or invalid bounds"); return false; }
        return true;
    }
    bool OtherResidencyDemand(UWorld* World, TSet<const UWorldPartitionRuntimeCell*>& Other, bool& bDestinationConsumed)
    {
        if (!ResidencyWorldCurrent(World)) return false;
        bDestinationConsumed = false;
        const auto& Sources = World->GetWorldPartition()->GetStreamingSources();
        if (Sources.Num() > ResidencySourceLimit)
        { ResidencyFailure = TEXT("Consumed source inventory above bounded8-source scope"); return false; }
        const FTransform ToLocal = World->GetWorldPartition()->GetInstanceTransform().Inverse();
        if (ToLocal.ContainsNaN()) { ResidencyFailure = TEXT("Invalid partition transform"); return false; }
        auto Expected = ResidencyDestination;
        Expected.Location = ToLocal.TransformPosition(Expected.Location);
        Expected.Rotation = ToLocal.TransformRotation(Expected.Rotation.Quaternion()).Rotator();
        int32 Matches = 0;
        for (const auto& Consumed : Sources)
        {
            if (Consumed.Name == Expected.Name && SameResidencySource(Consumed, Expected))
            {
                if (++Matches > 1) { ResidencyFailure = TEXT("Duplicate consumed destination identity"); return false; }
                // Validate expansion equivalence for the destination as well as every other source.
                TSet<const UWorldPartitionRuntimeCell*> Ignored;
                if (!QueryResidencyCells(World, Consumed, Ignored)) return false;
                bDestinationConsumed = true;
            }
            else if (!QueryResidencyCells(World, Consumed, Other)) return false;
        }
        return true;
    }
    struct FResidencyRead
    {
        int32 Loading = 0, Loaded = 0, Async = 0, LoadPending = 0, StreamingPending = 0, Unloaded = 0;
        bool bDrained = true;
        TArray<TSharedPtr<FJsonValue>> Rows;
    };
    bool ReadResidencyCells(UWorld* World, const TSet<const UWorldPartitionRuntimeCell*>& Other, FResidencyRead& Read)
    {
        for (auto& Entry : ResidencyCells)
        {
            const auto* Cell = Entry.Cell.Get();
            if (!IsValid(Cell) || Cell->GetGuid() != Entry.Guid || Cell->GetOuterWorld() != World || Other.Contains(Cell))
            { ResidencyFailure = TEXT("Selected cell identity retired or acquired another consumed demand; no unload claim"); return false; }
            const auto* Level = Cell->GetLevelStreaming();
            if ((Level && !IsValid(Level)) || (Entry.bLevelSeen && Entry.Level.Get() != Level) || (Level && Level->GetWorld() != World))
            { ResidencyFailure = TEXT("Observed native LevelStreaming identity retired/replaced; no fabricated unload"); return false; }
            if (Level && !Entry.bLevelSeen) { Entry.Level = Level; Entry.bLevelSeen = true; }
            const ELevelStreamingState State = Level ? Level->GetLevelStreamingState() : ELevelStreamingState::Unloaded;
            if (State == ELevelStreamingState::FailedToLoad)
            { ResidencyFailure = TEXT("Selected native level FailedToLoad; not an unload success"); return false; }
            const bool bHasLoaded = Level && Level->HasLoadedLevel(); // Includes PendingUnloadLevel.
            const int32 Async = Level ? Level->GetAsyncRequestIDs().Num() : 0;
            const bool bLoadPending = Level && Level->HasLoadRequestPending();
            const bool bStreamingPending = Level && Level->IsStreamingStatePending();
            const bool bShouldLoad = Level && Level->ShouldBeLoaded();
            const bool bLoading = State == ELevelStreamingState::Loading || Async > 0 || bLoadPending;
            const bool bUnloaded = (State == ELevelStreamingState::Unloaded || State == ELevelStreamingState::Removed)
                && !bHasLoaded && !Async && !bLoadPending && !bStreamingPending && !bShouldLoad
                && Cell->GetCurrentState() == EWorldPartitionRuntimeCellState::Unloaded && !Cell->IsVisible();
            Read.Loading += bLoading; Read.Loaded += bHasLoaded; Read.Async += Async;
            Read.LoadPending += bLoadPending; Read.StreamingPending += bStreamingPending; Read.Unloaded += bUnloaded;
            Read.bDrained &= bUnloaded;
            bResidencyTransition |= !bUnloaded; bResidencyLoading |= bLoading;
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("guid"), Entry.Guid.ToString()); Row->SetStringField(TEXT("name"), Entry.Name);
            Row->SetNumberField(TEXT("level_state"), int32(State)); Row->SetNumberField(TEXT("cell_state"), int32(Cell->GetCurrentState()));
            Row->SetBoolField(TEXT("has_loaded_level"), bHasLoaded); Row->SetNumberField(TEXT("async_ids_count"), Async);
            Row->SetBoolField(TEXT("load_pending"), bLoadPending); Row->SetBoolField(TEXT("streaming_pending"), bStreamingPending);
            Row->SetBoolField(TEXT("should_be_loaded"), bShouldLoad); Row->SetBoolField(TEXT("level_seen"), Entry.bLevelSeen);
            Row->SetStringField(TEXT("level_name"), Level ? Level->GetPathName() : FString());
            Row->SetBoolField(TEXT("other_demand"), false); Row->SetBoolField(TEXT("native_unloaded"), bUnloaded);
            Read.Rows.Add(MakeShared<FJsonValueObject>(Row));
        }
        return !ResidencyCells.IsEmpty();
    }
    bool SampleResidency(UWorld* World, const TCHAR* Stage, FResidencyRead& Read)
    {
        Read = FResidencyRead();
        TSet<const UWorldPartitionRuntimeCell*> Other; bool bConsumed = false;
        if (ResidencySamples.Num() >= ResidencySampleLimit) { ResidencyFailure = TEXT("Residency sample cap512 exceeded"); return false; }
        if (!OtherResidencyDemand(World, Other, bConsumed) || !ReadResidencyCells(World, Other, Read)) return false;
        auto* Execution = World->GetSubsystem<UProject_JWorldArrivalSubsystem>();
        if (!Execution) { ResidencyFailure = TEXT("Residency arrival writer unavailable"); return false; }
        const auto Memory = FPlatformMemory::GetStats(); const int32 Async = GetNumAsyncPackages();
        auto Row = MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("cycle"), ResidencyCycle); Row->SetStringField(TEXT("stage"), Stage);
        Row->SetNumberField(TEXT("elapsed_s"), FPlatformTime::Seconds()-Started);
        Row->SetStringField(TEXT("source_identity"), ResidencyDestination.Name.ToString());
        Row->SetBoolField(TEXT("destination_consumed"), bConsumed);
        Row->SetNumberField(TEXT("phase"), int32(Preparation->ArrivalPhase));
        Row->SetNumberField(TEXT("selected_cells"), ResidencyCells.Num()); Row->SetArrayField(TEXT("cells"), Read.Rows);
        AppendArrivalPreparationStats(World, *Row); Row->SetNumberField(TEXT("arrival_pending"), Execution->GetStats().Pending);
        Row->SetNumberField(TEXT("global_async_packages"), Async);
        Row->SetStringField(TEXT("used_physical"), LexToString(Memory.UsedPhysical));
        Row->SetStringField(TEXT("used_virtual"), LexToString(Memory.UsedVirtual));
        ResidencySamples.Add(MakeShared<FJsonValueObject>(Row));
        ResidencyRows += FString::Printf(TEXT("%d,%s,%.6f,%d,%d,%d,%d,%d,%d,%d,0,%d,%d,%d,%llu,%llu\n"),
            ResidencyCycle, Stage, FPlatformTime::Seconds()-Started, ResidencyCells.Num(), Read.Loading, Read.Loaded, Read.Async,
            Read.LoadPending, Read.StreamingPending, Read.Unloaded, Execution->GetStats().ActivePreparationPermits,
            Execution->GetStats().Pending, Async, Memory.UsedPhysical, Memory.UsedVirtual);
        NextResidencySample = FPlatformTime::Seconds()+.5;
        return true;
    }
    bool CaptureResidencyBaseline(UWorld* World, const UProject_JWorldDefinition* Definition)
    {
        auto* PC = Controller.Get(); auto* Source = Preparation.Get();
        auto* State = PC ? Cast<AProject_JPlayerState>(PC->PlayerState) : nullptr;
        auto* ASC = State ? State->GetProjectJAbilitySystemComponent() : nullptr;
        if (!PC || !Source || !State || !ASC || ASC->IsActorInfoChanging() || !ASC->GetActorInfoRevision()
            || ASC->GetAvatarActor() != Pawn.Get() || State->GetPawn() != Pawn.Get())
        { ResidencyFailure = TEXT("Origin recipient ASC tuple not current"); return false; }
        const auto* Execution = World->GetSubsystem<UProject_JWorldArrivalSubsystem>();
        if (!Execution || Execution->GetStats().Pending || Execution->GetStats().ActivePreparationPermits || Source->IsPreparedArrivalStarted()
            || Source->ArrivalPhase == EProject_JWorldArrivalPhase::Preparing || Source->ArrivalPhase == EProject_JWorldArrivalPhase::Queued
            || Source->ArrivalPhase == EProject_JWorldArrivalPhase::AwaitingLocalArrival)
        { ResidencyFailure = TEXT("A preexisting arrival owns demand before this cycle"); return false; }
        if (ResidencyCompleted && !ResidencyWorldCurrent(World)) return false;
        if (World->GetWorldPartition()->GetStreamingSources().IsEmpty())
        { ResidencyFailure = TEXT("Origin consumed source inventory empty"); return false; }
        ResidencyPartition = World->GetWorldPartition(); ResidencyDefinition = Definition;
        if (!ResidencyCompleted) ResidencyPartitionTransform = ResidencyPartition->GetInstanceTransform();
        ResidencyPlayerState = State; ResidencyASC = ASC; ResidencyOriginalEpoch = ASC->GetActorInfoRevision();
        ResidencyAccount = State->GetAccountId().Value; ResidencyCharacter = State->GetCharacterId().Value;
        ResidencyOldPawn = Pawn->GetPathName(); ResidencyNewPawn.Reset();
        auto* Layers = World->GetWorldPartition()->GetDataLayerManager();
        ResidencyActiveLayers = Layers->GetEffectiveActiveDataLayerNames(); ResidencyLoadedLayers = Layers->GetEffectiveLoadedDataLayerNames();
        if (!Source->GetStreamingSource(ResidencyDestination)) { ResidencyFailure = TEXT("Origin source unavailable"); return false; }
        ResidencyDestination.Location = Hint; ResidencyDestination.Rotation = FRotator::ZeroRotator;
        ResidencyDestination.TargetState = EStreamingSourceTargetState::Activated; ResidencyDestination.Shapes.Reset();
        FStreamingSourceShape Sphere; Sphere.bUseGridLoadingRange = false; Sphere.Radius = Definition->PreparationRadius;
        ResidencyDestination.Shapes.Add(Sphere);
        if (!ResidencyWorldCurrent(World)) return false;
        TSet<const UWorldPartitionRuntimeCell*> Other, Destination; bool bConsumed = false;
        if (!OtherResidencyDemand(World, Other, bConsumed) || bConsumed) { ResidencyFailure = TEXT("Destination already consumed before request"); return false; }
        auto Local = ResidencyDestination; const auto Inverse = World->GetWorldPartition()->GetInstanceTransform().Inverse();
        Local.Location = Inverse.TransformPosition(Local.Location); Local.Rotation = Inverse.TransformRotation(Local.Rotation.Quaternion()).Rotator();
        if (!QueryResidencyCells(World, Local, Destination)) return false;
        ResidencyCells.Reset();
        for (const auto* Cell : Destination)
        {
            if (Other.Contains(Cell) || Cell->GetIsHLOD() || Cell->IsAlwaysLoaded()) continue;
            const auto* Native = Cast<UWorldPartitionRuntimeLevelStreamingCell>(Cell);
            if (!Native || !Native->GetGuid().IsValid()) { ResidencyFailure = TEXT("Destination contains unsupported native cell type/Guid"); return false; }
            const auto* Level = Native->GetLevelStreaming();
            if (Level && Level->GetLevelStreamingState() == ELevelStreamingState::FailedToLoad)
            { ResidencyFailure = TEXT("Baseline destination has native FailedToLoad"); return false; }
            if (Native->GetCurrentState() != EWorldPartitionRuntimeCellState::Unloaded || Native->IsVisible()
                || (Level && (Level->HasLoadedLevel() || !Level->GetAsyncRequestIDs().IsEmpty() || Level->HasLoadRequestPending()
                    || Level->ShouldBeLoaded() || Level->IsStreamingStatePending() || (Level->GetLevelStreamingState() != ELevelStreamingState::Unloaded
                        && Level->GetLevelStreamingState() != ELevelStreamingState::Removed)))) continue;
            ResidencyCells.Add({Native, Native->GetGuid(), Native->GetDebugName()});
        }
        if (ResidencyCells.IsEmpty()) { ResidencyFailure = TEXT("No unloaded sole-demand destination cells; no physical-tail claim"); return false; }
        ResidencyCycleResult = MakeShared<FJsonObject>();
        ResidencyCycleResult->SetNumberField(TEXT("cycle"), ResidencyCycle);
        ResidencyCycleResult->SetStringField(TEXT("source_identity"), ResidencyDestination.Name.ToString());
        ResidencyCycleResult->SetNumberField(TEXT("sole_demand_cells"), ResidencyCells.Num());
        ResidencyCycleResult->SetStringField(TEXT("old_pawn"), ResidencyOldPawn);
        ResidencyCycleResult->SetStringField(TEXT("old_asc_revision"), LexToString(ResidencyOriginalEpoch));
        ResidencyResults.Add(MakeShared<FJsonValueObject>(ResidencyCycleResult));
        bResidencyTransition = bResidencyLoading = false;
        FResidencyRead Read; return SampleResidency(World, TEXT("baseline"), Read);
    }
    bool FinishResidency(bool bSuccess, const FString& Reason)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("success"), bSuccess); Result->SetStringField(TEXT("reason"), Reason);
        Result->SetStringField(TEXT("scope"), TEXT("Standalone actual Portal request/public recipient retirement; selected sole-demand native level tail only. Global async/process memory are auxiliary; no disk-byte/GPU/Nav/capacity/leak-free guarantee. Shape probe checks policy only, never substitutes actual grid ranges."));
        Result->SetBoolField(TEXT("null_rhi"), GUsingNullRHI); Result->SetNumberField(TEXT("requested_cycles"), 2);
        Result->SetNumberField(TEXT("completed_cycles"), ResidencyCompleted); Result->SetArrayField(TEXT("cycles"), ResidencyResults);
        Result->SetArrayField(TEXT("samples"), ResidencySamples);
        if (auto* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("wp.Runtime.MaxLoadingStreamingCells")))
            Result->SetNumberField(TEXT("native_max_loading_streaming_cells"), CVar->GetInt());
        FString Json;
        const bool bSaved = FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json))
            && FFileHelper::SaveStringToFile(ResidencyRows, *(OutputRoot()/TEXT("world-residency.csv")))
            && FFileHelper::SaveStringToFile(Json, *(OutputRoot()/TEXT("world-residency.json")));
        if (auto* Owned = Portal.Get(); Owned && !Owned->IsActorBeingDestroyed()) Owned->Destroy();
        FPlatformMisc::RequestExitWithStatus(false, bSuccess && bSaved ? 0 : 1);
        return false;
    }
    bool TickResidencyPreparing(UWorld* World)
    {
        if (!ResidencyWorldCurrent(World)) return Finish(false, ResidencyFailure);
        auto* Source = Preparation.Get(); auto* PC = Controller.Get(); auto* Player = Pawn.Get(); auto* ASC = ResidencyASC.Get();
        if (!Player || PC->GetPawn() != Player || Player->GetController() != PC || !ASC || ASC->IsActorInfoChanging()
            || ASC->GetAvatarActor() != Player || ASC->GetActorInfoRevision() != ResidencyOriginalEpoch
            || Source->ArrivalPhase != EProject_JWorldArrivalPhase::Preparing)
            return Finish(false, TEXT("Recipient changed or natural Preparing/load window missed; no injected delay"));
        TSet<const UWorldPartitionRuntimeCell*> Other; bool bConsumed = false;
        if (!OtherResidencyDemand(World, Other, bConsumed)) return Finish(false, ResidencyFailure);
        if (!bConsumed) return true;
        FResidencyRead Read;
        if (!ReadResidencyCells(World, Other, Read)) return Finish(false, ResidencyFailure);
        if (!bResidencyTransition) return true;
        if (!SampleResidency(World, TEXT("preparing"), Read)) return Finish(false, ResidencyFailure);
        auto* Execution = World->GetSubsystem<UProject_JWorldArrivalSubsystem>();
        FWorldPartitionStreamingSource Advertised;
        if (!Source->GetStreamingSource(Advertised) || !SameResidencySource(Advertised, ResidencyDestination)
            || !Source->IsPreparedArrivalStarted() || !Execution || Execution->GetStats().ActivePreparationPermits != 1
            || Execution->GetStats().Pending != 1 || FVector::DistSquared2D(Player->GetActorLocation(), Origin) > FMath::Square(500.))
            return Finish(false, TEXT("Preparing permit/source/origin proof changed before retirement"));
        ResidencyCycleResult->SetBoolField(TEXT("source_consumed"), true);
        ResidencyCycleResult->SetBoolField(TEXT("load_transition_observed"), bResidencyTransition);
        ResidencyCycleResult->SetBoolField(TEXT("loading_observed"), bResidencyLoading);
        ResidencyCycleResult->SetBoolField(TEXT("retired_before_queued"), true);
        const TStrongObjectPtr<APlayerController> KeepPC(PC);
        const TStrongObjectPtr<AProject_JPlayerCharacter> KeepPawn(Player);
        const TStrongObjectPtr<AProject_JPlayerState> KeepState(ResidencyPlayerState.Get());
        const TStrongObjectPtr<UProject_JWorldStreamingComponent> KeepSource(Source);
        const TStrongObjectPtr<UWorld> KeepWorld(World);
        PC->UnPossess();
        if (!ResidencyWorldCurrent(World) || PC->GetPawn() || Player->GetController())
            return Finish(false, TEXT("Recipient ownership changed during UnPossess"));
        const bool bDestroyed = Player->Destroy();
        if (!ResidencyWorldCurrent(World) || PC->GetPawn() || !bDestroyed || !Player->IsActorBeingDestroyed())
            return Finish(false, TEXT("Normal recipient public retirement failed"));
        bResidencyRetired = true; ResidencyStage = 1;
        FResidencyRead Retired; if (!SampleResidency(World, TEXT("retired"), Retired)) return Finish(false, ResidencyFailure);
        return true;
    }
    bool TickResidencyTail(UWorld* World)
    {
        if (!ResidencyWorldCurrent(World)) return Finish(false, ResidencyFailure);
        auto* PC = Controller.Get(); auto* Source = Preparation.Get();
        auto* Execution = World->GetSubsystem<UProject_JWorldArrivalSubsystem>();
        if (!Execution) return Finish(false, TEXT("Arrival writer retired during tail"));
        if (ResidencyStage == 1)
        {
            if (Source->ArrivalPhase != EProject_JWorldArrivalPhase::Failed) return true;
            FWorldPartitionStreamingSource Old;
            if (PC->GetPawn() || Source->IsPreparedArrivalStarted() || Source->GetStreamingSource(Old)
                || Execution->GetStats().ActivePreparationPermits || Execution->GetStats().Pending)
                return Finish(false, TEXT("Retired request retained a source/permit or another writer replaced its pawn"));
            ResidencyCycleResult->SetBoolField(TEXT("permit_returned"), true);
            ResidencyCycleResult->SetBoolField(TEXT("retired_destination_released"), true);
            auto* Mode = World->GetAuthGameMode();
            if (!Mode) return Finish(false, TEXT("Normal GameMode unavailable for origin restart"));
            const TStrongObjectPtr<APlayerController> KeepPC(PC);
            const TStrongObjectPtr<AGameModeBase> KeepMode(Mode);
            const TStrongObjectPtr<AProject_JPlayerState> KeepState(ResidencyPlayerState.Get());
            const TStrongObjectPtr<UProject_JWorldStreamingComponent> KeepSource(Source);
            const TStrongObjectPtr<UWorld> KeepWorld(World);
            ResidencyStage = 2; Mode->RestartPlayer(PC);
            if (!ResidencyWorldCurrent(World))
                return Finish(false, TEXT("Restart callback retired the original owner/world"));
            return true;
        }
        auto* Player = Cast<AProject_JPlayerCharacter>(PC->GetPawn()); auto* ASC = ResidencyASC.Get();
        if (!Player || Player->IsActorBeingDestroyed() || Player->GetWorld() != World || !ASC || ASC->IsActorInfoChanging()) return true;
        if (Player->GetPathName() == ResidencyOldPawn || Player->GetController() != PC || ResidencyPlayerState->GetPawn() != Player
            || ASC->GetAvatarActor() != Player || !ASC->GetActorInfoRevision() || ASC->GetActorInfoRevision() == ResidencyOriginalEpoch
            || Source->ArrivalPhase != EProject_JWorldArrivalPhase::Failed || Source->IsPreparedArrivalStarted()
            || Execution->GetStats().Pending || Execution->GetStats().ActivePreparationPermits
            || FVector::DistSquared2D(Player->GetActorLocation(), Origin) > FMath::Square(500.))
            return Finish(false, TEXT("Replacement did not preserve origin/session with a new recipient incarnation"));
        FWorldPartitionStreamingSource Provided;
        if (!Source->GetStreamingSource(Provided) || !Provided.Location.Equals(Player->GetActorLocation(), .1)) return true;
        auto LocalNormal = Provided;
        const auto Inverse = World->GetWorldPartition()->GetInstanceTransform().Inverse();
        if (Inverse.ContainsNaN()) return Finish(false, TEXT("Invalid restored partition transform"));
        LocalNormal.Location = Inverse.TransformPosition(LocalNormal.Location);
        LocalNormal.Rotation = Inverse.TransformRotation(LocalNormal.Rotation.Quaternion()).Rotator();
        int32 NormalMatches = 0;
        for (const auto& Consumed : World->GetWorldPartition()->GetStreamingSources())
            NormalMatches += SameResidencySource(Consumed, LocalNormal);
        if (NormalMatches != 1) return true; // An old same-name destination is not restored-origin evidence.
        if (ResidencyStage == 2)
        {
            const FString Name = FString::Printf(TEXT("world-residency-restored-%d.json"), ResidencyCycle);
            if (!WriteWorldReadinessSnapshot(World, *Name)) return true;
            ResidencyNewPawn = Player->GetPathName();
            ResidencyCycleResult->SetStringField(TEXT("new_pawn"), ResidencyNewPawn);
            ResidencyCycleResult->SetStringField(TEXT("new_asc_revision"), LexToString(ASC->GetActorInfoRevision()));
            ResidencyCycleResult->SetBoolField(TEXT("replacement_at_origin"), true);
            ResidencyCycleResult->SetBoolField(TEXT("origin_readiness_restored"), true);
            ResidencyStage = 3; ResidencyDrainSamples = 0; NextResidencySample = 0;
        }
        if (FPlatformTime::Seconds() < NextResidencySample) return true;
        FResidencyRead Read;
        if (!SampleResidency(World, TEXT("draining"), Read)) return Finish(false, ResidencyFailure);
        TSet<const UWorldPartitionRuntimeCell*> Other; bool bConsumed = false;
        if (!OtherResidencyDemand(World, Other, bConsumed)) return Finish(false, ResidencyFailure);
        ResidencyDrainSamples = Read.bDrained && !bConsumed ? ResidencyDrainSamples+1 : 0;
        if (ResidencyDrainSamples < 3) return true;
        ResidencyCycleResult->SetBoolField(TEXT("selected_native_tail_drained"), true);
        ResidencyCycleResult->SetNumberField(TEXT("drain_observations"), ResidencyDrainSamples);
        ++ResidencyCompleted;
        if (ResidencyCompleted == 2) return Finish(true, TEXT("Two natural Portal demand retirements restored origin and selected native level tails drained; memory/global async are observations only"));
        // Same world/PC/Portal; only the request recipient changes through normal restart.
        ++ResidencyCycle; ResidencyStage = 0; bResidencyRetired = false; bRequested = bOriginPrepared = false;
        ResidencyCells.Reset(); ResidencyCycleResult.Reset();
        return true;
    }

    ~FProjectJPreparedWorldProbe() { ReleaseHLODView(); }
    void ReleaseHLODView()
    {
        UGameViewportClient::OnViewportRendered().Remove(HLODDrawHandle); HLODDrawHandle.Reset();
        FScreenshotRequest::OnScreenshotRequestProcessed().Remove(HLODShotHandle); HLODShotHandle.Reset();
        if (bShotPending && FScreenshotRequest::GetFilename() == ShotPath) FScreenshotRequest::Reset();
        bShotPending = false;
        auto* Camera = HLODView.Get(); auto* PC = Controller.Get();
        const TStrongObjectPtr<ACameraActor> KeepCamera(Camera);
        const TStrongObjectPtr<APlayerController> KeepController(PC);
        const TStrongObjectPtr<AActor> KeepPrevious(PreviousViewTarget.Get());
        HLODView.Reset();
        if (Camera && PC && !PC->IsActorBeingDestroyed() && PC->GetWorld() && !PC->GetWorld()->bIsTearingDown && PC->GetViewTarget() == Camera)
            PC->SetViewTarget(KeepPrevious.IsValid() ? KeepPrevious.Get() : Pawn.Get());
        if (IsValid(Camera) && !Camera->IsActorBeingDestroyed() && Camera->GetWorld() && !Camera->GetWorld()->bIsTearingDown) Camera->Destroy();
    }
    bool PrepareHLODView(UWorld* World)
    {
        if (HLODView.IsValid()) return true;
        if (HLODDrawHandle.IsValid()) return false; // An unexpected camera retirement is not a retry/recreation policy.
        auto* PC = Controller.Get(); auto* Player = Pawn.Get(); const auto* Cell = HLODSourceCell.Get();
        auto* Viewport = World->GetGameViewport();
        if (!bObserveHLOD || GUsingNullRHI || !PC || !Player || !Cell || !Viewport || !Viewport->Viewport || Viewport->bDisableWorldRendering) return false;
        const TStrongObjectPtr<APlayerController> KeepController(PC);
        const TStrongObjectPtr<AProject_JPlayerCharacter> KeepPlayer(Player);
        const TStrongObjectPtr<UGameViewportClient> KeepViewport(Viewport);
        FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
        auto* Camera = World->SpawnActor<ACameraActor>(Player->GetActorLocation()+FVector(0,0,150), FRotator::ZeroRotator, Params);
        if (!IsValid(Camera) || Camera->IsActorBeingDestroyed()) return false;
        const TStrongObjectPtr<ACameraActor> KeepCamera(Camera);
        HLODView = Camera; PreviousViewTarget = PC->GetViewTarget(); HLODViewport = Viewport;
        if (World->bIsTearingDown || PC->IsActorBeingDestroyed() || Player->IsActorBeingDestroyed()) return false;
        // Attachment follows the normal Pawn teleport immediately. A stale origin
        // camera must not keep its PC view streaming source at the origin.
        if (!Camera->AttachToActor(Player, FAttachmentTransformRules::KeepWorldTransform)) return false;
        const FBox Bounds = Cell->GetStreamingBounds();
        if (!Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN()) return false;
        Camera->SetActorRotation((Bounds.GetCenter()-Camera->GetActorLocation()).Rotation());
        PC->SetViewTarget(Camera);
        if (World->bIsTearingDown || PC->IsActorBeingDestroyed() || Player->IsActorBeingDestroyed() || Camera->IsActorBeingDestroyed() ||
            Controller.Get() != PC || Pawn.Get() != Player || PC->GetPawn() != Player || PC->GetViewTarget() != Camera) return false;
        HLODDrawHandle = UGameViewportClient::OnViewportRendered().AddSP(AsShared(), &FProjectJPreparedWorldProbe::OnHLODViewportRendered);
        return true;
    }
    void OnHLODViewportRendered(FViewport* Viewport)
    {
        check(IsInGameThread()); // Installed UnrealClient broadcasts after GT Draw/Canvas flush, before screenshot processing.
        const auto* Client = HLODViewport.Get(); const auto* Cell = HLODSourceCell.Get(); const auto* Player = Pawn.Get(); const auto* Camera = HLODView.Get();
        if (!Client || Client->Viewport != Viewport || GUsingNullRHI || Client->bDisableWorldRendering || !Cell || !Player || !Camera ||
            Cell->GetOuterWorld() != Client->GetWorld() || Camera->GetWorld() != Client->GetWorld() || LastHLODDrawFrame == GFrameCounter) return;
        LastHLODDrawFrame = GFrameCounter; ++HLODDraws;
        const auto* PC = Controller.Get();
        if (!PC || PC->GetPawn() != Player || PC->GetViewTarget() != Camera)
        { bRHIError = true; HLODFailure = TEXT("Captured RHI controller/Pawn/view-target identity changed"); return; }
        if (FVector::DistSquared(Camera->GetActorLocation(), Player->GetActorLocation()) > FMath::Square(500.0))
        { bRHIError = true; HLODFailure = TEXT("Diagnostic camera moved away from the current Pawn"); return; }
        const auto* LevelCell = Cast<UWorldPartitionRuntimeLevelStreamingCell>(Cell);
        const auto* Streaming = LevelCell ? LevelCell->GetLevelStreaming() : nullptr;
        const auto* Level = Cell->GetLevel();
        int32 VisibleParts = 0;
        for (const auto& Part : HLODParts)
            if (const auto* Mesh = Part.Component.Get(); Mesh && Mesh->IsRegistered() && Mesh->GetVisibleFlag() && !Mesh->bHiddenInGame) ++VisibleParts;
        const bool bCoarseHidden = !Cell->IsVisible() && VisibleParts == 0;
        const bool bLevelHidden = (!Level || !Level->bIsVisible) && VisibleParts == 0;
        bSawCoarseBothHidden |= bCoarseHidden; bSawLevelAndProxyBothHidden |= bLevelHidden;
        const int32 State = (Streaming ? int32(Streaming->GetLevelStreamingState()) : -1)*8 +
            int32(Cell->IsVisible()) + (Level && Level->bIsVisible ? 2 : 0) + (Level && Level->bIsBeingRemoved ? 4 : 0);
        if (State == LastDrawState && VisibleParts == LastDrawVisibleParts) return;
        if (HLODDrawSamples.Num() >= 64) { bRHIError = true; HLODFailure = TEXT("Bounded viewport state-change sample cap exceeded"); return; }
        LastDrawState = State; LastDrawVisibleParts = VisibleParts;
        auto Row = MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("frame"), double(GFrameCounter)); Row->SetNumberField(TEXT("viewport_draw"), double(HLODDraws));
        Row->SetNumberField(TEXT("elapsed_s"), FPlatformTime::Seconds()-Started);
        Row->SetNumberField(TEXT("source_cell_state"), int32(Cell->GetCurrentState())); Row->SetBoolField(TEXT("source_cell_visible"), Cell->IsVisible());
        Row->SetStringField(TEXT("streaming_level_state"), Streaming ? EnumToString(Streaming->GetLevelStreamingState()) : TEXT("None"));
        Row->SetBoolField(TEXT("source_level_present"), Level != nullptr);
        Row->SetBoolField(TEXT("source_level_visible"), Level && Level->bIsVisible); Row->SetBoolField(TEXT("source_level_removing"), Level && Level->bIsBeingRemoved);
        Row->SetBoolField(TEXT("source_level_full_visibility_required"), Level && Level->bRequireFullVisibilityToRender);
        Row->SetNumberField(TEXT("captured_visible_parts"), VisibleParts);
        Row->SetStringField(TEXT("camera_position"), Camera->GetActorLocation().ToString()); Row->SetStringField(TEXT("pawn_position"), Player->GetActorLocation().ToString());
        HLODDrawSamples.Add(MakeShared<FJsonValueObject>(Row));
    }
    void OnHLODScreenshotProcessed()
    {
        FScreenshotRequest::OnScreenshotRequestProcessed().Remove(HLODShotHandle); HLODShotHandle.Reset();
        bShotPending = false;
        const int64 Bytes = IFileManager::Get().FileSize(*ShotPath);
        if (Bytes <= 0) { bRHIError = true; HLODFailure = TEXT("Requested RHI screenshot was processed without its actual image file"); return; }
        auto Row = MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("stage"), ShotStage); Row->SetStringField(TEXT("path"), ShotPath); Row->SetNumberField(TEXT("bytes"), double(Bytes));
        Row->SetNumberField(TEXT("request_frame"), double(ShotRequestFrame)); Row->SetNumberField(TEXT("processed_frame"), double(GFrameCounter));
        Row->SetNumberField(TEXT("viewport_draws_after_stage"), double(HLODDraws-ShotStageDraws));
        HLODShots.Add(MakeShared<FJsonValueObject>(Row));
        if (ShotStage == 1) bOriginShot = true; else if (ShotStage == 2) bFinalShot = true;
    }
    int32 CaptureHLODScreenshot(UWorld* World, int32 Stage)
    {
        if ((Stage == 1 && bOriginShot) || (Stage == 2 && bFinalShot)) return 1;
        if (bRHIError || !PrepareHLODView(World)) return -1;
        const auto* Player = Pawn.Get(); auto* Camera = HLODView.Get(); const auto* Cell = HLODSourceCell.Get();
        if (!Player || !Camera || !Cell || Camera->GetAttachParentActor() != Player) return -1;
        Camera->SetActorRotation((Cell->GetStreamingBounds().GetCenter()-Camera->GetActorLocation()).Rotation());
        if (ShotStage != Stage) { ShotStage = Stage; ShotStageDraws = HLODDraws; ShotStageAt = FPlatformTime::Seconds(); }
        if (FPlatformTime::Seconds()-ShotStageAt >= 15.) { HLODFailure = TEXT("Bounded RHI viewport/screenshot callback deadline expired"); return -1; }
        if (bShotPending || HLODDraws-ShotStageDraws < 2 || FScreenshotRequest::IsScreenshotRequested()) return 0;
        if (!ObserveHLOD(World, true)) return -1;
        const auto& Last = HLODSamples.Last()->AsObject();
        const bool bCorrectState = Stage == 1 ? Last->GetIntegerField(TEXT("source_cell_state")) == 2 && Last->GetBoolField(TEXT("source_cell_visible")) &&
            Last->GetIntegerField(TEXT("captured_visible_parts")) == 0 : Last->GetBoolField(TEXT("source_unloaded_and_captured_proxy_visible_same_sample"));
        if (!bCorrectState) return 0;
        ShotPath = OutputRoot()/(Stage == 1 ? TEXT("hlod-origin.png") : TEXT("hlod-destination.png"));
        bShotPending = true; ShotRequestFrame = GFrameCounter;
        HLODShotHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddSP(AsShared(), &FProjectJPreparedWorldProbe::OnHLODScreenshotProcessed);
        FScreenshotRequest::RequestScreenshot(ShotPath, false, false);
        return 0;
    }

    bool CaptureHLODOrigin(UWorld* World, const TWeakObjectPtr<const UWorldPartitionRuntimeCell>& Selected)
    {
        const auto* Cell = Selected.Get(); auto* Partition = World->GetWorldPartition();
        FString ExpectedText; FGuid Expected;
        if (!FParse::Value(FCommandLine::Get(), TEXT("ProjectJAuditHLODExpectedSourceGuid="), ExpectedText) ||
            !FGuid::Parse(ExpectedText, Expected) || !Expected.IsValid() || !IsValid(Cell) ||
            Cell->GetOuterWorld() != World || !Partition || !Partition->IsInitialized() || !Partition->IsStreamingEnabled() ||
            Cell->GetGuid() != Expected || Cell->GetCurrentState() != EWorldPartitionRuntimeCellState::Activated || !Cell->IsVisible()) return false;
        auto* HLODs = World->GetSubsystem<UWorldPartitionHLODRuntimeSubsystem>();
        if (!HLODs || !UWorldPartitionHLODRuntimeSubsystem::IsHLODEnabled()) return false;
        const auto& Objects = HLODs->GetHLODObjectsForCell(Cell);
        if (Objects.Num() > 8) return false;
        AWorldPartitionHLOD* SelectedProxy = nullptr;
        for (const auto* Object : Objects)
        {
            auto* Candidate = Object ? Cast<AWorldPartitionHLOD>(Object->GetUObject()) : nullptr;
            if (!IsValid(Candidate) || Candidate->IsActorBeingDestroyed() || Candidate->GetWorld() != World ||
                Candidate->IsStandalone() || Candidate->IsCustomHLOD() || Candidate->GetSourceCellGuid() != Expected) continue;
            if (SelectedProxy) return false; // This cohort follows one unambiguous existing normal proxy.
            SelectedProxy = Candidate;
        }
        if (!SelectedProxy) return false; // No build/freshness conclusion; this run lacks the selected representative.
        const TStrongObjectPtr<AWorldPartitionHLOD> KeepProxy(SelectedProxy);
        int32 Visits = 0;
        for (const UActorComponent* Component : SelectedProxy->GetComponents())
        {
            if (++Visits > 64) return false;
            const auto* Mesh = Cast<UStaticMeshComponent>(Component);
            if (!Mesh) continue;
            if (!Mesh->IsRegistered() || !Mesh->GetStaticMesh() || Mesh->GetVisibleFlag() || Mesh->bHiddenInGame) return false;
            const auto* Instances = Cast<UInstancedStaticMeshComponent>(Mesh);
            auto& Part = HLODParts.AddDefaulted_GetRef();
            Part.Component = Mesh; Part.Mesh = Mesh->GetStaticMesh();
            Part.Instances = Instances ? Instances->GetInstanceCount() : 0;
        }
        if (HLODParts.IsEmpty()) return false;
        HLODSourceCell = Selected; HLODSourceGuid = Expected; HLODPartition = Partition; HLODProxy = SelectedProxy;
        bOriginHLODHidden = true;
        return ObserveHLOD(World, true);
    }
    bool ObserveHLOD(UWorld* World, bool bForce = false)
    {
        const auto Fail = [&](const TCHAR* Reason) { HLODFailure = Reason; return false; };
        const double Now = FPlatformTime::Seconds();
        const auto* Cell = HLODSourceCell.Get(); auto* Partition = HLODPartition.Get(); auto* Proxy = HLODProxy.Get();
        if (!IsValid(Cell) || Cell->GetGuid() != HLODSourceGuid || Cell->GetOuterWorld() != World)
            return Fail(TEXT("Captured runtime source cell retired, changed GUID or changed World"));
        if (!IsValid(Partition) || World->bIsTearingDown || World->GetWorldPartition() != Partition ||
            !Partition->IsInitialized() || !Partition->IsStreamingEnabled())
            return Fail(TEXT("Captured WorldPartition retired, changed identity or disabled streaming"));
        if (!IsValid(Proxy) || Proxy->IsActorBeingDestroyed() || Proxy->GetWorld() != World || Proxy->GetSourceCellGuid() != HLODSourceGuid)
            return Fail(TEXT("Captured normal proxy retired or changed World/source GUID"));
        if (!bForce && Now < NextHLODSample) return true;
        if (++HLODObservations > 64) return Fail(TEXT("Bounded HLOD observation count exceeded64"));
        NextHLODSample = Now + .25; // Bounded observation only; not a production cadence/quality policy.
        auto* HLODs = World->GetSubsystem<UWorldPartitionHLODRuntimeSubsystem>();
        bool bAssociated = false; int32 AssociationCount = 0;
        if (HLODs)
        {
            const auto& Objects = HLODs->GetHLODObjectsForCell(Cell);
            AssociationCount = Objects.Num();
            if (Objects.Num() > 8) return Fail(TEXT("Selected source association count exceeded8"));
            for (const auto* Object : Objects)
                bAssociated |= Object && Object->GetUObject() == Proxy;
        }
        bool bSameParts = !HLODParts.IsEmpty(), bAllVisible = true;
        int32 VisibleParts = 0, InstanceCount = 0;
        for (const auto& Part : HLODParts)
        {
            const auto* Mesh = Part.Component.Get(); const auto* Instances = Cast<UInstancedStaticMeshComponent>(Mesh);
            const bool bSame = IsValid(Mesh) && Part.Mesh.IsValid() && Mesh->GetOwner() == Proxy && Mesh->GetStaticMesh() == Part.Mesh.Get() &&
                (Instances ? Instances->GetInstanceCount() : 0) == Part.Instances;
            const bool bVisible = bSame && Mesh->IsRegistered() && Mesh->GetVisibleFlag() && !Mesh->bHiddenInGame;
            bSameParts &= bSame; bAllVisible &= bVisible; VisibleParts += bVisible; InstanceCount += Part.Instances;
        }
        if (!bSameParts) return Fail(TEXT("Captured weak component/mesh/instance identity changed"));
        const bool bUnloaded = Cell->GetCurrentState() == EWorldPartitionRuntimeCellState::Unloaded && !Cell->IsVisible();
        bSourceUnloaded |= bObservedArrival && bUnloaded;
        const bool bCandidate = bObservedArrival && bOriginHLODHidden && bUnloaded && bAssociated && bAllVisible;
        const int32 StateKey = int32(Cell->GetCurrentState()) * 2 + int32(Cell->IsVisible());
        if (!bForce && !bCandidate && StateKey == LastHLODState && VisibleParts == LastHLODVisibleParts &&
            AssociationCount == LastHLODAssociations && bAssociated == bLastHLODAssociated) return true;
        LastHLODState = StateKey; LastHLODVisibleParts = VisibleParts; LastHLODAssociations = AssociationCount; bLastHLODAssociated = bAssociated;
        auto Row = ReadSelectedHLODCellSnapshot(World, Cell);
        Row->SetNumberField(TEXT("elapsed_s"), Now-Started);
        Row->SetBoolField(TEXT("after_physical_arrival"), bObservedArrival);
        bool bReadComplete = false;
        Row->TryGetBoolField(TEXT("bounded_read_complete"), bReadComplete);
        bHLODHandover |= bObservedArrival && bOriginHLODHidden && bUnloaded && bAssociated && bSameParts && bAllVisible && bReadComplete;
        Row->SetBoolField(TEXT("captured_proxy_currently_associated"), bAssociated);
        Row->SetStringField(TEXT("captured_proxy"), Proxy->GetPathName());
        Row->SetBoolField(TEXT("captured_geometry_current"), bSameParts);
        Row->SetNumberField(TEXT("captured_mesh_parts"), HLODParts.Num());
        Row->SetNumberField(TEXT("captured_instances"), InstanceCount);
        Row->SetNumberField(TEXT("captured_visible_parts"), VisibleParts);
        Row->SetBoolField(TEXT("source_unloaded_and_captured_proxy_visible_same_sample"), bObservedArrival && bUnloaded && bAssociated && bSameParts && bAllVisible && bReadComplete);
        HLODSamples.Add(MakeShared<FJsonValueObject>(Row));
        return true;
    }

    void Sample(UWorld* World)
    {
        auto* Source = Preparation.Get(); auto* Player = Pawn.Get();
        if (!Source || !Player || Samples.Num() >= 512) return;
        const int32 Phase = int32(Source->ArrivalPhase);
        if (LastPhase == Phase) return;
        LastPhase = Phase;
        auto Row = MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("elapsed_s"), FPlatformTime::Seconds()-Started);
        Row->SetNumberField(TEXT("phase"), Phase);
        AppendArrivalPreparationStats(World, *Row);
        Row->SetStringField(TEXT("pawn_position"), Player->GetActorLocation().ToString());
        FWorldPartitionStreamingSource Provided;
        const bool bProvided = Source->GetStreamingSource(Provided);
        Row->SetBoolField(TEXT("source_provided"), bProvided);
        if (bProvided) Row->SetStringField(TEXT("source_position"), Provided.Location.ToString());
        Samples.Add(MakeShared<FJsonValueObject>(Row));
    }
    bool Finish(bool bSuccess, const FString& Reason)
    {
        if (bObserveResidency) return FinishResidency(bSuccess, Reason);
        ReleaseHLODView();
        auto Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("success"), bSuccess);
        Result->SetStringField(TEXT("reason"), Reason);
        Result->SetStringField(TEXT("scope"), TEXT("Standalone actual MainWorld normal ServerTryInteract handler, source preparation, native Mover receipt and physical arrival. No actual socket, Nav/GPU readiness, HLOD visual or mass-portal performance claim."));
        Result->SetBoolField(TEXT("interaction_requested"), bRequested);
        Result->SetBoolField(TEXT("arrival_observed"), bObservedArrival);
        Result->SetStringField(TEXT("origin"), Origin.ToString());
        Result->SetStringField(TEXT("authored_hint"), Hint.ToString());
        if (Pawn.IsValid()) Result->SetStringField(TEXT("actual_destination"), Pawn->GetActorLocation().ToString());
        Result->SetArrayField(TEXT("phase_samples"), Samples);
        if (bObserveHLOD)
        {
            auto HLODResult = MakeShared<FJsonObject>();
            HLODResult->SetBoolField(TEXT("success"), bSuccess && bHLODHandover);
            HLODResult->SetStringField(TEXT("reason"), Reason);
            HLODResult->SetStringField(TEXT("scope"), TEXT("One unchanged runtime source-cell GUID and captured normal proxy/components under actual Portal travel. CPU cell state and component visibility only; no forced load/unload, render camera, GPU readiness or continuous pixels."));
            HLODResult->SetStringField(TEXT("source_cell_guid"), HLODSourceGuid.ToString());
            HLODResult->SetNumberField(TEXT("observations"), HLODObservations);
            HLODResult->SetNumberField(TEXT("recorded_samples"), HLODSamples.Num());
            HLODResult->SetBoolField(TEXT("origin_source_activated_proxy_hidden"), bOriginHLODHidden);
            HLODResult->SetBoolField(TEXT("source_unloaded_observed"), bSourceUnloaded);
            HLODResult->SetBoolField(TEXT("source_unloaded_and_same_proxy_visible_observed"), bHLODHandover);
            HLODResult->SetArrayField(TEXT("samples"), HLODSamples);
            HLODResult->SetBoolField(TEXT("rhi_capture_requested"), bCaptureHLODRHI);
            HLODResult->SetBoolField(TEXT("origin_screenshot_processed"), bOriginShot);
            HLODResult->SetBoolField(TEXT("destination_screenshot_processed"), bFinalShot);
            HLODResult->SetArrayField(TEXT("viewport_state_changes"), HLODDrawSamples);
            HLODResult->SetArrayField(TEXT("screenshots"), HLODShots);
            HLODResult->SetBoolField(TEXT("coarse_cell_and_proxy_hidden_at_viewport_draw"), bSawCoarseBothHidden);
            HLODResult->SetBoolField(TEXT("level_and_proxy_hidden_at_viewport_draw"), bSawLevelAndProxyBothHidden);
            HLODResult->SetStringField(TEXT("rhi_scope"), TEXT("Two actual screenshot files and GT viewport draw callbacks. Coarse/source-Level/component flags are observations; no RT/GPU-ready or continuous-pixel claim."));
            FString HLODJson;
            if (!FJsonSerializer::Serialize(HLODResult, TJsonWriterFactory<>::Create(&HLODJson)) ||
                !FFileHelper::SaveStringToFile(HLODJson, *(OutputRoot()/TEXT("world-hlod-handover.json")))) bSuccess = false;
            Result->SetBoolField(TEXT("success"), bSuccess);
        }
        FString Json;
        const bool bSaved = FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json))
            && FFileHelper::SaveStringToFile(Json, *(OutputRoot()/TEXT("world-arrival.json")));
        if (auto* Owned = Portal.Get(); Owned && !Owned->IsActorBeingDestroyed()) Owned->Destroy();
        FPlatformMisc::RequestExitWithStatus(false, bSuccess && bSaved ? 0 : 1);
        return false;
    }
    bool Tick(UWorld* World)
    {
        const double Elapsed = FPlatformTime::Seconds()-Started;
        if (Elapsed >= 120) return Finish(false, TEXT("Actual world arrival exceeded the bounded observation deadline"));
        if (!World || !World->HasBegunPlay() || Elapsed < 3) return true;
        if (World->GetNetMode() != NM_Standalone) return Finish(false, TEXT("This acceptance fixture requires standalone; it does not emulate a client socket"));
        if (bObserveResidency && bResidencyRetired) return TickResidencyTail(World);
        if (!bRequested)
        {
            FString ExpectedMap, ExpectedDefinition;
            FParse::Value(FCommandLine::Get(), TEXT("ProjectJAuditWorldMap="), ExpectedMap);
            FParse::Value(FCommandLine::Get(), TEXT("ProjectJAuditWorldDefinition="), ExpectedDefinition);
            int32 Count = 0; const UProject_JWorldDefinition* Definition = nullptr;
            for (TActorIterator<AProject_JWorldState> It(World); It; ++It) { ++Count; Definition = It->Definition; }
            const bool bMap = ExpectedMap == TEXT("/Game/World/MainWorld/Maps/L_MainWorld")
                && ExpectedDefinition == TEXT("/Game/World/MainWorld/Data/DA_MainWorld")
                && World->GetOutermost()->GetName() == ExpectedMap && Count == 1 && Definition
                && Definition->GetOutermost()->GetName() == ExpectedDefinition && Definition->Places.Num() == 20;
            if (!bMap) return Finish(false, TEXT("Current MainWorld/definition identity differs from the explicit acceptance inputs"));
            const FProject_JWorldPlace* Destination = nullptr;
            int32 Destinations = 0;
            for (const auto& Place : Definition->Places)
                if (Place.PlaceId == TEXT("S01")) { Destination = &Place; ++Destinations; }
            if (Destinations != 1 || !Destination || Destination->Location.ContainsNaN()
                || !Destination->Location.Equals(FVector(6800,16800,7800), .1))
                return Finish(false, TEXT("Current loaded S01 definition is not the preserved planned hint"));
            auto* PC = World->GetFirstPlayerController();
            auto* Player = PC ? Cast<AProject_JPlayerCharacter>(PC->GetPawn()) : nullptr;
            auto* Source = PC ? PC->FindComponentByClass<UProject_JWorldStreamingComponent>() : nullptr;
            if (!Player || !Source || !PC->IsLocalController() || !PC->HasAuthority()) return true;
            const auto Physical = Project_J::Movement::ReadPhysicalFrame(Player);
            if (!Physical.bValid || !Physical.bGrounded || !Physical.Velocity.IsNearlyZero(1.0)) return true;
            if (FVector::DistSquared2D(Player->GetActorLocation(), FVector(-93200,-93200,0)) > FMath::Square(500.0))
                return Finish(false, TEXT("Actual starting Pawn is not at the preserved S00 origin"));
            if (!bOriginPrepared)
            {
                TWeakObjectPtr<const UWorldPartitionRuntimeCell> SelectedHLODCell;
                const FString OriginFile = FString::Printf(TEXT("world-residency-origin-%d.json"), ResidencyCycle);
                if (!(bObserveResidency ? WriteWorldReadinessSnapshot(World, *OriginFile)
                    : WriteWorldReadinessSnapshot(World, TEXT("world-readiness-origin.json"), bObserveHLOD ? &SelectedHLODCell : nullptr))) return true;
                if (bObserveHLOD && !CaptureHLODOrigin(World, SelectedHLODCell))
                    return Finish(false, TEXT("Selected HLOD origin/GUID/proxy geometry is unavailable or ambiguous; no handover/build-freshness conclusion"));
                Origin = Player->GetActorLocation(); Hint = Destination->Location; Pawn = Player; Controller = PC; Preparation = Source;
                bOriginPrepared = true;
            }
            if (bCaptureHLODRHI)
            {
                if (Pawn.Get() != Player || Controller.Get() != PC || Preparation.Get() != Source)
                    return Finish(false, TEXT("Captured origin context changed while awaiting the RHI screenshot"));
                const int32 Capture = CaptureHLODScreenshot(World, 1);
                if (Capture < 0) return Finish(false, HLODFailure.IsEmpty() ? TEXT("RHI origin camera/viewport preparation failed") : HLODFailure);
                if (!Capture) return true;
            }
            if (bObserveResidency && !CaptureResidencyBaseline(World, Definition)) return Finish(false, ResidencyFailure);
            WriteResult(TEXT("world-smoke.json"), true, TEXT("Current loaded MainWorld and S01 identity verified before normal portal interaction"));
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Owned = bObserveResidency ? Portal.Get() : nullptr;
            if (bObserveResidency && ResidencyCycle > 1 && (!IsValid(Owned) || Owned->IsActorBeingDestroyed() || Owned->GetWorld() != World))
                return Finish(false, TEXT("The original Portal retired; cycle2 never recreates it"));
            if (!Owned) Owned = World->SpawnActor<AProject_JWorldPortal>(AProject_JWorldPortal::StaticClass(),
                Origin+Player->GetActorForwardVector()*150, Player->GetActorRotation(), Params);
            if (!IsValid(Owned) || Owned->IsActorBeingDestroyed() || Owned->GetWorld() != World)
                return Finish(false, TEXT("Owned runtime Portal did not survive normal spawning"));
            if (bObserveResidency && ResidencyCycle > 1)
            {
                if (Owned->DestinationPlaceId != Destination->PlaceId)
                    return Finish(false, TEXT("The retained Portal destination changed; no retuning"));
            }
            else { Portal = Owned; Owned->DestinationPlaceId = Destination->PlaceId; }
            auto* Interaction = Owned->FindComponentByClass<UProject_JInteractionTargetComponent>();
            if (!Interaction) return Finish(false, TEXT("Production Portal lacks its normal interaction policy"));
            if (bObserveResidency && ResidencyCycle > 1)
            {
                if (Interaction->Priority != MAX_int32) return Finish(false, TEXT("Retained Portal priority changed; no retuning"));
            }
            else Interaction->Priority = MAX_int32; // Only selects the owned fixture actor through the normal query.
            UFunction* Interact = Player->FindFunction(TEXT("ServerTryInteract"));
            if (!Interact) return Finish(false, TEXT("The production interaction RPC handler is unavailable"));
            bRequested = true; Player->ProcessEvent(Interact, nullptr);
            Sample(World);
            Source = Preparation.Get(); Player = Pawn.Get(); PC = Controller.Get();
            if (!Source || Source->IsBeingDestroyed() || Source->GetWorld() != World || !PC || PC->IsActorBeingDestroyed()
                || PC->GetWorld() != World || !Player || Player->IsActorBeingDestroyed() || Player->GetWorld() != World
                || PC->GetPawn() != Player || Player->GetController() != PC || Source->GetOwner() != PC)
                return Finish(false, TEXT("Expected context retired during the reflected interaction boundary"));
            if (Source->ArrivalPhase != EProject_JWorldArrivalPhase::Preparing
                && Source->ArrivalPhase != EProject_JWorldArrivalPhase::Queued)
                return Finish(false, TEXT("Normal quota/query/Portal request did not enter preparation"));
            return true;
        }
        auto* Player = Pawn.Get(); auto* Source = Preparation.Get(); auto* PC = Controller.Get();
        if (!Player || Player->IsActorBeingDestroyed() || Player->GetWorld() != World || !Source || Source->IsBeingDestroyed()
            || Source->GetWorld() != World || !PC || PC->IsActorBeingDestroyed() || PC->GetWorld() != World
            || PC->GetPawn() != Player || Player->GetController() != PC || Source->GetOwner() != PC)
            return Finish(false, TEXT("Expected Pawn/source retired during actual arrival"));
        Sample(World);
        if (bObserveResidency) return TickResidencyPreparing(World);
        if (Source->ArrivalPhase == EProject_JWorldArrivalPhase::Failed || Source->ArrivalPhase == EProject_JWorldArrivalPhase::ExpiredUnconfirmed)
            return Finish(false, TEXT("Production preparation/movement ended without confirmed arrival"));
        if (Source->ArrivalPhase != EProject_JWorldArrivalPhase::Arrived) return true;
        if (!bObservedArrival) { bObservedArrival = true; ArrivedAt = FPlatformTime::Seconds(); }
        if (FVector::DistSquared(Player->GetActorLocation(), Origin) < FMath::Square(100000.0)
            || FVector::DistSquared2D(Player->GetActorLocation(), Hint) > FMath::Square(42.0))
            return Finish(false, TEXT("Observed arrival did not reach the actual planned destination XY"));
        // Let WP consume the restored normal predictor; do not force transforms or readiness.
        if (bObserveHLOD && !ObserveHLOD(World))
            return Finish(false, HLODFailure);
        if (FPlatformTime::Seconds()-ArrivedAt < .3) return true;
        if (bObserveHLOD && !bHLODHandover)
        {
            if (FPlatformTime::Seconds()-ArrivedAt >= 15.)
                return Finish(false, TEXT("Selected source-cell unload/same-proxy-visible handover was not observed before the bounded diagnostic deadline"));
            return true;
        }
        if (!bCaptureHLODRHI || !bRHIDestinationPrepared)
        {
            if (!WriteWorldReadinessSnapshot(World)) return true;
            bRHIDestinationPrepared = bCaptureHLODRHI;
        }
        if (bCaptureHLODRHI)
        {
            const int32 Capture = CaptureHLODScreenshot(World, 2);
            if (Capture < 0) return Finish(false, HLODFailure.IsEmpty() ? TEXT("RHI destination camera/viewport preparation failed") : HLODFailure);
            if (!Capture) return true;
            if (!ObserveHLOD(World, true)) return Finish(false, HLODFailure);
            if (!WriteWorldReadinessSnapshot(World)) return true;
        }
        return Finish(true, TEXT("Real production Portal interaction, prepared destination, exact Mover receipt and physical MainWorld arrival completed"));
    }
};


// Same production Portal/component, with a normal MainWorld server and one actual owning client.
struct FProjectJPreparedOwnerProbe
{
    explicit FProjectJPreparedOwnerProbe(bool bServerRole) : bServer(bServerRole) {}
    bool bServer = false, bServerReady = false, bStarted = false, bInteractionRequested = false, bArrived = false;
    bool bDestinationSource = false, bSourceBeforePhysicalArrival = false, bPendingSourceMissing = false, bQueued = false;
    bool bWaitingForStartObserved = false, bUnstartedDestinationAdvertised = false, bLastPreparationStarted = false;
    double Started = FPlatformTime::Seconds(), ArrivedAt = 0;
    int32 LastPhase = INDEX_NONE;
    FVector Origin = FVector::ZeroVector, Hint = FVector(6800,16800,7800), ArrivedPosition = FVector::ZeroVector;
    FGuid Character;
    FString ObservedControllerClass;
    float CapsuleRadius = 0;
    TWeakObjectPtr<UWorld> ObservedWorld;
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<AProject_JPlayerCharacter> Pawn;
    TWeakObjectPtr<AProject_JPlayerState> PlayerState;
    TWeakObjectPtr<UProject_JWorldStreamingComponent> Preparation;
    TWeakObjectPtr<AProject_JWorldPortal> Portal;
    TArray<TSharedPtr<FJsonValue>> Samples;
    TSharedPtr<FJsonObject> RestoredConsumption;
    static TArray<TSharedPtr<FJsonValue>> Position(const FVector& Value)
    { return {MakeShared<FJsonValueNumber>(Value.X), MakeShared<FJsonValueNumber>(Value.Y), MakeShared<FJsonValueNumber>(Value.Z)}; }
    bool NormalSourceConsumed(UWorldPartition* Partition, const FWorldPartitionStreamingSource& Advertised)
    {
        RestoredConsumption = MakeShared<FJsonObject>();
        RestoredConsumption->SetStringField(TEXT("claim"), TEXT("Restored normal predictor advertisement has equivalent partition-local UE source hashes and identical shapes/target policy in the engine-consumed source; raw locations may differ within engine quantization."));
        RestoredConsumption->SetBoolField(TEXT("equivalent"), false);
        if (!Partition || !Partition->IsInitialized() || !Partition->CanStream() || Advertised.Name.IsNone()
            || Advertised.Location.ContainsNaN() || Advertised.Rotation.ContainsNaN() || Advertised.Shapes.IsEmpty()
            || Advertised.Shapes.Num() > UProject_JWorldDefinition::MaxPreparationShapes) return false;
        // Match the engine's world-to-partition transformation. Shapes remain source-relative.
        auto Local = Advertised;
        if (Partition->HasInstanceTransform())
        {
            const FTransform WorldToLocal = Partition->GetInstanceTransform().Inverse();
            Local.Location = WorldToLocal.TransformPosition(Local.Location);
            Local.Rotation = WorldToLocal.TransformRotation(Local.Rotation.Quaternion()).Rotator();
        }
        if (Local.Location.ContainsNaN() || Local.Rotation.ContainsNaN()) return false;
        const auto& ConsumedSources = Partition->GetStreamingSources();
        RestoredConsumption->SetNumberField(TEXT("source_count"), ConsumedSources.Num());
        if (ConsumedSources.Num() > 128) return false;
        const FWorldPartitionStreamingSource* Consumed = nullptr;
        int32 MatchingSources = 0;
        for (const auto& Candidate : ConsumedSources)
            if (Candidate.Name == Local.Name) { Consumed = &Candidate; ++MatchingSources; }
        RestoredConsumption->SetNumberField(TEXT("matching_identity_count"), MatchingSources);
        if (MatchingSources != 1 || !Consumed || Consumed->Location.ContainsNaN() || Consumed->Rotation.ContainsNaN()) return false;
        bool bPolicyEqual = Local.TargetState == Consumed->TargetState && Local.TargetBehavior == Consumed->TargetBehavior
            && Local.TargetGrids.Num() == Consumed->TargetGrids.Num() && Local.Priority == Consumed->Priority
            && Local.bBlockOnSlowLoading == Consumed->bBlockOnSlowLoading
            && Local.bUseVelocityContributionToCellsSorting == Consumed->bUseVelocityContributionToCellsSorting
            && Local.bReplay == Consumed->bReplay && Local.bRemote == Consumed->bRemote && Local.bForce2D == Consumed->bForce2D;
        for (const FName Grid : Local.TargetGrids) bPolicyEqual &= Consumed->TargetGrids.Contains(Grid);
        bool bShapesEqual = Local.Shapes.Num() == Consumed->Shapes.Num();
        for (int32 Index = 0; bShapesEqual && Index < Local.Shapes.Num(); ++Index)
        {
            const auto& A = Local.Shapes[Index]; const auto& B = Consumed->Shapes[Index];
            bShapesEqual = !A.Location.ContainsNaN() && !A.Rotation.ContainsNaN() && FMath::IsFinite(A.Radius)
                && FMath::IsFinite(A.LoadingRangeScale) && FMath::IsFinite(A.SectorAngle) && A.Radius > 0
                && A.bUseGridLoadingRange == B.bUseGridLoadingRange && A.LoadingRangeScale == B.LoadingRangeScale
                && A.Radius == B.Radius && A.bIsSector == B.bIsSector && A.SectorAngle == B.SectorAngle
                && A.Location == B.Location && A.Rotation == B.Rotation;
        }
        // Rebase the consumed pose into a fresh partition-local hash anchor. Re-evaluate
        // the advertisement from that anchor so UE's quantization hysteresis is preserved.
        // Cached engine hashes can still contain world-space anchors in instanced worlds.
        const auto HashLocal = [](const FWorldPartitionStreamingSource& Value)
        {
            FWorldPartitionStreamingSource Normalized(Value.Name, Value.Location, Value.Rotation, Value.TargetState,
                Value.bBlockOnSlowLoading, Value.Priority, Value.bRemote);
            Normalized.bUseVelocityContributionToCellsSorting = Value.bUseVelocityContributionToCellsSorting;
            Normalized.TargetBehavior = Value.TargetBehavior; Normalized.TargetGrids = Value.TargetGrids;
            Normalized.Shapes = Value.Shapes; Normalized.bReplay = Value.bReplay; Normalized.bForce2D = Value.bForce2D;
            Normalized.UpdateHash(); return Normalized;
        };
        if (!bPolicyEqual || !bShapesEqual)
        {
            RestoredConsumption->SetBoolField(TEXT("policy_equal"), bPolicyEqual);
            RestoredConsumption->SetBoolField(TEXT("shapes_equal"), bShapesEqual); return false;
        }
        const auto ActualHash = HashLocal(*Consumed);
        auto ExpectedHash = ActualHash;
        ExpectedHash.Location = Local.Location; ExpectedHash.Rotation = Local.Rotation; ExpectedHash.UpdateHash();
        const bool bHashEqual = ExpectedHash.GetHash() == ActualHash.GetHash() && ExpectedHash.GetHash(false) == ActualHash.GetHash(false);
        RestoredConsumption->SetBoolField(TEXT("policy_equal"), bPolicyEqual); RestoredConsumption->SetBoolField(TEXT("shapes_equal"), bShapesEqual);
        RestoredConsumption->SetBoolField(TEXT("equivalent"), bHashEqual);
        RestoredConsumption->SetArrayField(TEXT("advertised_partition_local_position"), Position(Local.Location));
        RestoredConsumption->SetArrayField(TEXT("consumed_partition_local_position"), Position(Consumed->Location));
        RestoredConsumption->SetNumberField(TEXT("position_difference_cm"), FVector::Dist(Local.Location, Consumed->Location));
        RestoredConsumption->SetNumberField(TEXT("rotation_difference_degrees"), Local.Rotation.GetManhattanDistance(Consumed->Rotation));
        RestoredConsumption->SetNumberField(TEXT("location_quantization_cm"), FWorldPartitionStreamingSource::GetLocationQuantization());
        RestoredConsumption->SetNumberField(TEXT("rotation_quantization_degrees"), FWorldPartitionStreamingSource::GetRotationQuantization());
        RestoredConsumption->SetNumberField(TEXT("advertised_hash3d"), ExpectedHash.GetHash()); RestoredConsumption->SetNumberField(TEXT("consumed_hash3d"), ActualHash.GetHash());
        RestoredConsumption->SetNumberField(TEXT("advertised_hash2d"), ExpectedHash.GetHash(false)); RestoredConsumption->SetNumberField(TEXT("consumed_hash2d"), ActualHash.GetHash(false));
        return bHashEqual;
    }
    bool PhysicalArrival(AProject_JPlayerCharacter* Player) const
    {
        auto* Capsule = Project_J::Movement::FindCapsule(Player);
        if (!IsValid(Capsule) || !Capsule->IsRegistered() || !Capsule->IsQueryCollisionEnabled()) return false;
        float Radius = 0, HalfHeight = 0; Capsule->GetScaledCapsuleSize(Radius, HalfHeight);
        if (!FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight) || Radius <= 0 || HalfHeight < Radius
            || Capsule->GetComponentLocation().ContainsNaN() || Capsule->GetComponentQuat().ContainsNaN()
            || !Capsule->GetComponentQuat().IsNormalized() || FVector::DistSquared2D(Capsule->GetComponentLocation(), Hint) > FMath::Square(Radius)) return false;
        const FVector Center = Capsule->GetComponentLocation(), Up = Capsule->GetUpVector();
        FCollisionQueryParams Params(SCENE_QUERY_STAT(ProjectJRemoteArrival), false, Player);
        const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
        auto GroundResponses = Responses; GroundResponses.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
        FHitResult Ground;
        return Player->GetWorld()->LineTraceSingleByChannel(Ground, Center, Center-Up*(HalfHeight+Radius), Capsule->GetCollisionObjectType(), Params, GroundResponses)
            && Ground.bBlockingHit && !Ground.bStartPenetrating && !Ground.ImpactPoint.ContainsNaN() && !Ground.ImpactNormal.ContainsNaN()
            && Project_J::Movement::IsWalkable(Player, Ground)
            && !Player->GetWorld()->OverlapBlockingTestByChannel(Center, Capsule->GetComponentQuat(), Capsule->GetCollisionObjectType(),
                FCollisionShape::MakeCapsule(Radius, HalfHeight), Params, Responses);
    }
    void Sample()
    {
        auto* Player = Pawn.Get(); auto* Source = Preparation.Get();
        if (!Player || !Source) return;
        const auto Phase = Source->ArrivalPhase;
        FWorldPartitionStreamingSource Provided;
        const bool bProvided = Source->GetStreamingSource(Provided);
        const bool bPending = Phase == EProject_JWorldArrivalPhase::Preparing || Phase == EProject_JWorldArrivalPhase::Queued
            || Phase == EProject_JWorldArrivalPhase::AwaitingLocalArrival;
        const bool bPreparationStarted = Source->IsPreparedArrivalStarted();
        const bool bWaitingForStart = bPending && Phase == EProject_JWorldArrivalPhase::Preparing && !bPreparationStarted;
        const bool bTarget = bProvided && Provided.Location.Equals(Hint,.1) && Provided.Shapes.Num() == 1
            && Provided.Shapes[0].Radius > 0 && FMath::IsFinite(Provided.Shapes[0].Radius);
        bWaitingForStartObserved |= bWaitingForStart;
        bUnstartedDestinationAdvertised |= bPending && !bPreparationStarted && bTarget;
        // Queued/Awaiting still require a target even if their permission is malformed.
        if (bPending && !bWaitingForStart)
        {
            bDestinationSource |= bTarget; bPendingSourceMissing |= !bTarget;
            bSourceBeforePhysicalArrival |= bTarget && FVector::DistSquared2D(Player->GetActorLocation(), Hint) > FMath::Square(500.0);
        }
        bQueued |= Phase == EProject_JWorldArrivalPhase::Queued;
        if ((LastPhase == int32(Phase) && bLastPreparationStarted == bPreparationStarted) || Samples.Num() >= 32) return;
        LastPhase = int32(Phase); bLastPreparationStarted = bPreparationStarted;
        auto Row = MakeShared<FJsonObject>(); Row->SetNumberField(TEXT("elapsed_s"), FPlatformTime::Seconds()-Started);
        Row->SetNumberField(TEXT("phase"), int32(Phase)); Row->SetArrayField(TEXT("pawn_position"), Position(Player->GetActorLocation()));
        AppendArrivalPreparationStats(Player->GetWorld(), *Row);
        Row->SetBoolField(TEXT("preparation_started"), bPreparationStarted); Row->SetBoolField(TEXT("waiting_for_preparation_start"), bWaitingForStart);
        Row->SetBoolField(TEXT("source_provided"), bProvided); Row->SetBoolField(TEXT("pending_destination_source"), bTarget);
        if (bProvided) Row->SetArrayField(TEXT("source_position"), Position(Provided.Location));
        Samples.Add(MakeShared<FJsonValueObject>(Row));
    }
    bool Finish(bool bSuccess, const FString& Reason)
    {
        bSuccess = bSuccess && !bUnstartedDestinationAdvertised;
        auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), bSuccess);
        Result->SetStringField(TEXT("role"), bServer ? TEXT("Server") : TEXT("Client")); Result->SetStringField(TEXT("reason"), bUnstartedDestinationAdvertised ? FString(TEXT("Destination advertised before authority start permission")) : Reason);
        Result->SetStringField(TEXT("scope"), TEXT("Normal MainWorld BP GameMode, one actual socket owner, client TryInteract production RPC, replicated Portal destination and existing component RPC/source/native effect/physical arrival. No private expected-data RPC, provider/auth/backend, Nav/GPU readiness, HLOD or mass-capacity certification."));
        Result->SetStringField(TEXT("character_id"), Character.ToString(EGuidFormats::Digits));
        Result->SetStringField(TEXT("controller_class"), ObservedControllerClass);
        Result->SetBoolField(TEXT("interaction_requested_by_local_client"), bInteractionRequested);
        Result->SetBoolField(TEXT("replicated_portal_observed"), !bServer && bStarted);
        Result->SetBoolField(TEXT("arrival_observed"), bArrived); Result->SetBoolField(TEXT("queued_observed"), bQueued);
        Result->SetBoolField(TEXT("destination_source_observed"), bDestinationSource);
        Result->SetBoolField(TEXT("destination_source_before_physical_arrival"), bSourceBeforePhysicalArrival);
        Result->SetBoolField(TEXT("pending_source_missing"), bPendingSourceMissing);
        Result->SetBoolField(TEXT("waiting_for_preparation_start_observed"), bWaitingForStartObserved);
        Result->SetBoolField(TEXT("unstarted_destination_advertised"), bUnstartedDestinationAdvertised);
        if (RestoredConsumption) Result->SetObjectField(TEXT("restored_normal_source_consumption"), RestoredConsumption);
        Result->SetNumberField(TEXT("capsule_radius_cm"), CapsuleRadius);
        Result->SetArrayField(TEXT("origin"), Position(Origin)); Result->SetArrayField(TEXT("authored_hint"), Position(Hint));
        Result->SetArrayField(TEXT("actual_destination"), Position(ArrivedPosition)); Result->SetArrayField(TEXT("phase_samples"), Samples);
        FString Json; IFileManager::Get().MakeDirectory(*OutputRoot(), true);
        const bool bSaved = FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json))
            && FFileHelper::SaveStringToFile(Json, *(OutputRoot()/TEXT("world-arrival.json")));
        if (bServer) if (auto* Owned = Portal.Get(); IsValid(Owned) && !Owned->IsActorBeingDestroyed()) Owned->Destroy();
        FPlatformMisc::RequestExitWithStatus(false, bSuccess && bSaved ? 0 : 1); return false;
    }
    bool Tick(UWorld* World)
    {
        const double Now = FPlatformTime::Seconds();
        // A completed server receipt stays valid during the owner's expected normal disconnect/cleanup.
        if (bServer && bArrived)
        {
            const auto* Driver = World ? World->GetNetDriver() : nullptr;
            if (Now-ArrivedAt >= 1 && Driver && Driver->ClientConnections.IsEmpty())
                return Finish(true, TEXT("Authority arrival confirmed; owner disconnected normally and is independently checked by wrapper"));
            if (Now-ArrivedAt >= 35) return Finish(false, TEXT("Authority arrived but owner did not disconnect within bounded grace"));
            return true;
        }
        if (Now-Started >= 180) return Finish(false, TEXT("Bounded normal MainWorld owner connection/arrival deadline expired"));
        if (!World || !World->HasBegunPlay() || Now-Started < 3) return true;
        const ENetMode ExpectedMode = bServer ? NM_DedicatedServer : NM_Client;
        if (World->GetNetMode() != ExpectedMode || World->GetOutermost()->GetName() != TEXT("/Game/World/MainWorld/Maps/L_MainWorld"))
            return bStarted ? Finish(false,TEXT("Expected normal connected MainWorld changed")) : true;
        int32 States = 0; const UProject_JWorldDefinition* Definition = nullptr;
        for (TActorIterator<AProject_JWorldState> It(World); It; ++It) { ++States; Definition = It->Definition; }
        auto* Partition = World->GetWorldPartition();
        if (States != 1 || !Definition || Definition->GetOutermost()->GetName() != TEXT("/Game/World/MainWorld/Data/DA_MainWorld")
            || Definition->Places.Num() != 20 || !Partition || !Partition->IsInitialized() || !Partition->CanStream()) return true;
        const FProject_JWorldPlace* Destination = nullptr; int32 Places = 0;
        for (const auto& Place : Definition->Places) if (Place.PlaceId == TEXT("S01")) { Destination = &Place; ++Places; }
        if (Places != 1 || !Destination || !Destination->Location.Equals(Hint,.1)) return Finish(false,TEXT("Current canonical S01 hint changed"));
        if (bServer && !bServerReady)
        {
            const auto* Mode = World->GetAuthGameMode();
            if (!Mode || Mode->GetClass()->GetPathName() != TEXT("/Game/ThirdPerson/Blueprints/BP_Project_JGameMode.BP_Project_JGameMode_C"))
                return Finish(false,TEXT("Normal configured BP GameMode is required; no diagnostic GameMode override"));
            WriteResult(TEXT("arrival-server-ready.json"),true,TEXT("Normal MainWorld BP GameMode/WP/definition is ready for one actual owner socket"));
            bServerReady = true;
        }
        auto* PC = bStarted ? Controller.Get() : World->GetFirstPlayerController();
        auto* Player = PC ? Cast<AProject_JPlayerCharacter>(PC->GetPawn()) : nullptr;
        auto* State = PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
        auto* Source = PC ? PC->FindComponentByClass<UProject_JWorldStreamingComponent>() : nullptr;
        if (!PC || !Player || !State || !Source || !State->GetCharacterId().IsValid() || PC->IsActorBeingDestroyed()
            || Player->IsActorBeingDestroyed() || Source->IsBeingDestroyed() || State->IsActorBeingDestroyed()) return bStarted ? Finish(false,TEXT("Current owner context retired")) : true;
        if (PC->HasAuthority() != bServer || PC->IsLocalController() == bServer || Player->HasAuthority() != bServer
            || Player->IsLocallyControlled() == bServer || Player->GetController() != PC || State->GetPawn() != Player
            || Source->GetOwner() != PC || Source->GetWorld() != World || Player->GetWorld() != World
            || (bServer && !PC->GetNetConnection()) || (!bServer && (!World->GetNetDriver() || !World->GetNetDriver()->ServerConnection)))
            return bStarted ? Finish(false,TEXT("Actual owning connection/authority contract changed")) : true;
        const TStrongObjectPtr<UWorld> KeepWorld(World);
        const TStrongObjectPtr<APlayerController> KeepController(PC);
        const TStrongObjectPtr<AProject_JPlayerCharacter> KeepPawn(Player);
        const TStrongObjectPtr<AProject_JPlayerState> KeepState(State);
        const TStrongObjectPtr<UProject_JWorldStreamingComponent> KeepSource(Source);
        const TStrongObjectPtr<const UProject_JWorldDefinition> KeepDefinition(Definition);
        if (!bStarted)
        {
            const auto Frame = Project_J::Movement::ReadPhysicalFrame(Player);
            if (!Frame.bValid || !Frame.bGrounded || !Frame.Velocity.IsNearlyZero(1.0)) return true;
            if (FVector::DistSquared2D(Player->GetActorLocation(), FVector(-93200,-93200,0)) > FMath::Square(500.0))
                return Finish(false,TEXT("Normal owner did not spawn at current S00"));
            auto* Capsule = Project_J::Movement::FindCapsule(Player); if (!Capsule) return true;
            CapsuleRadius = Capsule->GetScaledCapsuleRadius();
            const FName DestinationId = Destination->PlaceId;
            Origin = Player->GetActorLocation(); Character = State->GetCharacterId().Value;
            Pawn = Player; PlayerState = State; Controller = PC; Preparation = Source; ObservedWorld = World;
            ObservedControllerClass = PC->GetClass()->GetPathName();
            if (bServer)
            {
                int32 Owners = 0; for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It) ++Owners;
                if (Owners != 1) return Finish(false,TEXT("Exactly one normal remote owner is required"));
                FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn; Params.ObjectFlags |= RF_Transient;
                auto* Owned = World->SpawnActor<AProject_JWorldPortal>(AProject_JWorldPortal::StaticClass(),
                    Player->GetActorLocation()+Player->GetActorForwardVector()*150, Player->GetActorRotation(), Params);
                if (!IsValid(Owned) || Owned->IsActorBeingDestroyed()) return Finish(false,TEXT("Runtime owned Portal did not survive normal spawn"));
                Portal = Owned; Owned->DestinationPlaceId = DestinationId;
                auto* Policy = Owned->FindComponentByClass<UProject_JInteractionTargetComponent>();
                if (!Policy) return Finish(false,TEXT("Production Portal interaction policy missing"));
                Policy->Priority = MAX_int32; Owned->ForceNetUpdate();
            }
            else
            {
                AProject_JWorldPortal* ReplicatedPortal = nullptr; int32 Portals = 0;
                for (TActorIterator<AProject_JWorldPortal> It(World); It; ++It)
                    if (!It->IsActorBeingDestroyed() && It->DestinationPlaceId == TEXT("S01")) { ReplicatedPortal = *It; ++Portals; }
                if (Portals == 0) return true;
                if (Portals != 1 || !IsValid(ReplicatedPortal) || ReplicatedPortal->HasAuthority() || !ReplicatedPortal->IsSpatiallyAvailable(Player))
                    return Finish(false,TEXT("Actual replicated destination/normal Portal range is incoherent"));
                if (!WriteWorldReadinessSnapshot(World,TEXT("world-readiness-origin.json"))) return true;
                Portal = ReplicatedPortal;
            }
            bStarted = true;
            if (!bServer)
            {
                bInteractionRequested = true; Player->TryInteract(); // Actual owning-client production RPC; no test control RPC.
                PC = Controller.Get(); Player = Pawn.Get(); Source = Preparation.Get();
                if (!PC || !Player || !Source || PC->IsActorBeingDestroyed() || Player->IsActorBeingDestroyed() || Source->IsBeingDestroyed())
                    return Finish(false,TEXT("Owner retired during actual client interaction"));
            }
        }
        PC = Controller.Get(); Player = Pawn.Get(); State = PlayerState.Get(); Source = Preparation.Get();
        if (ObservedWorld.Get() != World || World->bIsTearingDown || !PC || PC->IsActorBeingDestroyed() || PC->GetWorld() != World
            || !Player || Player->IsActorBeingDestroyed() || Player->GetWorld() != World || PC->GetPawn() != Player || Player->GetController() != PC
            || !State || State->IsActorBeingDestroyed() || PC->GetPlayerState<AProject_JPlayerState>() != State || State->GetPawn() != Player
            || State->GetCharacterId().Value != Character || !Source || Source->IsBeingDestroyed() || Source->GetWorld() != World || Source->GetOwner() != PC)
            return Finish(false,TEXT("Captured World/Pawn/source/identity changed"));
        Sample();
        if (bPendingSourceMissing) return Finish(false,TEXT("Actual pending destination source was lost before arrival"));
        if (Source->ArrivalPhase == EProject_JWorldArrivalPhase::Failed || Source->ArrivalPhase == EProject_JWorldArrivalPhase::ExpiredUnconfirmed)
            return Finish(false,TEXT("Production request ended without confirmed remote arrival"));
        if (Source->ArrivalPhase != EProject_JWorldArrivalPhase::Arrived) return true;
        if (!bDestinationSource || !bSourceBeforePhysicalArrival || !PhysicalArrival(Player))
            return Finish(false,TEXT("Arrived lacks observed destination lease or actual normal capsule/floor evidence"));
        if (!bArrived) { bArrived = true; ArrivedAt = Now; ArrivedPosition = Player->GetActorLocation(); }
        if (bServer) return true;
        if (Now-ArrivedAt < .3) return true;
        FWorldPartitionStreamingSource Restored;
        if (!Source->GetStreamingSource(Restored) || !Restored.Location.Equals(Player->GetActorLocation(), .1))
            return Finish(false,TEXT("Owner normal predictor did not resume at actual replicated arrival"));
        if (!NormalSourceConsumed(Partition, Restored) || !WriteWorldReadinessSnapshot(World)) return true;
        return Finish(true,TEXT("Actual owner RPC/physical arrival and restored normal predictor advertisement with equivalent engine-consumed partition-local hash/shape passed"));
    }
};

// Fixed two-owner lifecycle case only. File barriers coordinate this same-host
// fixture; gameplay still uses the normal owning-client interaction/component RPCs.
struct FProjectJPreparedOwnerOverlapProbe
{
    struct FOwner
    {
        explicit FOwner(bool bAuthority) : Evidence(bAuthority) {}
        FProjectJPreparedOwnerProbe Evidence;
        TWeakObjectPtr<UNetConnection> Connection;
        TWeakObjectPtr<AProject_JPlayerCharacter> OriginalPawn;
        TWeakObjectPtr<UProject_JAbilitySystemComponent> ASC;
        uint64 OriginalEpoch = 0, CurrentEpoch = 0;
        FGuid Account;
        FString OriginalPawnName, CurrentPawnName, SourceName;
        FName StreamingIdentity;
        FVector LastPosition = FVector::ZeroVector;
        int32 LastRequestPhase = INDEX_NONE;
        bool bBound = false, bPendingPublished = false, bLocalProof = false, bProofPublished = false, bBaseSourceMaintained = true;
        bool bAdmissionReadyPublished = false, bAuthorityRequestAccepted = false;
    };
    explicit FProjectJPreparedOwnerOverlapProbe(bool bAuthority)
        : bServer(bAuthority), Owners{FOwner(bAuthority), FOwner(bAuthority)}
    {
        FParse::Value(FCommandLine::Get(), TEXT("ProjectJAuditArrivalSlot="), Slot);
        FParse::Value(FCommandLine::Get(), TEXT("ProjectJAuditArrivalRun="), Run);
        FParse::Value(FCommandLine::Get(), TEXT("ProjectJAuditArrivalControl="), ControlRoot);
        FPaths::NormalizeDirectoryName(ControlRoot);
        FString Expected = FPaths::GetPath(OutputRoot()) / TEXT("Control");
        FPaths::NormalizeDirectoryName(Expected);
        bArgumentsValid = (bServer ? Slot == 0 : Slot >= 1 && Slot <= 2) && !Run.IsEmpty() && Run.Len() <= 128
            && !FPaths::IsRelative(ControlRoot) && ControlRoot.Equals(Expected, ESearchCase::IgnoreCase);
        for (TCHAR C : Run) bArgumentsValid &= FChar::IsAlnum(C) || C == TEXT('_') || C == TEXT('-');
    }
    const bool bBackpressure = FParse::Param(FCommandLine::Get(), TEXT("ProjectJAuditPreparedOwnerBackpressure"));
    bool bServer = false, bArgumentsValid = false, bReady = false, bFinished = false;
    bool bAuthorityRequestsIssued = false, bBackpressureObserved = false, bWaitingAdmitted = false, bPolicyChanged = false;
    int32 RetiredIndex = 1, PreviousArrivalLimit = 0;
    uint64 AdmissionsBefore = 0, DeferralsBefore = 0;
    TWeakObjectPtr<UProject_JWorldDefinition> AdmissionDefinition;
    TArray<TSharedPtr<FJsonValue>> AdmissionSamples;
    bool bOverlap = false, bRetired = false, bRetiredSourceReleased = false, bRestarted = false, bReplacement = false, bTerminal = false;
    int32 Slot = 0, Stage = 0;
    FString Run, ControlRoot;
    double Started = FPlatformTime::Seconds(), TerminalAt = 0;
    FOwner Owners[2];
    TWeakObjectPtr<AProject_JWorldPortal> Portal;

    TSharedPtr<FJsonObject> Read(const FString& Name) const
    {
        const FString Path = ControlRoot / Name;
        const int64 Bytes = IFileManager::Get().FileSize(*Path);
        if (Bytes <= 0 || Bytes > 4096) return nullptr;
        FString Text; TSharedPtr<FJsonObject> Object;
        if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) || !Object) return nullptr;
        FString ObservedRun;
        return Object->TryGetStringField(TEXT("run"), ObservedRun) && ObservedRun == Run ? Object : nullptr;
    }
    bool Write(const FString& Name, int32 OwnerSlot = 0, const FGuid& Character = FGuid()) const
    {
        auto Object = MakeShared<FJsonObject>(); Object->SetStringField(TEXT("run"), Run);
        Object->SetNumberField(TEXT("slot"), OwnerSlot); Object->SetStringField(TEXT("character"), Character.ToString(EGuidFormats::Digits));
        FString Text;
        return FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text)) && Text.Len() < 4096
            && FFileHelper::SaveStringToFile(Text, *(ControlRoot / Name));
    }
    bool Matches(const TSharedPtr<FJsonObject>& Object, int32 OwnerSlot, const FGuid& Character) const
    {
        FString Id; int32 ObservedSlot = 0;
        return Object && Object->TryGetStringField(TEXT("character"), Id) && Object->TryGetNumberField(TEXT("slot"), ObservedSlot)
            && ObservedSlot == OwnerSlot && Id == Character.ToString(EGuidFormats::Digits);
    }
    bool CaptureAdmissionStats(UWorld* World, const TCHAR* Point)
    {
        const auto* Execution = World ? World->GetSubsystem<UProject_JWorldArrivalSubsystem>() : nullptr;
        if (!Execution || AdmissionSamples.Num() >= 8) return false;
        const auto& Stats = Execution->GetStats();
        auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("point"), Point);
        Row->SetStringField(TEXT("frame"), LexToString(GFrameCounter)); Row->SetNumberField(TEXT("elapsed_s"),FPlatformTime::Seconds()-Started);
        AppendArrivalPreparationStats(World,*Row); Row->SetNumberField(TEXT("pending"),Stats.Pending);
        if (const auto* Definition = AdmissionDefinition.Get()) Row->SetNumberField(TEXT("admission_limit"),Definition->MaxSimultaneousPreparedArrivals);
        Row->SetStringField(TEXT("registered"),LexToString(Stats.Registered)); Row->SetStringField(TEXT("retired"),LexToString(Stats.Retired));
        TArray<TSharedPtr<FJsonValue>> States;
        for (int32 Index=0; Index<2; ++Index)
        {
            auto Value = MakeShared<FJsonObject>(); Value->SetNumberField(TEXT("slot"),Index+1);
            const auto* Source = Owners[Index].Evidence.Preparation.Get(); FWorldPartitionStreamingSource Advertised;
            Value->SetNumberField(TEXT("phase"),Source ? int32(Source->ArrivalPhase) : INDEX_NONE);
            Value->SetBoolField(TEXT("preparation_started"),Source && Source->IsPreparedArrivalStarted());
            const bool bProvided = Source && Source->GetStreamingSource(Advertised);
            Value->SetBoolField(TEXT("source_provided"),bProvided);
            Value->SetBoolField(TEXT("destination_source"),bProvided && Advertised.Location.Equals(Owners[Index].Evidence.Hint,.1));
            States.Add(MakeShared<FJsonValueObject>(Value));
        }
        Row->SetArrayField(TEXT("owners"),States); AdmissionSamples.Add(MakeShared<FJsonValueObject>(Row)); return true;
    }
    bool BeginAdmissionPolicy(UWorld* World)
    {
        auto* Execution = World ? World->GetSubsystem<UProject_JWorldArrivalSubsystem>() : nullptr;
        if (!Execution || Execution->GetStats().Pending || Execution->GetStats().ActivePreparationPermits
            || Execution->GetStats().PeakPreparationPermits) return false;
        UProject_JWorldDefinition* Definition = nullptr; int32 Count = 0;
        for (TActorIterator<AProject_JWorldState> It(World); It; ++It) { ++Count; Definition = It->Definition.Get(); }
        if (Count != 1 || !Definition || !Definition->IsPreparedArrivalAdmissionValid()) return false;
        AdmissionDefinition = Definition; PreviousArrivalLimit = Definition->MaxSimultaneousPreparedArrivals;
        AdmissionsBefore = Execution->GetStats().PreparationAdmissions; DeferralsBefore = Execution->GetStats().PreparationDeferrals;
        Definition->MaxSimultaneousPreparedArrivals = 1; bPolicyChanged = true; // Process-local tuning only; never SavePackage/mark the authored asset dirty.
        return CaptureAdmissionStats(World,TEXT("baseline"));
    }
    bool RestoreAdmissionPolicy()
    {
        if (!bPolicyChanged) return true;
        auto* Definition = AdmissionDefinition.Get();
        if (!IsValid(Definition) || Definition->MaxSimultaneousPreparedArrivals != 1) return false; // Do not overwrite an unexpected writer.
        Definition->MaxSimultaneousPreparedArrivals = PreviousArrivalLimit; bPolicyChanged = false; return true;
    }
    bool WriteAdmissionSelection() const
    {
        auto Object = MakeShared<FJsonObject>(); Object->SetStringField(TEXT("run"),Run);
        Object->SetNumberField(TEXT("slot"),RetiredIndex+1); Object->SetStringField(TEXT("character"),Owners[RetiredIndex].Evidence.Character.ToString(EGuidFormats::Digits));
        Object->SetNumberField(TEXT("waiting_slot"),2-RetiredIndex); Object->SetStringField(TEXT("waiting_character"),Owners[1-RetiredIndex].Evidence.Character.ToString(EGuidFormats::Digits));
        FString Text;
        return FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Text)) && Text.Len()<4096
            && FFileHelper::SaveStringToFile(Text,*(ControlRoot/TEXT("admitted.json")));
    }
    bool Bind(FOwner& Row, APlayerController* PC, UWorld* World)
    {
        auto* Player = PC ? Cast<AProject_JPlayerCharacter>(PC->GetPawn()) : nullptr;
        auto* State = PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
        auto* Source = PC ? PC->FindComponentByClass<UProject_JWorldStreamingComponent>() : nullptr;
        auto* ASC = State ? State->GetProjectJAbilitySystemComponent() : nullptr;
        if (!IsValid(PC) || !IsValid(Player) || !IsValid(State) || !IsValid(Source) || !IsValid(ASC)
            || PC->IsActorBeingDestroyed() || Player->IsActorBeingDestroyed() || State->IsActorBeingDestroyed() || Source->IsBeingDestroyed()
            || PC->GetWorld() != World || Player->GetWorld() != World || State->GetWorld() != World || Source->GetWorld() != World
            || !Source->IsRegistered() || Source->GetOwner() != PC || State->GetOwner() != PC || State->GetPawn() != Player
            || Player->GetController() != PC || PC->HasAuthority() != bServer || Player->HasAuthority() != bServer || PC->IsLocalController() == bServer || Player->IsLocallyControlled() == bServer
            || !State->GetCharacterId().IsValid() || !State->GetAccountId().IsValid() || ASC->IsBeingDestroyed()
            || ASC->IsActorInfoChanging() || !ASC->GetActorInfoRevision() || ASC->GetOwnerActor() != State || ASC->GetAvatarActor() != Player
            || (bServer && !PC->GetNetConnection()) || (!bServer && (!World->GetNetDriver() || !World->GetNetDriver()->ServerConnection))) return false;
        const auto Frame = Project_J::Movement::ReadPhysicalFrame(Player);
        if (!Frame.bValid || !Frame.bMover || !Frame.bGrounded || !Frame.Velocity.IsNearlyZero(1.0)
            || FVector::DistSquared2D(Player->GetActorLocation(), FVector(-93200,-93200,0)) > FMath::Square(500.0)) return false;
        auto* Capsule = Project_J::Movement::FindCapsule(Player);
        if (!IsValid(Capsule) || !Capsule->IsRegistered() || !Capsule->IsQueryCollisionEnabled()) return false;
        auto& E = Row.Evidence;
        E.Controller = PC; E.Pawn = Player; E.PlayerState = State; E.Preparation = Source; E.ObservedWorld = World;
        E.Origin = Player->GetActorLocation(); E.Character = State->GetCharacterId().Value;
        E.CapsuleRadius = Capsule->GetScaledCapsuleRadius(); E.ObservedControllerClass = PC->GetClass()->GetPathName();
        Row.Account = State->GetAccountId().Value; Row.OriginalPawn = Player; Row.OriginalPawnName = Player->GetPathName();
        Row.CurrentPawnName = Row.OriginalPawnName; Row.LastPosition = Player->GetActorLocation();
        Row.ASC = ASC; Row.OriginalEpoch = Row.CurrentEpoch = ASC->GetActorInfoRevision();
        Row.StreamingIdentity = Source->GetStreamingSourceIdentity();
        if (Row.StreamingIdentity.IsNone()) return false;
        Row.SourceName = Source->GetPathName(); Row.Connection = bServer ? PC->GetNetConnection() : World->GetNetDriver()->ServerConnection.Get();
        Row.bBound = true; return true;
    }
    bool Current(FOwner& Row, UWorld* World, bool bOriginal) const
    {
        const auto& E = Row.Evidence;
        auto* PC = E.Controller.Get(); auto* Player = E.Pawn.Get(); auto* State = E.PlayerState.Get(); auto* Source = E.Preparation.Get();
        auto* ASC = Row.ASC.Get();
        return IsValid(PC) && !PC->IsActorBeingDestroyed() && IsValid(Player) && !Player->IsActorBeingDestroyed()
            && IsValid(State) && !State->IsActorBeingDestroyed() && IsValid(Source) && !Source->IsBeingDestroyed() && Source->IsRegistered()
            && IsValid(ASC) && !ASC->IsBeingDestroyed() && !ASC->IsActorInfoChanging() && ASC->GetActorInfoRevision() == Row.CurrentEpoch
            && (!bOriginal || (Row.OriginalPawn.Get() == Player && Row.CurrentEpoch == Row.OriginalEpoch))
            && PC->GetWorld() == World && Player->GetWorld() == World && State->GetWorld() == World && Source->GetWorld() == World
            && PC->GetPawn() == Player && Player->GetController() == PC && PC->PlayerState == State && State->GetPawn() == Player && State->GetOwner() == PC
            && State->GetCharacterId().Value == E.Character && State->GetAccountId().Value == Row.Account
            && State->GetProjectJAbilitySystemComponent() == ASC && ASC->GetOwnerActor() == State && ASC->GetAvatarActor() == Player
            && Source->GetOwner() == PC && Source->GetPathName() == Row.SourceName && Source->GetStreamingSourceIdentity() == Row.StreamingIdentity && PC->HasAuthority() == bServer && Player->HasAuthority() == bServer
            && PC->IsLocalController() != bServer && Player->IsLocallyControlled() != bServer
            && Row.Connection.IsValid() && (bServer ? PC->GetNetConnection() == Row.Connection.Get() : World->GetNetDriver() && World->GetNetDriver()->ServerConnection == Row.Connection.Get());
    }
    bool BaseSource(FOwner& Row) const
    {
        auto* PC = Row.Evidence.Controller.Get();
        TArray<FWorldPartitionStreamingSource> Sources;
        const bool bValid = PC && PC->GetStreamingSources(Sources) && !Sources.IsEmpty()
            && !Sources.ContainsByPredicate([&](const auto& Source) { return Source.Name == Row.StreamingIdentity; });
        Row.bBaseSourceMaintained &= bValid; return bValid;
    }
    bool Restored(FOwner& Row, UWorld* World)
    {
        auto* Source = Row.Evidence.Preparation.Get(); auto* Player = Row.Evidence.Pawn.Get();
        FWorldPartitionStreamingSource Advertised;
        return Source && Player && BaseSource(Row) && Source->GetStreamingSource(Advertised)
            && Advertised.Location.Equals(Player->GetActorLocation(), .1) && Row.Evidence.NormalSourceConsumed(World->GetWorldPartition(), Advertised);
    }
    bool NewAvatar(FOwner& Row, UWorld* World)
    {
        auto* PC = Row.Evidence.Controller.Get(); auto* State = Row.Evidence.PlayerState.Get();
        auto* Player = PC ? Cast<AProject_JPlayerCharacter>(PC->GetPawn()) : nullptr;
        auto* ASC = Row.ASC.Get();
        if (!IsValid(Player) || Player->IsActorBeingDestroyed() || TWeakObjectPtr<AProject_JPlayerCharacter>(Player) == Row.OriginalPawn
            || !Player->GetRootComponent() || !Player->GetRootComponent()->IsRegistered() || !State || !ASC || ASC->IsActorInfoChanging()
            || !ASC->GetActorInfoRevision() || ASC->GetActorInfoRevision() == Row.OriginalEpoch || ASC->GetAvatarActor() != Player
            || State->GetPawn() != Player || Player->GetController() != PC || Player->GetWorld() != World
            || FVector::DistSquared2D(Player->GetActorLocation(), Row.Evidence.Origin) > FMath::Square(500.0)) return false;
        const auto Frame = Project_J::Movement::ReadPhysicalFrame(Player);
        if (!Frame.bValid || !Frame.bMover || !Frame.bGrounded || !Frame.Velocity.IsNearlyZero(1.0)) return false;
        Row.Evidence.Pawn = Player; Row.CurrentEpoch = ASC->GetActorInfoRevision(); Row.CurrentPawnName = Player->GetPathName();
        return Current(Row, World, false);
    }
    bool Finish(bool bSuccess, const FString& Reason)
    {
        if (bFinished) return false;
        bFinished = true;
        bool bUnstartedLeak = false;
        for (const auto& Owner : Owners) bUnstartedLeak |= Owner.Evidence.bUnstartedDestinationAdvertised;
        bSuccess = bSuccess && !bUnstartedLeak;
        const bool bPolicyRestored = RestoreAdmissionPolicy(); bSuccess &= bPolicyRestored;
        auto Result = MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("success"), bSuccess);
        Result->SetStringField(TEXT("run"), Run); Result->SetStringField(TEXT("role"), bServer ? TEXT("Server") : TEXT("Client"));
        Result->SetNumberField(TEXT("slot"), Slot); Result->SetStringField(TEXT("reason"), bUnstartedLeak ? FString(TEXT("Destination advertised before authority start permission")) : Reason);
        Result->SetBoolField(TEXT("overlap_observed"), bOverlap); Result->SetBoolField(TEXT("old_pawn_retired"), bRetired);
        Result->SetBoolField(TEXT("restart_requested"), bRestarted); Result->SetBoolField(TEXT("replacement_observed"), bReplacement);
        Result->SetBoolField(TEXT("retired_destination_source_released"), bRetiredSourceReleased);
        Result->SetStringField(TEXT("scope"), bBackpressure
            ? TEXT("Opt-in cap1: two trusted server RequestPreparedArrival entries in one GT callback, actual authority permit deferral, dynamically selected admitted-owner public Pawn retirement/replacement and waiting-owner native arrival/source restoration. Not client input-RPC coverage, strict FIFO fairness, total client demand or physical I/O/capacity certification.")
            : TEXT("Two actual MainWorld owners; pre-queue request retirement by public Pawn lifecycle, survivor native arrival and local source restoration. Same-host file barriers are fixture control only; no queued-effect cancellation, Nav/GPU/high-speed/capacity certification."));
        if (bBackpressure)
        {
            Result->SetBoolField(TEXT("backpressure_observed"),bBackpressureObserved);
            Result->SetBoolField(TEXT("waiting_owner_admitted"),bWaitingAdmitted);
            Result->SetNumberField(TEXT("retired_admitted_slot"),RetiredIndex+1); Result->SetNumberField(TEXT("survivor_waiting_slot"),2-RetiredIndex);
            Result->SetBoolField(TEXT("authority_requests_issued"),bAuthorityRequestsIssued);
            Result->SetBoolField(TEXT("input_rpc_coverage"),false); Result->SetBoolField(TEXT("runtime_policy_restored"),bPolicyRestored);
            if (bServer)
            {
                Result->SetNumberField(TEXT("previous_admission_limit"),PreviousArrivalLimit);
                Result->SetNumberField(TEXT("admissions_before"),double(AdmissionsBefore)); Result->SetNumberField(TEXT("deferrals_before"),double(DeferralsBefore));
                Result->SetArrayField(TEXT("admission_samples"),AdmissionSamples);
            }
        }
        TArray<TSharedPtr<FJsonValue>> Rows;
        for (int32 Index = 0; Index < 2; ++Index)
        {
            if (!bServer && Index != Slot-1) continue;
            const auto& Row = Owners[Index]; const auto& E = Row.Evidence;
            auto Value = MakeShared<FJsonObject>(); Value->SetNumberField(TEXT("slot"), Index+1);
            Value->SetStringField(TEXT("character_id"), E.Character.ToString(EGuidFormats::Digits));
            Value->SetStringField(TEXT("original_pawn"), Row.OriginalPawnName); Value->SetStringField(TEXT("current_pawn"), Row.CurrentPawnName);
            Value->SetStringField(TEXT("source_name"), Row.SourceName); Value->SetStringField(TEXT("streaming_source_id"), Row.StreamingIdentity.ToString()); Value->SetBoolField(TEXT("local_proof"), Row.bLocalProof);
            Value->SetBoolField(TEXT("interaction_requested"), E.bInteractionRequested); Value->SetBoolField(TEXT("arrival_observed"), E.bArrived);
            Value->SetBoolField(TEXT("destination_source_observed"), E.bDestinationSource); Value->SetBoolField(TEXT("source_before_physical_arrival"), E.bSourceBeforePhysicalArrival);
            Value->SetBoolField(TEXT("pending_source_missing"), E.bPendingSourceMissing);
            Value->SetBoolField(TEXT("waiting_for_preparation_start_observed"), E.bWaitingForStartObserved);
            Value->SetBoolField(TEXT("unstarted_destination_advertised"), E.bUnstartedDestinationAdvertised);
            if (!bServer) Value->SetBoolField(TEXT("base_pc_source_maintained"), Row.bBaseSourceMaintained);
            if (bBackpressure)
            {
                Value->SetBoolField(TEXT("authority_request_accepted"),Row.bAuthorityRequestAccepted);
                Value->SetBoolField(TEXT("origin_ready_published"),Row.bAdmissionReadyPublished);
            }
            Value->SetNumberField(TEXT("last_request_phase"), E.Preparation.IsValid() ? int32(E.Preparation->ArrivalPhase) : Row.LastRequestPhase);
            Value->SetStringField(TEXT("original_epoch"), LexToString(Row.OriginalEpoch)); Value->SetStringField(TEXT("current_epoch"), LexToString(Row.CurrentEpoch));
            Value->SetArrayField(TEXT("phase_samples"), E.Samples); Value->SetArrayField(TEXT("origin"), E.Position(E.Origin));
            Value->SetNumberField(TEXT("capsule_radius_cm"), E.CapsuleRadius);
            Value->SetArrayField(TEXT("current_position"), E.Position(Row.LastPosition));
            if (E.RestoredConsumption) Value->SetObjectField(TEXT("restored_normal_source_consumption"), E.RestoredConsumption);
            Rows.Add(MakeShared<FJsonValueObject>(Value));
        }
        Result->SetArrayField(TEXT("owners"), Rows);
        FString Text; IFileManager::Get().MakeDirectory(*OutputRoot(), true);
        const bool bSaved = FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Text))
            && FFileHelper::SaveStringToFile(Text, *(OutputRoot()/(bBackpressure ? TEXT("world-arrival-backpressure.json") : TEXT("world-arrival-overlap.json"))));
        if (bServer) if (auto* Owned = Portal.Get(); IsValid(Owned) && !Owned->IsActorBeingDestroyed()) Owned->Destroy();
        FPlatformMisc::RequestExitWithStatus(false, bSuccess && bSaved ? 0 : 1); return false;
    }
    bool Tick(UWorld* World)
    {
        check(IsInGameThread());
        const double Now = FPlatformTime::Seconds();
        if (bFinished) return false;
        if (!bArgumentsValid) return Finish(false,TEXT("Explicit overlap slot/run/fresh sibling Control arguments required"));
        if (bServer && bTerminal)
        {
            const auto* Driver = World ? World->GetNetDriver() : nullptr;
            if (Now-TerminalAt >= 1 && Driver && Driver->ClientConnections.IsEmpty()) return Finish(true,TEXT("Both independent owner proofs and normal disconnects completed"));
            return Now-TerminalAt < 35 ? true : Finish(false,TEXT("Both owners did not disconnect within bounded completion grace"));
        }
        if (Now-Started >= 180) return Finish(false,TEXT("Bounded two-owner preparation/replacement deadline expired"));
        if (!World || !World->HasBegunPlay() || Now-Started < 3) return true;
        const TStrongObjectPtr<UWorld> KeepWorld(World);
        if (World->bIsTearingDown || World->GetNetMode() != (bServer ? NM_DedicatedServer : NM_Client)
            || World->GetOutermost()->GetName() != TEXT("/Game/World/MainWorld/Maps/L_MainWorld"))
            return bReady || Owners[0].bBound || Owners[1].bBound ? Finish(false,TEXT("Expected normal connected MainWorld retired/changed")) : true;
        const UProject_JWorldDefinition* Definition = nullptr; int32 States = 0;
        for (TActorIterator<AProject_JWorldState> It(World); It; ++It) { ++States; Definition = It->Definition; }
        auto* Partition = World->GetWorldPartition();
        if (States != 1 || !Definition || Definition->GetOutermost()->GetName() != TEXT("/Game/World/MainWorld/Data/DA_MainWorld")
            || Definition->Places.Num() != 20 || !Partition || !Partition->IsInitialized() || !Partition->CanStream()) return true;
        int32 Places = 0; for (const auto& Place : Definition->Places) if (Place.PlaceId == TEXT("S01") && Place.Location.Equals(Owners[0].Evidence.Hint,.1)) ++Places;
        if (Places != 1) return Finish(false,TEXT("Current canonical S01 changed"));
        if (bBackpressure && bServer && bPolicyChanged && (Definition != AdmissionDefinition.Get() || Definition->MaxSimultaneousPreparedArrivals != 1))
            return Finish(false,TEXT("Runtime admission policy/definition changed during the scoped cohort"));
        if (bServer) return TickServer(World);
        return TickClient(World);
    }
    bool TickServer(UWorld* World)
    {
        if (!bReady)
        {
            auto* Mode = World->GetAuthGameMode();
            if (!Mode || Mode->GetClass()->GetPathName() != TEXT("/Game/ThirdPerson/Blueprints/BP_Project_JGameMode.BP_Project_JGameMode_C")) return Finish(false,TEXT("Normal BP GameMode required"));
            WriteResult(TEXT("arrival-server-ready.json"),true,TEXT("Normal MainWorld ready for exactly two actual owners")); bReady = true;
        }
        if (Stage == 0)
        {
            TArray<APlayerController*> Controllers;
            for (auto It = World->GetPlayerControllerIterator(); It; ++It) if (auto* PC = It->Get(); PC && PC->GetNetConnection()) Controllers.Add(PC);
            if (Controllers.Num() < 2) return true;
            if (Controllers.Num() != 2 || !World->GetNetDriver() || World->GetNetDriver()->ClientConnections.Num() != 2) return Finish(false,TEXT("Exactly two actual owner connections required"));
            for (int32 Index = 0; Index < 2; ++Index)
            {
                auto Registration = Read(FString::Printf(TEXT("owner-%d.json"), Index+1));
                if (!Registration) return true;
                FString Id; if (!Registration->TryGetStringField(TEXT("character"), Id)) return Finish(false,TEXT("Owner registration identity missing"));
                APlayerController* Match = nullptr;
                for (auto* PC : Controllers) if (auto* State = PC->GetPlayerState<AProject_JPlayerState>(); State && State->GetCharacterId().Value.ToString(EGuidFormats::Digits) == Id) Match = PC;
                if (!Match || !Bind(Owners[Index], Match, World) || !Matches(Registration, Index+1, Owners[Index].Evidence.Character)) return true;
            }
            if (Owners[0].Evidence.Character == Owners[1].Evidence.Character || Owners[0].Connection == Owners[1].Connection || Owners[0].SourceName == Owners[1].SourceName || Owners[0].StreamingIdentity == Owners[1].StreamingIdentity) return Finish(false,TEXT("Actual owner identities alias"));
            const TStrongObjectPtr<AProject_JPlayerCharacter> KeepFirst(Owners[0].Evidence.Pawn.Get());
            const TStrongObjectPtr<AProject_JPlayerCharacter> KeepSecond(Owners[1].Evidence.Pawn.Get());
            auto* First = KeepFirst.Get(); auto* Second = KeepSecond.Get();
            FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn; Params.ObjectFlags |= RF_Transient;
            auto* Owned = World->SpawnActor<AProject_JWorldPortal>(AProject_JWorldPortal::StaticClass(),
                (First->GetActorLocation()+Second->GetActorLocation())*.5+First->GetActorForwardVector()*150, First->GetActorRotation(), Params);
            if (!IsValid(Owned)) return Finish(false,TEXT("Shared production Portal spawn failed"));
            Portal = Owned; Owned->DestinationPlaceId = TEXT("S01");
            auto* Policy = Owned->FindComponentByClass<UProject_JInteractionTargetComponent>();
            if (!Policy || !Owned->IsSpatiallyAvailable(First) || !Owned->IsSpatiallyAvailable(Second)) return Finish(false,TEXT("No legal shared Portal range/LoS for the normal spawns"));
            Policy->Priority = MAX_int32; Owned->ForceNetUpdate();
            if (bBackpressure && !BeginAdmissionPolicy(World)) return Finish(false,TEXT("Cannot isolate the authority runtime cap1 policy"));
            if (!Write(TEXT("start.json"))) return Finish(false,TEXT("Cannot save fresh start barrier"));
            Stage = 1;
        }
        if (bBackpressure && !bAuthorityRequestsIssued)
        {
            for (int32 Index=0; Index<2; ++Index)
            {
                if (!Current(Owners[Index],World,true)) return Finish(false,TEXT("Owner changed before the trusted authority request pair"));
                if (!Matches(Read(FString::Printf(TEXT("ready-%d.json"),Index+1)),Index+1,Owners[Index].Evidence.Character)) return true;
            }
            const TStrongObjectPtr<AProject_JWorldPortal> KeepPortal(Portal.Get());
            const TStrongObjectPtr<AProject_JPlayerCharacter> KeepFirst(Owners[0].Evidence.Pawn.Get()), KeepSecond(Owners[1].Evidence.Pawn.Get());
            const TStrongObjectPtr<UProject_JWorldStreamingComponent> KeepFirstSource(Owners[0].Evidence.Preparation.Get()), KeepSecondSource(Owners[1].Evidence.Preparation.Get());
            if (!KeepPortal || !KeepFirst || !KeepSecond || !KeepFirstSource || !KeepSecondSource) return Finish(false,TEXT("Trusted request inputs retired"));
            // Both actual remote entries register in this one GT callback before
            // the existing writer can grant either. No phase, ACK or movement injection.
            Owners[0].bAuthorityRequestAccepted = KeepFirstSource->RequestPreparedArrival(KeepPortal.Get(),KeepFirst.Get());
            if (!Owners[0].bAuthorityRequestAccepted || !Current(Owners[0],World,true) || !Current(Owners[1],World,true)) return Finish(false,TEXT("First trusted production request rejected or retired its inputs"));
            Owners[1].bAuthorityRequestAccepted = KeepSecondSource->RequestPreparedArrival(KeepPortal.Get(),KeepSecond.Get());
            if (!Owners[1].bAuthorityRequestAccepted) return Finish(false,TEXT("Second trusted production request rejected"));
            bAuthorityRequestsIssued = true;
            const auto* Execution = World->GetSubsystem<UProject_JWorldArrivalSubsystem>();
            if (!Execution || Execution->GetStats().Pending != 2 || Execution->GetStats().ActivePreparationPermits != 0
                || !CaptureAdmissionStats(World,TEXT("requests_registered"))) return Finish(false,TEXT("Paired public requests did not register before admission"));
            return true;
        }
        if (!Current(Owners[1-RetiredIndex],World,true)) return Finish(false,TEXT("Survivor owner/connection/epoch changed"));
        Owners[1-RetiredIndex].Evidence.Sample();
        const auto SurvivorPhase = Owners[1-RetiredIndex].Evidence.Preparation->ArrivalPhase;
        if (SurvivorPhase == EProject_JWorldArrivalPhase::Failed || SurvivorPhase == EProject_JWorldArrivalPhase::ExpiredUnconfirmed) return Finish(false,TEXT("Survivor production request failed"));
        if (Stage == 1)
        {
            if (!Current(Owners[RetiredIndex],World,true)) return Finish(false,TEXT("Replacement owner changed before cancellation"));
            Owners[RetiredIndex].Evidence.Sample();
            for (auto& Row : Owners)
            {
                const auto Phase = Row.Evidence.Preparation->ArrivalPhase;
                if (Phase == EProject_JWorldArrivalPhase::Failed || Phase == EProject_JWorldArrivalPhase::ExpiredUnconfirmed)
                    return Finish(false,TEXT("Production request failed before overlapping preparation"));
                if (Phase == EProject_JWorldArrivalPhase::Queued || Phase == EProject_JWorldArrivalPhase::Arrived)
                    return Finish(false,TEXT("Actual overlapping Preparing window missed; no artificial ACK delay"));
            }
            if (Owners[1-RetiredIndex].Evidence.Preparation->ArrivalPhase != EProject_JWorldArrivalPhase::Preparing
                || Owners[RetiredIndex].Evidence.Preparation->ArrivalPhase != EProject_JWorldArrivalPhase::Preparing) return true;
            if (!bBackpressure)
            {
                for (int32 Index = 0; Index < 2; ++Index)
                    if (!Owners[Index].Evidence.bDestinationSource || !Owners[Index].Evidence.bSourceBeforePhysicalArrival || Owners[Index].Evidence.bPendingSourceMissing
                        || !Matches(Read(FString::Printf(TEXT("pending-%d.json"),Index+1)), Index+1, Owners[Index].Evidence.Character)) return true;
                bOverlap = true;
            }
            else
            {
                const auto* Execution = World->GetSubsystem<UProject_JWorldArrivalSubsystem>();
                if (!Execution) return Finish(false,TEXT("Authority admission writer unavailable"));
                const auto& Stats = Execution->GetStats();
                const bool FirstStarted = Owners[0].Evidence.Preparation->IsPreparedArrivalStarted();
                const bool SecondStarted = Owners[1].Evidence.Preparation->IsPreparedArrivalStarted();
                if (Stats.ActivePreparationPermits > 1 || Stats.PeakPreparationPermits > 1 || (FirstStarted && SecondStarted)) return Finish(false,TEXT("Actual authority cap1 exceeded"));
                if (FirstStarted == SecondStarted || Stats.PreparationDeferrals <= DeferralsBefore) return true;
                RetiredIndex = FirstStarted ? 0 : 1;
                auto& Admitted = Owners[RetiredIndex].Evidence; auto& Waiting = Owners[1-RetiredIndex].Evidence;
                FWorldPartitionStreamingSource Unadmitted;
                if (Stats.ActivePreparationPermits != 1 || Stats.Pending != 2 || Stats.PreparationAdmissions != AdmissionsBefore+1
                    || !Admitted.bDestinationSource || !Admitted.bSourceBeforePhysicalArrival || Admitted.bPendingSourceMissing
                    || !Waiting.bWaitingForStartObserved || Waiting.bUnstartedDestinationAdvertised
                    || Waiting.Preparation->GetStreamingSource(Unadmitted) || Waiting.PhysicalArrival(Waiting.Pawn.Get()))
                    return Finish(false,TEXT("Actual blocked authority destination/source proof incomplete"));
                bBackpressureObserved = true;
                if (!CaptureAdmissionStats(World,TEXT("blocked")) || !WriteAdmissionSelection()) return Finish(false,TEXT("Cannot save actual admitted/waiting selection"));
            }
            const TStrongObjectPtr<APlayerController> KeepPC(Owners[RetiredIndex].Evidence.Controller.Get());
            const TStrongObjectPtr<AProject_JPlayerState> KeepState(Owners[RetiredIndex].Evidence.PlayerState.Get());
            const TStrongObjectPtr<UProject_JWorldStreamingComponent> KeepSource(Owners[RetiredIndex].Evidence.Preparation.Get());
            auto* PC = KeepPC.Get();
            const TStrongObjectPtr<AProject_JPlayerCharacter> Old(Owners[RetiredIndex].Evidence.Pawn.Get());
            if (!Old || Owners[RetiredIndex].Evidence.PhysicalArrival(Old.Get())) return Finish(false,TEXT("Retiring pawn already arrived"));
            PC->UnPossess();
            if (World->bIsTearingDown || PC->IsActorBeingDestroyed() || KeepState->IsActorBeingDestroyed() || KeepSource->IsBeingDestroyed()
                || PC->GetPawn() || Old->GetController() || !Old->Destroy() || !Old->IsActorBeingDestroyed()) return Finish(false,TEXT("Normal old pawn retirement failed"));
            bRetired = true; Stage = 2;
            if (!Write(TEXT("retired.json"),RetiredIndex+1,Owners[RetiredIndex].Evidence.Character)) return Finish(false,TEXT("Cannot save retirement observation"));
            if (bBackpressure && !CaptureAdmissionStats(World,TEXT("pawn_retired"))) return Finish(false,TEXT("Cannot save public retirement admission state"));
            return true;
        }
        auto& Replaced = Owners[RetiredIndex]; auto* PC = Replaced.Evidence.Controller.Get(); auto* Source = Replaced.Evidence.Preparation.Get();
        if (bBackpressure && !bWaitingAdmitted && Owners[1-RetiredIndex].Evidence.Preparation->IsPreparedArrivalStarted())
        {
            const auto* Execution = World->GetSubsystem<UProject_JWorldArrivalSubsystem>();
            if (!Execution || !Source || Source->ArrivalPhase != EProject_JWorldArrivalPhase::Failed || Source->IsPreparedArrivalStarted()
                || Execution->GetStats().ActivePreparationPermits != 1 || Execution->GetStats().PreparationAdmissions != AdmissionsBefore+2)
                return Finish(false,TEXT("Waiting admission occurred without exact old-request retirement"));
            bWaitingAdmitted = true;
            if (!CaptureAdmissionStats(World,TEXT("waiting_admitted"))) return Finish(false,TEXT("Cannot save authority permit handoff"));
        }
        if (!IsValid(PC) || PC->IsActorBeingDestroyed() || !IsValid(Source) || Source->IsBeingDestroyed() || !Replaced.Connection.IsValid() || PC->GetNetConnection() != Replaced.Connection.Get()) return Finish(false,TEXT("Replacement session/source connection retired"));
        if (Stage == 2)
        {
            if (PC->GetPawn()) return Finish(false,TEXT("Another writer replaced the Pawn before the fixture restart"));
            if (Source->ArrivalPhase != EProject_JWorldArrivalPhase::Failed) return true;
            FWorldPartitionStreamingSource Previous;
            if (Source->GetStreamingSource(Previous)) return Finish(false,TEXT("Retired remote request still advertises a destination"));
            bRetiredSourceReleased = true;
            auto* Mode = World->GetAuthGameMode(); if (!Mode) return Finish(false,TEXT("Normal GameMode retired before restart"));
            const TStrongObjectPtr<APlayerController> KeepPC(PC);
            const TStrongObjectPtr<AGameModeBase> KeepMode(Mode);
            const TStrongObjectPtr<UProject_JWorldStreamingComponent> KeepSource(Source);
            Mode->RestartPlayer(PC); bRestarted = true; Stage = 3;
            if (World->bIsTearingDown || PC->IsActorBeingDestroyed() || Source->IsBeingDestroyed()) return Finish(false,TEXT("Owner/world retired during normal restart"));
        }
        if (Stage == 3)
        {
            if (!NewAvatar(Replaced,World)) return true;
            bReplacement = true; Stage = 4;
        }
        if (!Current(Replaced,World,false) || Source->ArrivalPhase != EProject_JWorldArrivalPhase::Failed
            || Replaced.Evidence.PhysicalArrival(Replaced.Evidence.Pawn.Get())) return Finish(false,TEXT("Old request reopened or replacement identity/position changed"));
        if (SurvivorPhase == EProject_JWorldArrivalPhase::Arrived)
        {
            if (!Owners[1-RetiredIndex].Evidence.bDestinationSource || !Owners[1-RetiredIndex].Evidence.bSourceBeforePhysicalArrival || Owners[1-RetiredIndex].Evidence.bPendingSourceMissing
                || !Owners[1-RetiredIndex].Evidence.PhysicalArrival(Owners[1-RetiredIndex].Evidence.Pawn.Get())) return Finish(false,TEXT("Survivor lacks actual native receipt/capsule/floor proof"));
            Owners[1-RetiredIndex].Evidence.bArrived = true;
        }
        if (Owners[1-RetiredIndex].Evidence.bArrived && Matches(Read(FString::Printf(TEXT("proof-%d.json"),2-RetiredIndex)),2-RetiredIndex,Owners[1-RetiredIndex].Evidence.Character)
            && Matches(Read(FString::Printf(TEXT("proof-%d.json"),RetiredIndex+1)),RetiredIndex+1,Replaced.Evidence.Character))
        {
            for (auto& Row : Owners)
            {
                Row.LastPosition = Row.Evidence.Pawn->GetActorLocation();
                Row.LastRequestPhase = int32(Row.Evidence.Preparation->ArrivalPhase);
            }
            if (bBackpressure)
            {
                const auto* Execution = World->GetSubsystem<UProject_JWorldArrivalSubsystem>();
                if (!bBackpressureObserved || !bWaitingAdmitted || !Execution || Execution->GetStats().ActivePreparationPermits
                    || Execution->GetStats().Pending || Execution->GetStats().PeakPreparationPermits != 1
                    || Execution->GetStats().PreparationAdmissions != AdmissionsBefore+2 || Execution->GetStats().PreparationDeferrals <= DeferralsBefore
                    || !CaptureAdmissionStats(World,TEXT("terminal"))) return Finish(false,TEXT("Final authority cap1 lifecycle/counter proof incomplete"));
            }
            if (!Write(TEXT("terminal.json"))) return Finish(false,TEXT("Cannot save combined terminal"));
            bTerminal = true; TerminalAt = FPlatformTime::Seconds();
        }
        return true;
    }
    bool TickClient(UWorld* World)
    {
        auto& Row = Owners[Slot-1]; auto& E = Row.Evidence;
        if (!Row.bBound)
        {
            if (!Bind(Row, World->GetFirstPlayerController(), World)) return true;
            if (!Write(FString::Printf(TEXT("owner-%d.json"),Slot), Slot, E.Character)) return Finish(false,TEXT("Cannot save actual client registration"));
        }
        if (!BaseSource(Row)) return Finish(false,TEXT("Default PC source disabled or aliases the additional source"));
        if (bBackpressure ? !Row.bAdmissionReadyPublished : !E.bInteractionRequested)
        {
            if (!Current(Row,World,true)) return Finish(false,TEXT("Owner changed before normal interaction"));
            if (!Read(TEXT("start.json"))) return true;
            AProject_JWorldPortal* Owned = nullptr; int32 Count = 0;
            for (TActorIterator<AProject_JWorldPortal> It(World); It; ++It) if (!It->IsActorBeingDestroyed() && It->DestinationPlaceId == TEXT("S01")) { Owned = *It; ++Count; }
            if (!Count) return true;
            if (Count != 1 || !IsValid(Owned) || Owned->HasAuthority() || !Owned->IsSpatiallyAvailable(E.Pawn.Get())) return Finish(false,TEXT("Actual replicated shared Portal is not a legal interaction"));
            if (!WriteWorldReadinessSnapshot(World,TEXT("world-readiness-origin.json"))) return true;
            E.Portal = Owned;
            if (bBackpressure)
            {
                if (!Restored(Row,World)) return true;
                if (!Write(FString::Printf(TEXT("ready-%d.json"),Slot),Slot,E.Character)) return Finish(false,TEXT("Cannot save actual owner origin readiness"));
                Row.bAdmissionReadyPublished = true;
            }
            else { E.bInteractionRequested = true; E.Pawn->TryInteract(); }
        }
        auto* Source = E.Preparation.Get(); if (!IsValid(Source) || Source->IsBeingDestroyed()) return Finish(false,TEXT("Client source retired"));
        if (!Row.bPendingPublished && Current(Row,World,true))
        {
            E.Sample();
            if (Source->ArrivalPhase == EProject_JWorldArrivalPhase::Preparing && E.bDestinationSource && E.bSourceBeforePhysicalArrival && !E.bPendingSourceMissing)
            {
                if (!Write(FString::Printf(TEXT("pending-%d.json"),Slot),Slot,E.Character)) return Finish(false,TEXT("Cannot save actual pending source observation"));
                Row.bPendingPublished = true;
            }
        }
        if (bBackpressure)
        {
            const auto Selection = Read(TEXT("admitted.json"));
            if (!Selection) return true;
            int32 AdmittedSlot = 0, WaitingSlot = 0; FString AdmittedId, WaitingId; FGuid AdmittedCharacter, WaitingCharacter;
            if (!Selection->TryGetNumberField(TEXT("slot"),AdmittedSlot) || !Selection->TryGetNumberField(TEXT("waiting_slot"),WaitingSlot)
                || AdmittedSlot < 1 || AdmittedSlot > 2 || WaitingSlot != 3-AdmittedSlot
                || !Selection->TryGetStringField(TEXT("character"),AdmittedId) || !Selection->TryGetStringField(TEXT("waiting_character"),WaitingId)
                || !FGuid::Parse(AdmittedId,AdmittedCharacter) || !FGuid::Parse(WaitingId,WaitingCharacter)
                || !AdmittedCharacter.IsValid() || !WaitingCharacter.IsValid() || AdmittedCharacter == WaitingCharacter
                || (Slot == AdmittedSlot ? E.Character != AdmittedCharacter : E.Character != WaitingCharacter)
                || !Matches(Read(FString::Printf(TEXT("owner-%d.json"),AdmittedSlot)),AdmittedSlot,AdmittedCharacter)
                || !Matches(Read(FString::Printf(TEXT("owner-%d.json"),WaitingSlot)),WaitingSlot,WaitingCharacter)
                || (bBackpressureObserved && RetiredIndex != AdmittedSlot-1)) return Finish(false,TEXT("Fresh admitted/waiting owner selection identity mismatch"));
            RetiredIndex = AdmittedSlot-1; bBackpressureObserved = true;
        }
        if (Slot != RetiredIndex+1)
        {
            if (!Current(Row,World,true)) return Finish(false,TEXT("Survivor current owner/ASC changed"));
            E.Sample();
            if (E.bPendingSourceMissing || Source->ArrivalPhase == EProject_JWorldArrivalPhase::Failed || Source->ArrivalPhase == EProject_JWorldArrivalPhase::ExpiredUnconfirmed) return Finish(false,TEXT("Survivor pending source/request failed"));
            if (bBackpressure && Source->IsPreparedArrivalStarted()) bWaitingAdmitted = true;
            if (Source->ArrivalPhase == EProject_JWorldArrivalPhase::Arrived)
            {
                if (!E.bDestinationSource || !E.bSourceBeforePhysicalArrival || !E.PhysicalArrival(E.Pawn.Get())) return Finish(false,TEXT("Survivor lacks actual local arrival proof"));
                if (!E.bArrived) { E.bArrived = true; E.ArrivedAt = FPlatformTime::Seconds(); }
                if (FPlatformTime::Seconds()-E.ArrivedAt >= .3 && Restored(Row,World) && WriteWorldReadinessSnapshot(World)) Row.bLocalProof = true;
            }
        }
        else
        {
            if (Source->ArrivalPhase == EProject_JWorldArrivalPhase::Arrived || (Row.OriginalPawn.IsValid() && E.PhysicalArrival(Row.OriginalPawn.Get()))) return Finish(false,TEXT("Retired request committed on replaced owner"));
            if (Matches(Read(TEXT("retired.json")),RetiredIndex+1,E.Character))
            {
                bRetired = true; if (!bBackpressure) bOverlap = true;
            }
            if (bReplacement && !Current(Row,World,false)) return Finish(false,TEXT("Replacement identity changed again after first resolution"));
            if (bRetired && (bReplacement || NewAvatar(Row,World)))
            {
                bReplacement = true;
                if (Source->ArrivalPhase == EProject_JWorldArrivalPhase::Preparing || Source->ArrivalPhase == EProject_JWorldArrivalPhase::Queued
                    || Source->ArrivalPhase == EProject_JWorldArrivalPhase::AwaitingLocalArrival) return Finish(false,TEXT("Retired request reopened on the client replacement"));
                if (Source->ArrivalPhase == EProject_JWorldArrivalPhase::Failed && Restored(Row,World) && WriteWorldReadinessSnapshot(World))
                    Row.bLocalProof = bRetiredSourceReleased = true;
            }
        }
        if (Row.bLocalProof)
        {
            Row.LastPosition = E.Pawn->GetActorLocation(); Row.LastRequestPhase = int32(Source->ArrivalPhase);
            if (!Row.bProofPublished)
            {
                if (!Write(FString::Printf(TEXT("proof-%d.json"),Slot),Slot,E.Character)) return Finish(false,TEXT("Cannot save actual local proof"));
                Row.bProofPublished = true;
            }
            const bool bTerminalPhase = Source->ArrivalPhase == (Slot != RetiredIndex+1 ? EProject_JWorldArrivalPhase::Arrived : EProject_JWorldArrivalPhase::Failed);
            if (!bTerminalPhase) return Finish(false,TEXT("Resolved local role phase changed before terminal"));
            if (Read(TEXT("terminal.json")) && Restored(Row,World) && WriteWorldReadinessSnapshot(World))
                return Finish(true,TEXT("Actual role proof plus combined authority terminal completed"));
        }
        return true;
    }
};

#endif
