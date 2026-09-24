#pragma once

#include "Project_JLocomotionAnimTypes.h"

/** 게임 스레드에서 저작된 이동 일회성 상태의 수명을 관리한다. 살아 있는 UObject는 조회하지 않는다. */
class FProject_JStateControllerRuntime
{
public:
	struct FPivotPlayback
	{
		int32 RequestRevision = 0;
		int32 MoveIntentRevision = 0;
		int32 SuppressedRequestRevision = 0;
		FVector PreviousMovementDirection = FVector::ZeroVector;
		FVector MoveIntentDirection = FVector::ZeroVector;

		void Cancel()
		{
			RequestRevision = 0;
			MoveIntentRevision = 0;
			PreviousMovementDirection = FVector::ZeroVector;
			MoveIntentDirection = FVector::ZeroVector;
		}

		void Commit(int32 InRequestRevision, int32 InMoveIntentRevision,
			const FVector& InPreviousDirection, const FVector& InIntentDirection)
		{
			RequestRevision = InRequestRevision;
			MoveIntentRevision = InMoveIntentRevision;
			PreviousMovementDirection = InPreviousDirection;
			MoveIntentDirection = InIntentDirection;
		}
	};

	struct FIntent
	{
		bool bHasMoveInput = false;
		bool bIsLocallyControlled = false;
		bool bIsMotionMatchingMoving = false;
		bool bIsInAir = false;
		bool bFullBodyActionOrRecentExit = false;
		EProject_JGroundMotionMode GroundMotionMode = EProject_JGroundMotionMode::Idle;
		int32 TurnInPlaceSequence = 0;
		int32 LandingPresentationRevision = INDEX_NONE;
	};

	void Reset()
	{
		*this = FProject_JStateControllerRuntime();
	}

	EProject_JStateControllerPresentationState PrepareDesiredState(
		EProject_JStateControllerPresentationState DesiredState, const FIntent& Intent)
	{
		if (Intent.bHasMoveInput ||
			(!Intent.bIsLocallyControlled && Intent.bIsMotionMatchingMoving &&
				(Intent.GroundMotionMode == EProject_JGroundMotionMode::Start ||
					Intent.GroundMotionMode == EProject_JGroundMotionMode::Locomotion)))
		{
			bGroundStopConsumed = false;
		}
		if (bGroundStopConsumed && DesiredState == EProject_JStateControllerPresentationState::TransitionToIdle)
		{
			DesiredState = EProject_JStateControllerPresentationState::IdleLoop;
		}

		const int32 LastTurnSequence = Intent.bIsLocallyControlled
			? LastHandledLocalTurnSequence : LastHandledRemoteTurnSequence;
		if (DesiredState == EProject_JStateControllerPresentationState::TurnInPlace &&
			PlaybackHoldState != EProject_JStateControllerPresentationState::TurnInPlace &&
			Intent.TurnInPlaceSequence > 0 && Intent.TurnInPlaceSequence <= LastTurnSequence)
		{
			DesiredState = EProject_JStateControllerPresentationState::IdleLoop;
		}

		if (Intent.bFullBodyActionOrRecentExit && !Intent.bHasMoveInput)
		{
			bGroundStopConsumed = true;
			if (DesiredState == EProject_JStateControllerPresentationState::TransitionToIdle)
			{
				DesiredState = EProject_JStateControllerPresentationState::IdleLoop;
			}
		}

		if (!bGroundStopConsumed && !Intent.bFullBodyActionOrRecentExit &&
			!Intent.bIsInAir && !Intent.bIsMotionMatchingMoving &&
			DesiredState == EProject_JStateControllerPresentationState::IdleLoop &&
			(PlaybackHoldState == EProject_JStateControllerPresentationState::TransitionToLocomotion ||
				PlaybackHoldState == EProject_JStateControllerPresentationState::LocomotionLoop))
		{
			DesiredState = EProject_JStateControllerPresentationState::TransitionToIdle;
		}

		if (DesiredState == EProject_JStateControllerPresentationState::TransitionToLand &&
			Intent.LandingPresentationRevision == HeldLandingPresentationRevision &&
			PlaybackHoldState != EProject_JStateControllerPresentationState::TransitionToLand)
		{
			DesiredState = Intent.bIsMotionMatchingMoving
				? EProject_JStateControllerPresentationState::LocomotionLoop
				: EProject_JStateControllerPresentationState::IdleLoop;
		}
		return DesiredState;
	}

	void ConsumeTurnSequence(bool bLocal, int32 Sequence)
	{
		int32& LastSequence = bLocal ? LastHandledLocalTurnSequence : LastHandledRemoteTurnSequence;
		LastSequence = FMath::Max(LastSequence, Sequence);
	}

	// 프레젠테이션 어댑터와 테스트가 공유하는 최소한의 값이다.
	EProject_JStateControllerPresentationState PlaybackHoldState = EProject_JStateControllerPresentationState::Disabled;
	double PlaybackHoldStartedAtSeconds = 0.0;
	bool bGroundStopConsumed = false;
	int32 HeldLandingPresentationRevision = INDEX_NONE;
	int32 LastHandledLocalTurnSequence = 0;
	int32 LastHandledRemoteTurnSequence = 0;
	bool bForceTurnInPlaceReselect = false;
	FPivotPlayback Pivot;
};
