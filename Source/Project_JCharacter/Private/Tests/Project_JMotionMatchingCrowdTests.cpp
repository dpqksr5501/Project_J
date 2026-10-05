#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "UObject/UnrealType.h"
#include <limits>

namespace
{
FMotionMatchingState& MutableState(FAnimNode_MotionMatching& Node)
{
	auto* Property = FindFProperty<FStructProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("MotionMatchingState"));
	check(Property);
	return *Property->ContainerPtrToValuePtr<FMotionMatchingState>(&Node);
}
float Throttle(const FAnimNode_MotionMatching& Node)
{
	auto* Property = FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("SearchThrottleTime"));
	check(Property);
	return Property->GetPropertyValue_InContainer(&Node);
}
void SetThrottle(FAnimNode_MotionMatching& Node, float Value)
{
	auto* Property = FindFProperty<FFloatProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("SearchThrottleTime"));
	check(Property);
	Property->SetPropertyValue_InContainer(&Node, Value);
}

struct FReentrantAnimationTestNode : FAnimNode_Base
{
	TFunction<void(const FAnimationUpdateContext&)> Update;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override { Update(Context); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMotionMatchingReturnRequestTest,
	"ProjectJ.MMCrowd.ReturnRequestLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMotionMatchingReturnRequestTest::RunTest(const FString&)
{
	FProject_JCharacterAnimInstanceProxy Proxy;
	auto* Database = NewObject<UPoseSearchDatabase>();
	FProject_JAnimThreadSafeData Data;
	Data.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	Data.LocomotionContext.bIsMotionMatchingMoving = true;
	Data.OneShotPresentation.bShouldOverrideMotionMatching = true;
	Proxy.QueueGameThreadData(Data, Database, true, true, true);
	const uint64 EntryRequest = Proxy.PendingReselectRevision;
	Data.Movement.GroundSpeed = 250.0f;
	Data.MotionMatching.MinimumSearchInterval = 0.083f;
	Proxy.QueueGameThreadData(Data, Database, true, false, false, false);
	TestEqual(TEXT("Multiple snapshots retain an unprocessed request"), Proxy.PendingReselectRevision, EntryRequest);
	TestEqual(TEXT("Movement snapshot remains current"), Proxy.PendingGameThreadData.Movement.GroundSpeed, 250.0f);
	TestEqual(TEXT("Budget snapshot remains current"), Proxy.PendingGameThreadData.MotionMatching.MinimumSearchInterval, 0.083f);
	// A stale force pulse republished after a hidden skip must not create a new request.
	Proxy.QueueGameThreadData(Data, Database, true, false, true, false);
	TestEqual(TEXT("Same force pulse is coalesced"), Proxy.PendingReselectRevision, EntryRequest);
	Data.OneShotPresentation.bShouldOverrideMotionMatching = false;
	Proxy.QueueGameThreadData(Data, Database, true, false, false);
	TestTrue(TEXT("Natural override exit creates a return search"), Proxy.PendingReselectRevision > EntryRequest);
	TestTrue(TEXT("Return query uses the externally evaluated pose history"), Proxy.bReselectFromPoseHistory);
	TestEqual(TEXT("Return invalidates only the stale MM query pose"), Proxy.ResolveReselectInterruptMode(),
		EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose);
	const uint64 ReturnRequest = Proxy.PendingReselectRevision;
	Data.MotionMatching.SelectionRevision++;
	Proxy.QueueGameThreadData(Data, Database, true, true, true);
	TestTrue(TEXT("New semantic force merges with the return"), Proxy.PendingReselectRevision > ReturnRequest);
	TestTrue(TEXT("Coalescing retains an unconsumed history requirement"), Proxy.bReselectFromPoseHistory);
	const uint64 PreviousPulse = Proxy.PendingReselectRevision;
	Proxy.QueueGameThreadData(Data, Database, true, false, false);
	Proxy.QueueGameThreadData(Data, Database, true, false, true);
	TestTrue(TEXT("A new input force pulse works even with the same semantic revision"), Proxy.PendingReselectRevision > PreviousPulse);
	Data.LocomotionMode = EProject_JAnimationLocomotionMode::Mounted;
	Proxy.QueueGameThreadData(Data, nullptr, false, false, false);
	TestEqual(TEXT("Mount cancels stale on-foot requests"), Proxy.PendingReselectRevision, uint64(0));
	TestFalse(TEXT("Mount releases the pending force flag"), Proxy.bForceMotionMatchingReselect);
	Data.LocomotionMode = EProject_JAnimationLocomotionMode::OnFoot;
	Proxy.QueueGameThreadData(Data, Database, true, true, true);
	Proxy.ThreadSafeData = Data;
	TestFalse(TEXT("Fresh generic reselect does not inherit an old return query"), Proxy.bReselectFromPoseHistory);
	Proxy.ThreadSafeData.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Turn;
	Proxy.ThreadSafeData.LocomotionContext.bIsMotionMatchingMoving = true;
	TestEqual(TEXT("Moving redirects keep continuing pose features"), Proxy.ResolveReselectInterruptMode(),
		EPoseSearchInterruptMode::ForceInterrupt);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMotionMatchingSearchExecutionTest,
	"ProjectJ.MMCrowd.ActualEngineSearch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMotionMatchingSearchExecutionTest::RunTest(const FString&)
{
	auto* Mesh = NewObject<USkeletalMeshComponent>();
	auto* Anim = NewObject<UAnimInstance>(Mesh);
	FAnimInstanceProxy EngineProxy(Anim);
	FAnimationUpdateSharedContext SharedContext;
	FAnimationUpdateContext Context(&EngineProxy, 1.0f / 60.0f, &SharedContext);
	FProject_JCharacterAnimInstanceProxy Proxy;
	Proxy.PendingReselectRevision = 1;
	Proxy.bForceMotionMatchingReselect = true;
	FAnimNode_MotionMatching Node;
	FAnimNode_PoseSearchHistoryCollector History;
	History.Source.SetLinkNode(&Node);
	History.bGenerateTrajectory = false;
	History.Initialize_AnyThread(FAnimationInitializeContext(&EngineProxy));
	SetThrottle(Node, 0.083f);
	MutableState(Node).ElapsedPoseSearchTime = 0.04f;
	FProject_JCharacterAnimInstanceProxy::FNodeReselectState State;
	Proxy.ArmMotionMatchingReselect(Node, State);
	TestEqual(TEXT("Requested search bypasses the budget throttle"), Throttle(Node), 0.0f);
	TestFalse(TEXT("Inactive node cannot acknowledge a search"), Proxy.CompleteMotionMatchingReselect(Node, State));
	TestEqual(TEXT("Inactive node retains its actual search timer"), MutableState(Node).ElapsedPoseSearchTime, 0.04f);
	TestEqual(TEXT("Inactive node restores its normal throttle"), Throttle(Node), 0.083f);
	Proxy.ArmMotionMatchingReselect(Node, State);
	AddExpectedError(TEXT("missing IPoseHistory"), EAutomationExpectedErrorFlags::Contains, 1);
	Node.Update_AnyThread(Context);
	TestFalse(TEXT("An actual update without history is not a search"), Proxy.CompleteMotionMatchingReselect(Node, State));
	// Execute the real engine history-provider -> MM -> MotionMatch path. The
	// candidate list is intentionally empty: unchanged result is still a search.
	Proxy.ArmMotionMatchingReselect(Node, State);
	History.Update_AnyThread(Context);
	TestTrue(TEXT("Actual engine search acknowledges an unchanged empty result"), Proxy.CompleteMotionMatchingReselect(Node, State));
	TestEqual(TEXT("Search restores the crowd interval"), Throttle(Node), 0.083f);
	TestEqual(TEXT("Search acknowledges its own revision"), State.HandledRevision, uint64(1));
	Proxy.ArmMotionMatchingReselect(Node, State);
	TestEqual(TEXT("Handled node does not repeat the forced search"), State.ArmedRevision, uint64(0));
	FAnimNode_MotionMatching OtherNode;
	FProject_JCharacterAnimInstanceProxy::FNodeReselectState OtherState;
	Proxy.ArmMotionMatchingReselect(OtherNode, OtherState);
	TestEqual(TEXT("Another inactive node retains its own request"), OtherState.ArmedRevision, uint64(1));
	Proxy.CompleteMotionMatchingReselect(OtherNode, OtherState);
	MutableState(OtherNode).SearchResult.bIsInteraction = true;
	Proxy.ArmMotionMatchingReselect(OtherNode, OtherState);
	TestEqual(TEXT("Active interaction retains selection ownership"), OtherState.ArmedRevision, uint64(0));
	Proxy.ClearMotionMatchingReselects();
	Proxy.ArmMotionMatchingReselect(OtherNode, OtherState);
	TestEqual(TEXT("Cancelled request cannot arm an inactive node later"), OtherState.ArmedRevision, uint64(0));
	// Trace only real forced-search acknowledgements, including unchanged results.
	Proxy.SetFlowTraceEnabled(true);
	Proxy.PendingReselectRevision = 2;
	Proxy.bForceMotionMatchingReselect = true;
	Proxy.NativePoseHistoryNode.Initialize_AnyThread(FAnimationInitializeContext(&EngineProxy));
	Proxy.ArmMotionMatchingReselect(Proxy.NativeMotionMatchingNode, Proxy.NativeReselectState);
	Proxy.CompleteMotionMatchingReselects();
	TestEqual(TEXT("Diagnostics do not count an inactive node as a search"), Proxy.GetFlowTraceWork().ReselectSearches, uint64(0));
	Proxy.ArmMotionMatchingReselect(Proxy.NativeMotionMatchingNode, Proxy.NativeReselectState);
	Proxy.NativePoseHistoryNode.Update_AnyThread(Context);
	Proxy.CompleteMotionMatchingReselects();
	TestEqual(TEXT("Diagnostics count an actual unchanged-result search"), Proxy.GetFlowTraceWork().ReselectSearches, uint64(1));
	TestEqual(TEXT("Diagnostics retain the searched request identity"), Proxy.GetFlowTraceWork().LastSearchRequest, uint64(2));
	Proxy.SetFlowTraceEnabled(false);
	TestEqual(TEXT("Disabling diagnostics resets session counters"), Proxy.GetFlowTraceWork().ReselectSearches, uint64(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMotionMatchingNestedGraphTest,
	"ProjectJ.MMCrowd.NestedGraphSearch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMotionMatchingNestedGraphTest::RunTest(const FString&)
{
	auto* Mesh = NewObject<USkeletalMeshComponent>();
	auto* Anim = NewObject<UAnimInstance>(Mesh);
	FAnimInstanceProxy EngineProxy(Anim);
	FAnimationUpdateSharedContext SharedContext;
	FAnimationUpdateContext Context(&EngineProxy, 1.0f / 60.0f, &SharedContext);
	FProject_JCharacterAnimInstanceProxy Proxy;
	Proxy.CurrentActiveDatabase = NewObject<UPoseSearchDatabase>();
	Proxy.bUpdateMotionMatchingThisFrame = false;
	Proxy.PendingReselectRevision = 1;
	Proxy.bForceMotionMatchingReselect = true;
	Proxy.bReselectFromPoseHistory = true;
	Proxy.SetFlowTraceEnabled(true);
	SetThrottle(Proxy.NativeMotionMatchingNode, 0.083f);
	Proxy.ThreadSafeData.MotionMatching.MinimumSearchInterval = 0.083f;
	Proxy.NativePoseHistoryNode.Initialize_AnyThread(FAnimationInitializeContext(&EngineProxy));
	FReentrantAnimationTestNode Nested;
	Nested.Update = [&](const FAnimationUpdateContext&)
	{
		TestEqual(TEXT("Linked layer runs inside the still-armed outer search"), Proxy.NativeReselectState.ArmedRevision, Proxy.PendingReselectRevision);
	};
	FReentrantAnimationTestNode Outer;
	Outer.Update = [&](const FAnimationUpdateContext& UpdateContext)
	{
		Proxy.UpdateAnimationNode_WithRoot(UpdateContext, &Nested, TEXT("LinkedLayer"));
		TestEqual(TEXT("Linked layer cannot disarm before the outer saved pose updates"), Proxy.NativeReselectState.ArmedRevision, uint64(1));
		TestEqual(TEXT("Temporary search bypass survives linked-layer completion"), Throttle(Proxy.NativeMotionMatchingNode), 0.0f);
		Proxy.NativePoseHistoryNode.Update_AnyThread(UpdateContext);
	};
	Proxy.UpdateAnimationNode_WithRoot(Context, &Outer, TEXT("AnimGraph"));
	TestEqual(TEXT("Actual engine search is acknowledged after the complete graph"), Proxy.NativeReselectState.HandledRevision, uint64(1));
	TestFalse(TEXT("Fulfilled request releases force policy"), Proxy.bForceMotionMatchingReselect);
	TestEqual(TEXT("Crowd throttle is restored after the outer traversal"), Throttle(Proxy.NativeMotionMatchingNode), 0.083f);
	TestEqual(TEXT("One graph traversal owns search completion"), Proxy.GetFlowTraceWork().Traversals, uint64(1));
	TestEqual(TEXT("Nested graph execution remains observable"), Proxy.GetFlowTraceWork().NestedTraversals, uint64(1));
	TestEqual(TEXT("Same empty result is still counted once as a real search"), Proxy.GetFlowTraceWork().ReselectSearches, uint64(1));
	TestTrue(TEXT("Outer return search preserves its history query identity"), Proxy.GetFlowTraceWork().bLastSearchFromHistory);
	TestFalse(TEXT("Reentry guard is released after graph execution"), Proxy.bUpdatingMotionMatchingGraph);
	Proxy.PendingReselectRevision = 2;
	Proxy.bForceMotionMatchingReselect = true;
	Outer.Update = [&](const FAnimationUpdateContext& UpdateContext)
	{
		Proxy.UpdateAnimationNode_WithRoot(UpdateContext, &Nested, TEXT("LinkedLayer"));
		// The MM branch stays inactive in this traversal.
	};
	Proxy.UpdateAnimationNode_WithRoot(Context, &Outer, TEXT("AnimGraph"));
	TestTrue(TEXT("An inactive outer traversal keeps the pending request"), Proxy.bForceMotionMatchingReselect);
	TestEqual(TEXT("Inactive traversal does not invent a search completion"), Proxy.NativeReselectState.HandledRevision, uint64(1));
	TestEqual(TEXT("Inactive traversal leaves the search count unchanged"), Proxy.GetFlowTraceWork().ReselectSearches, uint64(1));
	TestEqual(TEXT("Inactive traversal restores its normal budget"), Throttle(Proxy.NativeMotionMatchingNode), 0.083f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMotionMatchingCrowdPolicyTest,
	"ProjectJ.MMCrowd.WorkerSearchPolicy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMotionMatchingCrowdPolicyTest::RunTest(const FString&)
{
	FProject_JMotionMatchingSearchPolicy Policy;
	const auto Cycle = EProject_JLocomotionPhaseFamily::Cycle;
	TestEqual(TEXT("Local/Near retain the authored node throttle"), Policy.ResolveSearchThrottleTime(Cycle, false, 0.05f, false, 0.0f), 0.05f);
	TestEqual(TEXT("Mid never increases search frequency above the node policy"), Policy.ResolveSearchThrottleTime(Cycle, false, 0.05f, false, 0.033f), 0.05f);
	TestEqual(TEXT("Far limits real worker searches"), Policy.ResolveSearchThrottleTime(Cycle, false, 0.05f, false, 0.083f), 0.083f);
	TestEqual(TEXT("Hidden retains its search floor"), Policy.ResolveSearchThrottleTime(Cycle, false, 0.05f, false, 0.10f), 0.10f);
	TestEqual(TEXT("Bad floor cannot poison worker timing"), Policy.ResolveSearchThrottleTime(Cycle, false, 0.05f, false,
		std::numeric_limits<float>::quiet_NaN()), 0.05f);
	Policy.bSearchStartEveryUpdate = false;
	TestEqual(TEXT("Phase suppression survives distance policy"), Policy.ResolveSearchThrottleTime(
		EProject_JLocomotionPhaseFamily::Start, false, 0.05f, false, 0.083f), Policy.SuppressedSearchThrottleTime);
	FProject_JCharacterAnimInstanceProxy Proxy;
	Proxy.ThreadSafeData.MotionMatching.MinimumSearchInterval = 0.083f;
	Proxy.ThreadSafeData.LocomotionContext.PhaseFamily = Cycle;
	SetThrottle(Proxy.NativeMotionMatchingNode, 0.05f);
	Proxy.ApplyMotionMatchingSearchPolicy();
	TestEqual(TEXT("Snapshot budget is applied to the real native node"), Throttle(Proxy.NativeMotionMatchingNode), 0.083f);
	Proxy.PendingReselectRevision = 1;
	Proxy.ForceReselectMotionMatchingNodes();
	TestEqual(TEXT("Urgent search takes precedence after normal policy"), Throttle(Proxy.NativeMotionMatchingNode), 0.0f);
	Proxy.CompleteMotionMatchingReselects();
	TestEqual(TEXT("An inactive update restores the policy without consuming"), Throttle(Proxy.NativeMotionMatchingNode), 0.083f);
	TestEqual(TEXT("Pending hidden request survives a skipped graph"), Proxy.NativeReselectState.HandledRevision, uint64(0));
	Proxy.ThreadSafeData.MotionMatching.MinimumSearchInterval = 0.0f;
	Proxy.ApplyMotionMatchingSearchPolicy();
	TestEqual(TEXT("Visibility wake restores the original quality throttle"), Throttle(Proxy.NativeMotionMatchingNode), 0.05f);
	return true;
}
#endif
