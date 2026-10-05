#pragma once

#include "CoreMinimal.h"

class AActor;

/** Diagnostic identities, independent of gameplay state and animation clocks. */
struct FProject_JAnimationFlowKey
{
	int32 Phase = 0;
	int32 Presentation = 0;
	int32 Rotation = 0;
	int32 MoveRevision = 0;
	int32 LandingRevision = 0;
	int32 SelectionRevision = 0;
	FName Asset;
	bool bOverride = false;
	bool bHasInput = false;
	bool bLanding = false;
	bool bForce = false;
	bool operator==(const FProject_JAnimationFlowKey& Other) const
	{
		return Phase == Other.Phase && Presentation == Other.Presentation && Rotation == Other.Rotation &&
			MoveRevision == Other.MoveRevision && LandingRevision == Other.LandingRevision &&
			SelectionRevision == Other.SelectionRevision && Asset == Other.Asset && bOverride == Other.bOverride &&
			bHasInput == Other.bHasInput && bLanding == Other.bLanding && bForce == Other.bForce;
	}
};

/** Edges bypass sampling; periodic rows expire one second after the last edge/one-shot. */
struct FProject_JAnimationFlowSampler
{
	bool bInitialized = false;
	FProject_JAnimationFlowKey LastKey;
	float LastControlYaw = 0.0f;
	double LastSampleTime = -1.0;
	double FocusUntil = -1.0;
	bool ShouldRecord(const FProject_JAnimationFlowKey& Key, float ControlYaw, double Now, int32 Mode,
		bool bTransition, bool& OutEdge)
	{
		OutEdge = false;
		if (Mode <= 0) { *this = {}; return false; }
		if (bInitialized && Now < LastSampleTime) { *this = {}; }
		OutEdge = !bInitialized || !(Key == LastKey) ||
			FMath::Abs(FMath::FindDeltaAngleDegrees(LastControlYaw, ControlYaw)) >= 3.0f;
		if (OutEdge || bTransition) { FocusUntil = Now + 1.0; }
		LastKey = Key;
		bInitialized = true;
		if (OutEdge) { LastControlYaw = ControlYaw; }
		if (!OutEdge && (Mode < 2 && Now > FocusUntil)) { return false; }
		if (!OutEdge && Now - LastSampleTime < 0.1) { return false; }
		LastSampleTime = Now;
		return true;
	}
};

struct FProject_JAnimationFlowWork
{
	uint64 QueuedSnapshot = 0;
	uint64 ConsumedSnapshot = 0;
	uint64 Traversals = 0;
	uint64 NestedTraversals = 0;
	uint64 ReselectSearches = 0;
	uint64 LastSearchRequest = 0;
	uint64 LastSearchSnapshot = 0;
	bool bLastSearchFromHistory = false;
};

namespace Project_J::AnimationFlowDebug
{
PROJECT_JCHARACTER_API bool ShouldCapture(const AActor* Actor);
/** Call on the game thread, at an existing decision site. Never changes an animation request. */
PROJECT_JCHARACTER_API void Decision(const AActor* Actor, const TCHAR* Layer, const TCHAR* Reason);
}
