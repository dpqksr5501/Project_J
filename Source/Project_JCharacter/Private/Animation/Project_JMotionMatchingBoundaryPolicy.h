#pragma once
#include "Animation/Project_JAnimationSnapshot.h"

namespace ProjectJ::Animation
{
// GT captures pointer equality and completed-node freshness before calling this
// value-only policy. It never traverses a graph or acknowledges a search.
struct FSearchPublication
{
	const FProject_JAnimThreadSafeData& Previous;
	const FProject_JAnimThreadSafeData& Next;
	bool bWasEnabled = true, bEnabled = true, bFresh = true, bForce = false, bLastForce = false;
	bool bHasCurrentCompanion = false, bReturningToCompanion = false;
	bool bDatabaseChanged = false, bCompanionChanged = false, bHasDatabase = false, bHasCompanion = false;
	bool bUpdate = true, bFreshCycleResult = false;
};
struct FSearchBoundary
{
	bool bClear = false, bRequest = false, bFromHistory = false, bRetireContinuation = false;
};
inline FSearchBoundary ResolveSearchBoundary(const FSearchPublication& I)
{
	const auto& Old = I.Previous;
	const auto& Next = I.Next;
	const auto& O = Old.MotionMatching.SelectionContext;
	const auto& N = Next.MotionMatching.SelectionContext;
	const bool bIdle = I.bWasEnabled && I.bEnabled && !Next.Air.bIsInAir &&
		!Next.LocomotionContext.bIsMotionMatchingMoving && Next.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Idle &&
		(Old.LocomotionContext.bIsMotionMatchingMoving || Old.LocomotionContext.PhaseFamily != EProject_JLocomotionPhaseFamily::Idle);
	const bool bReturn = I.bWasEnabled && I.bEnabled && Old.OneShotPresentation.bShouldOverrideMotionMatching &&
		!Next.OneShotPresentation.bShouldOverrideMotionMatching;
	const bool bForce = I.bFresh && I.bForce && (!I.bLastForce || Next.MotionMatching.SelectionRevision != Old.MotionMatching.SelectionRevision);
	const bool bTurnReturn = O.bMovingTurn180 && N.bAllowTurnContinuation && I.bHasCurrentCompanion && I.bReturningToCompanion &&
		Old.LocomotionContext.RotationMode == Next.LocomotionContext.RotationMode &&
		Old.LocomotionContext.GaitIntent == Next.LocomotionContext.GaitIntent &&
		Next.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Cycle && Next.LocomotionContext.bIsMotionMatchingMoving &&
		!Next.Air.bIsInAir && !Next.OneShotPresentation.bShouldOverrideMotionMatching;
	const bool bRefaceNormalization = (O.bBackwardRefaceCandidates || O.bAllowBackwardRefaceContinuation) !=
		(N.bBackwardRefaceCandidates || N.bAllowBackwardRefaceContinuation);
	const bool bGeneralCancel = (O.bAllowGeneralTurnContinuation && !N.bAllowGeneralTurnContinuation) ||
		(O.bGeneralTurnCandidates && !N.bGeneralTurnCandidates && !N.bAllowGeneralTurnContinuation);
	const bool bRefaceCancel = (O.bAllowBackwardRefaceContinuation && !N.bAllowBackwardRefaceContinuation) ||
		(O.bBackwardRefaceCandidates && !N.bBackwardRefaceCandidates && !N.bAllowBackwardRefaceContinuation);
	const bool bForwardNormalization = I.bDatabaseChanged && (O.bUseForwardCycle || N.bUseForwardCycle);
	const bool bRetire = bForwardNormalization || bRefaceNormalization || (O.bAllowTurnContinuation && !N.bAllowTurnContinuation) ||
		((bGeneralCancel || bRefaceCancel) && !I.bFreshCycleResult);
	const bool bDatabase = I.bUpdate && !bTurnReturn &&
		((I.bHasDatabase && I.bDatabaseChanged) || (I.bHasCompanion && I.bCompanionChanged));
	return {!I.bEnabled || Next.LocomotionMode != EProject_JAnimationLocomotionMode::OnFoot,
		bForce || bIdle || bReturn || bDatabase || bRetire, bReturn || bForwardNormalization, bRetire};
}
}
