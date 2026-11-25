#include "World/Project_JWorldStreamingComponent.h"
#include "World/Project_JWorldArrivalSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "World/Project_JWorldState.h"
#include "World/Project_JWorldPortal.h"
#include "World/Project_JPreparedArrivalEffect.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JGameplayTags.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "Components/CapsuleComponent.h"
#include "Mount/Project_JFlyingMountCharacter.h"
#include "Mount/Project_JMountMoverComponent.h"
#include "Movement/Project_JMovementQueries.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "MoverSimulationTypes.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "UObject/StrongObjectPtr.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
bool ApplyDetailGridSuppression(FWorldPartitionStreamingSource& Source, FName DetailGrid)
{
    if (DetailGrid.IsNone()) return true;
    if (Source.TargetBehavior == EStreamingSourceTargetBehavior::Include && !Source.TargetGrids.IsEmpty())
    {
        Source.TargetGrids.Remove(DetailGrid);
        // An empty include set would expand to every grid.
        return !Source.TargetGrids.IsEmpty();
    }
    Source.TargetBehavior = EStreamingSourceTargetBehavior::Exclude;
    Source.TargetGrids.Add(DetailGrid);
    return true;
}

bool IsFlyingMount(const APawn& Pawn)
{
    if (const auto* Mount = Cast<AProject_JMountMoverPawn>(&Pawn)) return Mount->IsFlying();
    if (const auto* Mount = Cast<AProject_JFlyingMountCharacter>(&Pawn)) return Mount->IsFlyingMount();
    return false;
}

bool AreFlightRouteCellsReady(const UWorldPartition& Partition, const FWorldPartitionStreamingSource& Source)
{
    const auto* DataLayers = Partition.GetDataLayerManager();
    if (!Partition.RuntimeHash || !DataLayers || Source.Shapes.IsEmpty() || Partition.GetInstanceTransform().ContainsNaN()) return false;
    const FTransform WorldToPartition = Partition.GetInstanceTransform().Inverse();
    if (WorldToPartition.ContainsNaN()) return false;

    FWorldPartitionStreamingQuerySource Query;
    Query.Location = WorldToPartition.TransformPosition(Source.Location);
    Query.Rotation = WorldToPartition.TransformRotation(Source.Rotation.Quaternion()).Rotator();
    Query.TargetBehavior = Source.TargetBehavior;
    Query.TargetGrids = Source.TargetGrids;
    Query.Shapes = Source.Shapes;
    Query.bUseGridLoadingRange = true;
    Query.DataLayers = DataLayers->GetEffectiveActiveDataLayerNames().Array();

    const FWorldPartitionStreamingContext Context = FWorldPartitionStreamingContext::Create(Partition.GetWorld());
    constexpr int32 MaxVisitedCells = 512;
    int32 VisitedCells = 0;
    bool bFoundReadySourceCell = false;
    bool bIncomplete = false;
    Partition.RuntimeHash->ForEachStreamingCellsQuery(Query, [&](const UWorldPartitionRuntimeCell* Cell)
    {
        if (++VisitedCells > MaxVisitedCells)
        {
            bIncomplete = true;
            return false;
        }
        if (!Cell || !Cell->IsSpatiallyLoaded() || Cell->GetIsHLOD() || Cell->GetIsCustomHLODPlaceholderCell()
            || (Cell->HasDataLayers() && Cell->GetCellEffectiveWantedState(Context) != EDataLayerRuntimeState::Activated))
        {
            return true;
        }
        if (Cell->GetCurrentState() != EWorldPartitionRuntimeCellState::Activated)
        {
            bIncomplete = true;
            return false;
        }
        bFoundReadySourceCell = true;
        return true;
    });
    return bFoundReadySourceCell && !bIncomplete;
}
}

UProject_JWorldStreamingComponent::UProject_JWorldStreamingComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    SetIsReplicatedByDefault(true); // PC owns the connection; only acknowledgements are client commands.
    TargetState = EStreamingSourceTargetState::Activated;
    Priority = EStreamingSourcePriority::Low;
}
void UProject_JWorldStreamingComponent::OnRegister()
{
    StreamingSourceIdentity = NAME_None;
    Super::OnRegister();
    auto* World = GetWorld();
    if (World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE))
        if (auto* Execution = World->GetSubsystem<UProject_JWorldArrivalSubsystem>())
            StreamingSourceIdentity = Execution->AllocateStreamingSourceIdentity();
}
FName UProject_JWorldStreamingComponent::GetStreamingSourceIdentity() const
{
    const auto* World = GetWorld();
    if (!World) return NAME_None;
    // Arrival execution deliberately supports Game/PIE only. Preserve the existing
    // editor/preview/RPC-world provider identity without adding a scheduler there.
    return World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE
        ? StreamingSourceIdentity : FName(*GetPathName());
}
void UProject_JWorldStreamingComponent::BeginPlay()
{
    Super::BeginPlay();
    CachedWorldState.Reset(); InitializedDefinition.Reset(); NextDefinitionSearch = 0;
    if (const auto* Definition = ResolveWorldDefinition())
    {
        PredictionSeconds = Definition->PredictionSeconds;
        PreparationRadius = Definition->PreparationRadius;
        MaxPredictionDistance = Definition->MaxPredictionDistance;
        InitializedDefinition = Definition;
    }
}
const UProject_JWorldDefinition* UProject_JWorldStreamingComponent::ResolveWorldDefinition() const
{
    check(IsInGameThread());
    if (const auto* State = CachedWorldState.Get(); State && !State->IsActorBeingDestroyed() && State->GetWorld() == GetWorld()) return State->Definition;
    if (!GetWorld() || FPlatformTime::Seconds() < NextDefinitionSearch) return nullptr;
    NextDefinitionSearch = FPlatformTime::Seconds() + 1.0;
    for (TActorIterator<AProject_JWorldState> It(GetWorld()); It; ++It)
        if (IsValid(*It) && !It->IsActorBeingDestroyed()) { CachedWorldState = *It; return It->Definition; }
    return nullptr;
}
FVector UProject_JWorldStreamingComponent::PredictOffset(const FVector& Velocity, float Seconds, float Maximum)
{
    if (Velocity.ContainsNaN() || !FMath::IsFinite(Seconds) || !FMath::IsFinite(Maximum)) { return FVector::ZeroVector; }
    const FVector Offset = (Velocity*FMath::Clamp(Seconds, 0.f, 10.f)).GetClampedToMaxSize(FMath::Clamp(Maximum, 0.f, 200000.f));
    return Offset.ContainsNaN() ? FVector::ZeroVector : Offset;
}
bool UProject_JWorldStreamingComponent::BuildPreparationShapes(const FVector& Velocity, float Seconds, float Radius,
    float Maximum, TArray<FStreamingSourceShape>& OutShapes)
{
    OutShapes.Reset();
    if (Velocity.ContainsNaN() || !UProject_JWorldDefinition::AreStreamingSettingsValid(Seconds, Radius, Maximum)) return false;
    const FVector Offset = PredictOffset(Velocity, Seconds, Maximum);
    const double Distance = Offset.Size();
    const int32 Segments = Distance > UE_KINDA_SMALL_NUMBER ? FMath::Clamp(FMath::CeilToInt(Distance / Radius), 1,
        UProject_JWorldDefinition::MaxPreparationShapes - 1) : 0;
    OutShapes.Reserve(Segments + 1);
    FStreamingSourceShape Shape;
    Shape.bUseGridLoadingRange = false; Shape.Radius = Radius;
    for (int32 Index = 0; Index <= Segments; ++Index)
    {
        Shape.Location = Segments ? Offset * (double(Index) / Segments) : FVector::ZeroVector;
        OutShapes.Add(Shape);
    }
    return true;
}

