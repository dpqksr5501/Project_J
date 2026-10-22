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
    Data.Movement.GroundSpeed = 585.f;
    Data.Ground.RunSpeed=600.f;Data.Ground.bScalePlayRateWithSpeed=true;
    Proxy.QueueGameThreadData(Data);
    Proxy.ConsumeQueuedGameThreadData();
    TestEqual(TEXT("Traversal consumes the final completed publication"), Proxy.GetThreadSafeData().Movement.GroundSpeed, 585.f);
    TestEqual(TEXT("Speed tuning uses the same completed publication as movement"),Proxy.GetThreadSafeData().Ground.RunSpeed,600.f);
    TestTrue(TEXT("Cadence policy is published with its speed tuning"),Proxy.GetThreadSafeData().Ground.bScalePlayRateWithSpeed);
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
    return true;
}
#endif
