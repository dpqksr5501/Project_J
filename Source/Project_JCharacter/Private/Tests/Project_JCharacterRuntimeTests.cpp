#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/Project_JMotionMatchingRuntime.h"
#include "Animation/Project_JMotionMatchingSelectionPolicy.h"
#include "Animation/Project_JLocomotionContextBuilder.h"
#include "Animation/Project_JRemoteLocomotionRuntime.h"
#include "Animation/Project_JTurnInPlacePresentationRuntime.h"
#include "Animation/Project_JStateControllerRuntime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMotionMatchingRuntimeTest,
	"ProjectJ.Animation.MotionMatchingRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJMotionMatchingRuntimeTest::RunTest(const FString&)
{
	FProject_JMotionMatchingRuntime Runtime;
	FProject_JMotionMatchingRuntime::FContext Context;
	TestTrue(TEXT("캐시되지 않은 컨텍스트는 평가를 요청한다"), Runtime.HasContextChanged(Context));
	Runtime.CacheEvaluatedContext(Context);
	TestFalse(TEXT("동일한 컨텍스트는 재선택을 강제하지 않는다"), Runtime.HasContextChanged(Context));
	Context.SelectionRevision++;
	TestTrue(TEXT("의미 선택 리비전이 바뀌면 다시 평가한다"), Runtime.HasContextChanged(Context));
	Runtime.CacheEvaluatedContext(Context);
	TestFalse(TEXT("리비전 변경은 한 번만 소비한다"), Runtime.HasContextChanged(Context));
	Runtime.InvalidateContext();
	TestTrue(TEXT("MM 비활성화는 이전 데이터베이스 컨텍스트를 무효화한다"), Runtime.HasContextChanged(Context));
	Runtime.CacheEvaluatedContext(Context);
	Runtime.Reset();
	TestTrue(TEXT("재초기화는 캐시된 컨텍스트를 지운다"), Runtime.HasContextChanged(Context));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStateControllerRuntimeTest,
	"ProjectJ.Animation.StateControllerRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJStateControllerRuntimeTest::RunTest(const FString&)
{
	FProject_JStateControllerRuntime Runtime;
	FProject_JStateControllerRuntime::FIntent Intent;
	Intent.bIsLocallyControlled = true;
	Runtime.PlaybackHoldState = EProject_JStateControllerPresentationState::LocomotionLoop;
	TestEqual(TEXT("표현 중인 이동 루프에서 입력을 놓으면 Stop에 한 번 진입한다"),
		Runtime.PrepareDesiredState(EProject_JStateControllerPresentationState::IdleLoop, Intent),
		EProject_JStateControllerPresentationState::TransitionToIdle);
	Runtime.bGroundStopConsumed = true;
	TestEqual(TEXT("소비한 Stop은 같은 입력 해제 중 다시 시작하지 않는다"),
		Runtime.PrepareDesiredState(EProject_JStateControllerPresentationState::TransitionToIdle, Intent),
		EProject_JStateControllerPresentationState::IdleLoop);
	Intent.bHasMoveInput = true;
	Runtime.PrepareDesiredState(EProject_JStateControllerPresentationState::LocomotionLoop, Intent);
	TestFalse(TEXT("새 이동 입력은 Stop을 다시 활성화한다"), Runtime.bGroundStopConsumed);

	Intent.bHasMoveInput = false;
	Intent.bFullBodyActionOrRecentExit = true;
	TestEqual(TEXT("전신 동작은 숨겨진 Stop을 소비한다"),
		Runtime.PrepareDesiredState(EProject_JStateControllerPresentationState::TransitionToIdle, Intent),
		EProject_JStateControllerPresentationState::IdleLoop);

	Runtime.PlaybackHoldState = EProject_JStateControllerPresentationState::IdleLoop;
	Runtime.ConsumeTurnSequence(true, 3);
	Intent.bFullBodyActionOrRecentExit = false;
	Intent.TurnInPlaceSequence = 3;
	TestEqual(TEXT("소비한 로컬 TIP 순번은 재진입하지 않는다"),
		Runtime.PrepareDesiredState(EProject_JStateControllerPresentationState::TurnInPlace, Intent),
		EProject_JStateControllerPresentationState::IdleLoop);
	Intent.bIsLocallyControlled = false;
	TestEqual(TEXT("원격 TIP 순번은 별도 수명을 가진다"),
		Runtime.PrepareDesiredState(EProject_JStateControllerPresentationState::TurnInPlace, Intent),
		EProject_JStateControllerPresentationState::TurnInPlace);

	Runtime.HeldLandingPresentationRevision = 5;
	Intent.LandingPresentationRevision = 5;
	TestEqual(TEXT("중단된 착지 리비전은 다시 살아나지 않는다"),
		Runtime.PrepareDesiredState(EProject_JStateControllerPresentationState::TransitionToLand, Intent),
		EProject_JStateControllerPresentationState::IdleLoop);
	Runtime.Pivot.Commit(4, 9, FVector::ForwardVector, FVector::RightVector);
	Runtime.Pivot.SuppressedRequestRevision = 3;
	Runtime.Pivot.Cancel();
	TestEqual(TEXT("Pivot 중단은 확정된 요청을 지운다"), Runtime.Pivot.RequestRevision, 0);
	TestEqual(TEXT("Pivot 중단은 소비한 방향 전환 리비전을 유지한다"), Runtime.Pivot.SuppressedRequestRevision, 3);
	Runtime.Reset();
	TestEqual(TEXT("리셋은 로컬 TIP 이벤트 경계를 지운다"), Runtime.LastHandledLocalTurnSequence, 0);
	TestEqual(TEXT("리셋은 착지 리비전을 지운다"), Runtime.HeldLandingPresentationRevision, INDEX_NONE);
	TestEqual(TEXT("소유자 리셋은 억제된 Pivot 리비전을 지운다"), Runtime.Pivot.SuppressedRequestRevision, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMotionMatchingSelectionPolicyTest,
	"ProjectJ.Animation.MotionMatchingSelectionPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJMotionMatchingSelectionPolicyTest::RunTest(const FString&)
{
	FProject_JMotionMatchingSelectionPolicy Policy;
	FProject_JMotionMatchingSelectionPolicy::FKey Key;
	TestTrue(TEXT("첫 의미 컨텍스트는 선택을 게시한다"), Policy.Publish(Key));
	const int32 FirstRevision = Policy.GetRevision();
	TestFalse(TEXT("변하지 않은 컨텍스트는 선택 이벤트를 다시 만들지 않는다"), Policy.Publish(Key));
	TestEqual(TEXT("변하지 않은 컨텍스트는 리비전을 유지한다"), Policy.GetRevision(), FirstRevision);
	Key.Phase = EProject_JLocomotionPhaseFamily::Stop;
	TestTrue(TEXT("Stop은 선택 계열을 바꾼다"), Policy.Publish(Key));
	TestEqual(TEXT("의미 이벤트 하나는 리비전을 한 번 올린다"), Policy.GetRevision(), FirstRevision + 1);
	TestTrue(TEXT("첫 방향 전환은 동일 데이터베이스 재검색을 요청한다"), Policy.RequestRedirectReselect(true, 10.0, 0.1f));
	TestFalse(TEXT("대기 시간 내 방향 전환은 합쳐진다"), Policy.RequestRedirectReselect(true, 10.05, 0.1f));
	TestTrue(TEXT("대기 시간 이후 방향 전환은 허용된다"), Policy.RequestRedirectReselect(true, 10.2, 0.1f));
	Policy.Reset();
	TestFalse(TEXT("리셋은 게시된 키를 무효화한다"), Policy.HasPublished());
	TestTrue(TEXT("리셋 후에는 같은 키도 다시 게시한다"), Policy.Publish(Key));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLocomotionContextBuilderTest,
	"ProjectJ.Animation.LocomotionContextBuilder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJLocomotionContextBuilderTest::RunTest(const FString&)
{
	FProject_JLocomotionContextBuilder::FMotionMatchingMovement Movement;
	Movement.bUsingLocalInput = true;
	Movement.GroundSpeed = 150.0f;
	Movement.FutureTrajectorySpeed = 150.0f;
	Movement.bHasFutureTrajectoryVelocity = true;
	Movement.MovingSpeedThreshold = 30.0f;
	TestFalse(TEXT("로컬 입력을 놓으면 잔여 속도가 있어도 Stop에 진입한다"),
		FProject_JLocomotionContextBuilder::IsMotionMatchingMoving(Movement));
	Movement.bUsingLocalInput = false;
	TestTrue(TEXT("원격 이동은 복제된 속도로 재구성할 수 있다"),
		FProject_JLocomotionContextBuilder::IsMotionMatchingMoving(Movement));
	Movement.bInAirOrLanding = true;
	TestFalse(TEXT("공중과 착지는 지상 MM 사이클을 요청하지 않는다"),
		FProject_JLocomotionContextBuilder::IsMotionMatchingMoving(Movement));

	FProject_JLocomotionContextBuilder::FPhaseInput Phase;
	Phase.bMoving = true;
	Phase.bHasMoveInput = true;
	Phase.GroundSpeed = 150.0f;
	Phase.MoveInputTurnAngle = 80.0f;
	Phase.TurnMinSpeed = 50.0f;
	Phase.TurnAngleThreshold = 60.0f;
	TestEqual(TEXT("OTM 이동 방향 전환은 Turn을 선택한다"), FProject_JLocomotionContextBuilder::ResolvePhaseFamily(Phase),
		EProject_JLocomotionPhaseFamily::Turn);
	Phase.bCombatStrafe = true;
	TestEqual(TEXT("전투 스트레이프의 일반 방향 전환은 Cycle에 남는다"), FProject_JLocomotionContextBuilder::ResolvePhaseFamily(Phase),
		EProject_JLocomotionPhaseFamily::Cycle);
	Phase.bPivoting = true;
	TestEqual(TEXT("명시적 Pivot은 Cycle보다 우선한다"), FProject_JLocomotionContextBuilder::ResolvePhaseFamily(Phase),
		EProject_JLocomotionPhaseFamily::Pivot);
	Phase.bLanding = true;
	TestEqual(TEXT("착지는 Pivot보다 우선한다"), FProject_JLocomotionContextBuilder::ResolvePhaseFamily(Phase),
		EProject_JLocomotionPhaseFamily::Landing);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRemoteLocomotionRuntimeTest,
	"ProjectJ.Animation.RemoteLocomotionRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJRemoteLocomotionRuntimeTest::RunTest(const FString&)
{
	FProject_JRemoteLocomotionRuntime Runtime;
	Runtime.OnMoveStopped(0.2f);
	TestTrue(TEXT("확정된 원격 Stop은 시각적 이동 의도를 소유한다"), Runtime.IsStopVisualIntentActive());
	TestTrue(TEXT("잔여 속도는 Start를 즉시 재시작하지 못한다"), Runtime.ConsumeStopStartSuppress(0.1f));
	TestTrue(TEXT("현재 억제 프레임도 보호한다"), Runtime.ConsumeStopStartSuppress(0.1f));
	TestFalse(TEXT("억제 기간이 지나면 종료된다"), Runtime.ConsumeStopStartSuppress(0.1f));
	TestTrue(TEXT("대기 시간이 지나도 확정된 Stop은 유지한다"), Runtime.IsStopVisualIntentActive());
	Runtime.OnMoveStarted();
	TestFalse(TEXT("확정된 원격 Start는 시각적 Stop을 해제한다"), Runtime.IsStopVisualIntentActive());

	Runtime.OnTurnStarted(7, 0.5f, 3, 90.0f);
	TestTrue(TEXT("원격 TIP는 수신한 이벤트에서 시작한다"), Runtime.IsTurnActive());
	TestEqual(TEXT("원격 TIP는 복제된 순번을 유지한다"), Runtime.GetTurnSequence(), 7);
	Runtime.AdvanceTurn(2.0f);
	TestFalse(TEXT("원격 TIP 유효 기간은 직접적인 컨트롤 회전 조회 없이 끝난다"), Runtime.IsTurnActive());
	Runtime.OnTurnStarted(8, 0.0f, 1, -90.0f);
	Runtime.CancelTurn();
	TestFalse(TEXT("이동으로 중단하면 원격 TIP를 취소한다"), Runtime.IsTurnActive());
	Runtime.Reset();
	TestEqual(TEXT("소유자 리셋은 원격 TIP 순번을 지운다"), Runtime.GetTurnSequence(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTurnInPlacePresentationRuntimeTest,
	"ProjectJ.Animation.TurnInPlacePresentationRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJTurnInPlacePresentationRuntimeTest::RunTest(const FString&)
{
	FProject_JTurnInPlacePresentationRuntime Runtime;
	Runtime.BeginSelection(nullptr, 1, 30.0f);
	TestEqual(TEXT("선택 시 액터의 바라보는 방향을 고정한다"), Runtime.GetSelectionStartActorYaw(), 30.0f);
	Runtime.BeginSelection(nullptr, 1, 45.0f);
	TestEqual(TEXT("같은 선택에서는 저작된 루트 기준을 옮기지 않는다"), Runtime.GetSelectionStartActorYaw(), 30.0f);
	Runtime.BeginSelection(nullptr, 2, 45.0f);
	TestEqual(TEXT("같은 에셋을 재선택하면 새 루트 기준을 설정한다"), Runtime.GetSelectionStartActorYaw(), 45.0f);
	TestEqual(TEXT("루트 yaw는 고정된 목표 방향에서 멈춘다"),
		FProject_JTurnInPlacePresentationRuntime::ClampAuthoredYaw(90.0f, 35.0f), 35.0f);
	TestEqual(TEXT("반대 방향 목표는 이전 회전을 계속하지 않는다"),
		FProject_JTurnInPlacePresentationRuntime::ClampAuthoredYaw(90.0f, -20.0f), 0.0f);
	TestTrue(TEXT("첫 활성 yaw를 전송한다"), Runtime.ShouldSendActiveYaw(20.0f, 1.0));
	TestFalse(TEXT("작은 yaw 변화는 전송을 제한한다"), Runtime.ShouldSendActiveYaw(21.0f, 1.1));
	TestTrue(TEXT("대기 시간 이후 바뀐 yaw를 전송한다"), Runtime.ShouldSendActiveYaw(25.0f, 1.2));
	TestTrue(TEXT("중단 시 비활성 이벤트를 한 번 전송한다"), Runtime.FinishFrame(false, false, 25.0f, true));
	TestFalse(TEXT("비활성 이벤트는 반복 전송하지 않는다"), Runtime.FinishFrame(false, false, 25.0f, true));
	Runtime.Reset();
	TestTrue(TEXT("리셋 후 새 활성 이벤트를 허용한다"), Runtime.ShouldSendActiveYaw(25.0f, 2.0));
	return true;
}

#endif
