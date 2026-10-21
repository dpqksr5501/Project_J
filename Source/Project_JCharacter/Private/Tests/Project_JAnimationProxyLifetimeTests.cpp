#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JCharacterAnimInstance.h"
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
    TStrongObjectPtr<UProject_JCharacterAnimInstance> Anim(NewObject<UProject_JCharacterAnimInstance>(Mesh.Get()));
    auto& Proxy = Anim->GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
    FProject_JAnimThreadSafeData Data;
    Data.Movement.GroundSpeed = 100.f;
    Proxy.QueueGameThreadData(Data);
    TestEqual(TEXT("GT publication cannot mutate the current traversal"), Proxy.GetThreadSafeData().Movement.GroundSpeed, 0.f);
    Data.Movement.GroundSpeed = 585.f;
    Proxy.QueueGameThreadData(Data);
    Proxy.ConsumeQueuedGameThreadData();
    TestEqual(TEXT("Traversal consumes the final completed publication"), Proxy.GetThreadSafeData().Movement.GroundSpeed, 585.f);
    TestEqual(TEXT("Publication revision is consumed once"), Proxy.ConsumedSnapshotRevision, Proxy.PublishedSnapshotRevision);
    Proxy.Initialize(Anim.Get());
    TestEqual(TEXT("Reinitialize clears the prior owner's snapshot"), Proxy.GetThreadSafeData().Movement.GroundSpeed, 0.f);
    TestTrue(TEXT("Reinitialize clears stale playback observations"), Proxy.GetLatestPostSelection().SelectedAnimation.IsNone());
    return true;
}
#endif
