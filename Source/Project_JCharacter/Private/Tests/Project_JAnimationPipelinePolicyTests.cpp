#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JAnimationQualityPolicy.h"
#include "Animation/Project_JMotionMatchingBoundaryPolicy.h"
#include "Animation/Project_JMotionMatchingEngineAdapter.h"
#include "Animation/Project_JPlaybackReturnPolicy.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPipelineContracts, "ProjectJ.Animation.Pipeline.PolicyContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPipelineContracts::RunTest(const FString&)
{
	using namespace ProjectJ::Animation;
	FProject_JAnimThreadSafeData Old, Next;
	Old.OneShotPresentation.bShouldOverrideMotionMatching = true;
	Next.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	Next.LocomotionContext.bIsMotionMatchingMoving = true;
	FSearchPublication Publication{Old, Next};
	auto Boundary = ResolveSearchBoundary(Publication);
	TestTrue(TEXT("Authored return preserves a history search boundary"), Boundary.bRequest && Boundary.bFromHistory);
	Old = Next; Publication.bFresh = false; Publication.bForce = true;
	TestFalse(TEXT("Republishing a skipped snapshot is not a new force pulse"), ResolveSearchBoundary(Publication).bRequest);
	Publication.bFresh = true;
	TestTrue(TEXT("Fresh explicit pulse requests search"), ResolveSearchBoundary(Publication).bRequest);
	Publication.bForce = false;
	Old.MotionMatching.SelectionContext.bGeneralTurnCandidates = true;
	Publication.bFreshCycleResult = true;
	TestFalse(TEXT("A completed Cycle winner is not retired with its old permission"), ResolveSearchBoundary(Publication).bRetireContinuation);
	Publication.bFreshCycleResult = false;
	TestTrue(TEXT("Stale turn result is retired"), ResolveSearchBoundary(Publication).bRetireContinuation);
	Publication.bEnabled = false;
	TestTrue(TEXT("Disabled MM clears pending search ownership"), ResolveSearchBoundary(Publication).bClear);

	FQualityObservation Quality; FProject_JAnimationBudgetSettings Settings;
	TestEqual(TEXT("No owner hides animation data"), ResolveSourceQuality(Quality, Settings).Tier, EProject_JAnimBudgetTier::Hidden);
	Quality.bHasOwner = Quality.bLocal = true;
	TestEqual(TEXT("Local owner preserves full presentation"), ResolveSourceQuality(Quality, Settings).Tier, EProject_JAnimBudgetTier::Local);
	Quality.bLocal = false; Quality.bGameplayPose = true;
	TestTrue(TEXT("Gameplay pose outranks hidden visual state"), ResolveSourceQuality(Quality, Settings).bEnableHandIK);
	Quality.bGameplayPose = false; Quality.bRecentlyRendered = Quality.bHasSignificance = true; Quality.Significance = 2;
	const auto Far = ResolveSourceQuality(Quality, Settings);
	TestTrue(TEXT("Far preserves follower evaluation but reduces optional IK"), Far.bEnableFollowerRetarget && !Far.bEnableHandIK && !Far.bEnableFootIK);
	TestEqual(TEXT("Protected visuals retain full rate at any distance"), ResolveVisualInterval(true, false, 2, true), 0.f);
	TestEqual(TEXT("Unprotected far visuals retain authored cadence"), ResolveVisualInterval(false, false, 2, false), 1.f/15);

	FInertialReturnRuntime Lifetime; Lifetime.Begin(.2f); Lifetime.Advance(.05f);
	TestTrue(TEXT("Return follows requested duration"), FMath::IsNearlyEqual(Lifetime.GetRemaining(), .15f));
	Lifetime.OnReselection(); TestEqual(TEXT("Reselection preserves original request duration"), Lifetime.GetRemaining(), .2f);
	Lifetime.Cancel(); TestFalse(TEXT("New owner cancels the prior return"), Lifetime.IsActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEngineAdapterScope, "ProjectJ.Animation.Pipeline.EngineAdapterScopes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJEngineAdapterScope::RunTest(const FString&)
{
	using namespace ProjectJ::Animation;
	FAnimNode_MotionMatching Node;
	SetSearchThrottleTime(Node, .4f);
	const auto* Blend = FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("BlendTime"));
	const auto* Inertial = FindFProperty<FBoolProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("bUseInertialBlend"));
	const float OriginalBlend = Blend->GetPropertyValue_InContainer(&Node);
	const bool bOriginalInertial = Inertial->GetPropertyValue_InContainer(&Node);
	{
		FScopedMotionMatchingReturn Outer(Node, .2f, true, false);
		TestEqual(TEXT("Return reselection retains regular search interval"), GetSearchThrottleTime(Node), .4f);
		{
			FScopedMotionMatchingReturn Inner(Node, .1f, false);
			TestEqual(TEXT("First return explicitly forces one search"), GetSearchThrottleTime(Node), 0.f);
		}
		TestEqual(TEXT("Nested scope restores outer throttle"), GetSearchThrottleTime(Node), .4f);
		TestEqual(TEXT("Nested scope restores outer blend"), Blend->GetPropertyValue_InContainer(&Node), .2f);
		TestTrue(TEXT("Nested scope restores outer inertial mode"), Inertial->GetPropertyValue_InContainer(&Node));
	}
	TestEqual(TEXT("All scopes restore node defaults"), Blend->GetPropertyValue_InContainer(&Node), OriginalBlend);
	TestEqual(TEXT("All scopes restore inertial defaults"), Inertial->GetPropertyValue_InContainer(&Node), bOriginalInertial);
	TestNotNull(TEXT("Engine search-completion state is available"), GetMutableMotionMatchingState(Node));
	return true;
}
#endif
