#include "Movement/Project_JMoverComponent.h"
#include "Movement/Project_JGroundTrajectoryPrediction.h"
#include "Movement/Project_JMoverInputs.h"
#include "Movement/Project_JMoverLifecycle.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "DefaultMovementSet/LayeredMoves/AnimRootMotionLayeredMove.h"
#include "MoveLibrary/GroundMovementUtils.h"
#include "MoveLibrary/MovementUtils.h"
#include "MoveLibrary/BasedMovementUtils.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "MoveLibrary/RollbackBlackboard.h"

namespace
{
int32 PreparedGroundPrediction = 1;
FAutoConsoleVariableRef CVarPreparedGroundPrediction(
    TEXT("ProjectJ.Mover.PreparedGroundPrediction"), PreparedGroundPrediction,
    TEXT("Prepare canonical native ground trajectory inputs once per synchronous query. 0 retains ordinary mode/BP prediction, 1 enables the value path for admitted queries."),
    ECVF_Default);
#if !UE_BUILD_SHIPPING
int32 PreparedGroundQueries = 0;
FAutoConsoleVariableRef CVarPreparedGroundQueries(
    TEXT("ProjectJ.Mover.PreparedGroundQueries"), PreparedGroundQueries,
    TEXT("Read-only saturated number of queries that consumed the native ground value path."), ECVF_ReadOnly);
int32 PreparedGroundCaptures = 0;
FAutoConsoleVariableRef CVarPreparedGroundCaptures(
    TEXT("ProjectJ.Mover.PreparedGroundCaptures"), PreparedGroundCaptures,
    TEXT("Read-only saturated GT admissions of owned ground packets. A captured packet may retire without worker consumption."), ECVF_ReadOnly);
#endif

bool HasCanonicalNativeParent(const UClass* Class)
{
    while (Class && !Class->HasAnyClassFlags(CLASS_Native)) Class = Class->GetSuperClass();
    return Class == UProject_JGroundWalkingMode::StaticClass();
}

bool UsesNativeEvent(const UBaseMovementMode& Mode, FName Name)
{
    const UFunction* Function = Mode.FindFunction(Name);
    return Function && Function->GetOwnerClass()->HasAnyClassFlags(CLASS_Native);
}

bool CaptureCanonicalBasePose(const UPrimitiveComponent* Base, FName Bone,
    const UProject_JMoverComponent& Mover, FVector& Position, FQuat& Rotation)
{
    if (!IsValid(Base) || !Base->IsRegistered() || Base->IsBeingDestroyed() || Base->GetWorld() != Mover.GetWorld() || !Bone.IsNone()) return false;
    const UClass* Class = Base->GetClass();
    if (Class != UBoxComponent::StaticClass() && Class != UCapsuleComponent::StaticClass() &&
        Class != USphereComponent::StaticClass() && Class != UStaticMeshComponent::StaticClass()) return false;
    return UBasedMovementUtils::GetMovementBaseTransform(Base, NAME_None, Position, Rotation) &&
        !Position.ContainsNaN() && !Rotation.ContainsNaN() && Rotation.IsNormalized();
}

bool IsFiniteTuning(const FProject_JGroundMovementSettings& Tuning, const FSmoothWalkingMoveParams& Smooth)
{
    const float Values[] = {Tuning.WalkSpeed, Tuning.RunSpeed, Tuning.SprintSpeed,
        Tuning.WalkAcceleration, Tuning.RunAcceleration, Tuning.SprintAcceleration,
        Tuning.GaitChangeDeceleration, Tuning.StoppingDeceleration, Tuning.RunTurnStrength,
        Tuning.SprintTurnStrength, Tuning.RunFacingTime, Tuning.SprintFacingTime, Tuning.IdleFacingTime,
        Smooth.Acceleration, Smooth.Deceleration, Smooth.DirectionalAccelerationFactor,
        Smooth.TurningStrength, Smooth.AccelerationSmoothingTime, Smooth.DecelerationSmoothingTime,
        Smooth.AccelerationSmoothingCompensation, Smooth.DecelerationSmoothingCompensation,
        Smooth.VelocityDeadzoneThreshold, Smooth.AccelerationDeadzoneThreshold,
        Smooth.OutsideInfluenceSmoothingTime, Smooth.FacingSmoothingTime,
        Smooth.FacingDeadzoneThreshold, Smooth.AngularVelocityDeadzoneThreshold};
    for (const float Value : Values) if (!FMath::IsFinite(Value)) return false;
    return true;
}

// Own every value used in the inner loop. No mode, collection, blackboard or UObject
// reference enters the calculation. The real mode remains the only tuning writer.
using FGroundPredictionState = FProject_JGroundTrajectoryPrediction::FState;
using FGroundPredictionParams = FProject_JGroundTrajectoryPrediction::FParameters;

// Only values and already allocated output storage are touched here. The caller
// admits the canonical native implementation; unsupported callbacks use Super.
void ComputeGroundPrediction(FGroundPredictionParams& Params, FGroundPredictionState& State,
    FMoverTimeStep Step, float SecondsPerSample,
    TArray<FTrajectorySampleInfo>& Samples)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ProjectJ_Trajectory_NativeGroundLoop);
    FVector PriorVelocity = State.Velocity;
    FRotator PriorOrientation = State.Orientation;
    for (int32 Index = 0; Index < Samples.Num(); ++Index)
    {
        FTrajectorySampleInfo& Sample = Samples[Index];
        Sample.Transform.SetTranslationAndScale3D(State.Location, FVector::OneVector);
        Sample.Transform.SetRotation(State.Orientation.Quaternion());
        Sample.LinearVelocity = State.Velocity;
        Sample.InstantaneousAcceleration = (State.Velocity - PriorVelocity) / SecondsPerSample;
        Sample.AngularVelocity = (State.Orientation - PriorOrientation) * (1.f / SecondsPerSample);
        Sample.SimTimeMs = Step.BaseSimTimeMs;
        PriorVelocity = State.Velocity;
        PriorOrientation = State.Orientation;

        Params.Input.CurrentFacing = State.Orientation.Quaternion();
        if (!Params.bHasInput)
        {
            Params.Input.MoveInput = Params.FallbackIntent;
            Params.Input.FacingIntent = Params.Input.CurrentFacing.GetForwardVector();
        }
        // The old outer mode reads the previous step's cap before the ground
        // adapter writes its next cap. Preserve that order, including step zero.
        Params.Input.bHasMaxMoveSpeed = Params.MaxSpeedOverride >= 0.f || Params.bHasLegacySpeed;
        Params.Input.MaxMoveSpeed = Params.MaxSpeedOverride >= 0.f ? Params.MaxSpeedOverride : Params.LegacyMaxSpeed;
        const FSimpleWalkingInputResult Input = UGroundMovementUtils::ComputeSimpleWalkingInput(Params.Input);
        const FProject_JPreparedGroundMove Ground = UProject_JGroundMovementLibrary::PrepareGroundMove(
            Params.Ground, Input.DesiredVelocity, Input.DesiredFacing, Params.Input.CurrentFacing, State.Velocity);
        if (Ground.bVelocityBypass)
        {
            State.Velocity = Ground.Velocity;
            const FQuat ToFacing = Params.Input.CurrentFacing.Inverse() * Input.DesiredFacing;
            State.AngularVelocity = Params.Smooth.DeltaSeconds > 0.f ?
                FMath::RadiansToDegrees(ToFacing.ToRotationVector() / Params.Smooth.DeltaSeconds) : FVector::ZeroVector;
        }
        else
        {
            Params.MaxSpeedOverride = Ground.MaxSpeedOverride;
            Params.Smooth.Acceleration = Ground.Acceleration;
            Params.Smooth.Deceleration = Ground.Deceleration;
            Params.Smooth.TurningStrength = Ground.TurningStrength;
            Params.Smooth.FacingSmoothingTime = Ground.FacingSmoothingTime;
            Params.Smooth.bInitializeSpring = Index == 0 ? Params.bInitializeSpring : Params.bInitializeSubsequentSpring;
            Params.Smooth.DesiredVelocity = Ground.Velocity;
            Params.Smooth.DesiredFacing = Ground.Facing;
            Params.Smooth.CurrentFacing = Params.Input.CurrentFacing;
            FSmoothWalkingStateView View{State.Spring.SpringVelocity, State.Spring.SpringAcceleration,
                State.Spring.IntermediateVelocity, State.Spring.IntermediateFacing, State.Spring.IntermediateAngularVelocity};
            UGroundMovementUtils::ComputeSmoothWalkingMove(Params.Smooth, View, State.AngularVelocity, State.Velocity);
            UProject_JGroundMovementLibrary::ApplyGroundRotationLimit(Ground, State.AngularVelocity);
        }
        State.Location += State.Velocity * SecondsPerSample;
        State.Orientation = UMovementUtils::ApplyAngularVelocityToRotator(
            State.Orientation, State.AngularVelocity, SecondsPerSample);
        if (Params.bBased)
        {
            // Preserve SetTransforms_WorldSpace -> next Get*_WorldSpace exactly.
            // Initial getters used the sync's old cached base pose; this advance
            // captures the current base pose just as the ordinary setter does.
            FVector LocalLocation, LocalVelocity, LocalAngular;
            FRotator LocalOrientation;
            UBasedMovementUtils::TransformLocationToLocal(Params.BasePosition, Params.BaseRotation, State.Location, LocalLocation);
            UBasedMovementUtils::TransformRotatorToLocal(Params.BaseRotation, State.Orientation, LocalOrientation);
            UBasedMovementUtils::TransformDirectionToLocal(Params.BaseRotation, State.Velocity, LocalVelocity);
            UBasedMovementUtils::TransformDirectionToLocal(Params.BaseRotation, State.AngularVelocity, LocalAngular);
            UBasedMovementUtils::TransformLocationToWorld(Params.BasePosition, Params.BaseRotation, LocalLocation, State.Location);
            UBasedMovementUtils::TransformRotatorToWorld(Params.BaseRotation, LocalOrientation, State.Orientation);
            UBasedMovementUtils::TransformDirectionToWorld(Params.BaseRotation, LocalVelocity, State.Velocity);
            UBasedMovementUtils::TransformDirectionToWorld(Params.BaseRotation, LocalAngular, State.AngularVelocity);
            Params.FallbackIntent = Params.BaseRotation.RotateVector(Params.FallbackIntentBase);
            // Spring fields are world-space state and must not rotate with a base.
        }
        Step.BaseSimTimeMs += Step.StepMs;
        ++Step.ServerFrame;
    }
}
}

