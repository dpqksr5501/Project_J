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
}
