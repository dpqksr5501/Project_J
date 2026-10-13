#pragma once

#include "PoseSearch/AnimNode_MotionMatching.h"

struct FProject_JMotionMatchingSearchPolicy;

namespace ProjectJ::Animation
{
// All UE-version-specific reflected MM access lives here. No private offsets,
// asset mutation, search dispatch or UObject ownership is introduced.
float GetSearchThrottleTime(const FAnimNode_MotionMatching& Node);
void SetSearchThrottleTime(FAnimNode_MotionMatching& Node, float Seconds);
FMotionMatchingState* GetMutableMotionMatchingState(FAnimNode_MotionMatching& Node);
void ApplyMotionMatchingPresentationPolicy(FAnimNode_MotionMatching& Node,
	const FProject_JMotionMatchingSearchPolicy& Policy, bool bInAir, bool bWasInAir, float VerticalSpeed);

// Nested graph scopes restore exactly the settings they observed on entry.
// Authored return forces one search; subsequent return reselections keep its interval.
class FScopedMotionMatchingReturn
{
public:
	FScopedMotionMatchingReturn(FAnimNode_MotionMatching& InNode, float Seconds, bool bInertial, bool bForceSearch = true);
	~FScopedMotionMatchingReturn();
	FScopedMotionMatchingReturn(const FScopedMotionMatchingReturn&) = delete;
	FScopedMotionMatchingReturn& operator=(const FScopedMotionMatchingReturn&) = delete;
private:
	FAnimNode_MotionMatching& Node;
	bool bReset, bOldInertial;
	float Blend, Throttle;
};
}