void Project_J::Movement::ComputePreparedGroundTrajectory(const FProject_JGroundTrajectoryPrediction& Prediction,
    TArray<FTrajectorySampleInfo>& OutSamples)
{
    FGroundPredictionParams Params = Prediction.Params;
    FGroundPredictionState State = Prediction.State;
    OutSamples.SetNumUninitialized(Prediction.NumPredictionSamples);
    ComputeGroundPrediction(Params, State, Prediction.FirstStep, Prediction.SecondsPerSample, OutSamples);
    if (Prediction.bUseVisualRoot)
    {
        for (FTrajectorySampleInfo& Sample : OutSamples)
        {
            Sample.Transform = Prediction.VisualRootTransform * Sample.Transform;
        }
    }
}

bool UProject_JMoverComponent::TryPrepareGroundTrajectory(const FMoverPredictTrajectoryParams& Query,
    FProject_JGroundTrajectoryPrediction& OutPrediction)
{
    if (!IsInGameThread() || PreparedGroundPrediction <= 0 || IsBackendAsync() || Query.NumPredictionSamples <= 0) return false;
    FProject_JGroundTrajectoryPrediction Captured;
    const bool bCaptured = WithPredictionContext(Query,
        [&](const UBaseMovementMode& Mode, const FMoverSimContext& SimContext, FMoverTickStartData& StartState,
            const FMoverTimeStep& FirstStep, URollbackBlackboard& Blackboard)
        {
            // The base scope restores gravity then closes this real GT lifetime,
            // including unsupported admission. No blackboard reaches the packet.
            Blackboard.BeginPredictionFrame(FirstStep);
            const auto* Ground = Cast<UProject_JGroundWalkingMode>(&Mode);
            if (!Ground || !Ground->TryCaptureTrajectory(*this, SimContext, StartState, FirstStep, Query, Captured)) return false;
            if (Query.bUseVisualComponentRoot)
            {
                if (const USceneComponent* Visual = GetPrimaryVisualComponent())
                {
                    Captured.VisualRootTransform = Visual->GetRelativeTransform();
                    Captured.bUseVisualRoot = true;
                }
            }
            return true;
        });
    if (!bCaptured) return false;
    OutPrediction = MoveTemp(Captured);
#if !UE_BUILD_SHIPPING
    if (PreparedGroundCaptures < MAX_int32)
    {
        CVarPreparedGroundCaptures->Set(PreparedGroundCaptures + 1,
            EConsoleVariableFlags(ECVF_SetByCode | ECVF_Set_SetOnly_Unsafe));
    }
#endif
    return true;
}

