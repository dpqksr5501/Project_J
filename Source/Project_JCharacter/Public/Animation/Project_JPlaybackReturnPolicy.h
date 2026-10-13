#pragma once

#include "Animation/Project_JAnimationSnapshot.h"

namespace ProjectJ::Animation
{
// Presentation-only decisions. Physical landing, input, clips and network
// facts are immutable inputs; this policy neither searches nor owns UObjects.
inline bool CanOverlapAuthoredReturn(const FProject_JAnimThreadSafeData& Data,
	const FProject_JAnimOneShotPresentationThreadSafeData& Shot, bool bRootTurnEpisode, bool bConsoleEnabled)
{
	using P = EProject_JStateControllerPresentationState;
	const bool bEligible = (Shot.PresentationState == P::TransitionToLocomotion &&
		Shot.PhaseFamily != EProject_JLocomotionPhaseFamily::Pivot) ||
		Shot.PresentationState == P::TransitionToIdle || Shot.PresentationState == P::TransitionToLand;
	const bool bRecovery = Shot.PresentationState == P::TransitionToLand && Data.Landing.bIsLanding &&
		!Shot.bTransitionAnimationAlmostComplete && Data.MotionMatchingSearchPolicy.bUseInertialLandingInterruptReturn;
	return bEligible && !bRecovery && bConsoleEnabled && !bRootTurnEpisode &&
		Data.MotionMatchingSearchPolicy.bEnableLiveOneShotReturn && Data.LocomotionMode == EProject_JAnimationLocomotionMode::OnFoot &&
		!Data.Air.bIsInAir && !Data.Combat.bIsAttacking && !Data.Combat.bIsDodging && !Data.Combat.bIsHitReacting &&
		Data.ProceduralIK.FullBodyMontageWeight <= UE_SMALL_NUMBER &&
		Data.LocomotionContext.RotationMode == Shot.RotationMode;
}

inline bool CanContinueInertialReturn(const FProject_JAnimThreadSafeData& Data, bool bInteraction)
{
	return Data.MotionMatchingSearchPolicy.bUseInertialLandingInterruptReturn &&
		Data.MotionMatching.SelectionContext.bUseForwardCycle &&
		Data.LocomotionContext.PhaseFamily == EProject_JLocomotionPhaseFamily::Cycle &&
		!Data.Air.bIsInAir && !Data.Combat.bIsAttacking && !Data.Combat.bIsDodging && !Data.Combat.bIsHitReacting &&
		!(Data.ProceduralIK.FullBodyMontageWeight > UE_SMALL_NUMBER) && !bInteraction;
}

// Blend lifetime only, not a candidate lock/search timer. Each reselection uses
// the original requested duration; engine inertialization owns its deficit.
struct FInertialReturnRuntime
{
	void Reset() { Remaining = Duration = 0.f; }
	void Cancel() { Remaining = 0.f; }
	void Begin(float Seconds) { Remaining = Duration = Seconds; }
	void OnReselection() { Remaining = Duration; }
	void Advance(float Delta) { Remaining = FMath::Max(0.f, Remaining - Delta); }
	bool IsActive() const { return Remaining > UE_SMALL_NUMBER; }
	float GetRemaining() const { return Remaining; }
	float GetDuration() const { return Duration; }
private:
	float Remaining = 0.f, Duration = 0.f;
};
}