bool UProject_JWorldStreamingComponent::BuildTwoStageVelocityShapes(
    const FVector& Velocity,
    bool bFlying,
    float YawRateDegPerSec,
    TOptional<float> AltitudeAboveGroundCm,
    const FProject_JStreamingVelocityProfile& Profile,
    FTwoStageStreamingPlan& OutPlan)
{
    OutPlan = FTwoStageStreamingPlan{};
    if (Velocity.ContainsNaN() || !FMath::IsFinite(YawRateDegPerSec)
        || (AltitudeAboveGroundCm.IsSet() && !FMath::IsFinite(AltitudeAboveGroundCm.GetValue())))
    {
        return false;
    }

    const FVector PathVelocity = bFlying ? Velocity : FVector(Velocity.X, Velocity.Y, 0.0);
    const float Speed = static_cast<float>(PathVelocity.Size());
    if (Speed <= UE_KINDA_SMALL_NUMBER || Speed < Profile.HighSpeedThresholdCmPerSec)
    {
        return true;
    }

    OutPlan.bHighSpeedActive = true;
    OutPlan.bTurnWidened = PathVelocity.SizeSquared2D() > FMath::Square(10.0)
        && FMath::Abs(YawRateDegPerSec) >= Profile.TurnWideningYawRateDegPerSec;

    const float Alpha = FMath::GetRangePct(
        Profile.HighSpeedThresholdCmPerSec,
        FMath::Max(Profile.HighSpeedThresholdCmPerSec + 1.0f, Profile.FlyingSpeedCmPerSec),
        Speed);
    const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);

    const float LoadedLead = FMath::Lerp(Profile.LoadedLeadSecondsMin, Profile.LoadedLeadSecondsMax, ClampedAlpha);
    const float ActivatedLead = FMath::Lerp(Profile.ActivatedLeadSecondsMin, Profile.ActivatedLeadSecondsMax, ClampedAlpha);
    const float LoadedCap = FMath::Lerp(Profile.LoadedMaxDistanceCm * 0.5f, Profile.LoadedMaxDistanceCm, ClampedAlpha);
    const float ActivatedCap = FMath::Lerp(Profile.ActivatedMaxDistanceCm * 0.45f, Profile.ActivatedMaxDistanceCm, ClampedAlpha);
    const FVector LoadedOffset = PredictOffset(PathVelocity, LoadedLead, LoadedCap);
    const FVector ActivatedOffset = PredictOffset(PathVelocity, ActivatedLead, ActivatedCap);
    const double LowestOffset = FMath::Min3(0.0, LoadedOffset.Z, ActivatedOffset.Z);
    OutPlan.bSuppressDetailGrid = bFlying && AltitudeAboveGroundCm.IsSet()
        && AltitudeAboveGroundCm.GetValue() + LowestOffset >= Profile.SuppressDetailAltitudeCm;

    auto AppendCorridor = [](TArray<FStreamingSourceShape>& OutCorridorShapes, const FVector& Offset, float Radius)
    {
        const double Dist = Offset.Size();
        const int32 Segments = Dist > UE_KINDA_SMALL_NUMBER ? FMath::Clamp(FMath::CeilToInt(Dist / FMath::Max(1000.0f, Radius)), 1, 6) : 1;
        FStreamingSourceShape Shape;
        Shape.bUseGridLoadingRange = false;
        Shape.Radius = Radius;
        for (int32 i = 1; i <= Segments; ++i)
        {
            Shape.Location = Offset * (double(i) / Segments);
            OutCorridorShapes.Add(Shape);
        }
    };

    const float ActivatedRadius = Profile.LoadedShapeRadiusCm * 0.75f;
    AppendCorridor(OutPlan.Stage1LoadedShapes, LoadedOffset, Profile.LoadedShapeRadiusCm);
    AppendCorridor(OutPlan.Stage2ActivatedShapes, ActivatedOffset, ActivatedRadius);
    if (OutPlan.bTurnWidened)
    {
        for (const double Yaw : {-45.0, 45.0})
        {
            AppendCorridor(OutPlan.Stage1LoadedShapes, LoadedOffset.RotateAngleAxis(Yaw, FVector::UpVector) * 0.85, Profile.LoadedShapeRadiusCm);
            AppendCorridor(OutPlan.Stage2ActivatedShapes, ActivatedOffset.RotateAngleAxis(Yaw, FVector::UpVector) * 0.85, ActivatedRadius);
        }
    }
    return true;
}

bool UProject_JWorldStreamingComponent::ShouldHoldHysteresisAnchor(
    const FVector& CurrentLocation,
    const FVector& AnchorLocation,
    double ElapsedSeconds,
    const FProject_JStreamingVelocityProfile& Profile,
    bool bFlying)
{
    if (CurrentLocation.ContainsNaN() || AnchorLocation.ContainsNaN() || ElapsedSeconds < 0.0)
    {
        return false;
    }
    if (ElapsedSeconds <= static_cast<double>(Profile.UnloadHysteresisSeconds))
    {
        return true;
    }
    const double DistanceSquared = bFlying ? FVector::DistSquared(CurrentLocation, AnchorLocation)
        : FVector::DistSquared2D(CurrentLocation, AnchorLocation);
    return DistanceSquared <= FMath::Square(static_cast<double>(Profile.TrailingBufferCm));
}

float UProject_JWorldStreamingComponent::ComputeSafeStreamingSpeedScale(
    float CurrentSpeedCmPerSec,
    bool bForwardCellsReady,
    const FProject_JStreamingVelocityProfile& Profile)
{
    if (bForwardCellsReady || !FMath::IsFinite(CurrentSpeedCmPerSec) || CurrentSpeedCmPerSec < Profile.HighSpeedThresholdCmPerSec)
    {
        return 1.0f;
    }
    const float Alpha = FMath::Clamp(
        FMath::GetRangePct(Profile.HighSpeedThresholdCmPerSec, FMath::Max(Profile.HighSpeedThresholdCmPerSec + 1.0f, Profile.FlyingSpeedCmPerSec), CurrentSpeedCmPerSec),
        0.0f,
        1.0f);
    return FMath::Lerp(1.0f, FMath::Clamp(Profile.UnreadyForwardSpeedClampMin, 0.1f, 1.0f), Alpha);
}
bool UProject_JWorldStreamingComponent::IsPreparedArrivalStarted() const
{
    if (!ArrivalRequest || !bArrivalPreparationStarted) return false;
    const auto* Controller = Cast<APlayerController>(GetOwner());
    if (!IsValid(Controller)) return false;
    // Client preparation is already authorized by ClientPrepareArrival. Preserve
    // its definition-independent hint and post-authority local-arrival lifetime.
    if (!Controller->HasAuthority()) return Controller->IsLocalController();
    const auto* Execution = ArrivalExecutionOwner.Get();
    return Execution && Execution->HasPreparationPermit(ArrivalExecutionToken, this, ArrivalRequest);
}
FName UProject_JWorldStreamingComponent::GetDetailGridName() const
{
    static const FName DefaultDetailGrid = FProject_JFoliageHLODPolicy{}.DetailGridName;
    return HighSpeedPolicy ? HighSpeedPolicy->FoliagePolicy.DetailGridName : DefaultDetailGrid;
}

const FProject_JStreamingVelocityProfile& UProject_JWorldStreamingComponent::GetVelocityProfile() const
{
    return HighSpeedPolicy ? HighSpeedPolicy->VelocityProfile : VelocityProfile;
}

