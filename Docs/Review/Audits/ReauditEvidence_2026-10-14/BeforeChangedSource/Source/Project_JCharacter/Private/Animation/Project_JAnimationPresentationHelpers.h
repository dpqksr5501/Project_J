#pragma once
#include "Animation/Project_JAnimationSnapshot.h"

namespace ProjectJ::Animation::Presentation
{
	constexpr float StateControllerOneShotMouseTurnCancelAngle = 15.0f;
	// Let a very short Shift tap settle before committing the authored Start.
	// After this window, switching the source animation mid-stride is worse than
	// letting the current Start finish and entering the current-gait Cycle.
	constexpr double StateControllerStartGaitCommitWindowSeconds = 0.15;

	constexpr float StrafeDirectionSectorHalfWidth = 22.5f;

	inline float GetStrafeDirectionCenterDegrees(const EProject_JStateControllerStrafeDirection Direction)
	{
		switch (Direction)
		{
		case EProject_JStateControllerStrafeDirection::Forward:
			return 0.0f;
		case EProject_JStateControllerStrafeDirection::ForwardLeft:
			return -45.0f;
		case EProject_JStateControllerStrafeDirection::Left:
			return -90.0f;
		case EProject_JStateControllerStrafeDirection::BackwardLeft:
			return -135.0f;
		case EProject_JStateControllerStrafeDirection::Backward:
			return 180.0f;
		case EProject_JStateControllerStrafeDirection::BackwardRight:
			return 135.0f;
		case EProject_JStateControllerStrafeDirection::Right:
			return 90.0f;
		case EProject_JStateControllerStrafeDirection::ForwardRight:
			return 45.0f;
		default:
			return 0.0f;
		}
	}

	inline const TCHAR* StateControllerStrafeDirectionToString(const EProject_JStateControllerStrafeDirection Direction)
	{
		switch (Direction)
		{
		case EProject_JStateControllerStrafeDirection::Forward:
			return TEXT("Forward");
		case EProject_JStateControllerStrafeDirection::ForwardLeft:
			return TEXT("ForwardLeft");
		case EProject_JStateControllerStrafeDirection::Left:
			return TEXT("Left");
		case EProject_JStateControllerStrafeDirection::BackwardLeft:
			return TEXT("BackwardLeft");
		case EProject_JStateControllerStrafeDirection::Backward:
			return TEXT("Backward");
		case EProject_JStateControllerStrafeDirection::BackwardRight:
			return TEXT("BackwardRight");
		case EProject_JStateControllerStrafeDirection::Right:
			return TEXT("Right");
		case EProject_JStateControllerStrafeDirection::ForwardRight:
			return TEXT("ForwardRight");
		default:
			return TEXT("None");
		}
	}

