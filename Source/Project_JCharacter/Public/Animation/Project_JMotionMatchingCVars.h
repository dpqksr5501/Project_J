#pragma once

#include "CoreMinimal.h"

namespace Project_J::MotionMatchingCVars
{
bool IsDebugRemoteTrajectoryEnabled();
bool IsDebugInAirBlendStackEnabled();
bool ShouldRepairRemoteTrajectoryFacing();
float GetRepairRemoteTrajectoryFacingMinSpeed();
float GetRepairRemoteTrajectoryFacingMaxYawDelta();
bool ShouldDisableRemoteAccelerationReset();
bool ShouldSmoothRemoteTrajectoryPosition();
bool ShouldSmoothRemoteTrajectoryRotation();
}