float UProject_JWorldStreamingComponent::GetLocalFlightStreamingSpeedScale(const APawn& Pawn) const
{
    check(IsInGameThread());
    if (!bEnableHighSpeedTwoStage || !bLimitFlightSpeedForLocalStreaming || !IsRegistered() || !IsStreamingSourceEnabled()) return 1.f;
    const auto* Mount = Cast<AProject_JMountMoverPawn>(&Pawn);
    const auto* Mover = Mount ? Mount->GetMountMover() : nullptr;
    const auto* World = GetWorld();
    const auto* Partition = World ? World->GetWorldPartition() : nullptr;
    const auto* Streaming = World ? World->GetSubsystem<UWorldPartitionSubsystem>() : nullptr;
    if (!Mover || !Mount->IsFlying() || !World || World->bIsTearingDown || !Partition || !Partition->IsInitialized() || !Partition->IsStreamingEnabled()
        || !Partition->CanStream() || !Streaming || !Streaming->IsStreamingSourceProviderRegistered(const_cast<ThisClass*>(this))
        || !Pawn.IsLocallyControlled() || !ResolveWorldDefinition()) return 1.f;

    const double Now = World->GetTimeSeconds();
    if (FlightReadinessPawn.Get() == &Pawn && Now < NextFlightReadinessSample) return FlightReadinessScale;
    FlightReadinessPawn = &Pawn;
    NextFlightReadinessSample = Now + 0.25;
    FlightReadinessScale = 1.f;

    const auto& Profile = GetVelocityProfile();
    const auto* Flight = Mover->GetSyncState().SyncStateCollection.FindDataByType<FProject_JMountMoverSyncState>();
    const FVector Position = Pawn.GetActorLocation();
    if (!Flight || Position.ContainsNaN()) return 1.f;

    FTwoStageStreamingPlan Plan;
    FWorldPartitionStreamingSource ForwardSource;
    if (!BuildPawnVelocityPlan(Pawn, Profile, Plan) || !Plan.bHighSpeedActive || !Super::GetStreamingSource(ForwardSource)
        || ForwardSource.TargetState != EStreamingSourceTargetState::Activated) return 1.f;
    ForwardSource.Location = Position;
    ForwardSource.Rotation = FRotator::ZeroRotator;
    ForwardSource.Shapes = MoveTemp(Plan.Stage2ActivatedShapes);
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_WorldFlightReadiness);
    const bool bHasTargetGrid = !Plan.bSuppressDetailGrid || ApplyDetailGridSuppression(ForwardSource, GetDetailGridName());
    const bool bReady = bHasTargetGrid && AreFlightRouteCellsReady(*Partition, ForwardSource);
    FlightReadinessScale = ComputeSafeStreamingSpeedScale(Flight->AirSpeed, bReady, Profile);
    return FlightReadinessScale;
}

bool UProject_JWorldStreamingComponent::BuildPawnVelocityPlan(
    const APawn& Pawn, const FProject_JStreamingVelocityProfile& Profile, FTwoStageStreamingPlan& OutPlan) const
{
    const bool bSamePawn = ObservedPawn.Get() == &Pawn;
    const bool bFlying = IsFlyingMount(Pawn);
    if (!bSamePawn || bObservedFlying != bFlying || VelocitySampleFrame != GFrameCounter)
    {
        if (!bSamePawn || bObservedFlying != bFlying) HysteresisAnchorTime = -1000.0;
        const double Now = Pawn.GetWorld()->GetTimeSeconds();
        FVector Velocity = Pawn.GetVelocity();
        if (bFlying && bLimitFlightSpeedForLocalStreaming && !Velocity.ContainsNaN() && !Velocity.IsNearlyZero())
        {
            // Keep both providers and the readiness query pointed at the same nominal route while speed is limited.
            const auto* Mount = Cast<AProject_JMountMoverPawn>(&Pawn);
            const auto* Mover = Mount ? Mount->GetMountMover() : nullptr;
            const auto* Flight = Mover ? Mover->GetSyncState().SyncStateCollection.FindDataByType<FProject_JMountMoverSyncState>() : nullptr;
            if (Flight && FMath::IsFinite(Flight->AirSpeed) && Flight->AirSpeed > Velocity.Size())
                Velocity = Velocity.GetSafeNormal() * Flight->AirSpeed;
        }
        const double SampleInterval = Now - LastObservedVelocityTime;
        ObservedYawRate = 0.0f;
        if (bSamePawn && bObservedFlying == bFlying && SampleInterval > 0.0 && SampleInterval < 0.5
            && !LastObservedVelocity.ContainsNaN() && !Velocity.ContainsNaN()
            && LastObservedVelocity.SizeSquared2D() > FMath::Square(10.0) && Velocity.SizeSquared2D() > FMath::Square(10.0))
        {
            ObservedYawRate = FRotator::NormalizeAxis(Velocity.Rotation().Yaw - LastObservedVelocity.Rotation().Yaw)
                / static_cast<float>(FMath::Max(0.01, SampleInterval));
        }
        ObservedPawn = &Pawn;
        VelocitySampleFrame = GFrameCounter;
        LastObservedVelocityTime = Now;
        LastObservedVelocity = Velocity;
        bObservedFlying = bFlying;
        ObservedFlightAltitude.Reset();
        const FVector Position = Pawn.GetActorLocation();
        if (bFlying && !Position.ContainsNaN() && !Velocity.ContainsNaN() && Velocity.Size() >= Profile.HighSpeedThresholdCmPerSec)
        {
            FCollisionQueryParams Query(SCENE_QUERY_STAT(ProjectJ_StreamingFlightAltitude), false, &Pawn);
            FHitResult Ground;
            constexpr double MaxGroundProbeCm = 100000.0;
            if (Pawn.GetWorld()->LineTraceSingleByObjectType(Ground, Position, Position - FVector(0.0, 0.0, MaxGroundProbeCm),
                FCollisionObjectQueryParams(ECC_WorldStatic), Query) && Ground.IsValidBlockingHit())
            {
                ObservedFlightAltitude = static_cast<float>(Ground.Distance);
            }
        }
    }
    return BuildTwoStageVelocityShapes(LastObservedVelocity, bObservedFlying, ObservedYawRate, ObservedFlightAltitude, Profile, OutPlan);
}

bool UProject_JWorldStreamingComponent::GetStreamingSource(FWorldPartitionStreamingSource& Out) const
{
    check(IsInGameThread());
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_WorldPreparationSource);
    const APlayerController* PC = Cast<APlayerController>(GetOwner());
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (!IsRegistered() || IsBeingDestroyed() || !PC || PC->IsActorBeingDestroyed() || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed() ||
        !GetWorld() || GetWorld()->bIsTearingDown || !IsStreamingSourceEnabled()) return false;
    // The central budget owns terminal transitions, not how long a retired
    // incarnation may advertise demand while waiting for its next poll.
    // Expired uncommitted preparation cannot progress on either endpoint. Withdraw
    // only this demand; queued outcomes and client completion may still arrive late.
    const bool bCurrentArrival = IsPreparedArrivalStarted()
        && (ArrivalPhase != EProject_JWorldArrivalPhase::Preparing || FPlatformTime::Seconds() < ArrivalDeadline)
        && IsArrivalCurrent(false);
    if ((!PC->IsLocalController() && !(PC->HasAuthority() && bCurrentArrival)) ||
        (GetNetMode() == NM_DedicatedServer && !bCurrentArrival)) return false;
    const FName SourceIdentity = GetStreamingSourceIdentity();
    if (SourceIdentity.IsNone()) return false;
    if (bCurrentArrival)
    {
        if (ArrivalPawn.Get() != Pawn || ArrivalPlayerState.Get() != PC->PlayerState || ArrivalHint.ContainsNaN() ||
            !FMath::IsFinite(ArrivalRadius) || ArrivalRadius < 1000 || ArrivalRadius > 200000) return false;
        if (!Super::GetStreamingSource(Out)) return false;
        Out.Name = SourceIdentity; Out.Location = ArrivalHint; Out.Rotation = FRotator::ZeroRotator;
        Out.TargetState = EStreamingSourceTargetState::Activated;
        Out.Shapes.Reset();
        FStreamingSourceShape Shape; Shape.bUseGridLoadingRange = false; Shape.Radius = ArrivalRadius; Out.Shapes.Add(Shape);
        return true;
    }
    const auto* Definition = ResolveWorldDefinition();
    if (!Definition) return false;
    // Preserve the existing post-BeginPlay component tuning when it initialized
    // normally. Late world definitions supply their values without mutating a UObject in this const query.
    const bool bUseComponentTuning = InitializedDefinition.Get() == Definition;
    const float Seconds = bUseComponentTuning ? PredictionSeconds : Definition->PredictionSeconds;
    const float Radius = bUseComponentTuning ? PreparationRadius : Definition->PreparationRadius;
    const float Maximum = bUseComponentTuning ? MaxPredictionDistance : Definition->MaxPredictionDistance;
    const FVector Position = Pawn->GetActorLocation();
    if (Position.ContainsNaN()) return false;
    if (!Super::GetStreamingSource(Out)) { return false; }
    // WP velocity/history lookup is keyed by source name; the controller
    // default and other components must not share this provider identity.
    Out.Name = SourceIdentity;
    Out.Location = Position;
    Out.Rotation = FRotator::ZeroRotator;
    const FVector Vel = Pawn->GetVelocity();
    const auto& Profile = GetVelocityProfile();
    if (bEnableHighSpeedTwoStage)
    {
        const double Now = GetWorld()->GetTimeSeconds();
        FTwoStageStreamingPlan Plan;
        if (BuildPawnVelocityPlan(*Pawn, Profile, Plan) && Plan.bHighSpeedActive)
        {
            Out.Shapes = MoveTemp(Plan.Stage2ActivatedShapes);
            if (Profile.TrailingBufferCm > 0.0f)
            {
                FStreamingSourceShape Trail;
                Trail.bUseGridLoadingRange = false;
                Trail.Radius = Profile.TrailingBufferCm;
                const FVector Forward = bObservedFlying ? LastObservedVelocity.GetSafeNormal() : LastObservedVelocity.GetSafeNormal2D();
                Trail.Location = -Forward * (Profile.TrailingBufferCm * 0.5);
                Out.Shapes.Add(Trail);
            }
            if (Profile.TrailingBufferCm > 0.0f && HysteresisAnchorTime >= 0.0
                && ShouldHoldHysteresisAnchor(Position, HysteresisAnchorLocation, Now - HysteresisAnchorTime, Profile, bObservedFlying))
            {
                FStreamingSourceShape HoldShape;
                HoldShape.bUseGridLoadingRange = false;
                HoldShape.Radius = Profile.TrailingBufferCm;
                HoldShape.Location = HysteresisAnchorLocation - Position;
                Out.Shapes.Add(HoldShape);
            }
            else
            {
                HysteresisAnchorLocation = Position;
                HysteresisAnchorTime = Now;
            }
            return !Plan.bSuppressDetailGrid || ApplyDetailGridSuppression(Out, GetDetailGridName());
        }
    }
    HysteresisAnchorTime = -1.0;
    return BuildPreparationShapes(Vel, Seconds, Radius, Maximum, Out.Shapes);
}

