#pragma once

#include "CoreMinimal.h"
#include "Movement/Project_JGroundTrajectoryPrediction.h"
#include "MoverNativeFlyingTrajectory.h"

struct FProject_JAnimThreadSafeData;

/** Owns one admitted same-frame forecast. No live mode, predictor, source, delegate
 * or borrowed history enters the existing animation update task. */
struct FProject_JPreparedAnimTrajectory
{
    enum class EKind : uint8 { Ground, NativeFlying };
    EKind Kind = EKind::Ground;
    FProject_JGroundTrajectoryPrediction Prediction;
    FMoverNativeFlyingTrajectory FlyingPrediction;
    uint64 SourceGeneration = 0;
    TOptional<float> ReferenceYawOverride;

    static void UpdateDerivedMovement(FProject_JAnimThreadSafeData& Data,
        TOptional<float> ReferenceYawOverride);
    bool Compute(FProject_JAnimThreadSafeData& Data,
        TArray<FTrajectorySampleInfo>& Scratch) const;
};
