#pragma once

#include "CoreMinimal.h"
#include "MoverTypes.h"
#include "MoveLibrary/AirMovementUtils.h"

struct FTrajectorySampleInfo;

/** One exact-native, no-base Flying forecast. Owns only values: no input/sync
 * collection, movement mode, component, world, callback or prediction blackboard. */
struct FMoverNativeFlyingTrajectory
{
    FFreeMoveParams MoveParams;
    FVector Location = FVector::ZeroVector;
    FRotator Orientation = FRotator::ZeroRotator;
    FVector Velocity = FVector::ZeroVector;
    FMoverTimeStep FirstStep;
    int32 NumPredictionSamples = 0;
    float SecondsPerSample = 0.f;
    bool bUsesSyncFacing = false;
    bool bRemainVertical = true;
    FTransform VisualRootTransform = FTransform::Identity;
    bool bUseVisualRoot = false;
};

namespace UE::MoverUtils
{
/** Pure owned-value calculation; no console, UObject, world or blackboard access.
 * Failure clears output rather than publishing a partially computed forecast. */
MOVER_API bool ComputeNativeFlyingTrajectory(const FMoverNativeFlyingTrajectory& Prediction,
    TArray<FTrajectorySampleInfo>& OutSamples);
}
