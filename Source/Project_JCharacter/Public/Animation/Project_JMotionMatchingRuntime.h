#pragma once

#include "Animation/Project_JAnimationUpdateSchedule.h"
#include "Project_JLocomotionAnimTypes.h"

/** 게임 스레드의 선택 상태를 관리한다. Chooser와 Pose Search 호출은 AnimInstance가 담당한다. */
class FProject_JMotionMatchingRuntime
{
public:
	struct FContext
	{
		EProject_JGroundMotionMode GroundMotionMode = EProject_JGroundMotionMode::Idle;
		EProject_JLocomotionGaitIntent GaitIntent = EProject_JLocomotionGaitIntent::Run;
		EProject_JLocomotionRotationMode RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
		EProject_JLocomotionPhaseFamily PhaseFamily = EProject_JLocomotionPhaseFamily::Idle;
		bool bStartRequested = false;
		bool bStartWasSprinting = false;
		int32 SelectionRevision = 0;
	};

	void Reset()
	{
		SelectionSchedule = {};
		bHasEvaluatedContext = false;
		LastEvaluatedContext = {};
	}

	void InvalidateContext() { bHasEvaluatedContext = false; }

	bool ShouldEvaluate(float DeltaSeconds, float UpdateInterval, uint32 OwnerId, bool bForceRefresh)
	{
		return SelectionSchedule.Advance(DeltaSeconds, UpdateInterval, OwnerId, bForceRefresh);
	}

	bool HasContextChanged(const FContext& Context) const
	{
		return !bHasEvaluatedContext ||
			LastEvaluatedContext.SelectionRevision != Context.SelectionRevision ||
			LastEvaluatedContext.GroundMotionMode != Context.GroundMotionMode ||
			LastEvaluatedContext.GaitIntent != Context.GaitIntent ||
			LastEvaluatedContext.RotationMode != Context.RotationMode ||
			LastEvaluatedContext.PhaseFamily != Context.PhaseFamily ||
			LastEvaluatedContext.bStartRequested != Context.bStartRequested ||
			LastEvaluatedContext.bStartWasSprinting != Context.bStartWasSprinting;
	}

	void CacheEvaluatedContext(const FContext& Context)
	{
		LastEvaluatedContext = Context;
		bHasEvaluatedContext = true;
	}

private:
	FProjectJAnimationUpdateSchedule SelectionSchedule;
	FContext LastEvaluatedContext;
	bool bHasEvaluatedContext = false;
};
