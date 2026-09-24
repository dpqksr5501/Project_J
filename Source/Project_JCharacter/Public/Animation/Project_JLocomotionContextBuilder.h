#pragma once

#include "Project_JLocomotionAnimTypes.h"

/** 컴포넌트가 이동 의미 컨텍스트를 게시할 때 사용하는 값 기반 정책. */
struct FProject_JLocomotionContextBuilder
{
	struct FMotionMatchingMovement
	{
		bool bInAirOrLanding = false;
		bool bUsingLocalInput = false;
		bool bHasMoveInput = false;
		bool bIsAccelerating = false;
		bool bHasPredictedMovement = false;
		bool bHasFutureTrajectoryVelocity = false;
		float GroundSpeed = 0.0f;
		float FutureTrajectorySpeed = 0.0f;
		float PredictedSpeedGain = 0.0f;
		float MovingSpeedThreshold = 0.0f;
	};

	static bool IsMotionMatchingMoving(const FMotionMatchingMovement& Input)
	{
		if (Input.bInAirOrLanding || (Input.bUsingLocalInput && !Input.bHasMoveInput))
		{
			return false;
		}
		const float FutureSpeed = Input.bHasFutureTrajectoryVelocity
			? Input.FutureTrajectorySpeed
			: FMath::Max(Input.GroundSpeed + Input.PredictedSpeedGain, 0.0f);
		const bool bFutureMoving = FutureSpeed > Input.MovingSpeedThreshold;
		return (Input.GroundSpeed > Input.MovingSpeedThreshold &&
			(Input.bHasMoveInput || bFutureMoving)) ||
			(Input.bHasMoveInput && bFutureMoving &&
				(Input.bIsAccelerating || Input.bHasPredictedMovement));
	}

	struct FPhaseInput
	{
		bool bLanding = false;
		bool bJumping = false;
		bool bFallOffOrInAir = false;
		bool bShouldTurnInPlace = false;
		bool bPivoting = false;
		bool bStarting = false;
		bool bMoving = false;
		bool bCombatStrafe = false;
		bool bHasMoveInput = false;
		EProject_JGroundMotionMode GroundMode = EProject_JGroundMotionMode::Idle;
		float GroundSpeed = 0.0f;
		float MoveInputTurnAngle = 0.0f;
		float TurnMinSpeed = 0.0f;
		float TurnAngleThreshold = 0.0f;
	};

	static EProject_JLocomotionPhaseFamily ResolvePhaseFamily(const FPhaseInput& Input)
	{
		if (Input.bLanding) return EProject_JLocomotionPhaseFamily::Landing;
		if (Input.bJumping) return EProject_JLocomotionPhaseFamily::JumpStart;
		if (Input.bFallOffOrInAir) return EProject_JLocomotionPhaseFamily::Fall;
		if (Input.GroundMode == EProject_JGroundMotionMode::Stop) return EProject_JLocomotionPhaseFamily::Stop;
		if (Input.bShouldTurnInPlace) return EProject_JLocomotionPhaseFamily::TurnInPlace;
		if (Input.bPivoting) return EProject_JLocomotionPhaseFamily::Pivot;
		if (Input.bStarting) return EProject_JLocomotionPhaseFamily::Start;
		if (Input.bCombatStrafe) return Input.bMoving ? EProject_JLocomotionPhaseFamily::Cycle : EProject_JLocomotionPhaseFamily::Idle;
		if (Input.bMoving && Input.bHasMoveInput && Input.GroundSpeed >= Input.TurnMinSpeed &&
			FMath::Abs(Input.MoveInputTurnAngle) >= Input.TurnAngleThreshold)
		{
			return EProject_JLocomotionPhaseFamily::Turn;
		}
		return Input.bMoving ? EProject_JLocomotionPhaseFamily::Cycle : EProject_JLocomotionPhaseFamily::Idle;
	}
};
