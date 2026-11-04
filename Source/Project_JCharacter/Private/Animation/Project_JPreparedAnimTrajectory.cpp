#include "Animation/Project_JPreparedAnimTrajectory.h"
#include "Animation/Project_JAnimationSnapshot.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Movement/Project_JMoverComponent.h"
#include "MoverPoseSearchTrajectoryPredictor.h"
#include "DefaultMovementSet/Modes/FlyingMode.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Class.h"

namespace
{
constexpr int32 HistorySamples = 15;
constexpr int32 PredictionSamples = 15;
constexpr float HistoryInterval = .033f;
constexpr float PredictionInterval = .1f;

TAutoConsoleVariable<int32> DeferGroundPrediction(
    TEXT("ProjectJ.PlayerTrajectory.DeferGroundPrediction"), 1,
    TEXT("0: synchronous forecasts. 1 (default): defer admitted native ground forecasts. 2: also defer native Flying for explicit performance qualification. Unsupported queries retain the synchronous path."));

bool HasCanonicalNativeParent(const UClass* Class)
{
    while (Class && !Class->HasAnyClassFlags(CLASS_Native)) Class = Class->GetSuperClass();
    return Class == UProject_JPlayerAnimInstance::StaticClass();
}
}

void FProject_JPreparedAnimTrajectory::UpdateDerivedMovement(
    FProject_JAnimThreadSafeData& Data, TOptional<float> ReferenceYawOverride)
{
    Data.Movement.FutureTrajectoryVelocity =
        (Data.Movement.Trajectory.GetSampleAtTime(.6f).Position -
         Data.Movement.Trajectory.GetSampleAtTime(.4f).Position) / .2f;
    const float ReferenceYaw = ReferenceYawOverride.IsSet() ? ReferenceYawOverride.GetValue() :
        Data.Movement.Trajectory.GetSampleAtTime(0.f).Facing.Rotator().Yaw;
    Data.Movement.FutureTrajectoryTurnAngle = FMath::FindDeltaAngleDegrees(ReferenceYaw,
        Data.Movement.Trajectory.GetSampleAtTime(.5f).Facing.Rotator().Yaw);
    Data.Movement.bHasTrajectory = !Data.Movement.Trajectory.Samples.IsEmpty();
}

bool FProject_JPreparedAnimTrajectory::Compute(FProject_JAnimThreadSafeData& Data,
    TArray<FTrajectorySampleInfo>& Scratch) const
{
    // Admission fixes these dimensions before the packet is published. A retired
    // or malformed packet must never be labelled a completed trajectory.
    const int32 SampleCount = Kind == EKind::NativeFlying ?
        FlyingPrediction.NumPredictionSamples : Prediction.NumPredictionSamples;
    if (!ensureMsgf(SourceGeneration != 0 && SampleCount >= 2 &&
        Data.Movement.Trajectory.Samples.Num() == HistorySamples + 1 + PredictionSamples,
        TEXT("An admitted animation forecast lost its source or sample dimensions")))
    {
        Data.Movement.Trajectory.Samples.Reset();
        Data.Movement.bHasTrajectory = false;
        Data.Movement.FutureTrajectoryVelocity = FVector::ZeroVector;
        Data.Movement.FutureTrajectoryTurnAngle = 0.f;
        return false;
    }
    if (Kind == EKind::NativeFlying)
    {
        if (!ensureMsgf(UE::MoverUtils::ComputeNativeFlyingTrajectory(FlyingPrediction, Scratch),
            TEXT("An admitted native Flying forecast could not be computed")))
        {
            Data.Movement.Trajectory.Samples.Reset();
            Data.Movement.bHasTrajectory = false;
            Data.Movement.FutureTrajectoryVelocity = FVector::ZeroVector;
            Data.Movement.FutureTrajectoryTurnAngle = 0.f;
            return false;
        }
    }
    else Project_J::Movement::ComputePreparedGroundTrajectory(Prediction, Scratch);
    if (!ensureMsgf(UMoverTrajectoryPredictor::ResamplePredictionSamples(Scratch,
        Data.Movement.Trajectory, PredictionSamples, PredictionInterval, HistorySamples),
        TEXT("An admitted animation forecast could not be resampled")))
    {
        Data.Movement.Trajectory.Samples.Reset();
        Data.Movement.bHasTrajectory = false;
        Data.Movement.FutureTrajectoryVelocity = FVector::ZeroVector;
        Data.Movement.FutureTrajectoryTurnAngle = 0.f;
        return false;
    }
    UpdateDerivedMovement(Data, ReferenceYawOverride);
    return true;
}