bool UProject_JMoverComponent::TryGetPreparedPrediction(const UBaseMovementMode& Mode,
    const FMoverSimContext& SimContext, const FMoverTickStartData& StartState,
    const FMoverTimeStep& FirstStep, const FMoverPredictTrajectoryParams& PredictionParams,
    TArray<FTrajectorySampleInfo>& OutSamples)
{
    if (PreparedGroundPrediction <= 0) return false;
    if (const auto* Ground = Cast<UProject_JGroundWalkingMode>(&Mode))
    {
        const bool bPrepared = const_cast<UProject_JGroundWalkingMode*>(Ground)->TryPredictTrajectory(
            *this, SimContext, StartState, FirstStep, PredictionParams, OutSamples);
#if !UE_BUILD_SHIPPING
        if (bPrepared && PreparedGroundQueries < MAX_int32)
        {
            CVarPreparedGroundQueries->Set(PreparedGroundQueries + 1,
                EConsoleVariableFlags(ECVF_SetByCode | ECVF_Set_SetOnly_Unsafe));
        }
#endif
        return bPrepared;
    }
    return false;
}

bool UProject_JGroundWalkingMode::TryPredictTrajectory(UProject_JMoverComponent& Mover,
    const FMoverSimContext& SimContext, const FMoverTickStartData& StartState,
    const FMoverTimeStep& FirstStep, const FMoverPredictTrajectoryParams& Query,
    TArray<FTrajectorySampleInfo>& OutSamples)
{
    if (OutSamples.Num() != Query.NumPredictionSamples) return false;
    FProject_JGroundTrajectoryPrediction Captured;
    if (!TryCaptureTrajectory(Mover, SimContext, StartState, FirstStep, Query, Captured)) return false;
    ComputeGroundPrediction(Captured.Params, Captured.State, FirstStep, Query.SecondsPerSample, OutSamples);
    return true;
}

