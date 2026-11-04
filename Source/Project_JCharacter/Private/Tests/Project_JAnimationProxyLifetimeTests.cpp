#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Chooser.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "Async/Async.h"
#include "MoverNativeFlyingTrajectory.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Project_JSpellbladeCharacter.h"
#include "Movement/Project_JMoverComponent.h"
#include "Tests/Project_JMoverFrameFixture.h"
#include "DefaultMovementSet/CharacterMoverSimulationTypes.h"
#include "MoveLibrary/FloorQueryUtils.h"
#include "MoveLibrary/MoverBlackboard.h"
#include "Engine/SkeletalMesh.h"

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
    {
        FProject_JAnimThreadSafeData Owned;
        Owned.Movement.GroundSpeed=42.f;
        Owned.Movement.Trajectory.Samples.SetNum(31);
        Owned.Movement.Trajectory.Samples[0].Position=FVector(123.,456.,789.);
        const auto* Samples=Owned.Movement.Trajectory.Samples.GetData();
        Owned.PlayerLocomotionAir.Chooser=NewObject<UChooserTable>();
        const TWeakObjectPtr<UChooserTable> Chooser(Owned.PlayerLocomotionAir.Chooser);
        Proxy.QueueGameThreadData(MoveTemp(Owned));
        TestTrue(TEXT("Owning publication transfers the existing sample buffer"),Proxy.PendingGameThreadData.Movement.Trajectory.Samples.GetData()==Samples);
        TestEqual(TEXT("The producer no longer owns the moved samples"),Owned.Movement.Trajectory.Samples.Num(),0);
        TestEqual(TEXT("Pending publication preserves the current traversal"),Proxy.GetThreadSafeData().Movement.GroundSpeed,0.f);
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        TestTrue(TEXT("Pending owning publication pins its chooser"),Chooser.IsValid());
        Proxy.ConsumeQueuedGameThreadData();
        TestTrue(TEXT("Consumption transfers that same sample buffer"),Proxy.GetThreadSafeData().Movement.Trajectory.Samples.GetData()==Samples);
        TestNull(TEXT("The consumed pending slot drops its scalar asset reference"),Proxy.PendingGameThreadData.PlayerLocomotionAir.Chooser.Get());
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        TestTrue(TEXT("The active traversal pins its consumed chooser"),Chooser.IsValid());
        Proxy.ConsumeQueuedGameThreadData();
        TestTrue(TEXT("Repeated consumption retains the current owned buffer"),Proxy.GetThreadSafeData().Movement.Trajectory.Samples.GetData()==Samples);
        TestTrue(TEXT("Repeated consumption retains the current complete values"),Proxy.GetThreadSafeData().Movement.Trajectory.Samples[0].Position.Equals(FVector(123.,456.,789.)));
        Proxy.QueueGameThreadData(FProject_JAnimThreadSafeData{});
        Proxy.ConsumeQueuedGameThreadData();
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        TestFalse(TEXT("Replacing the consumed chooser does not pin a retired asset"),Chooser.IsValid());
        Proxy.Initialize(Anim.Get());
    }
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
    TestTrue(TEXT("Lvalue publication keeps independent producer storage"),Proxy.PendingGameThreadData.Movement.Trajectory.Samples.GetData()!=Data.Movement.Trajectory.Samples.GetData());
    Anim->NativeThreadSafeUpdateAnimation(1.f/60.f);
    TestEqual(TEXT("Native decisions consume the final publication before graph traversal"), Proxy.GetThreadSafeData().Movement.GroundSpeed, 585.f);
    TestEqual(TEXT("Immutable graph pins use that same publication"), Anim->GraphCombatSpeed, 585.f);
    Anim->PlayerVelocity=FVector(500.,0.,0.);
    Anim->UpdateNativeTrajectory();
    TestEqual(TEXT("A keyboard reversal is visible before state entry selects its clip"), FMath::Abs(Anim->PlayerTrajectoryTurnAngle), 180.);
    TestEqual(TEXT("Speed tuning uses the same completed publication as movement"),Proxy.GetThreadSafeData().Ground.RunSpeed,600.f);
    TestTrue(TEXT("Cadence policy is published with its speed tuning"),Proxy.GetThreadSafeData().Ground.bScalePlayRateWithSpeed);
    // Native traversal may read the immutable proxy directly; its BP-visible
    // retained trajectory and public value getter must still own independent data.
    const FVector PublishedFirst = Proxy.GetThreadSafeData().Movement.Trajectory.Samples[0].Position;
    TestTrue(TEXT("Retained trajectory owns its sample storage"),
        Anim->PlayerTrajectory.Samples.GetData() != Proxy.GetThreadSafeData().Movement.Trajectory.Samples.GetData());
    Anim->PlayerTrajectory.Samples[0].Position += FVector(123.,0.,0.);
    auto PublicTrajectory = Anim->GetFoundationTrajectory();
    PublicTrajectory.Samples[0].Position += FVector(456.,0.,0.);
    TestTrue(TEXT("Retained/BP value edits cannot mutate the active proxy publication"),
        Proxy.GetThreadSafeData().Movement.Trajectory.Samples[0].Position.Equals(PublishedFirst));
    Anim->UpdateNativeTrajectory();
    TestTrue(TEXT("Native trajectory consumes the unchanged published value"),
        Anim->PlayerTrajectory.Samples[0].Position.Equals(PublishedFirst));
    FProject_JAnimThreadSafeData NextTrajectory = Data;
    NextTrajectory.Movement.Trajectory.Samples[0].Position += FVector(789.,0.,0.);
    Proxy.QueueGameThreadData(NextTrajectory); Proxy.ConsumeQueuedGameThreadData();
    TestTrue(TEXT("A later publication cannot overwrite the retained traversal's owned trajectory"),
        Anim->PlayerTrajectory.Samples[0].Position.Equals(PublishedFirst));
    Anim->UpdateNativeTrajectory();
    TestTrue(TEXT("The next native traversal consumes the new complete publication"),
        Anim->PlayerTrajectory.Samples[0].Position.Equals(NextTrajectory.Movement.Trajectory.Samples[0].Position));
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
        using Rule=EProject_JPlayerTransitionRule;
        Frame.DeltaTime=1.f/60.f;
        RecoveryAnim->PlayerStateMachineState=State::IdleBreak;
        RecoveryAnim->SetNativeTransitionToLoop(true);RecoveryAnim->SetNativeReTransition(true);
        Frame.Montage.FullBodyMontageWeight=.2f;Tick(false);
        TestEqual(TEXT("Remaining slot weight retires an idle break even without an active montage instance"),RecoveryAnim->PlayerStateMachineState,State::IdleLoop);
        TestFalse(TEXT("Covered source notify requests do not survive takeover"),RecoveryAnim->EvaluateNativeTransition(Rule::NotifyLoop,10.f)||RecoveryAnim->EvaluateNativeTransition(Rule::NotifyRedirect,10.f));
        RecoveryAnim->RequestLocomotionTransition(true,false,EProject_JLocomotionGaitIntent::Run);
        TestFalse(TEXT("Covered source cannot enqueue another ground request"),RecoveryAnim->EvaluateNativeTransition(Rule::NotifyLoop,10.f));
        RecoveryAnim->SelectNativeStateAnimation(TEXT("Idle Break"),uint8(State::IdleBreak),false);
        TestEqual(TEXT("Idle-break admission shares the ground one-shot ownership contract"),RecoveryAnim->PlayerStateMachineState,State::IdleLoop);
        TestFalse(TEXT("A hidden state clock cannot mature an idle break"),RecoveryAnim->EvaluateNativeTransition(Rule::IdleBreakElapsed,20.f));
        Frame.Montage.FullBodyMontageWeight=0.f;Frame.DeltaTime=3.f;Tick(true);
        TestFalse(TEXT("Residual future displacement after release cannot mint a start"),RecoveryAnim->EvaluateIsMoving());
        RecoveryAnim->SelectNativeStateAnimation(TEXT("Locomotion Transition"),uint8(State::LocomotionTransition),false);
        TestEqual(TEXT("Post-action residual prediction cannot enter a fresh start or pivot"),RecoveryAnim->PlayerStateMachineState,State::IdleLoop);
        TestFalse(TEXT("The original idle-break threshold remains strict after release"),RecoveryAnim->EvaluateNativeTransition(Rule::IdleBreakElapsed,20.f));
        Frame.DeltaTime=.001f;Tick(false);
        TestTrue(TEXT("Continuous uncovered idle can use the original idle break again"),RecoveryAnim->EvaluateNativeTransition(Rule::IdleBreakElapsed,20.f));
        RecoveryAnim->SelectNativeStateAnimation(TEXT("Idle Break"),uint8(State::IdleBreak),false);
        Frame.Montage.bFullBodyMontageActive=true;Tick(false);
        TestEqual(TEXT("A second action retires a break even without an intervening movement episode"),RecoveryAnim->PlayerStateMachineState,State::IdleLoop);
        Frame.Montage.bFullBodyMontageActive=false;Tick(false);
        TestFalse(TEXT("A consecutive action owns its own presentation interval"),RecoveryAnim->EvaluateNativeTransition(Rule::IdleBreakElapsed,20.f));
        Frame.DeltaTime=1.f/60.f;
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
        RecoveryAnim->PlayerMovementStateRecent=EProject_JPlayerMovementState::Moving;
        TestEqual(TEXT("A facing request cannot reopen stop rows from recent movement"),SelectRecovery(),State::IdleLoop);
        RecoveryAnim->PlayerMovementStateRecent=EProject_JPlayerMovementState::Idle;
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
        TestTrue(TEXT("A new source lifetime retains the original idle state-clock contract"),RecoveryAnim->EvaluateNativeTransition(Rule::IdleBreakElapsed,3.001f));
    }
    //125: actual player producer -> native thread-safe phase -> GT PostUpdate, with no graph/evaluation required.
    {
        auto* WorkerWorld125=UWorld::CreateWorld(EWorldType::Game,false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(WorkerWorld125);
        WorkerWorld125->InitializeActorsForPlay(FURL());WorkerWorld125->GetWorldSettings()->NotifyBeginPlay();WorkerWorld125->GetWorldSettings()->NotifyMatchStarted();
        ON_SCOPE_EXIT {WorkerWorld125->EndPlay(EEndPlayReason::Quit);WorkerWorld125->DestroyWorld(false);GEngine->DestroyWorldContext(WorkerWorld125);};
        auto* WorkerPlayer125=WorkerWorld125->SpawnActor<AProject_JSpellbladeCharacter>();
        auto* WorkerMover125=WorkerPlayer125?Cast<UProject_JMoverComponent>(WorkerPlayer125->GetMover()):nullptr;
        auto* DeferredSwitch125=IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.PlayerTrajectory.DeferGroundPrediction"));
        auto* GroundSwitch125=IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Mover.PreparedGroundPrediction"));
        if(!TestNotNull(TEXT("Native worker integration source"),WorkerMover125) || !TestNotNull(TEXT("Deferred player switch"),DeferredSwitch125) ||
            !TestNotNull(TEXT("Owned ground admission switch"),GroundSwitch125)) return false;
        const int32 SavedDeferred125=DeferredSwitch125->GetInt(),SavedGround125=GroundSwitch125->GetInt();
        ON_SCOPE_EXIT {DeferredSwitch125->Set(SavedDeferred125,ECVF_SetByCode);GroundSwitch125->Set(SavedGround125,ECVF_SetByCode);};
        GroundSwitch125->Set(1,ECVF_SetByCode);
        TStrongObjectPtr<UProject_JPlayerAnimInstance> LegacyPlayerAnim125(NewObject<UProject_JPlayerAnimInstance>(WorkerPlayer125->GetMesh()));
        TStrongObjectPtr<UProject_JPlayerAnimInstance> DeferredPlayerAnim125(NewObject<UProject_JPlayerAnimInstance>(WorkerPlayer125->GetMesh()));
        LegacyPlayerAnim125->NativeInitializeAnimation();DeferredPlayerAnim125->NativeInitializeAnimation();
        auto& LegacyProxy125=LegacyPlayerAnim125->GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
        auto& DeferredProxy125=DeferredPlayerAnim125->GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
        const auto SameTrajectory125=[](const FTransformTrajectory& Left125,const FTransformTrajectory& Right125)
        {
            if(Left125.Samples.Num()!=Right125.Samples.Num() || Left125.Samples.IsEmpty()) return false;
            for(int32 TrajectoryIndex125=0;TrajectoryIndex125<Left125.Samples.Num();++TrajectoryIndex125)
            {
                const auto& LeftSample125=Left125.Samples[TrajectoryIndex125];const auto& RightSample125=Right125.Samples[TrajectoryIndex125];
                if(!FMath::IsFinite(RightSample125.TimeInSeconds) || RightSample125.Position.ContainsNaN() || RightSample125.Facing.ContainsNaN() ||
                    !FMath::IsNearlyEqual(LeftSample125.TimeInSeconds,RightSample125.TimeInSeconds,1.e-6) ||
                    !LeftSample125.Position.Equals(RightSample125.Position,1.e-5) || !LeftSample125.Facing.Equals(RightSample125.Facing,1.e-5)) return false;
            }
            return true;
        };
        for(int32 WorkerFrame125=0;WorkerFrame125<2;++WorkerFrame125)
        {
            if(!TestTrue(TEXT("Actual native source publishes a completed ground frame"),ProjectJ::Tests::PublishMoverFrame(WorkerPlayer125,MOVE_Walking,FVector(150+WorkerFrame125*15,30,0)))) return false;
            FMoverInputCmdContext WorkerInput125;
            auto& WorkerCommand125=WorkerInput125.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
            WorkerCommand125.SetMoveInput(EMoveInputType::DirectionalIntent,FVector(.8,.2,0));WorkerCommand125.OrientationIntent=FVector(.6,.8,0);
            const auto WorkerState125=WorkerMover125->GetSyncState();
            FMoverTickStartData WorkerStart125;WorkerStart125.InputCmd=WorkerInput125;WorkerStart125.SyncState=WorkerState125;
            FMoverTickEndData WorkerEnd125;WorkerEnd125.SyncState=WorkerState125;
            FMoverTimeStep WorkerStep125;WorkerStep125.ServerFrame=17+WorkerFrame125;WorkerStep125.BaseSimTimeMs=250.+WorkerFrame125*1000./60.;WorkerStep125.StepMs=1000.f/60.f;
            WorkerMover125->OnPostSimulate(WorkerStep125,WorkerStart125,WorkerEnd125);WorkerMover125->FinalizeFrame(&WorkerEnd125.SyncState,&WorkerEnd125.AuxState);
            FFloorCheckResult WorkerFloor125;WorkerFloor125.bBlockingHit=WorkerFloor125.bWalkableFloor=true;
            WorkerFloor125.HitResult.Normal=WorkerFloor125.HitResult.ImpactNormal=FVector::UpVector;
            WorkerMover125->GetSimBlackboard_Mutable()->Set(CommonBlackboard::LastFloorResult,WorkerFloor125);
            DeferredSwitch125->Set(0,ECVF_SetByCode);
            LegacyProxy125.PreUpdate(LegacyPlayerAnim125.Get(),1.f/60.f);
            LegacyPlayerAnim125->NativeUpdateAnimation(1.f/60.f);
            TestFalse(TEXT("CVar0 retains the complete GT forecast without a worker packet"),LegacyProxy125.PendingPreparedTrajectory.IsSet());
            LegacyPlayerAnim125->NativeThreadSafeUpdateAnimation(1.f/60.f);LegacyProxy125.PostUpdate(LegacyPlayerAnim125.Get());
            const auto LegacyFrame125=LegacyPlayerAnim125->GetFoundationTrajectory();
            DeferredSwitch125->Set(1,ECVF_SetByCode);
            DeferredProxy125.PreUpdate(DeferredPlayerAnim125.Get(),1.f/60.f);
            DeferredPlayerAnim125->NativeUpdateAnimation(1.f/60.f);
            if(!TestTrue(TEXT("Actual native GT producer admits a same-frame owning packet"),DeferredProxy125.PendingPreparedTrajectory.IsSet())) return false;
            const uint64 Published125=DeferredProxy125.PublishedSnapshotRevision;
            TestTrue(TEXT("Publication waits for this frame's native thread-safe consumer"),Published125!=DeferredProxy125.ConsumedSnapshotRevision);
            DeferredPlayerAnim125->NativeThreadSafeUpdateAnimation(1.f/60.f);
            const auto DeferredFrame125=DeferredPlayerAnim125->GetFoundationTrajectory();
            TestEqual(TEXT("Current native trajectory has complete history/current/future dimensions"),DeferredFrame125.Samples.Num(),31);
            TestTrue(TEXT("Both native paths produce the same current-frame trajectory over two history updates"),SameTrajectory125(LegacyFrame125,DeferredFrame125));
            TestTrue(TEXT("Prediction completes this exact publication before graph/state readers"),DeferredProxy125.CompletedPreparedRevision==Published125 && DeferredProxy125.ConsumedSnapshotRevision==Published125 && !DeferredProxy125.PreparedTrajectory.IsSet());
            TestTrue(TEXT("Current player trajectory consumes the same completed frame"),SameTrajectory125(DeferredPlayerAnim125->PlayerTrajectory,DeferredFrame125));
            DeferredProxy125.PostUpdate(DeferredPlayerAnim125.Get());
            TestTrue(TEXT("GT PostUpdate mirror receives the completed forecast without post-evaluate"),SameTrajectory125(DeferredPlayerAnim125->ThreadSafeData.Movement.Trajectory,DeferredFrame125));
            TestTrue(TEXT("GT derived forecast velocity and yaw match the synchronous producer"),
                DeferredPlayerAnim125->ThreadSafeData.Movement.FutureTrajectoryVelocity.Equals(LegacyPlayerAnim125->ThreadSafeData.Movement.FutureTrajectoryVelocity,1.e-5) &&
                FMath::IsNearlyEqual(DeferredPlayerAnim125->ThreadSafeData.Movement.FutureTrajectoryTurnAngle,LegacyPlayerAnim125->ThreadSafeData.Movement.FutureTrajectoryTurnAngle,1.e-5));
            DeferredPlayerAnim125->NativeThreadSafeUpdateAnimation(1.f/60.f);
            TestTrue(TEXT("Repeated same-frame consumption cannot regenerate or replace the forecast"),DeferredProxy125.CompletedPreparedRevision==Published125 && DeferredProxy125.ConsumedSnapshotRevision==Published125 && SameTrajectory125(DeferredPlayerAnim125->GetFoundationTrajectory(),DeferredFrame125));
        }
        //135: exact native Flying uses the same owning publication/history/derived contract.
        {
            auto* FlightPlayer135=WorkerWorld125->SpawnActor<AProject_JSpellbladeCharacter>();
            auto* FlightMover135=FlightPlayer135 ? Cast<UProject_JMoverComponent>(FlightPlayer135->GetMover()):nullptr;
            auto* FlightPolicy135=IConsoleManager::Get().FindConsoleVariable(TEXT("mover.Prediction.ReuseFlyingParams"));
            if(!TestNotNull(TEXT("Flying animation source"),FlightMover135) || !TestNotNull(TEXT("Flying reuse policy"),FlightPolicy135)) return false;
            const int32 SavedPolicy135=FlightPolicy135->GetInt();ON_SCOPE_EXIT {FlightPolicy135->Set(SavedPolicy135,ECVF_SetByCode);};
            TStrongObjectPtr<UProject_JPlayerAnimInstance> LegacyFlight135(NewObject<UProject_JPlayerAnimInstance>(FlightPlayer135->GetMesh()));
            TStrongObjectPtr<UProject_JPlayerAnimInstance> DeferredFlight135(NewObject<UProject_JPlayerAnimInstance>(FlightPlayer135->GetMesh()));
            LegacyFlight135->NativeInitializeAnimation();DeferredFlight135->NativeInitializeAnimation();
            auto& LegacyFlightProxy135=LegacyFlight135->GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
            auto& DeferredFlightProxy135=DeferredFlight135->GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
            for(int32 FlightFrame135=0;FlightFrame135<2;++FlightFrame135)
            {
                if(!TestTrue(TEXT("Actual player publishes a native Flying frame"),ProjectJ::Tests::PublishMoverFrame(FlightPlayer135,MOVE_Flying,FVector(130+15*FlightFrame135,30,50)))) return false;
                FMoverInputCmdContext Input135;auto& Command135=Input135.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
                Command135.SetMoveInput(EMoveInputType::DirectionalIntent,FVector(.7,.2,.1));Command135.OrientationIntent=FVector(.3,.9,.1).GetSafeNormal();
                FMoverTickStartData Start135;Start135.InputCmd=Input135;Start135.SyncState=FlightMover135->GetSyncState();
                FMoverTickEndData End135;End135.SyncState=Start135.SyncState;
                FMoverTimeStep Step135;Step135.ServerFrame=17+FlightFrame135;Step135.BaseSimTimeMs=250.+FlightFrame135*1000./60.;Step135.StepMs=1000.f/60.f;
                FlightMover135->OnPostSimulate(Step135,Start135,End135);FlightMover135->FinalizeFrame(&End135.SyncState,&End135.AuxState);
                const auto* Physical135=FlightMover135->GetSyncState().SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
                if(!TestNotNull(TEXT("Flying physical state is published"),Physical135)) return false;
                const FVector PhysicalLocation135=Physical135->GetLocation_WorldSpace(),PhysicalVelocity135=Physical135->GetVelocity_WorldSpace();const FRotator PhysicalFacing135=Physical135->GetOrientation_WorldSpace();
                DeferredSwitch125->Set(0,ECVF_SetByCode);FlightPolicy135->Set(0,ECVF_SetByCode);
                LegacyFlightProxy135.PreUpdate(LegacyFlight135.Get(),1.f/60.f);LegacyFlight135->NativeUpdateAnimation(1.f/60.f);
                TestFalse(TEXT("Policy0 Flying retains canonical GT GenerateMove without a packet"),LegacyFlightProxy135.PendingPreparedTrajectory.IsSet());
                LegacyFlight135->NativeThreadSafeUpdateAnimation(1.f/60.f);LegacyFlightProxy135.PostUpdate(LegacyFlight135.Get());
                const auto Reference135=LegacyFlight135->GetFoundationTrajectory();
                DeferredSwitch125->Set(2,ECVF_SetByCode);FlightPolicy135->Set(1,ECVF_SetByCode);
                DeferredFlightProxy135.PreUpdate(DeferredFlight135.Get(),1.f/60.f);DeferredFlight135->NativeUpdateAnimation(1.f/60.f);
                if(!TestTrue(TEXT("Explicit-facing Flying publishes a real prepared packet"),DeferredFlightProxy135.PendingPreparedTrajectory.IsSet())) return false;
                const auto Packet135=DeferredFlightProxy135.PendingPreparedTrajectory.GetValue();
                TestTrue(TEXT("Prepared animation packet is NativeFlying rather than ground fallback"),Packet135.Kind==FProject_JPreparedAnimTrajectory::EKind::NativeFlying);
                FProject_JAnimMovementThreadSafeData Movement135=DeferredFlightProxy135.PendingGameThreadData.Movement;
                struct FFlightWorker135 { FProject_JAnimMovementThreadSafeData Movement; bool bWorker=false,bSuccess=false; };
                auto Work135=Async(EAsyncExecution::ThreadPool,[Packet135,Movement135=MoveTemp(Movement135)]() mutable
                {
                    FProject_JAnimThreadSafeData Values135;Values135.Movement=MoveTemp(Movement135);TArray<FTrajectorySampleInfo> Scratch135;
                    FFlightWorker135 Result135;Result135.bWorker=!IsInGameThread();Result135.bSuccess=Packet135.Compute(Values135,Scratch135);Result135.Movement=MoveTemp(Values135.Movement);return Result135;
                });
                const auto Worker135=Work135.Get();TestTrue(TEXT("Flying history/resample/derived value compute executes off GT"),Worker135.bWorker && Worker135.bSuccess);
                TestTrue(TEXT("Worker Flying history/current/resample equals canonical GT over two frames"),SameTrajectory125(Reference135,Worker135.Movement.Trajectory));
                TestTrue(TEXT("Worker Flying derived velocity/yaw equals canonical GT"),Worker135.Movement.FutureTrajectoryVelocity.Equals(LegacyFlight135->ThreadSafeData.Movement.FutureTrajectoryVelocity,1.e-5) &&
                    FMath::IsNearlyEqual(Worker135.Movement.FutureTrajectoryTurnAngle,LegacyFlight135->ThreadSafeData.Movement.FutureTrajectoryTurnAngle,1.e-5));
                const uint64 Revision135=DeferredFlightProxy135.PublishedSnapshotRevision;
                DeferredFlight135->NativeThreadSafeUpdateAnimation(1.f/60.f);DeferredFlightProxy135.PostUpdate(DeferredFlight135.Get());
                TestTrue(TEXT("Same-frame Flying publication completes before graph readers and returns its mirror"),DeferredFlightProxy135.CompletedPreparedRevision==Revision135 && DeferredFlightProxy135.ConsumedSnapshotRevision==Revision135 &&
                    SameTrajectory125(Reference135,DeferredFlight135->GetFoundationTrajectory()) && SameTrajectory125(Reference135,DeferredFlight135->ThreadSafeData.Movement.Trajectory));
                const auto* After135=FlightMover135->GetSyncState().SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
                TestTrue(TEXT("Flying forecast/history/worker publication does not mutate physical transforms or velocity"),After135 && After135->GetLocation_WorldSpace()==PhysicalLocation135 && After135->GetOrientation_WorldSpace()==PhysicalFacing135 && After135->GetVelocity_WorldSpace()==PhysicalVelocity135);
            }
            DeferredSwitch125->Set(1,ECVF_SetByCode);
            DeferredFlightProxy135.PreUpdate(DeferredFlight135.Get(),1.f/60.f);DeferredFlight135->NativeUpdateAnimation(1.f/60.f);
            TestFalse(TEXT("Default level1 preserves synchronous Flying until end-to-end performance is qualified"),DeferredFlightProxy135.PendingPreparedTrajectory.IsSet());
            DeferredFlight135->NativeThreadSafeUpdateAnimation(1.f/60.f);DeferredFlightProxy135.PostUpdate(DeferredFlight135.Get());
            TestEqual(TEXT("Default Flying still publishes the complete native trajectory"),DeferredFlight135->GetFoundationTrajectory().Samples.Num(),31);
            DeferredSwitch125->Set(2,ECVF_SetByCode);
            DeferredFlightProxy135.PreUpdate(DeferredFlight135.Get(),1.f/60.f);DeferredFlight135->NativeUpdateAnimation(1.f/60.f);
            if(!TestTrue(TEXT("Flying retirement owns a genuinely pending packet"),DeferredFlightProxy135.PendingPreparedTrajectory.IsSet())) return false;
            const auto RetiredMirror135=DeferredFlight135->ThreadSafeData.Movement.Trajectory;DeferredFlight135->NativeUninitializeAnimation();
            TestTrue(TEXT("Flying uninitialize retires pending and consumed generations"),!DeferredFlightProxy135.PendingPreparedTrajectory.IsSet() && !DeferredFlightProxy135.PreparedTrajectory.IsSet() && DeferredFlightProxy135.CompletedPreparedRevision==0);
            DeferredFlight135->NativeThreadSafeUpdateAnimation(1.f/60.f);DeferredFlightProxy135.PostUpdate(DeferredFlight135.Get());
            TestTrue(TEXT("Retired Flying packet cannot overwrite the last completed GT mirror"),SameTrajectory125(RetiredMirror135,DeferredFlight135->ThreadSafeData.Movement.Trajectory));
            LegacyFlight135->NativeUninitializeAnimation();
        }
        DeferredProxy125.PreUpdate(DeferredPlayerAnim125.Get(),1.f/60.f);
        DeferredPlayerAnim125->NativeUpdateAnimation(1.f/60.f);
        if(!TestTrue(TEXT("Retirement case owns a genuinely pending capture"),DeferredProxy125.PendingPreparedTrajectory.IsSet())) return false;
        const auto MirrorBeforeRetirement125=DeferredPlayerAnim125->ThreadSafeData.Movement.Trajectory;
        DeferredPlayerAnim125->NativeUninitializeAnimation();
        TestTrue(TEXT("Source uninitialization retires queued and consumed packets and completion identity"),!DeferredProxy125.PendingPreparedTrajectory.IsSet() && !DeferredProxy125.PreparedTrajectory.IsSet() && DeferredProxy125.CompletedPreparedRevision==0 && DeferredProxy125.CompletedPreparedSourceGeneration==0);
        DeferredProxy125.PostUpdate(DeferredPlayerAnim125.Get());
        TestTrue(TEXT("An old pending completion cannot publish into the retired GT mirror"),SameTrajectory125(DeferredPlayerAnim125->ThreadSafeData.Movement.Trajectory,MirrorBeforeRetirement125));
        TestFalse(TEXT("Retired source keeps native graph admission closed"),DeferredPlayerAnim125->PlayerHasLocomotionSource);
        LegacyPlayerAnim125->NativeUninitializeAnimation();
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
