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

	struct FHoldUpdate
	{
		EProject_JStateControllerPresentationState DesiredState = EProject_JStateControllerPresentationState::Disabled;
		EProject_JStateControllerPresentationState PreviousState = EProject_JStateControllerPresentationState::Disabled;
		bool bNewLanding = false;
		bool bStartedTransition = false;
		bool bNaturalContinuation = false;
	};

	enum class EPivotInterruption : uint8
	{
		None, Stop, Superseded, Redirect
	};

	struct FPivotIntent
	{
		bool bStopRequested = false;
		bool bHasMoveInput = false;
		bool bIsPivoting = false;
		bool bInAir = false;
		bool bLanding = false;
		int32 RequestRevision = 0;
		int32 MoveIntentRevision = 0;
		EProject_JLocomotionPhaseFamily PhaseFamily = EProject_JLocomotionPhaseFamily::Idle;
	};

	struct FPivotUpdate
	{
		EPivotInterruption Interruption = EPivotInterruption::None;
		int32 PreviousRequestRevision = 0;
		int32 PreviousMoveIntentRevision = 0;
		bool bForceCycle = false;
		bool bKeepCommittedPivot = false;
	};

	struct FMountBoundary
	{
		int32 PivotRequestRevision = 0;
		int32 LandingRevision = INDEX_NONE;
		int32 TurnSequence = 0;
		bool bLanding = false;
		bool bLocalTurn = false;
		bool bHasMoveInput = false;
	};

	void Reset()
	{
		*this = FProject_JStateControllerRuntime();
	}

	void OnMountBoundary(const FMountBoundary& Boundary)
	{
		Reset();
		Pivot.SuppressedRequestRevision = Boundary.PivotRequestRevision;
		if (Boundary.bLanding) { HeldLandingPresentationRevision = Boundary.LandingRevision; }
		ConsumeTurnSequence(Boundary.bLocalTurn, Boundary.TurnSequence);
		bGroundStopConsumed = !Boundary.bHasMoveInput;
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

	static bool IsTransitionState(EProject_JStateControllerPresentationState State)
	{
		return State == EProject_JStateControllerPresentationState::TransitionToLocomotion ||
			State == EProject_JStateControllerPresentationState::TransitionToIdle ||
			State == EProject_JStateControllerPresentationState::TransitionToInAir ||
			State == EProject_JStateControllerPresentationState::TransitionToLand ||
			State == EProject_JStateControllerPresentationState::TurnInPlace;
	}

	static bool IsNaturalLoopContinuation(EProject_JStateControllerPresentationState Held,
		EProject_JStateControllerPresentationState Desired)
	{
		return (Held == EProject_JStateControllerPresentationState::TransitionToLocomotion && Desired == EProject_JStateControllerPresentationState::LocomotionLoop) ||
			(Held == EProject_JStateControllerPresentationState::TransitionToIdle && Desired == EProject_JStateControllerPresentationState::IdleLoop) ||
			(Held == EProject_JStateControllerPresentationState::TransitionToInAir && Desired == EProject_JStateControllerPresentationState::InAirLoop) ||
			(Held == EProject_JStateControllerPresentationState::TransitionToLand &&
				(Desired == EProject_JStateControllerPresentationState::LocomotionLoop || Desired == EProject_JStateControllerPresentationState::IdleLoop ||
					Desired == EProject_JStateControllerPresentationState::TurnInPlace)) ||
			(Held == EProject_JStateControllerPresentationState::TurnInPlace &&
				(Desired == EProject_JStateControllerPresentationState::IdleLoop || Desired == EProject_JStateControllerPresentationState::TurnInPlace));
	}

	FHoldUpdate BeginDesiredHold(EProject_JStateControllerPresentationState Desired, const FIntent& Intent, double NowSeconds)
	{
		FHoldUpdate Result;
		Result.DesiredState = PrepareDesiredState(Desired, Intent);
		Result.PreviousState = PlaybackHoldState;
		Result.bNewLanding = Result.DesiredState == EProject_JStateControllerPresentationState::TransitionToLand &&
			Intent.LandingPresentationRevision != HeldLandingPresentationRevision;
		const bool bHeldTransition = IsTransitionState(PlaybackHoldState);
		Result.bNaturalContinuation = bHeldTransition && IsNaturalLoopContinuation(PlaybackHoldState, Result.DesiredState);
		const bool bStart = Result.bNewLanding || !bHeldTransition ||
			(!Result.bNaturalContinuation && Result.DesiredState != PlaybackHoldState);
		if (bStart)
		{
			BeginHold(Result.DesiredState, NowSeconds);
			if (Result.DesiredState == EProject_JStateControllerPresentationState::TurnInPlace && Intent.TurnInPlaceSequence > 0)
			{
				ConsumeTurnSequence(Intent.bIsLocallyControlled, Intent.TurnInPlaceSequence);
			}
			if (Result.bNewLanding)
			{
				HeldLandingPresentationRevision = Intent.LandingPresentationRevision;
			}
			Result.bStartedTransition = IsTransitionState(Result.DesiredState);
		}
		return Result;
	}

	void BeginHold(EProject_JStateControllerPresentationState State, double NowSeconds)
	{
		PlaybackHoldState = State;
		PlaybackHoldStartedAtSeconds = NowSeconds;
		if (State == EProject_JStateControllerPresentationState::TransitionToIdle)
		{
			bGroundStopConsumed = true;
		}
	}

	void SetFallbackHold(EProject_JStateControllerPresentationState State, double NowSeconds)
	{
		PlaybackHoldState = State;
		PlaybackHoldStartedAtSeconds = NowSeconds;
	}

	void InvalidateHold(double NowSeconds, bool bClearTurnReselect = true)
	{
		SetFallbackHold(EProject_JStateControllerPresentationState::Disabled, NowSeconds);
		if (bClearTurnReselect) { bForceTurnInPlaceReselect = false; }
	}

	void OnFullBodyActionStart(double NowSeconds)
	{
		Pivot.Cancel();
		InvalidateHold(NowSeconds);
	}

	void OnFullBodyActionEnd(double NowSeconds)
	{
		InvalidateHold(NowSeconds);
	}

	void OnCombatPresentationBoundary(double NowSeconds, int32 LandingRevision, bool bDiscardingLanding)
	{
		Pivot.Cancel();
		InvalidateHold(NowSeconds);
		if (bDiscardingLanding && LandingRevision != INDEX_NONE)
		{
			HeldLandingPresentationRevision = LandingRevision;
		}
	}

	void RestartTurnSequence(bool bLocal, int32 Sequence, double NowSeconds)
	{
		ConsumeTurnSequence(bLocal, Sequence);
		BeginHold(EProject_JStateControllerPresentationState::TurnInPlace, NowSeconds);
		bForceTurnInPlaceReselect = true;
	}

	bool HasNewTurnSequence(bool bLocal, int32 Sequence) const
	{
		return Sequence > 0 && Sequence > (bLocal ? LastHandledLocalTurnSequence : LastHandledRemoteTurnSequence);
	}

	FPivotUpdate ReconcilePivot(const FPivotIntent& Intent, double NowSeconds)
	{
		FPivotUpdate Result;
		Result.PreviousRequestRevision = Pivot.RequestRevision;
		Result.PreviousMoveIntentRevision = Pivot.MoveIntentRevision;
		const bool bActive = Pivot.RequestRevision != 0;
		const bool bSuperseded = bActive && Intent.bIsPivoting && Intent.RequestRevision != 0 &&
			Intent.RequestRevision != Pivot.RequestRevision;
		if (bActive && Intent.bStopRequested) { Result.Interruption = EPivotInterruption::Stop; }
		else if (bSuperseded) { Result.Interruption = EPivotInterruption::Superseded; }
		else if (bActive && Intent.bHasMoveInput && Pivot.MoveIntentRevision != 0 &&
			Intent.MoveIntentRevision != Pivot.MoveIntentRevision)
		{
			Result.Interruption = EPivotInterruption::Redirect;
		}
		const bool bAlreadySuppressed = !bActive && Intent.RequestRevision != 0 &&
			Intent.RequestRevision == Pivot.SuppressedRequestRevision &&
			Intent.PhaseFamily == EProject_JLocomotionPhaseFamily::Pivot;
		if (Result.Interruption != EPivotInterruption::None)
		{
			Pivot.Cancel();
			InvalidateHold(NowSeconds, false);
			if (Result.Interruption == EPivotInterruption::Redirect)
			{
				Pivot.SuppressedRequestRevision = Intent.RequestRevision;
			}
		}
		Result.bForceCycle = Result.Interruption == EPivotInterruption::Redirect || bAlreadySuppressed;
		Result.bKeepCommittedPivot = Pivot.RequestRevision != 0 && !Intent.bInAir && !Intent.bLanding;
		return Result;
	}

	void CancelPivot() { Pivot.Cancel(); }
	void CancelPivotAndHold(double NowSeconds) { Pivot.Cancel(); InvalidateHold(NowSeconds); }
	bool CommitPivot(int32 RequestRevision, int32 MoveIntentRevision, const FVector& PreviousDirection, const FVector& IntentDirection)
	{
		if (Pivot.RequestRevision == RequestRevision && Pivot.MoveIntentRevision == MoveIntentRevision) { return false; }
		Pivot.Commit(RequestRevision, MoveIntentRevision, PreviousDirection, IntentDirection);
		return true;
	}
	bool IsPivotRequestSuppressed(int32 Revision) const { return Revision != 0 && Revision == Pivot.SuppressedRequestRevision; }
	bool HasCommittedPivot() const { return Pivot.RequestRevision != 0; }
	const FPivotPlayback& GetPivot() const { return Pivot; }
	EProject_JStateControllerPresentationState GetHeldState() const { return PlaybackHoldState; }
	float GetHoldElapsed(double NowSeconds) const { return FMath::Max(static_cast<float>(NowSeconds - PlaybackHoldStartedAtSeconds), 0.0f); }
	void AlignHoldToAssetTime(double NowSeconds, float AssetTime) { PlaybackHoldStartedAtSeconds = NowSeconds - AssetTime; }
	bool IsTurnReselectRequested() const { return bForceTurnInPlaceReselect; }
	void ClearTurnReselect() { bForceTurnInPlaceReselect = false; }
	bool IsGroundStopConsumed() const { return bGroundStopConsumed; }
	int32 GetHeldLandingRevision() const { return HeldLandingPresentationRevision; }
	int32 GetLastTurnSequence(bool bLocal) const { return bLocal ? LastHandledLocalTurnSequence : LastHandledRemoteTurnSequence; }

private:
	EProject_JStateControllerPresentationState PlaybackHoldState = EProject_JStateControllerPresentationState::Disabled;
	double PlaybackHoldStartedAtSeconds = 0.0;
	bool bGroundStopConsumed = false;
	int32 HeldLandingPresentationRevision = INDEX_NONE;
	int32 LastHandledLocalTurnSequence = 0;
	int32 LastHandledRemoteTurnSequence = 0;
	bool bForceTurnInPlaceReselect = false;
	FPivotPlayback Pivot;
};
