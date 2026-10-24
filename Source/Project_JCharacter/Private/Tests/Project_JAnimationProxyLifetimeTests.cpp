#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Chooser.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationProxyLifetimeTest, "ProjectJ.RuntimeOwnership.AnimationProxyPinsQueuedAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAnimationProxyLifetimeTest::RunTest(const FString&)
{
    TStrongObjectPtr<USkeletalMeshComponent> Mesh(NewObject<USkeletalMeshComponent>());
    TStrongObjectPtr<UProject_JCharacterAnimInstance> Anim(NewObject<UProject_JCharacterAnimInstance>(Mesh.Get()));
    auto& Proxy = Anim->GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
    FProject_JAnimThreadSafeData Data;
    Data.PlayerLocomotionAir.Chooser = NewObject<UChooserTable>();
    const TWeakObjectPtr<UChooserTable> WeakChooser(Data.PlayerLocomotionAir.Chooser);
    Proxy.QueueGameThreadData(Data);
    Data.PlayerLocomotionAir.Chooser = nullptr;
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestTrue(TEXT("Pending GASP air chooser survives producer replacement"), WeakChooser.IsValid());
    Proxy.QueueGameThreadData(Data);
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestFalse(TEXT("Replaced chooser is not pinned indefinitely"), WeakChooser.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerLocomotionSnapshotBoundaryTest, "ProjectJ.PlayerLocomotion.SnapshotPublicationAndReinitialize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPlayerLocomotionSnapshotBoundaryTest::RunTest(const FString&)
{
    TStrongObjectPtr<USkeletalMeshComponent> Mesh(NewObject<USkeletalMeshComponent>());
    TStrongObjectPtr<UProject_JPlayerAnimInstance> Anim(NewObject<UProject_JPlayerAnimInstance>(Mesh.Get()));
    auto& Proxy = Anim->GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
    FProject_JAnimThreadSafeData Data;
    Data.Movement.GroundSpeed = 100.f;
    Data.Ground.RunSpeed=500.f;Data.Ground.SprintSpeed=750.f;
    Proxy.QueueGameThreadData(Data);
    TestEqual(TEXT("GT publication cannot mutate the current traversal"), Proxy.GetThreadSafeData().Movement.GroundSpeed, 0.f);
    Proxy.PreUpdate(Anim.Get(), 1.f/60.f);
    TestEqual(TEXT("Proxy PreUpdate can precede the producer's final publication"), Proxy.GetThreadSafeData().Movement.GroundSpeed, 100.f);
    Data.Movement.GroundSpeed = 585.f;
    Data.Ground.RunSpeed=600.f;Data.Ground.bScalePlayRateWithSpeed=true;
    for (float Time : {0.f,.4f,.5f,1.5f})
    {
        auto& Sample=Data.Movement.Trajectory.Samples.AddDefaulted_GetRef();
        Sample.TimeInSeconds=Time;Sample.Position=FVector(-500.f*Time,0.,0.);Sample.Facing=FQuat::Identity;
    }
    Proxy.QueueGameThreadData(Data);
    Anim->NativeThreadSafeUpdateAnimation(1.f/60.f);
    TestEqual(TEXT("Native decisions consume the final publication before graph traversal"), Proxy.GetThreadSafeData().Movement.GroundSpeed, 585.f);
    TestEqual(TEXT("Immutable graph pins use that same publication"), Anim->GraphCombatSpeed, 585.f);
    Anim->PlayerVelocity=FVector(500.,0.,0.);
    Anim->UpdateNativeTrajectory();
    TestEqual(TEXT("A keyboard reversal is visible before state entry selects its clip"), FMath::Abs(Anim->PlayerTrajectoryTurnAngle), 180.);
    TestEqual(TEXT("Speed tuning uses the same completed publication as movement"),Proxy.GetThreadSafeData().Ground.RunSpeed,600.f);
    TestTrue(TEXT("Cadence policy is published with its speed tuning"),Proxy.GetThreadSafeData().Ground.bScalePlayRateWithSpeed);
    using Direction=EProject_JPlayerMovementDirection;
    for (Direction Value : {Direction::Forward,Direction::Backward,Direction::LeftLeftFootForward,Direction::LeftRightFootForward,Direction::RightLeftFootForward,Direction::RightRightFootForward})
    for (double Side : {-1.,1.})
    {
        FProject_JAnimThreadSafeData FacingData;
        for (float Time : {0.f,.25f,.75f,1.5f})
        {
            auto& Sample=FacingData.Movement.Trajectory.Samples.AddDefaulted_GetRef();
            Sample.TimeInSeconds=Time;Sample.Position=FVector(500.f*Time,0.,0.);
            Sample.Facing=FRotator(0.,Side*(Time==0.f?0.:Time==.25f?135.:225.),0.).Quaternion();
        }
        Proxy.QueueGameThreadData(FacingData);Anim->NativeThreadSafeUpdateAnimation(1.f/60.f);
        Anim->PlayerVelocity=FVector(500.,0.,0.);Anim->PlayerFutureFacingDelta=0.;
        Anim->PlayerMovementDirection=Anim->PlayerMovementDirectionRecent=Value;
        Anim->UpdateNativeTrajectory();
        TestTrue(TEXT("The accumulated facing path retains turns beyond a half rotation"),FMath::IsNearlyEqual(Anim->PlayerFutureFacingDelta,Side*225.,.001));
        TestEqual(TEXT("A facing discontinuity cannot fabricate a movement direction"),Anim->PlayerMovementDirection,Value);
        TestEqual(TEXT("Recent direction remains owned by the state history, on either side of the seam"),Anim->PlayerMovementDirectionRecent,Value);
    }
    Anim->PlayerMovementState=EProject_JPlayerMovementState::Moving;Anim->PlayerStance=EProject_JPlayerStance::Stand;
    Anim->PlayerGait=EProject_JLocomotionGaitIntent::Sprint;Anim->PlayerTrajectoryTurnAngle=60.;
    Data.Ground.SprintSpeed=900.f;Proxy.QueueGameThreadData(Data);Proxy.ConsumeQueuedGameThreadData();
    Anim->PlayerSpeed2D=900.;TestTrue(TEXT("Pivot admission follows completed tuning above the old 700 limit"),Anim->EvaluateIsPivoting());
    Data.Ground.SprintSpeed=585.f;Proxy.QueueGameThreadData(Data);Proxy.ConsumeQueuedGameThreadData();
    Anim->PlayerSpeed2D=700.;TestTrue(TEXT("Original speed tuning retains its original upper boundary"),Anim->EvaluateIsPivoting());
    Anim->PlayerSpeed2D=700.1;TestFalse(TEXT("Original tuning still rejects overspeed beyond its headroom"),Anim->EvaluateIsPivoting());
    Data.Ground.RunSpeed=500.f;Data.Ground.SprintSpeed=750.f;Proxy.QueueGameThreadData(Data);Proxy.ConsumeQueuedGameThreadData();
    Anim->PlayerGait=EProject_JLocomotionGaitIntent::Run;Anim->PlayerTrajectoryTurnAngle=75.;Anim->PlayerSpeed2D=750.;
    TestTrue(TEXT("Run pivot accepts carried Sprint momentum after a downshift or landing"),Anim->EvaluateIsPivoting());
    Anim->PlayerSpeed2D=765.1;TestFalse(TEXT("Carried momentum admission remains bounded"),Anim->EvaluateIsPivoting());
    TestEqual(TEXT("Publication revision is consumed once"), Proxy.ConsumedSnapshotRevision, Proxy.PublishedSnapshotRevision);
    Proxy.Initialize(Anim.Get());
    TestEqual(TEXT("Reinitialize clears the prior owner's snapshot"), Proxy.GetThreadSafeData().Movement.GroundSpeed, 0.f);
    TestTrue(TEXT("Reinitialize clears stale playback observations"), Proxy.GetLatestPostSelection().SelectedAnimation.IsNone());
    TestFalse(TEXT("Reinitialize cannot retain a previous owner's cadence policy"),Proxy.GetThreadSafeData().Ground.bScalePlayRateWithSpeed);
    Anim->PlayerMovementMode=Anim->PlayerMovementModeRecent=EProject_JPlayerMovementMode::InAir;
    Anim->PlayerVelocity=FVector(0,0,-900);
    Anim->NativeInitializeAnimation();
    Anim->UpdateNativeLocomotionLogic();
    TestFalse(TEXT("Reinitialized graph remains closed until a completed source"),Anim->PlayerHasLocomotionSource);
    Anim->PlayerCharacterProperties={};
    Anim->PlayerCharacterProperties.MovementMode=EProject_JPlayerMovementMode::OnGround;
    Anim->PlayerHasOwningActor=Anim->PlayerHasLocomotionSource=Anim->PlayerUseThreadSafeUpdateAnimation=true;
    Anim->UpdateNativeLocomotionLogic();
    TestFalse(TEXT("The first admitted frame cannot fabricate a heavy landing"),Anim->EvaluateHeavyLanding());
    TestFalse(TEXT("The first admitted frame cannot fabricate a light landing"),Anim->EvaluateLightLanding());
    TestTrue(TEXT("First frame derivatives are seeded from the new source"),Anim->PlayerVelocityAcceleration.IsNearlyZero());
    TestEqual(TEXT("Recent mode belongs to the new lifetime"),Anim->PlayerMovementModeRecent,EProject_JPlayerMovementMode::OnGround);
    Anim->PlayerCharacterProperties.MovementMode=EProject_JPlayerMovementMode::InAir;
    Anim->PlayerCharacterProperties.Velocity=FVector(0,0,-900);Anim->UpdateNativeLocomotionLogic();
    Anim->PlayerCharacterProperties.MovementMode=EProject_JPlayerMovementMode::OnGround;
    Anim->PlayerCharacterProperties.Velocity=FVector::ZeroVector;Anim->UpdateNativeLocomotionLogic();
    TestTrue(TEXT("A real landing in the new lifetime is retained"),Anim->EvaluateHeavyLanding());
    Anim->ThreadSafeData={};Anim->ThreadSafeData.Input.bHasMoveInput=true;
    Anim->ThreadSafeData.Movement.Velocity=FVector(300,0,0);Anim->ThreadSafeData.Movement.Acceleration=FVector(-4000,0,0);
    TestTrue(TEXT("Strafe velocity reversal retains full visual evaluation without a facing turn"),Anim->NeedsFullRateVisualUpdate());
    Anim->ThreadSafeData.Movement.Acceleration=FVector(4000,0,0);
    TestFalse(TEXT("Steady straight movement can use the authored visual budget"),Anim->NeedsFullRateVisualUpdate());
    // Action recovery consumes the same immutable snapshot and source lifetime.
    {
        auto* RecoveryMesh=NewObject<USkeletalMeshComponent>();auto* RecoveryAnim=NewObject<UProject_JPlayerAnimInstance>(RecoveryMesh);
        auto& RecoveryProxy=RecoveryAnim->GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
        FProject_JAnimThreadSafeData Frame;
        auto Tick=[&](bool bMoving)
        {
         FTransformTrajectory Trajectory;
         for (float Time:{0.f,1.f,1.5f})
         {auto& Sample=Trajectory.Samples.AddDefaulted_GetRef();Sample.TimeInSeconds=Time;Sample.Position=FVector(bMoving?500.*Time:0.,0,0);Sample.Facing=FQuat::Identity;}
         RecoveryAnim->PlayerTrajectory=Trajectory;
         RecoveryProxy.QueueGameThreadData(Frame);RecoveryProxy.ConsumeQueuedGameThreadData();RecoveryAnim->UpdateNativeStates();
        };
        auto SelectRecovery=[&]()
        {
         RecoveryAnim->SelectNativeStateAnimation(TEXT("Idle Transition"),uint8(EProject_JPlayerPlaybackState::IdleTransition),false);
         return RecoveryAnim->PlayerStateMachineState;
        };
        using State=EProject_JPlayerPlaybackState;
        Frame.Input.bHasMoveInput=true;Tick(true);
        RecoveryAnim->PlayerStateMachineState=State::LocomotionTransition;
        Frame.Montage.bFullBodyMontageActive=true;Tick(true);
        TestEqual(TEXT("A full-body action retires a start already in flight"),RecoveryAnim->PlayerStateMachineState,State::LocomotionLoop);
        RecoveryAnim->SelectNativeStateAnimation(TEXT("Locomotion Transition"),uint8(State::LocomotionTransition),false);
        TestEqual(TEXT("Movement beginning under a full-body action keeps the existing locomotion loop"),RecoveryAnim->PlayerStateMachineState,State::LocomotionLoop);
        Frame.Montage.bFullBodyMontageActive=false;Tick(true);
        TestEqual(TEXT("Independent movement retains its stop transition"),SelectRecovery(),State::IdleTransition);
        Frame.Montage.bFullBodyMontageActive=true;Frame.Montage.FullBodyMontageWeight=0.f;
        Frame.Input.bHasMoveInput=false;Tick(true);
        TestFalse(TEXT("First blend-in frame cannot turn root-motion prediction into locomotion"),RecoveryAnim->EvaluateIsMoving());
        TestEqual(TEXT("An action also retires a stop that began before it"),RecoveryAnim->PlayerStateMachineState,State::IdleLoop);
        TestEqual(TEXT("An input release under the action selects the existing idle fallback"),SelectRecovery(),State::IdleLoop);
        Frame.Montage.bFullBodyMontageActive=false;Frame.Movement.bHasGameplayRootMotion=true;Tick(true);
        TestEqual(TEXT("Movement-owned root motion covers montage teardown order"),SelectRecovery(),State::IdleLoop);
        Frame.Movement.bHasGameplayRootMotion=false;Tick(false);
        TestEqual(TEXT("A completed/cancelled action cannot replay its hidden stop or forced-flight landing"),SelectRecovery(),State::IdleLoop);
        RecoveryAnim->PlayerFutureFacingDelta=70.;RecoveryAnim->PlayerSpeed2D=0.;
        RecoveryAnim->PlayerMovementModeRecent=EProject_JPlayerMovementMode::InAir;
        TestEqual(TEXT("A facing request cannot reopen landing rows from action-owned flight history"),SelectRecovery(),State::IdleLoop);
        RecoveryAnim->PlayerMovementModeRecent=EProject_JPlayerMovementMode::OnGround;
        TestEqual(TEXT("A new stationary facing request can still select turn-in-place"),SelectRecovery(),State::IdleTransition);
        RecoveryAnim->PlayerFutureFacingDelta=0.;
        Frame.Input.bHasMoveInput=true;Tick(true);
        Frame.Input.bHasMoveInput=false;Tick(false);
        TestEqual(TEXT("A fresh independent movement episode restores normal stopping"),SelectRecovery(),State::IdleTransition);
        Frame.Montage.bFullBodyMontageActive=true;Tick(false);
        Frame.Montage.bFullBodyMontageActive=false;Frame.Air.bIsFalling=true;
        RecoveryAnim->PlayerCharacterProperties.MovementMode=EProject_JPlayerMovementMode::InAir;Tick(false);
        Frame.Air.bIsFalling=false;RecoveryAnim->PlayerCharacterProperties.MovementMode=EProject_JPlayerMovementMode::OnGround;Tick(false);
        TestEqual(TEXT("A real fall after an interrupted aerial action restores landing"),SelectRecovery(),State::IdleTransition);
        RecoveryAnim->NativeUninitializeAnimation();
        TestEqual(TEXT("A retained instance does not inherit the retired action's recovery suppression"),SelectRecovery(),State::IdleTransition);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlaybackObservationLifetimeTest, "ProjectJ.PlayerLocomotion.PlaybackObservationLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPlaybackObservationLifetimeTest::RunTest(const FString&)
{
    TStrongObjectPtr<USkeletalMeshComponent> Mesh(NewObject<USkeletalMeshComponent>());
    TStrongObjectPtr<UProject_JCharacterAnimInstance> Anim(NewObject<UProject_JCharacterAnimInstance>(Mesh.Get()));
    auto& Proxy=Anim->GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
    // A consumer requesting the same completed traversal must not manufacture
    // a fresh frame or alter the previously observed result-change history.
    Proxy.PlaybackUpdateRevision=Proxy.CapturedPlaybackRevision=1;
    Proxy.PlaybackUpdateFrame=1;
    Proxy.LatestPostSelection.SelectedAnimation=TEXT("PreviouslyObservedPose");
    Proxy.LatestPostSelection.CaptureFrame=1;
    Proxy.LatestPostSelection.ResultLastChangedFrame=1;
    const auto Cached=Anim->GetPlaybackSnapshot();
    TestEqual(TEXT("A request retains the real completed traversal frame"),Cached.CaptureFrame,uint64(1));
    TestEqual(TEXT("Observed result history is not stamped with the request frame"),Cached.ResultLastChangedFrame,uint64(1));
    TestEqual(TEXT("Same-traversal consumers share the captured result"),Cached.SelectedAnimation,FName(TEXT("PreviouslyObservedPose")));
    // The next completed traversal has no active BlendStack in this native
    // fixture. A previous owner's/graph's result must not survive that boundary.
    ++Proxy.PlaybackUpdateRevision;
    const auto Empty=Anim->GetPlaybackSnapshot();
    TestTrue(TEXT("A completed traversal without a player clears stale observations"),Empty.SelectedAnimation.IsNone());
    TestEqual(TEXT("A missing player cannot satisfy network pose freshness"),Empty.CaptureFrame,uint64(0));
    Proxy.Initialize(Anim.Get());
    TestEqual(TEXT("Owner reinitialization clears the observation revision"),Proxy.PlaybackUpdateRevision,uint64(0));
    TestEqual(TEXT("Owner reinitialization clears captured revision"),Proxy.CapturedPlaybackRevision,uint64(0));
    TestEqual(TEXT("Owner reinitialization clears the previous traversal frame"),Proxy.PlaybackUpdateFrame,uint64(0));
    return true;
}
#endif