bool UProject_JGroundWalkingMode::TryCaptureTrajectory(UProject_JMoverComponent& Mover,
    const FMoverSimContext& SimContext, const FMoverTickStartData& StartState,
    const FMoverTimeStep& FirstStep, const FMoverPredictTrajectoryParams& Query,
    FProject_JGroundTrajectoryPrediction& OutPrediction) const
{
    static const FName GenerateName(TEXT("GenerateMove"));
    static const FName WalkName(TEXT("GenerateWalkMove"));
    if (!IsInGameThread() || Mover.IsBackendAsync() || GetOuter() != &Mover ||
        !HasCanonicalNativeParent(GetClass()) || !UsesNativeEvent(*this, GenerateName) ||
        !UsesNativeEvent(*this, WalkName) || Query.NumPredictionSamples <= 0 ||
        !Query.OptionalInputCmds.IsEmpty() ||
        Query.OptionalStartTime.IsSet() || Query.OptionalStartSyncState.IsSet() || Query.OptionalStartAuxState.IsSet() ||
        !FMath::IsFinite(Query.SecondsPerSample) || !FMath::IsFinite(FirstStep.StepMs) ||
        FirstStep.StepMs * 0.001f <= FLT_EPSILON || !FMath::IsFinite(FirstStep.BaseSimTimeMs)) return false;

    const auto* Sync = StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
    const auto* Input = StartState.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
    const auto* Intent = StartState.InputCmd.InputCollection.FindDataByType<FProject_JMoverInputs>();
    const auto* Life = StartState.SyncState.SyncStateCollection.FindDataByType<FProject_JMoverLifecycle>();
    if (!Sync || Sync->GetScriptStruct() != FMoverDefaultSyncState::StaticStruct() ||
        (Input && (Input->GetScriptStruct() != FCharacterDefaultInputs::StaticStruct() ||
            (Input->GetMoveInputType() != EMoveInputType::DirectionalIntent && Input->GetMoveInputType() != EMoveInputType::Velocity)))) return false;

    if (Input && Input->bUsingMovementBase && Input->MovementBase)
    {
        // Its world-space input getters read the same cached component pose on
        // every native step. Capture that admitted direction/facing once on GT.
        FVector InputBasePosition;
        FQuat InputBaseRotation;
        if (!CaptureCanonicalBasePose(Input->MovementBase.Get(), Input->MovementBaseBoneName,
            Mover, InputBasePosition, InputBaseRotation)) return false;
    }

    // A custom tag provider may have observable work on every GenerateMove.
    // Capture only known constant native montage tags; unknown providers retain
    // their original per-step dispatch and no borrowed layered move is retained.
    const FMoverSyncState& Physical = Mover.GetSyncState();
    if (Mover.FindMovementModeByName(Physical.MovementMode) != this ||
        Physical.MovementModifiers.GetActiveModifiersIterator()) return false;
    for (const TSharedPtr<FLayeredMoveBase>& Move : Physical.LayeredMoves.GetActiveMoves())
    {
        if (!Move || (Move->GetScriptStruct() != FLayeredMove_AnimRootMotion::StaticStruct() &&
            Move->GetScriptStruct() != FLayeredMove_AnimRootMotion_SimDriven::StaticStruct())) return false;
    }

    FGroundPredictionParams Params;
    if (const auto* Base = Sync->GetMovementBase())
    {
        // Its pose remains owned native values, never a borrowed worker base.
        if (!CaptureCanonicalBasePose(Base, Sync->GetMovementBaseBoneName(), Mover,
            Params.BasePosition, Params.BaseRotation)) return false;
        Params.bBased = true;
    }
    FGroundPredictionState State;
    State.Location = Sync->GetLocation_WorldSpace();
    State.Orientation = Sync->GetOrientation_WorldSpace();
    State.Velocity = Sync->GetVelocity_WorldSpace();
    State.AngularVelocity = Sync->GetAngularVelocityDegrees_WorldSpace();
    Params.bHasInput = Input != nullptr;
    Params.Input.MoveInput = Input ? Input->GetMoveInput_WorldSpace() : Sync->GetIntent_WorldSpace();
    Params.Input.MoveInputType = Input ? Input->GetMoveInputType() : EMoveInputType::DirectionalIntent;
    Params.Input.FacingIntent = Input ? Input->GetOrientationIntentDir_WorldSpace() : State.Orientation.Quaternion().GetForwardVector();
    Params.Input.UpDirection = Mover.GetUpDirection();
    Params.FallbackIntent = Sync->GetIntent_WorldSpace();
    Params.FallbackIntentBase = Sync->GetIntent_BaseSpace();
    Params.MaxSpeedOverride = MaxSpeedOverride;
    Params.bHasLegacySpeed = CommonLegacySettings != nullptr;
    Params.LegacyMaxSpeed = CommonLegacySettings ? CommonLegacySettings->MaxSpeed : 0.f;
    CaptureSmoothWalkingSettings(Params.Smooth);
    Params.Smooth.DeltaSeconds = FirstStep.StepMs * 0.001f;
    Params.Ground.Tuning = Mover.GroundMovementSettings;
    Params.Ground.InputType = Params.Input.MoveInputType;
    Params.Ground.bHasInput = Input != nullptr;
    Params.Ground.bIdle = !Input || Input->GetMoveInput().IsNearlyZero();
    Params.Ground.bSprint = Intent && Intent->bSprint;
    Params.Ground.bWalk = Intent && Intent->bWalk;
    Params.Ground.bHasLandingBraking = Life && Life->LandingBrakingTimeMs > 0.f;
    Params.Ground.bHasGameplayRootMotion = Mover.HasGameplayTag(Mover_AnimRootMotion, false);
    Params.Ground.bStrafe = Intent && Intent->bStrafe;
    Params.Ground.RotationOffset = Intent ? Intent->RotationOffset : 0.f;
    Params.Ground.ControlRotationRate = Intent ? Intent->ControlRotationRate : 0.f;
    if (const auto* Policy = Mover.FindSharedSettings<UCommonLegacyMovementSettings>()) Params.Ground.PolicyMaxSpeed = Policy->MaxSpeed;
    if (!Params.Ground.bHasGameplayRootMotion && Params.Ground.Tuning.bSlopeRelativeVelocity)
    {
        FHitResult Floor;
        Mover.TryGetFloorCheckHitResult(Floor);
        Params.Ground.FloorNormal = Floor.Normal;
    }
    if (State.Location.ContainsNaN() || State.Orientation.ContainsNaN() || State.Velocity.ContainsNaN() || State.AngularVelocity.ContainsNaN() ||
        Params.Input.MoveInput.ContainsNaN() || Params.Input.FacingIntent.ContainsNaN() || Params.Input.UpDirection.ContainsNaN() ||
        Params.FallbackIntent.ContainsNaN() || Params.FallbackIntentBase.ContainsNaN() || Params.Ground.FloorNormal.ContainsNaN() ||
        !FMath::IsFinite(Params.MaxSpeedOverride) || !FMath::IsFinite(Params.LegacyMaxSpeed) ||
        (Params.Ground.PolicyMaxSpeed.IsSet() && !FMath::IsFinite(Params.Ground.PolicyMaxSpeed.GetValue())) ||
        !IsFiniteTuning(Params.Ground.Tuning, Params.Smooth)) return false;

    // This read-only capture is the last admission check. After it succeeds,
    // prime the scoped prediction receipt and calculate only owned values.
    if (!CapturePredictionState(StartState.SyncState, SimContext, State.Spring, Params.bInitializeSpring)) return false;
    // Initial reset was captured before this write. The closed calculation has no
    // callbacks/blackboard access, so prime once and use the actual readback for
    // later samples; BeginPredictionFrame does not advance the real read timestamp.
    MarkPredictionGenerated(SimContext, Params.bInitializeSubsequentSpring);
    OutPrediction = FProject_JGroundTrajectoryPrediction();
    OutPrediction.Params = MoveTemp(Params);
    OutPrediction.State = MoveTemp(State);
    OutPrediction.FirstStep = FirstStep;
    OutPrediction.SecondsPerSample = Query.SecondsPerSample;
    OutPrediction.NumPredictionSamples = Query.NumPredictionSamples;
    // Forecast tuning remains packet-owned. The physical native mode keeps its
    // existing admission/generation writers; no deferred mode write is required.
    return true;
}
