#pragma once

#include "CoreMinimal.h"

namespace Project_J::MotionMatchingCVars
{
bool ShouldRepairRemoteTrajectoryFacing();
float GetRepairRemoteTrajectoryFacingMinSpeed();
float GetRepairRemoteTrajectoryFacingMaxYawDelta();
bool ShouldDisableRemoteAccelerationReset();
bool ShouldSmoothRemoteTrajectoryPosition();
bool ShouldSmoothRemoteTrajectoryRotation();
int32 GetNetworkDebugMode();
bool ShouldLogNetworkDebugPeriodically();
bool ShouldCapturePivotDebugTrace();
bool ShouldCaptureTransitionDebugTrace();
/** 0=off, 1=state/asset changes, 2=also sampled TIP telemetry every 0.1 seconds. */
int32 GetTurnInPlaceDebugMode();
}
