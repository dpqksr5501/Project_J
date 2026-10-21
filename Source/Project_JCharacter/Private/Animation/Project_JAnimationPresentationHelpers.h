#pragma once
#include "Project_JLocomotionAnimTypes.h"

namespace ProjectJ::Animation::Presentation
{
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

}