UProject_JWorldPreloadStreamingComponent::UProject_JWorldPreloadStreamingComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    TargetState = EStreamingSourceTargetState::Loaded;
    Priority = EStreamingSourcePriority::Lowest;
}

void UProject_JWorldPreloadStreamingComponent::OnRegister()
{
    StreamingSourceIdentity = NAME_None;
    Super::OnRegister();
    if (auto* World = GetWorld())
    {
        if (auto* Execution = World->GetSubsystem<UProject_JWorldArrivalSubsystem>())
            StreamingSourceIdentity = Execution->AllocateStreamingSourceIdentity();
        else if (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE)
            StreamingSourceIdentity = FName(*GetPathName());
    }
}

bool UProject_JWorldPreloadStreamingComponent::GetStreamingSource(FWorldPartitionStreamingSource& Out) const
{
    check(IsInGameThread());
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_WorldPreloadSource);
    const APlayerController* PC = Cast<APlayerController>(GetOwner());
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (!IsRegistered() || StreamingSourceIdentity.IsNone() || IsBeingDestroyed() || !PC || !PC->IsLocalController() || !IsValid(Pawn)
        || !GetWorld() || GetWorld()->bIsTearingDown || GetNetMode() == NM_DedicatedServer || !IsStreamingSourceEnabled())
    {
        return false;
    }
    const auto* Companion = PC->FindComponentByClass<UProject_JWorldStreamingComponent>();
    const auto& Profile = Companion ? Companion->GetVelocityProfile() : VelocityProfile;
    UProject_JWorldStreamingComponent::FTwoStageStreamingPlan Plan;
    if ((Companion && !Companion->bEnableHighSpeedTwoStage)
        || !(Companion ? Companion->BuildPawnVelocityPlan(*Pawn, Profile, Plan)
            : UProject_JWorldStreamingComponent::BuildTwoStageVelocityShapes(Pawn->GetVelocity(), IsFlyingMount(*Pawn), 0.0f, {}, Profile, Plan))
        || !Plan.bHighSpeedActive || Plan.Stage1LoadedShapes.IsEmpty())
    {
        return false;
    }
    if (!Super::GetStreamingSource(Out))
    {
        return false;
    }
    Out.Name = StreamingSourceIdentity;
    Out.Location = Pawn->GetActorLocation();
    if (Out.Location.ContainsNaN()) return false;
    Out.Rotation = FRotator::ZeroRotator;
    Out.TargetState = EStreamingSourceTargetState::Loaded;
    Out.Shapes = MoveTemp(Plan.Stage1LoadedShapes);
    return !Companion || !Plan.bSuppressDetailGrid || ApplyDetailGridSuppression(Out, Companion->GetDetailGridName());
}


namespace
{
bool EligiblePortalAvatar(const APlayerController* Controller, const APawn* Pawn)
{
    const auto* Player = Controller ? Cast<AProject_JPlayerState>(Controller->PlayerState) : nullptr;
    const auto* Character = Cast<AProject_JPlayerCharacter>(Pawn);
    const auto* ASC = Player ? Player->GetProjectJAbilitySystemComponent() : nullptr;
    const auto* Attributes = Player ? Player->GetProjectJAttributeSet() : nullptr;
    const auto* Mover = Character ? Character->GetMover() : nullptr;
    if (!IsValid(Player) || Player->IsActorBeingDestroyed() || Player->GetPawn() != Pawn || !ASC || ASC->IsBeingDestroyed()
        || ASC->IsActorInfoChanging() || !ASC->GetActorInfoRevision()
        || ASC->GetAvatarActor() != Pawn || !Attributes || !FMath::IsFinite(Attributes->GetHealth()) || Attributes->GetHealth() <= 0
        || !Mover || Mover->IsBeingDestroyed() || Mover->IsBackendAsync()) return false;
    const auto& Tags = FProject_JGameplayTags::Get();
    if (ASC->HasMatchingGameplayTag(Tags.State_Mounted) || ASC->HasMatchingGameplayTag(Tags.State_Dead)
        || ASC->HasMatchingGameplayTag(Tags.State_Attacking) || ASC->HasMatchingGameplayTag(Tags.State_Dodging)
        || ASC->HasMatchingGameplayTag(Tags.State_HitReacting)) return false;
    const auto Frame = Project_J::Movement::ReadPhysicalFrame(Pawn);
    // This first consumer is stationary, grounded and on foot. Teleport retains velocity.
    return Frame.bValid && Frame.bMover && Frame.bGrounded && !Frame.bHasRootMotion
        && !Frame.Velocity.ContainsNaN() && Frame.Velocity.IsNearlyZero(1.0);
}
const FProject_JWorldPlace* UniquePlace(const UProject_JWorldDefinition* Definition, FName Id)
{
    const FProject_JWorldPlace* Result = nullptr;
    if (!Definition || Id.IsNone()) return nullptr;
    for (const auto& Place : Definition->Places)
        if (Place.PlaceId == Id) { if (Result) return nullptr; Result = &Place; }
    return Result && !Result->Location.ContainsNaN() && FMath::IsFinite(Result->Radius) && Result->Radius > 0 ? Result : nullptr;
}
}