	inline EProject_JStateControllerStrafeDirection ResolveStateControllerStrafeDirection(
		const float DirectionDegrees,
		const EProject_JStateControllerStrafeDirection PreviousDirection,
		const float HysteresisDegrees)
	{
		const float Direction = FRotator::NormalizeAxis(DirectionDegrees);
		const float ClampedHysteresis = FMath::Clamp(HysteresisDegrees, 0.0f, StrafeDirectionSectorHalfWidth - KINDA_SMALL_NUMBER);
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(GetStrafeDirectionCenterDegrees(PreviousDirection), Direction)) <=
			StrafeDirectionSectorHalfWidth + ClampedHysteresis)
		{
			return PreviousDirection;
		}

		if (Direction >= -22.5f && Direction <= 22.5f)
		{
			return EProject_JStateControllerStrafeDirection::Forward;
		}
		if (Direction < -22.5f && Direction >= -67.5f)
		{
			return EProject_JStateControllerStrafeDirection::ForwardLeft;
		}
		if (Direction < -67.5f && Direction >= -112.5f)
		{
			return EProject_JStateControllerStrafeDirection::Left;
		}
		if (Direction < -112.5f && Direction >= -157.5f)
		{
			return EProject_JStateControllerStrafeDirection::BackwardLeft;
		}
		if (Direction > 22.5f && Direction <= 67.5f)
		{
			return EProject_JStateControllerStrafeDirection::ForwardRight;
		}
		if (Direction > 67.5f && Direction <= 112.5f)
		{
			return EProject_JStateControllerStrafeDirection::Right;
		}
		if (Direction > 112.5f && Direction <= 157.5f)
		{
			return EProject_JStateControllerStrafeDirection::BackwardRight;
		}
		return EProject_JStateControllerStrafeDirection::Backward;
	}

	inline EProject_JStateControllerPresentationState ResolveStateControllerPresentationState(
		const FProject_JAnimThreadSafeData& Data,
		const FProject_JAnimOneShotPresentationThreadSafeData& OneShot)
	{
		if (!OneShot.bEnabled)
		{
			return EProject_JStateControllerPresentationState::Disabled;
		}

		const bool bIsJumpingOrJumpStart = Data.Air.bIsJumping ||
			Data.Air.bIsFallOffStart ||
			OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::JumpStart ||
			OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::Fall;

		if (Data.Air.bIsInAir || bIsJumpingOrJumpStart)
		{
			return (Data.Air.bIsJumping || Data.Air.bIsFallOffStart || OneShot.PhaseFamily == EProject_JLocomotionPhaseFamily::JumpStart)
				? EProject_JStateControllerPresentationState::TransitionToInAir
				: EProject_JStateControllerPresentationState::InAirLoop;
		}

		if (Data.Landing.bIsLanding)
		{
			return EProject_JStateControllerPresentationState::TransitionToLand;
		}

		if (OneShot.bRequested)
		{
			switch (OneShot.PhaseFamily)
			{
			case EProject_JLocomotionPhaseFamily::Start:
				// A release/reversal can happen before the component's phase has
				// settled from Start/Pivot to Stop.  Let the trajectory-derived
				// intent preempt it so the State Controller can select a Stop now.
				return Data.LocomotionContext.bIsMotionMatchingMoving
					? EProject_JStateControllerPresentationState::TransitionToLocomotion
					: EProject_JStateControllerPresentationState::TransitionToIdle;
			case EProject_JLocomotionPhaseFamily::Pivot:
				// A committed Pivot is presentation-owned. Stop is queued until its
				// authored direct asset reaches its normal exit window.
				return EProject_JStateControllerPresentationState::TransitionToLocomotion;
			case EProject_JLocomotionPhaseFamily::Stop:
				return EProject_JStateControllerPresentationState::TransitionToIdle;
			case EProject_JLocomotionPhaseFamily::TurnInPlace:
				return (Data.LocomotionContext.TurnInPlaceDirectionBucket > 0 ||
						(Data.LocomotionContext.bShouldTurnInPlace && FMath::Abs(Data.LocomotionContext.DesiredFacingDeltaYaw) >= 30.0f))
					? EProject_JStateControllerPresentationState::TurnInPlace
					: EProject_JStateControllerPresentationState::IdleLoop;
			case EProject_JLocomotionPhaseFamily::Landing:
				return EProject_JStateControllerPresentationState::TransitionToLand;
			case EProject_JLocomotionPhaseFamily::JumpStart:
			case EProject_JLocomotionPhaseFamily::Fall:
				return EProject_JStateControllerPresentationState::TransitionToInAir;
			default:
				break;
			}
		}

		if (Data.LocomotionContext.bShouldTurnInPlace &&
			(FMath::Abs(Data.LocomotionContext.DesiredFacingDeltaYaw) >= 30.0f ||
			 Data.LocomotionContext.TurnInPlaceDirectionBucket > 0))
		{
			return EProject_JStateControllerPresentationState::TurnInPlace;
		}

		return Data.LocomotionContext.bIsMotionMatchingMoving
			? EProject_JStateControllerPresentationState::LocomotionLoop
			: EProject_JStateControllerPresentationState::IdleLoop;
	}

	inline auto IsTransitionState = [](const EProject_JStateControllerPresentationState State)
	{
		return State == EProject_JStateControllerPresentationState::TransitionToLocomotion ||
			State == EProject_JStateControllerPresentationState::TransitionToIdle ||
			State == EProject_JStateControllerPresentationState::TransitionToInAir ||
			State == EProject_JStateControllerPresentationState::TransitionToLand ||
			State == EProject_JStateControllerPresentationState::TurnInPlace;
	};

	inline bool ShouldStateControllerPresentationLoop(const EProject_JStateControllerPresentationState PresentationState)
	{
		switch (PresentationState)
		{
		case EProject_JStateControllerPresentationState::IdleLoop:
		case EProject_JStateControllerPresentationState::LocomotionLoop:
		case EProject_JStateControllerPresentationState::InAirLoop:
			return true;
		case EProject_JStateControllerPresentationState::TurnInPlace:
		default:
			return false;
		}
	}
}
