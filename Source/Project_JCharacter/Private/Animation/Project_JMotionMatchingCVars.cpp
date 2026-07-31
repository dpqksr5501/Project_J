#include "Animation/Project_JMotionMatchingCVars.h"

#include "HAL/IConsoleManager.h"

namespace
{
TAutoConsoleVariable<int32> CVarProjectJRepairRemoteTrajectoryFacing(
	TEXT("p.ProjectJ.MM.RepairRemoteTrajectoryFacing"),
	1,
	TEXT("Repairs simulated proxy trajectory facing after network-smoothed turns so straight remote running queries remain cycle-like."));

TAutoConsoleVariable<float> CVarProjectJRepairRemoteTrajectoryFacingMinSpeed(
	TEXT("p.ProjectJ.MM.RepairRemoteTrajectoryFacingMinSpeed"),
	80.0f,
	TEXT("Minimum simulated proxy ground speed required before repairing remote trajectory facing."));

TAutoConsoleVariable<float> CVarProjectJRepairRemoteTrajectoryFacingMaxYawDelta(
	TEXT("p.ProjectJ.MM.RepairRemoteTrajectoryFacingMaxYawDelta"),
	35.0f,
	TEXT("Maximum absolute actor yaw vs velocity yaw delta allowed before remote trajectory facing repair is skipped."));

TAutoConsoleVariable<int32> CVarProjectJDisableRemoteAccelReset(
	TEXT("p.ProjectJ.MM.DisableRemoteAccelReset"),
	1,
	TEXT("Prevents simulated proxy GetCurrentAcceleration flicker from resetting Motion Matching trajectory history."));

TAutoConsoleVariable<int32> CVarProjectJSmoothRemoteTrajectoryRotation(
	TEXT("p.ProjectJ.MM.SmoothRemoteTrajectoryRotation"),
	0,
	TEXT("Allows simulated proxy trajectory sample rotation smoothing. Disabled by default."));

TAutoConsoleVariable<int32> CVarProjectJSmoothRemoteTrajectoryPosition(
	TEXT("p.ProjectJ.MM.SmoothRemoteTrajectoryPosition"),
	0,
	TEXT("Allows simulated proxy trajectory sample position smoothing. Disabled by default because local-space smoothing can bend remote history samples."));

TAutoConsoleVariable<int32> CVarProjectJMMNetworkDebug(
	TEXT("p.ProjectJ.MMNetDebug"),
	0,
	TEXT("Motion Matching network debug. 0=off, 1=selection/PSD changes, 2=also periodic animation updates."));

TAutoConsoleVariable<int32> CVarProjectJMMPivotDebug(
	TEXT("p.ProjectJ.MMPivotDebug"),
	0,
	TEXT("Captures native Motion Matching Pivot/BlendStack frames. Use DumpMotionMatchingPivotTrace after moving. 0=off, 1=on."));

TAutoConsoleVariable<int32> CVarProjectJMMTransitionDebug(
	TEXT("p.ProjectJ.MMTransitionDebug"),
	0,
	TEXT("Captures native Motion Matching Start/Stop/Jump/Landing/Pivot BlendStack frames. Use DumpMotionMatchingTransitionTrace after moving. 0=off, 1=on."));
}

namespace Project_J::MotionMatchingCVars
{
bool ShouldRepairRemoteTrajectoryFacing()
{
	return CVarProjectJRepairRemoteTrajectoryFacing.GetValueOnAnyThread() != 0;
}

float GetRepairRemoteTrajectoryFacingMinSpeed()
{
	return CVarProjectJRepairRemoteTrajectoryFacingMinSpeed.GetValueOnAnyThread();
}

float GetRepairRemoteTrajectoryFacingMaxYawDelta()
{
	return CVarProjectJRepairRemoteTrajectoryFacingMaxYawDelta.GetValueOnAnyThread();
}

bool ShouldDisableRemoteAccelerationReset()
{
	return CVarProjectJDisableRemoteAccelReset.GetValueOnAnyThread() != 0;
}

bool ShouldSmoothRemoteTrajectoryPosition()
{
	return CVarProjectJSmoothRemoteTrajectoryPosition.GetValueOnAnyThread() != 0;
}

bool ShouldSmoothRemoteTrajectoryRotation()
{
	return CVarProjectJSmoothRemoteTrajectoryRotation.GetValueOnAnyThread() != 0;
}

int32 GetNetworkDebugMode()
{
	return CVarProjectJMMNetworkDebug.GetValueOnAnyThread();
}

bool ShouldLogNetworkDebugPeriodically()
{
	return GetNetworkDebugMode() >= 2;
}

bool ShouldCapturePivotDebugTrace()
{
	return CVarProjectJMMPivotDebug.GetValueOnAnyThread() != 0;
}

bool ShouldCaptureTransitionDebugTrace()
{
	return CVarProjectJMMTransitionDebug.GetValueOnAnyThread() != 0;
}
}