bool UProject_JWorldStreamingComponent::RequestPreparedArrival(AProject_JWorldPortal* Portal, APawn* Interactor)
{
    check(IsInGameThread());
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<AProject_JWorldPortal> KeepPortal(Portal);
    const TStrongObjectPtr<APawn> KeepPawn(Interactor);
    auto* PC = Cast<APlayerController>(GetOwner());
    if (!IsRegistered() || IsBeingDestroyed() || StreamingSourceIdentity.IsNone() || ArrivalRequest || !PC || !PC->HasAuthority() || PC->IsActorBeingDestroyed()
        || !GetWorld() || GetWorld()->bIsTearingDown || !IsStreamingSourceEnabled() || !IsValid(Interactor)
        || Interactor->GetController() != PC || PC->GetPawn() != Interactor || !IsValid(Portal)
        || Portal->GetWorld() != GetWorld() || !Portal->IsSpatiallyAvailable(Interactor) || !EligiblePortalAvatar(PC, Interactor)) return false;
    const auto* Definition = ResolveWorldDefinition();
    const auto* Place = UniquePlace(Definition, Portal->DestinationPlaceId);
    const auto* Partition = GetWorld()->GetWorldPartition();
    const auto* Subsystem = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
    if (!Place || !Partition || !Partition->IsInitialized() || !Partition->IsStreamingEnabled() || !Partition->CanStream()
        || !Subsystem || !Subsystem->IsStreamingSourceProviderRegistered(this)
        || !UProject_JWorldDefinition::AreStreamingSettingsValid(Definition->PredictionSeconds, Definition->PreparationRadius, Definition->MaxPredictionDistance)
        || !Definition->IsPreparedArrivalAdmissionValid() || NextArrivalRequest == MAX_uint64) return false;
    ArrivalRequest = ++NextArrivalRequest; ArrivalPortal = Portal; ArrivalPawn = Interactor; ArrivalPlayerState = PC->PlayerState;
    ArrivalAbilitySystem = CastChecked<AProject_JPlayerState>(PC->PlayerState)->GetProjectJAbilitySystemComponent();
    ArrivalActorInfoRevision = ArrivalAbilitySystem->GetActorInfoRevision();
    ArrivalDefinition = Definition; ArrivalAccountId = CastChecked<AProject_JPlayerState>(PC->PlayerState)->GetAccountId().Value;
    ArrivalCharacterId = CastChecked<AProject_JPlayerState>(PC->PlayerState)->GetCharacterId().Value; ArrivalPlaceId = Place->PlaceId; ArrivalHint = Place->Location; ArrivalRadius = Definition->PreparationRadius;
    ArrivalDeadline = FPlatformTime::Seconds() + ArrivalTimeoutSeconds; ArrivalPhase = EProject_JWorldArrivalPhase::Preparing;
    bArrivalPreparationStarted = false;
    bClientArrivalReady = bArrivalOutcomeReceived = bArrivalOutcomeSucceeded = false;
    if (!RegisterArrivalExecution()) { FinishArrival(EProject_JWorldArrivalPhase::Failed, false); return false; }
    // Preserve immediate local starts when capacity is available. A full budget
    // leaves this same Preparing entry/deadline for the existing round-robin writer.
    if (PC->IsLocalController()) TryStartArrivalPreparation();
    return true;
}
bool UProject_JWorldStreamingComponent::IsArrivalCurrent(bool bRequireOrigin) const
{
    const auto* PC = Cast<APlayerController>(GetOwner());
    const auto* Pawn = ArrivalPawn.Get();
    if (!ArrivalRequest || !IsRegistered() || IsBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown || !IsStreamingSourceEnabled()
        || !IsValid(PC) || PC->IsActorBeingDestroyed() || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed()
        || PC->GetPawn() != Pawn || Pawn->GetController() != PC || PC->PlayerState != ArrivalPlayerState.Get()
        || !ArrivalPlayerState.IsValid() || Pawn->GetWorld() != GetWorld()) return false;
    if (!PC->HasAuthority()) return PC->IsLocalController();
    const auto* Player = Cast<AProject_JPlayerState>(ArrivalPlayerState.Get());
    const auto* ASC = Player ? Player->GetProjectJAbilitySystemComponent() : nullptr;
    if (!Player || Player->IsActorBeingDestroyed() || Player->GetOwner() != PC || Player->GetPawn() != Pawn
        || Player->GetAccountId().Value != ArrivalAccountId || Player->GetCharacterId().Value != ArrivalCharacterId
        || !ASC || ASC->IsBeingDestroyed() || ASC != ArrivalAbilitySystem.Get() || ASC->IsActorInfoChanging()
        || !ArrivalActorInfoRevision || ASC->GetActorInfoRevision() != ArrivalActorInfoRevision || ASC->GetAvatarActor() != Pawn) return false;
    if (!bRequireOrigin) return true; // The queued effect owns the movement commit; the old portal can unload afterwards.
    const auto* Portal = ArrivalPortal.Get();
    const auto* Definition = ArrivalDefinition.Get();
    const auto* Place = UniquePlace(Definition, ArrivalPlaceId);
    return IsValid(Portal) && !Portal->IsActorBeingDestroyed() && Portal->DestinationPlaceId == ArrivalPlaceId
        && Portal->IsSpatiallyAvailable(Pawn) && ResolveWorldDefinition() == Definition && Place
        && Place->Location.Equals(ArrivalHint, .01) && Definition->PreparationRadius == ArrivalRadius && EligiblePortalAvatar(PC, Pawn);
}
bool UProject_JWorldStreamingComponent::IsArrivalRegionReady() const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_WorldArrivalReadiness);
    FWorldPartitionStreamingSource Source;
    auto* World = GetWorld();
    auto* Partition = World ? World->GetWorldPartition() : nullptr;
    auto* Subsystem = World ? World->GetSubsystem<UWorldPartitionSubsystem>() : nullptr;
    if (!IsArrivalCurrent(false) || !Partition || !Partition->IsInitialized() || !Partition->IsStreamingEnabled() || !Partition->CanStream()
        || !Partition->RuntimeHash || !Partition->GetDataLayerManager() || !Subsystem || !Subsystem->IsStreamingSourceProviderRegistered(const_cast<ThisClass*>(this))
        || !GetStreamingSource(Source) || Source.Location.ContainsNaN() || !Source.Location.Equals(ArrivalHint, .01)
        || Source.TargetState != EStreamingSourceTargetState::Activated || Source.Shapes.Num() != 1
        || Source.Shapes[0].Radius != ArrivalRadius || Source.Shapes[0].bUseGridLoadingRange
        || !Source.Shapes[0].Location.IsNearlyZero(.01)) return false;
    // A deadline may cross during this query. A local provider then resumes its
    // ordinary predictor; never treat that different geometry as the destination.
    if (Partition->GetInstanceTransform().ContainsNaN()) return false;
    const FTransform WorldToPartition = Partition->GetInstanceTransform().Inverse();
    if (WorldToPartition.ContainsNaN()) return false;
    // Server streaming-off activates all server cells; a local consumed-source is not its authority.
    // Client/standalone and server-streaming-on must actually consume this exact destination generation geometry.
    const bool bAllServerCells = Partition->IsServer() && !Partition->IsServerStreamingEnabled();
    if (!bAllServerCells)
    {
        const bool bConsumed = Partition->GetStreamingSources().ContainsByPredicate([&](const FWorldPartitionStreamingSource& Candidate)
        {
            return Candidate.Name == Source.Name && Candidate.Location.Equals(WorldToPartition.TransformPosition(Source.Location), .01)
                && Candidate.TargetState == EStreamingSourceTargetState::Activated && Candidate.Shapes.Num() == 1
                && Candidate.Shapes[0].Radius == ArrivalRadius && !Candidate.Shapes[0].bUseGridLoadingRange;
        });
        if (!bConsumed) return false;
    }
    if (!Subsystem->IsStreamingCompleted(this)) return false;
    FWorldPartitionStreamingQuerySource Query;
    Query.Location = WorldToPartition.TransformPosition(Source.Location);
    Query.Rotation = WorldToPartition.TransformRotation(Source.Rotation.Quaternion()).Rotator();
    Query.TargetBehavior = Source.TargetBehavior; Query.TargetGrids = Source.TargetGrids; Query.Shapes = Source.Shapes;
    Query.bUseGridLoadingRange = true; Query.DataLayers = Partition->GetDataLayerManager()->GetEffectiveActiveDataLayerNames().Array();
    int32 Visits = 0, SpatialCells = 0; bool bOverflow = false, bBoundsValid = true;
    Partition->RuntimeHash->ForEachStreamingCellsQuery(Query, [&](const UWorldPartitionRuntimeCell* Cell)
    {
        if (bOverflow) return false;
        ++Visits;
        if (Cell && Cell->IsSpatiallyLoaded() && !(Partition->IsServer() && Cell->GetClientOnlyVisible()))
        {
            ++SpatialCells; const FBox Bounds = Cell->GetStreamingBounds();
            bBoundsValid &= Bounds.IsValid && !Bounds.Min.ContainsNaN() && !Bounds.Max.ContainsNaN();
        }
        if (Visits >= MaxArrivalCells) { bOverflow = true; return false; }
        return true;
    });
    // Completion/state/data-layer handling stays engine-owned. This bounded check prevents vacuous success.
    return !bOverflow && bBoundsValid && SpatialCells > 0;
}
bool UProject_JWorldStreamingComponent::FindArrivalPlacement(APawn* Interactor, const FVector& Hint, FVector& Placement) const
{
    auto* Capsule = Project_J::Movement::FindCapsule(Interactor);
    float Radius = 0, HalfHeight = 0;
    if (Capsule) Capsule->GetScaledCapsuleSize(Radius, HalfHeight);
    if (!GetWorld() || !IsValid(Interactor) || Interactor->IsActorBeingDestroyed() || Hint.ContainsNaN() || !IsValid(Capsule)
        || Capsule->IsBeingDestroyed() || !Capsule->IsRegistered() || !Capsule->IsQueryCollisionEnabled()
        || !FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight) || Radius <= 0 || HalfHeight < Radius
        || Capsule->GetComponentQuat().ContainsNaN() || !Capsule->GetComponentQuat().IsNormalized()) return false;
    const FVector Up = Capsule->GetUpVector();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ProjectJPortalPlacement), false, Interactor);
    const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
    auto GroundResponses = Responses; GroundResponses.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
    const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
    const FQuat CapsuleQuat = Capsule->GetComponentQuat();
    const ECollisionChannel ObjectType = Capsule->GetCollisionObjectType();

    auto TryProbe = [&](const FVector& ProbeHint, FVector& OutCandidate) -> bool
    {
        FHitResult Ground;
        if (!GetWorld()->LineTraceSingleByChannel(Ground, ProbeHint + Up * 5000, ProbeHint - Up * 15000, ObjectType, Params, GroundResponses)
            || !Ground.bBlockingHit || Ground.bStartPenetrating || Ground.ImpactPoint.ContainsNaN() || Ground.ImpactNormal.ContainsNaN()
            || !Project_J::Movement::IsWalkable(Interactor, Ground)) return false;
        const FVector Candidate = Ground.ImpactPoint + Up * (HalfHeight + 2);
        if (GetWorld()->OverlapBlockingTestByChannel(Candidate, CapsuleQuat, ObjectType, CapsuleShape, Params, Responses)) return false;
        OutCandidate = Candidate;
        return true;
    };

    // Preserve exact authored arrival point when clear; fall back to concentric rings when occupied (e.g. another player).
    if (TryProbe(Hint, Placement)) return true;
    const float RingRadii[] = { Radius * 2.5f, Radius * 5.0f };
    for (const float RingRadius : RingRadii)
    {
        for (int32 Slot = 0; Slot < 8; ++Slot)
        {
            const double Angle = (2.0 * UE_DOUBLE_PI * Slot) / 8.0;
            const FVector Offset(FMath::Cos(Angle) * RingRadius, FMath::Sin(Angle) * RingRadius, 0.0);
            if (TryProbe(Hint + Offset, Placement)) return true;
        }
    }
    return false;
}
bool UProject_JWorldStreamingComponent::IsArrivalPlacementValid(APawn* Interactor, const FVector& Origin, const FQuat& Rotation) const
{
    auto* Capsule = Project_J::Movement::FindCapsule(Interactor);
    float Radius = 0, HalfHeight = 0; if (Capsule) Capsule->GetScaledCapsuleSize(Radius, HalfHeight);
    if (!GetWorld() || !IsValid(Interactor) || Interactor->IsActorBeingDestroyed() || Interactor->GetWorld() != GetWorld()
        || !IsValid(Capsule) || Capsule->IsBeingDestroyed() || !Capsule->IsRegistered() || !Capsule->IsQueryCollisionEnabled()
        || Rotation.ContainsNaN() || !Rotation.IsNormalized()
        || !FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight) || Radius <= 0 || HalfHeight < Radius || Origin.ContainsNaN()) return false;
    // Same no-scale unit-axis operation as Capsule->GetUpVector(), also valid
    // for the native teleport's explicitly requested final rotation.
    const FVector Up = FTransform(Rotation).GetUnitAxis(EAxis::Z);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ProjectJPortalArrival), false, Interactor);
    const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
    auto GroundResponses = Responses; GroundResponses.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
    FHitResult Ground;
    return GetWorld()->LineTraceSingleByChannel(Ground, Origin, Origin-Up*(HalfHeight+Radius), Capsule->GetCollisionObjectType(), Params, GroundResponses)
        && Ground.bBlockingHit && !Ground.bStartPenetrating && !Ground.ImpactPoint.ContainsNaN() && !Ground.ImpactNormal.ContainsNaN()
        && Project_J::Movement::IsWalkable(Interactor, Ground)
        && !GetWorld()->OverlapBlockingTestByChannel(Origin, Rotation, Capsule->GetCollisionObjectType(),
            FCollisionShape::MakeCapsule(Radius, HalfHeight), Params, Responses);
}
bool UProject_JWorldStreamingComponent::IsArrivalPlacementCurrent(APawn* Interactor, const FVector& Expected) const
{
    auto* Capsule = Project_J::Movement::FindCapsule(Interactor);
    float Radius = 0, HalfHeight = 0; if (Capsule) Capsule->GetScaledCapsuleSize(Radius, HalfHeight);
    if (!IsValid(Capsule) || !FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight) || Radius <= 0 || HalfHeight < Radius
        || Expected.ContainsNaN() || Capsule->GetComponentLocation().ContainsNaN()
        || FVector::DistSquared(Capsule->GetComponentLocation(), Expected) > FMath::Square(Radius)) return false;
    // Completion still requires the real capsule to be near the recorded target;
    // only the fixed-origin ground/overlap predicate is shared with native commit.
    return IsArrivalPlacementValid(Interactor, Capsule->GetComponentLocation(), Capsule->GetComponentQuat());
}
bool UProject_JWorldStreamingComponent::RegisterArrivalExecution()
{
    check(IsInGameThread());
    if (ArrivalExecutionToken || ArrivalExecutionOwner.IsValid()) return false;
    auto* Execution = GetWorld() ? GetWorld()->GetSubsystem<UProject_JWorldArrivalSubsystem>() : nullptr;
    if (!Execution) return false;
    const uint64 Token = Execution->Register(this, ArrivalRequest);
    if (!Token) return false;
    ArrivalExecutionOwner = Execution; ArrivalExecutionToken = Token;
    return true;
}
bool UProject_JWorldStreamingComponent::TryStartArrivalPreparation()
{
    check(IsInGameThread());
    if (IsPreparedArrivalStarted()) return true;
    auto* Controller = Cast<APlayerController>(GetOwner());
    const auto* Definition = ArrivalDefinition.Get();
    auto* Execution = ArrivalExecutionOwner.Get();
    if (!ArrivalRequest || bArrivalPreparationStarted || ArrivalPhase != EProject_JWorldArrivalPhase::Preparing
        || !IsValid(Controller) || !Controller->HasAuthority() || !Definition || !Definition->IsPreparedArrivalAdmissionValid()
        || !Execution || !Execution->TryAdmitPreparation(ArrivalExecutionToken, this, ArrivalRequest, Definition->MaxSimultaneousPreparedArrivals)) return false;
    // Commit permission before a potentially reentrant RPC. Its caller must not
    // retain the old request state afterward. Admission is not WP/Mover readiness.
    bArrivalPreparationStarted = true;
    if (!Controller->IsLocalController()) ClientPrepareArrival(ArrivalRequest, ArrivalHint, ArrivalRadius);
    return true;
}
void UProject_JWorldStreamingComponent::AdvancePreparedArrival(double Now)
{
    check(IsInGameThread());
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    const TStrongObjectPtr<AActor> KeepOwner(GetOwner());
    auto* PC = Cast<APlayerController>(GetOwner());
    if (!ArrivalRequest || !PC) { FinishArrival(EProject_JWorldArrivalPhase::Failed, false); return; }
    if (!FMath::IsFinite(Now) || !IsArrivalCurrent(PC->HasAuthority() && ArrivalPhase == EProject_JWorldArrivalPhase::Preparing))
    { FinishArrival(ArrivalPhase == EProject_JWorldArrivalPhase::Queued ? EProject_JWorldArrivalPhase::ExpiredUnconfirmed : EProject_JWorldArrivalPhase::Failed, true); return; }
    // The native outcome may arrive just before the deadline and be observed by this10Hz tick afterwards.
    // Current binding and actual arrival are still required; a deadline cannot erase known evidence.
    if (PC->HasAuthority() && ArrivalPhase == EProject_JWorldArrivalPhase::Queued && bArrivalOutcomeReceived)
    {
        FinishArrival(bArrivalOutcomeSucceeded && IsArrivalPlacementCurrent(ArrivalPawn.Get(), ArrivalPlacement)
            ? EProject_JWorldArrivalPhase::Arrived : EProject_JWorldArrivalPhase::Failed, true);
        return;
    }
    if (Now >= ArrivalDeadline)
    { FinishArrival(ArrivalPhase == EProject_JWorldArrivalPhase::Queued ? EProject_JWorldArrivalPhase::ExpiredUnconfirmed : EProject_JWorldArrivalPhase::Failed, true); return; }
    if (!PC->HasAuthority())
    {
        if (ArrivalPhase == EProject_JWorldArrivalPhase::Preparing)
        {
            if (!bClientArrivalReady && IsArrivalRegionReady())
            {
                FVector Placement;
                bClientArrivalReady = FindArrivalPlacement(ArrivalPawn.Get(), ArrivalHint, Placement);
            }
            // Reliable delivery does not imply server admission: a shared quota may reject it.
            // Same-generation retries stop on the owner's terminal reply, timeout or binding change.
            if (TakeArrivalReadySend(Now)) ServerReadyForArrival(ArrivalRequest);
        }
        else if (ArrivalPhase == EProject_JWorldArrivalPhase::AwaitingLocalArrival && IsArrivalPlacementCurrent(ArrivalPawn.Get(), ArrivalPlacement) && IsArrivalRegionReady())
            FinishArrival(EProject_JWorldArrivalPhase::Arrived, false);
        return;
    }
    if (ArrivalPhase == EProject_JWorldArrivalPhase::Queued) return;
    if (ArrivalPhase == EProject_JWorldArrivalPhase::Preparing && !bArrivalPreparationStarted)
    {
        // Remote and deferred-local starts share the exact Entry permit gate.
        // This Advance was charged already; a full budget does not refund it.
        TryStartArrivalPreparation();
        return;
    }
    // WP gathers the destination source independently. While the remote owner
    // prepares it, completion polling cannot permit a commit and also forces
    // the engine's streaming-state epoch to change on every query.
    if (!PC->IsLocalController() && !bClientArrivalReady) return;
    const bool bServerRegionReady = IsArrivalRegionReady();
    if (PC->IsLocalController()) bClientArrivalReady = bServerRegionReady;
    if (!bClientArrivalReady || !bServerRegionReady) return;
    FVector Placement;
    if (!FindArrivalPlacement(ArrivalPawn.Get(), ArrivalHint, Placement)) return;
    const uint64 Request = ArrivalRequest;
    const TStrongObjectPtr<AProject_JWorldPortal> KeepPortal(ArrivalPortal.Get());
    const TStrongObjectPtr<APawn> KeepPawn(ArrivalPawn.Get());
    if (!KeepPortal || !IProject_JInteractable::Execute_CanInteract(KeepPortal.Get(), KeepPawn.Get())
        || ArrivalRequest != Request || !IsArrivalCurrent(true) || !FindArrivalPlacement(KeepPawn.Get(), ArrivalHint, Placement)) return;
    auto* Character = Cast<AProject_JPlayerCharacter>(KeepPawn.Get());
    auto* Mover = Character ? Character->GetMover() : nullptr;
    if (!Mover || Mover->IsBeingDestroyed() || Mover->IsBackendAsync()) { FinishArrival(EProject_JWorldArrivalPhase::Failed, true); return; }
    auto Effect = MakeShared<FProject_JPreparedArrivalEffect>();
    Effect->Request = Request; Effect->Recipient = this; Effect->ExpectedPawn = KeepPawn.Get(); Effect->TargetLocation = Placement;
    ArrivalPlacement = Placement; ArrivalPhase = EProject_JWorldArrivalPhase::Queued;
    // This is the movement commit boundary. No Actor transform or artificial success delegate.
    Mover->QueueInstantMovementEffect(Effect);
}
bool UProject_JWorldStreamingComponent::TakeArrivalReadySend(double Now)
{
    if (!ArrivalRequest || ArrivalPhase != EProject_JWorldArrivalPhase::Preparing || !bClientArrivalReady
        || !FMath::IsFinite(Now) || Now >= ArrivalDeadline || Now < NextClientArrivalAck) return false;
    NextClientArrivalAck = Now + .5;
    return true;
}
void UProject_JWorldStreamingComponent::RecordArrivalOutcome(uint64 Request, APawn* ExpectedPawn, const UMoverComponent* ExpectedMover, bool bSucceeded)
{
    check(IsInGameThread());
    if (ArrivalRequest != Request || ArrivalPhase != EProject_JWorldArrivalPhase::Queued || !IsArrivalCurrent(false)
        || ArrivalPawn.Get() != ExpectedPawn || !ExpectedMover || ExpectedMover->IsBeingDestroyed() || ExpectedMover->GetOwner() != ExpectedPawn
        || Project_J::Movement::FindMover(ExpectedPawn) != ExpectedMover) return;
    bArrivalOutcomeReceived = true; bArrivalOutcomeSucceeded = bSucceeded;
}
void UProject_JWorldStreamingComponent::FinishArrival(EProject_JWorldArrivalPhase Phase, bool bNotifyClient)
{
    const uint64 Request = ArrivalRequest; const FVector Placement = ArrivalPlacement;
    // Terminal notification follows the start already sent, even after the
    // current source permission has retired or the component starts teardown.
    const bool bPreparationStarted = Request && bArrivalPreparationStarted;
    const uint64 ExecutionToken = ArrivalExecutionToken;
    auto* Execution = ArrivalExecutionOwner.Get();
    ArrivalExecutionToken = 0; ArrivalExecutionOwner.Reset();
    if (Execution) Execution->Unregister(ExecutionToken, this, Request);
    auto* PC = Cast<APlayerController>(GetOwner());
    const bool bSend = bNotifyClient && Request && bPreparationStarted && PC && !PC->IsActorBeingDestroyed() && PC->HasAuthority() && !PC->IsLocalController()
        && GetWorld() && !GetWorld()->bIsTearingDown;
    ArrivalRequest = 0; ArrivalPortal.Reset(); ArrivalPawn.Reset(); ArrivalPlayerState.Reset(); ArrivalDefinition.Reset(); ArrivalPlaceId = NAME_None;
    ArrivalAbilitySystem.Reset(); ArrivalActorInfoRevision = 0;
    bArrivalPreparationStarted = false;
    bClientArrivalReady = bArrivalOutcomeReceived = bArrivalOutcomeSucceeded = false;
    ArrivalPhase = Phase;
    if (bSend) ClientFinishArrival(Request, Phase == EProject_JWorldArrivalPhase::Arrived, Placement);
}
void UProject_JWorldStreamingComponent::ClientPrepareArrival_Implementation(uint64 Request, FVector Hint, float Radius)
{
    auto* PC = Cast<APlayerController>(GetOwner());
    if (!PC || PC->HasAuthority() || !PC->IsLocalController() || !Request || Request <= NextArrivalRequest || Hint.ContainsNaN()
        || !FMath::IsFinite(Radius) || Radius < 1000 || Radius > 200000) return;
    FinishArrival(EProject_JWorldArrivalPhase::Idle, false);
    NextArrivalRequest = ArrivalRequest = Request; ArrivalPawn = PC->GetPawn(); ArrivalPlayerState = PC->PlayerState;
    ArrivalHint = Hint; ArrivalRadius = Radius; ArrivalDeadline = FPlatformTime::Seconds()+ArrivalTimeoutSeconds; NextClientArrivalAck = 0;
    ArrivalPhase = EProject_JWorldArrivalPhase::Preparing; bArrivalPreparationStarted = true;
    if (!RegisterArrivalExecution()) FinishArrival(EProject_JWorldArrivalPhase::Failed, false);
}
void UProject_JWorldStreamingComponent::ServerReadyForArrival_Implementation(uint64 Request)
{
    if (Request != ArrivalRequest || ArrivalPhase != EProject_JWorldArrivalPhase::Preparing || !IsPreparedArrivalStarted()
        || bClientArrivalReady || !IsArrivalCurrent(false)) return;
    // Validate the current session before charging it, then reject rate-limited
    // ACKs before the origin's place lookup, visibility trace and eligibility work.
    if (!UProject_JCommandBudgetComponent::AdmitClientCommand(ArrivalPlayerState.Get(), NextClientArrivalAck, .1)) return;
    if (!IsArrivalCurrent(true)) return;
    bClientArrivalReady = true; // Advisory only: the authority still checks its own cells, current session and collision.
}
void UProject_JWorldStreamingComponent::ClientFinishArrival_Implementation(uint64 Request, bool bSucceeded, FVector Placement)
{
    if (!GetOwner() || GetOwner()->HasAuthority() || Request != ArrivalRequest) return;
    if (!bSucceeded || Placement.ContainsNaN()) { FinishArrival(EProject_JWorldArrivalPhase::Failed, false); return; }
    // Keep the destination source until the owning client's real pawn has arrived.
    ArrivalPlacement = Placement; ArrivalPhase = EProject_JWorldArrivalPhase::AwaitingLocalArrival;
    ArrivalDeadline = FPlatformTime::Seconds()+ArrivalTimeoutSeconds;
}
void UProject_JWorldStreamingComponent::EndPlay(EEndPlayReason::Type Reason)
{
    FinishArrival(EProject_JWorldArrivalPhase::Failed, false); Super::EndPlay(Reason);
}
void UProject_JWorldStreamingComponent::OnUnregister()
{
    StreamingSourceIdentity = NAME_None;
    FinishArrival(EProject_JWorldArrivalPhase::Failed, false); Super::OnUnregister();
}

