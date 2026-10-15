#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/AnimInstance.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"
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
	Proxy.ClearMotionMatchingReselects();
	Data.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	Data.MotionMatching.SelectionContext.bUseForwardCycle = true;
	Proxy.QueueGameThreadData(Data, NewObject<UPoseSearchDatabase>(), true, true, false);
	TestTrue(TEXT("Isolated forward statistics query current history at the boundary"), Proxy.bReselectFromPoseHistory);
	TestEqual(TEXT("Forward boundary retires differently normalized continuing cost"), Proxy.ResolveReselectInterruptMode(),
		EPoseSearchInterruptMode::ForceInterruptAndInvalidateContinuingPose);
	const uint64 ForwardRequest = Proxy.PendingReselectRevision;
	Proxy.QueueGameThreadData(Data, Proxy.CurrentActiveDatabase, true, true, false);
	TestEqual(TEXT("Stable forward eligibility does not re-arm a boundary every tick"), Proxy.PendingReselectRevision, ForwardRequest);
	Data.MotionMatching.SelectionContext.bUseSettledCycle = true;
	Proxy.QueueGameThreadData(Data, Proxy.CurrentActiveDatabase, true, true, false);
	TestEqual(TEXT("Steady/curve evidence inside one forward pool does not re-arm a normalization boundary"), Proxy.PendingReselectRevision, ForwardRequest);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGeneratedMMDatabaseLifecycleTest,
	"ProjectJ.MMCrowd.GeneratedDatabaseLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGeneratedMMDatabaseLifecycleTest::RunTest(const FString&)
{
	// Exercise the compiled production graph, including its inherited MM node.
	UClass* Class = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master.ABP_Humanoid_Master_C"));
	if (!TestNotNull(TEXT("Production master class"), Class)) return false;
	TStrongObjectPtr<USkeletalMeshComponent> Mesh(NewObject<USkeletalMeshComponent>());
	TStrongObjectPtr<UAnimInstance> Anim(NewObject<UAnimInstance>(Mesh.Get(), Class));
	TStrongObjectPtr<UPoseSearchDatabase> Database(NewObject<UPoseSearchDatabase>());
	TStrongObjectPtr<UPoseSearchDatabase> Companion(NewObject<UPoseSearchDatabase>());
	FProject_JCharacterAnimInstanceProxy Proxy(Anim.Get());
	Proxy.Initialize(Anim.Get());
	const TArray<int32> Indices = Proxy.GetGeneratedMotionMatchingNodeIndices();
	if (!TestTrue(TEXT("Production graph contains a generated MM node"), !Indices.IsEmpty())) return false;
	const auto* Databases = FindFProperty<FArrayProperty>(FAnimNode_MotionMatching::StaticStruct(), TEXT("DatabasesToSearch"));
	const auto* Object = CastField<FObjectPropertyBase>(Databases ? Databases->Inner : nullptr);
	if (!TestNotNull(TEXT("Engine database array"), Databases) || !TestNotNull(TEXT("Engine database element"), Object)) return false;
	FProject_JAnimThreadSafeData Data;
	Data.LocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::Cycle;
	Data.LocomotionContext.bIsMotionMatchingMoving = true;
	const auto CheckDatabases = [&](int32 ExpectedCount)
	{
		for (const int32 Index : Indices)
		{
			const auto* Node = Proxy.GetNodeFromIndex<FAnimNode_MotionMatching>(Index);
			if (!TestNotNull(TEXT("Generated node remains valid"), Node)) return;
			FScriptArrayHelper View(Databases, Databases->ContainerPtrToValuePtr<void>(const_cast<FAnimNode_MotionMatching*>(Node)));
			TestEqual(TEXT("Actual generated node database count"), View.Num(), ExpectedCount);
			if (ExpectedCount == 2 && View.Num() == 2)
			{
				TestEqual(TEXT("Primary binding reaches the actual node"), Object->GetObjectPropertyValue(View.GetRawPtr(0)), static_cast<UObject*>(Database.Get()));
				TestEqual(TEXT("Companion binding reaches the actual node"), Object->GetObjectPropertyValue(View.GetRawPtr(1)), static_cast<UObject*>(Companion.Get()));
			}
		}
	};
	Proxy.QueueGameThreadData(Data, Database.Get(), true, true, false, true, Companion.Get());
	Proxy.ConsumeQueuedGameThreadData(); Proxy.ApplySelectedDatabaseToNativeNode(); CheckDatabases(2);
	Proxy.ApplySelectedDatabaseToNativeNode(); CheckDatabases(2);
	Proxy.QueueGameThreadData(Data, nullptr, false, false, false);
	Proxy.ConsumeQueuedGameThreadData(); Proxy.ApplySelectedDatabaseToNativeNode(); CheckDatabases(0);
	TestTrue(TEXT("Disable releases generated primary and companion bindings"), Proxy.AppliedGeneratedDatabases.IsEmpty() && Proxy.AppliedGeneratedCompanions.IsEmpty());
	Proxy.QueueGameThreadData(Data, Database.Get(), true, true, false, true, Companion.Get());
	Proxy.ConsumeQueuedGameThreadData(); Proxy.ApplySelectedDatabaseToNativeNode(); CheckDatabases(2);
	// The engine can change class metadata before the next project traversal.
	TStrongObjectPtr<UAnimInstance> Plain(NewObject<UAnimInstance>(Mesh.Get()));
	Proxy.bForceMotionMatchingReselect = false;
	Proxy.FAnimInstanceProxy::Initialize(Plain.Get());
	TestTrue(TEXT("A replacement without a generated graph has no stale indices"), Proxy.GetGeneratedMotionMatchingNodeIndices().IsEmpty());
	TestTrue(TEXT("Class change releases old database bindings and acknowledgements"), Proxy.AppliedGeneratedDatabases.IsEmpty() && Proxy.AppliedGeneratedCompanions.IsEmpty() && Proxy.GeneratedReselectStates.IsEmpty());
	TestEqual(TEXT("Fulfilled old-class request cannot become a new return"), Proxy.PendingReselectRevision, uint64(0));
	Proxy.Initialize(Anim.Get());
	Proxy.QueueGameThreadData(Data, Database.Get(), true, true, false, true, Companion.Get());
	Proxy.ConsumeQueuedGameThreadData(); Proxy.ApplySelectedDatabaseToNativeNode(); CheckDatabases(2);
	// The legacy BP remains a handover test asset. Exercise its real compiled callbacks
	// with the AnimInstance-owned proxy, rather than a second proxy invisible to BP getters.
	UClass* LegacyClass = LoadClass<UProject_JCharacterAnimInstance>(nullptr,
		TEXT("/Game/Animation_Logic/ABPs/ABP_Player.ABP_Player_C"));
	if (!TestNotNull(TEXT("Preserved handover animation class"), LegacyClass)) return false;
	TStrongObjectPtr<UProject_JCharacterAnimInstance> Legacy(NewObject<UProject_JCharacterAnimInstance>(Mesh.Get(), LegacyClass));
	auto& OwnedProxy = Legacy->GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
	OwnedProxy.Initialize(Legacy.Get());
	const TArray<int32> LegacyIndices = OwnedProxy.GetGeneratedMotionMatchingNodeIndices();
	if (!TestTrue(TEXT("Legacy graph has a generated MM node"), !LegacyIndices.IsEmpty())) return false;
	const auto CheckLegacy = [&](int32 ExpectedCount, bool bRunCallbacks)
	{
		TestEqual(TEXT("BP getter and the writer use the same real proxy"), Legacy->GetCurrentActivePoseSearchDatabaseThreadSafe(), OwnedProxy.GetCurrentActiveDatabase());
		for (int32 Index : LegacyIndices)
		{
			auto* Node = const_cast<FAnimNode_MotionMatching*>(OwnedProxy.GetNodeFromIndex<FAnimNode_MotionMatching>(Index));
			if (!TestNotNull(TEXT("Legacy generated node"), Node)) continue;
			if (bRunCallbacks)
			{
				FAnimationUpdateSharedContext SharedContext;
				FAnimationUpdateContext Context(&OwnedProxy, 1.f / 60.f, &SharedContext);
				FPoseLink Link;
				Link.SetLinkNode(Node);
				Link.Initialize(FAnimationInitializeContext(&OwnedProxy));
				// This deliberately isolated node has no history collector. The engine reports
				// that after executing its real node-function callbacks; no pose search is claimed.
				AddExpectedError(TEXT("missing IPoseHistory"), EAutomationExpectedErrorFlags::Contains, 1);
				Link.Update(Context);
			}
			FScriptArrayHelper View(Databases, Databases->ContainerPtrToValuePtr<void>(Node));
			TestEqual(TEXT("Compiled node callbacks preserve the native candidate set"), View.Num(), ExpectedCount);
			if (ExpectedCount > 0 && View.Num() == ExpectedCount)
			{
				TestEqual(TEXT("Legacy primary stays selected"), Object->GetObjectPropertyValue(View.GetRawPtr(0)), static_cast<UObject*>(Database.Get()));
				if (ExpectedCount == 2)
					TestEqual(TEXT("Legacy companion survives callbacks"), Object->GetObjectPropertyValue(View.GetRawPtr(1)), static_cast<UObject*>(Companion.Get()));
			}
		}
	};
	const auto QueueLegacy = [&](bool bEnabled, UPoseSearchDatabase* InCompanion)
	{
		OwnedProxy.QueueGameThreadData(Data, bEnabled ? Database.Get() : nullptr, bEnabled, bEnabled, false, true, InCompanion);
		OwnedProxy.ConsumeQueuedGameThreadData();
		OwnedProxy.ApplySelectedDatabaseToNativeNode();
	};
	QueueLegacy(true, Companion.Get()); CheckLegacy(2, true);
	QueueLegacy(true, Companion.Get()); CheckLegacy(2, true); // Unchanged IDs must not leave a stale native cache.
	QueueLegacy(true, nullptr); CheckLegacy(1, true);
	QueueLegacy(true, Companion.Get()); CheckLegacy(2, true);
	QueueLegacy(false, nullptr); CheckLegacy(0, false);
	QueueLegacy(true, Companion.Get()); CheckLegacy(2, true);
	return true;
}
#endif