bool UProject_JPlayerAnimInstance::TryPrepareNativeTrajectory(float DeltaSeconds,
    int32 PredictionHz, FProject_JAnimThreadSafeData& Data,
    FProject_JPreparedAnimTrajectory& Out)
{
    check(IsInGameThread());
    // A GT Blueprint update executes before the animation task. Its consumers
    // must continue to see the complete synchronous trajectory for this frame.
    const int32 DeferredPredictionMode = DeferGroundPrediction.GetValueOnGameThread();
    if (DeferredPredictionMode <= 0 ||
        !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f || PredictionHz <= 0 ||
        !HasCanonicalNativeParent(GetClass()) ||
        GetClass()->IsFunctionImplementedInScript(TEXT("BlueprintUpdateAnimation"))) return false;

    // Cache only registered object identities on GT; read their live values below.
    // A missing registration is retried rather than becoming a permanent null.
    static const IConsoleVariable* WarpedMontage = nullptr;
    if (!WarpedMontage) WarpedMontage = IConsoleManager::Get().FindConsoleVariable(
        TEXT("a.PoseSearchTrajectory.UseWarpedMontage"));
    if (!WarpedMontage || (WarpedMontage->GetInt() != 0 && GetActiveMontageInstance())) return false;

    auto* Pawn = TryGetPawnOwner();
    auto* Mover = Cast<UProject_JMoverComponent>(LocomotionSource.Get());
    auto* Predictor = MoverPredictor.Get();
    const uint64 Generation = LocomotionSourceGeneration;
    if (!IsValid(Pawn) || !IsValid(Mover) || !IsValid(Predictor) ||
        Pawn->IsActorBeingDestroyed() || Mover->IsBeingDestroyed() ||
        Mover->GetOwner() != Pawn || Mover->GetWorld() != GetWorld() ||
        !Mover->HasBeenInitialized() || !Mover->HasBegunPlay() || Generation == 0 || Generation == MAX_uint64 ||
        Predictor->GetClass() != UMoverTrajectoryPredictor::StaticClass() || Predictor->GetOuter() != this) return false;

    const UBaseMovementMode* Mode = Mover->GetMovementMode();
    const bool bPrepareNativeFlying = Mode && Mode->GetClass() == UFlyingMode::StaticClass();
    // Moving calculation off GT did not establish an end-to-end gain under
    // the measured mixed workload. Preserve default placement until qualified.
    if (bPrepareNativeFlying && DeferredPredictionMode < 2) return false;

    const FFrameRate& Rate = Predictor->GetPredictionSamplingFrameRate();
    if (Rate.Numerator <= 0 || Rate.Denominator <= 0) return false;
    const float SamplingInterval = static_cast<float>(Rate.AsInterval());
    const float LookAheadTime = PredictionSamples * PredictionInterval;
    const float RequiredSamples = LookAheadTime / SamplingInterval;
    if (!FMath::IsFinite(SamplingInterval) || SamplingInterval <= 0.f ||
        !FMath::IsFinite(RequiredSamples) || RequiredSamples >= float(MAX_int32 - 2)) return false;

    FVector Position, Velocity, AngularVelocity;
    FQuat Facing;
    // Preserve the wrapper's virtual state reads on GT. The exact native
    // predictor cannot dispatch a custom Predict implementation on the worker.
    Predictor->GetCurrentState(Position, Facing, Velocity);
    Predictor->GetAngularVelocity(AngularVelocity);
    if (Position.ContainsNaN() || Velocity.ContainsNaN() || AngularVelocity.ContainsNaN() ||
        Facing.ContainsNaN() || !Facing.IsNormalized()) return false;

    FMoverPredictTrajectoryParams Query;
    Query.NumPredictionSamples = FMath::FloorToInt32(RequiredSamples) + 2;
    Query.SecondsPerSample = SamplingInterval;
    Query.bUseVisualComponentRoot = true;
    Query.bDisableGravity = Predictor->IsPredictionGravityDisabled();
    FProject_JPreparedAnimTrajectory Prepared;
    if (bPrepareNativeFlying)
    {
        if (!Mover->TryPrepareNativeFlyingTrajectory(Query, Prepared.FlyingPrediction)) return false;
        Prepared.Kind = FProject_JPreparedAnimTrajectory::EKind::NativeFlying;
    }
    else if (!Mover->TryPrepareGroundTrajectory(Query, Prepared.Prediction)) return false;
    // No history/output changes precede successful native admission. Keep this
    // source fence next to the subsequent history publication as well.
    if (LocomotionSourceGeneration != Generation || LocomotionSource.Get() != Mover ||
        MoverPredictor.Get() != Predictor || TryGetPawnOwner() != Pawn ||
        !IsValid(Mover) || Mover->IsBeingDestroyed() || Pawn->IsActorBeingDestroyed()) return false;

    FPoseSearchTrajectoryData::FSampling Sampling;
    Sampling.NumHistorySamples = HistorySamples;
    Sampling.SecondsPerHistorySample = HistoryInterval;
    Sampling.NumPredictionSamples = PredictionSamples;
    Sampling.SecondsPerPredictionSample = PredictionInterval;
    UPoseSearchTrajectoryLibrary::InitTrajectorySamples(MoverTrajectory, Position, Facing, Sampling, DeltaSeconds);
    UPoseSearchTrajectoryLibrary::UpdateHistory_TransformHistoryWithRotation(
        MoverTrajectory, Position, Velocity, Facing, AngularVelocity, Sampling, DeltaSeconds);
    auto& CurrentSample = MoverTrajectory.Samples[HistorySamples];
    CurrentSample.TimeInSeconds = DeltaSeconds;
    CurrentSample.Position = Position;
    CurrentSample.Facing = Facing;
    Data.Movement.Trajectory = MoverTrajectory;

    Prepared.SourceGeneration = Generation;
    static const IConsoleVariable* SourcePolicy = nullptr;
    if (!SourcePolicy) SourcePolicy = IConsoleManager::Get().FindConsoleVariable(
        TEXT("ProjectJ.AnimationQuality.SourcePolicy.Enabled"));
    if (SourcePolicy && SourcePolicy->GetInt() == 0)
        Prepared.ReferenceYawOverride = Pawn->GetActorRotation().Yaw;
    Out = MoveTemp(Prepared);
    return true;
}