bool FProject_JPreparedArrivalEffect::ApplyMovementEffect(FApplyMovementEffectParams& Params, FMoverSyncState& Output)
{
    check(IsInGameThread());
    const TStrongObjectPtr<UProject_JWorldStreamingComponent> KeepRecipient(Recipient.Get());
    const TStrongObjectPtr<APawn> KeepPawn(ExpectedPawn.Get());
    const TStrongObjectPtr<const UMoverComponent> KeepMover(Params.MoverComp);
    const TStrongObjectPtr<USceneComponent> KeepUpdated(Params.UpdatedComponent);
    const auto WeakRecipient = Recipient; const auto WeakPawn = ExpectedPawn; const uint64 Generation = Request;
    const TWeakObjectPtr<const UMoverComponent> WeakMover(Params.MoverComp);
    const auto IsCurrentCommit = [&]()
    {
        return KeepRecipient && KeepPawn && KeepMover && KeepUpdated && Params.TimeStep && !KeepMover->IsBackendAsync()
            && !KeepMover->IsBeingDestroyed() && !KeepUpdated->IsBeingDestroyed()
            && KeepMover->GetOwner() == KeepPawn.Get() && KeepUpdated->GetOwner() == KeepPawn.Get() && KeepUpdated.Get() == KeepPawn->GetRootComponent()
            && Project_J::Movement::FindCapsule(KeepPawn.Get()) == KeepUpdated.Get()
            && KeepRecipient->ArrivalRequest == Generation && KeepRecipient->ArrivalPhase == EProject_JWorldArrivalPhase::Queued
            && KeepRecipient->IsPreparedArrivalStarted()
            && KeepRecipient->IsArrivalCurrent(false) && KeepPawn->HasAuthority()
            && FMath::IsFinite(KeepRecipient->ArrivalDeadline) && FPlatformTime::Seconds() < KeepRecipient->ArrivalDeadline;
    };
    if (!IsCurrentCommit()) return false;
    const auto HasLiveAvatar = [&]()
    {
        const auto* Player = Cast<AProject_JPlayerState>(KeepRecipient->ArrivalPlayerState.Get());
        const auto* ASC = Player ? Player->GetProjectJAbilitySystemComponent() : nullptr;
        const auto* Attributes = Player ? Player->GetProjectJAttributeSet() : nullptr;
        const float Health = Attributes ? Attributes->GetHealth() : 0.f;
        return ASC && Attributes && FMath::IsFinite(Health) && Health > 0
            && !ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Dead);
    };
    const FVector FixedTarget = TargetLocation;
    const FVector PreviousLocation = KeepUpdated->GetComponentLocation();
    const FQuat PreviousRotation = KeepUpdated->GetComponentQuat();
    const FRotator FinalRotation = bUseActorRotation ? KeepUpdated->GetComponentRotation() : TargetRotation;
    const FQuat FinalQuat(FinalRotation);
    if (FixedTarget.ContainsNaN() || PreviousLocation.ContainsNaN() || PreviousRotation.ContainsNaN() || !PreviousRotation.IsNormalized()
        || FinalRotation.ContainsNaN() || FinalQuat.ContainsNaN() || !FinalQuat.IsNormalized()) return false;
    // Recheck the fixed queued target immediately before the native effect. Never
    // re-project/reselect a target after the owning preparation has committed it.
    bool bPlacementValid = HasLiveAvatar() && FixedTarget == KeepRecipient->ArrivalPlacement
        && KeepRecipient->IsArrivalPlacementValid(KeepPawn.Get(), FixedTarget, FinalQuat);
    if (!IsCurrentCommit()) return false;
    bPlacementValid &= TargetLocation == FixedTarget && FixedTarget == KeepRecipient->ArrivalPlacement;
    const int32 FirstEvent = Params.OutputEvents.Num();
    bool bApplied = false;
    // Life is pre-apply eligibility, never a shared identity/receipt fence.
    // An irreversible native result remains deliverable if life changes afterwards.
    if (bPlacementValid && HasLiveAvatar())
    {
        bApplied = Super::ApplyMovementEffect(Params, Output);
    }
    else
    {
        // A supported eligibility/geometry rejection is a real attempted-effect failure.
        // Like native TeleportTo failure it changes no Actor/Output state, and
        // still uses the normal queued event delivery (not a direct Finish).
        Params.OutputEvents.Add(MakeShared<FTeleportFailedEventData>(*Params.TimeStep, PreviousLocation, PreviousRotation,
            FixedTarget, FinalQuat, ETeleportFailureReason::Reason_NotAvailable));
    }
    // UE5.8 FTeleportEffect appends exactly one native outcome. Fail closed if that contract changes.
    if (Params.OutputEvents.Num() != FirstEvent + 1) return bApplied;
    auto& Event = Params.OutputEvents[FirstEvent];
    if (Event && (Event->CastTo<FTeleportSucceededEventData>() || Event->CastTo<FTeleportFailedEventData>()))
        Event->SetEventProcessedCallback([WeakRecipient, WeakPawn, WeakMover, Generation](const FMoverSimulationEventData& Data, const FMoverSimEventGameThreadContext& Context)
        {
            if (Context.MoverComp != WeakMover.Get()) return;
            if (auto* Receiver = WeakRecipient.Get()) Receiver->RecordArrivalOutcome(Generation, WeakPawn.Get(), Context.MoverComp,
                Data.CastTo<FTeleportSucceededEventData>() != nullptr);
        });
    return bApplied;
}
