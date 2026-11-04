#pragma once

#include "Movement/Project_JGroundMovement.h"
#include "MoveLibrary/GroundMovementUtils.h"
#include "MoverSimulationTypes.h"

/** One admitted native ground query. Owns values only: never a live mode, base,
 * input/sync collection, blackboard, delegate or borrowed spring-state view. */
struct PROJECT_JCORE_API FProject_JGroundTrajectoryPrediction
{
    struct FState
    {
        FVector Location = FVector::ZeroVector;
        FRotator Orientation = FRotator::ZeroRotator;
        FVector Velocity = FVector::ZeroVector;
        FVector AngularVelocity = FVector::ZeroVector;
        FSmoothWalkingStateValues Spring;
    };
    struct FParameters
    {
        FSimpleWalkingInputParams Input;
        FProject_JGroundMoveParams Ground;
        FSmoothWalkingMoveParams Smooth;
        FVector FallbackIntent = FVector::ZeroVector;
        FVector FallbackIntentBase = FVector::ZeroVector;
        FVector BasePosition = FVector::ZeroVector;
        FQuat BaseRotation = FQuat::Identity;
        bool bBased = false;
        float MaxSpeedOverride = -1.f;
        float LegacyMaxSpeed = 0.f;
        bool bHasLegacySpeed = false;
        bool bHasInput = false;
        bool bInitializeSpring = false;
        bool bInitializeSubsequentSpring = false;
    };

    FParameters Params;
    FState State;
    FMoverTimeStep FirstStep;
    float SecondsPerSample = 0.f;
    int32 NumPredictionSamples = 0;
    FTransform VisualRootTransform = FTransform::Identity;
    bool bUseVisualRoot = false;
};

namespace Project_J::Movement
{
/** Calculates only owned values using the same native kernel as synchronous
 * prediction. No world/object lookup, blackboard write, scheduler or GT callback. */
PROJECT_JCORE_API void ComputePreparedGroundTrajectory(const FProject_JGroundTrajectoryPrediction& Prediction,
    TArray<FTrajectorySampleInfo>& OutSamples);
}
